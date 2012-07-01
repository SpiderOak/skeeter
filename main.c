/*----------------------------------------------------------------------------
 * main.c
 * 
 *
 *--------------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <zmq.h>

#include <libpq-fe.h>

#include "bstrlib.h"
#include "dbg.h"
#include "display_strings.h"
#include "signal_handler.h"

enum DESCRIPTOR_INDICES {
   HEARTBEAT_TIMER,
   POSTGRES_CONNECTION,
   DESCRIPTOR_COUNT
};

enum POSTGRES_STATUS {
   POSTGRES_OK,
   POSTGRES_IN_PROGRESS,
   POSTGRES_FAILED
};

struct Config {
   int zmq_thread_pool_size;
   time_t heartbeat_period;
   long zmq_polling_interval;
};

struct State;

typedef int (* state_postgres_callback)(const struct Config * config, 
                                        struct State * state);

struct State {
   int heartbeat_timerfd;
   PGconn * postgres_connection;
   state_postgres_callback postgres_callback;
   zmq_pollitem_t poll_items[DESCRIPTOR_COUNT];
};

//----------------------------------------------------------------------------
// load config from skeeterrc
const struct Config *
load_config() {
//----------------------------------------------------------------------------
   // TODO: load config from skeeterrc
   struct Config * config = malloc(sizeof(struct Config)); 
   check_mem(config);
   bzero(config, sizeof(struct Config));

   config->zmq_thread_pool_size = 3;
   config->heartbeat_period = 15;
   config->zmq_polling_interval = 5 * 1000 * 1000;

   return config;

error:

   return NULL;
}

//----------------------------------------------------------------------------
// release resources used by config
void
clear_config(const struct Config * config) {
//----------------------------------------------------------------------------
   free((void *) config);
}

//----------------------------------------------------------------------------
// create and initialize a timerfd for use with poll
// return the fd on success, -1 on error
static int
create_and_set_timer(time_t timer_period) {
//----------------------------------------------------------------------------

   // create the timer fd
   int timerfd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
   check(timerfd != -1, "timerfd_create");

   // define the firing interval
   struct itimerspec timer_value;
   timer_value.it_interval.tv_sec = timer_period;
   timer_value.it_interval.tv_nsec = 0;
   timer_value.it_value.tv_sec = timer_period; // first expiration
   timer_value.it_value.tv_nsec = 0;

   // set the firing interval
   int result = timerfd_settime(timerfd, 0, &timer_value, NULL);
   check(result == 0, "timerfd_settime");

   return timerfd;

error:

   return -1;
}

//----------------------------------------------------------------------------
struct State *
create_state(const struct Config * config) {
//----------------------------------------------------------------------------

   struct State * state = malloc(sizeof(struct State));
   check_mem(state);
   bzero(state, sizeof(struct State));

   state->heartbeat_timerfd = create_and_set_timer(config->heartbeat_period);
   check(state->heartbeat_timerfd != -1, "create_and_set_timer");

   state->postgres_connection = NULL;

   state->poll_items[HEARTBEAT_TIMER].socket = NULL;
   state->poll_items[HEARTBEAT_TIMER].fd = state->heartbeat_timerfd;
   state->poll_items[HEARTBEAT_TIMER].events = ZMQ_POLLIN | ZMQ_POLLERR;

   state->poll_items[POSTGRES_CONNECTION].socket = NULL;
   state->poll_items[POSTGRES_CONNECTION].fd = 0;
   state->poll_items[POSTGRES_CONNECTION].events = 0;

   return state;

error:

   return NULL;
}

//----------------------------------------------------------------------------
// release resources used by state
void
clear_state(struct State * state) {
//----------------------------------------------------------------------------
   if (state->heartbeat_timerfd != -1) close(state->heartbeat_timerfd);
   if (state->postgres_connection != NULL) {
      PQfinish(state->postgres_connection); 
   }
   free(state);
}

//----------------------------------------------------------------------------
int
check_notifications_cb(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   debug("check_notifications_cb");
   ConnStatusType status = PQstatus(state->postgres_connection);
   check(status == CONNECTION_OK, 
         "Invalid status in callback '%s'", CONN_STATUS[status]);
   
   check(PQconsumeInput(state->postgres_connection) == 1, 
         "PQconsumeInput %s",
         PQerrorMessage(state->postgres_connection));
   bool more_notifications = true;
   while (more_notifications) {
      PGnotify * notification = PQnotifies(state->postgres_connection);
      if (notification != NULL) {
         log_info("notification %s", notification->relname);
         PQfreemem(notification);
      } else {
         more_notifications = false;
      }
   }

   return 0;

error:

   return 1;
}

//----------------------------------------------------------------------------
int
check_listen_command_cb(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   PGresult * result = NULL;

   debug("check_listen_commnd_cb");
   ConnStatusType status = PQstatus(state->postgres_connection);
   check(status == CONNECTION_OK, 
         "Invalid status in callback '%s'", CONN_STATUS[status]);
   
   result = PQgetResult(state->postgres_connection);
   if (result == NULL) {
      debug("result = NULL, query complete");
      state->poll_items[POSTGRES_CONNECTION].events = ZMQ_POLLIN | ZMQ_POLLERR;
      state->postgres_callback = check_notifications_cb;
   } else {
      debug("non-NULL result");
      check(PQconsumeInput(state->postgres_connection) == 1, 
            "PQconsumeInput %s",
            PQerrorMessage(state->postgres_connection));
   }

   if (result != NULL) PQclear(result);
   return 0;

error:

   if (result != NULL) PQclear(result);
   return 1;
}

//----------------------------------------------------------------------------
int
send_listen_command(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   bstring bquery;
   const char * query = NULL;

   debug("send_listen_commnd");
   ConnStatusType status = PQstatus(state->postgres_connection);
   check(status == CONNECTION_OK, 
         "Invalid status '%s'", CONN_STATUS[status]);
   
   bquery = bformat("LISTEN %s;", "channel1");
   query = bstr2cstr(bquery, '?');
   check_mem(query);

   debug("query = %s", query);
   check(PQsendQuery(state->postgres_connection, query) == 1,
         "PQsendQuery");
   
   bdestroy(bquery);
   bcstrfree((char *) query);

   return 0;

error:

   bdestroy(bquery);
   bcstrfree((char *) query);
   return 1;
}

//----------------------------------------------------------------------------
// start the asynchronous connection process
// returns 0 on success, 1 on failure
int
start_postgres_connection(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   // TODO: get keywords from config
   const char * keywords[] = {
      "dbname",
      NULL
   };
   const char * values[] = {
      "postgres",
      NULL
   };

   state->postgres_connection = PQconnectStartParams(keywords, values, 0);
   check(state->postgres_connection != NULL, "PQconnectStartParams");
   check(PQstatus(state->postgres_connection) != CONNECTION_BAD, 
         "CONNECTION_BAD");

   return 0;

error:
 
   return 1;
}

//----------------------------------------------------------------------------
// poll the postgres connection to test status
// if it wants to read or write the socket, set the polling item
// return ok, in_progress or error
enum POSTGRES_STATUS
test_and_set_postgres_polling(struct State * state) {
//----------------------------------------------------------------------------
   PostgresPollingStatusType polling_status = \
      PQconnectPoll(state->postgres_connection);
   debug("polling status = %s", POLLING_STATUS[polling_status]);
   switch (polling_status) {

      case PGRES_POLLING_FAILED:
         return POSTGRES_FAILED;

      case PGRES_POLLING_READING:
         state->poll_items[POSTGRES_CONNECTION].fd = \
            PQsocket(state->postgres_connection);
         state->poll_items[POSTGRES_CONNECTION].events = \
            ZMQ_POLLIN | ZMQ_POLLERR;
         return POSTGRES_IN_PROGRESS;

      case PGRES_POLLING_WRITING:
         state->poll_items[POSTGRES_CONNECTION].fd = \
            PQsocket(state->postgres_connection);
         state->poll_items[POSTGRES_CONNECTION].events = \
            ZMQ_POLLOUT | ZMQ_POLLERR;
         return POSTGRES_IN_PROGRESS;

      case PGRES_POLLING_OK:
         return POSTGRES_OK;

      case PGRES_POLLING_ACTIVE:
         return POSTGRES_IN_PROGRESS;

      default:
         break;
   } //switch 
         
   log_err("Unknown polling status %d", polling_status); 
   return POSTGRES_FAILED;
}

//----------------------------------------------------------------------------
// send the heartbeat message
// return 0 on success, 1 on failure
int
send_heartbeat(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   uint64_t expiration_count = 0;
   ssize_t bytes_read = read(state->heartbeat_timerfd, 
                             &expiration_count, 
                             sizeof(expiration_count));
   check(bytes_read == sizeof(expiration_count), "read timerfd");
   debug("heartbeat timer fired expiration_count = %ld", expiration_count);

   return 0;

error:

   return 1;
}

//----------------------------------------------------------------------------
int
postgres_connection_cb(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------

   enum POSTGRES_STATUS postgres_status = test_and_set_postgres_polling(state);
   check(postgres_status != POSTGRES_FAILED, "postgres_status");
   if (postgres_status == POSTGRES_OK) {
      check(send_listen_command(config, state) == 0, "send_listen_command");
      state->poll_items[POSTGRES_CONNECTION].events = ZMQ_POLLIN | ZMQ_POLLERR;
      state->postgres_callback = check_listen_command_cb;

   }

   return 0;

error:

   return 1;

}

//----------------------------------------------------------------------------
// one call to poll()
// return 0 on success, 1 on failure
int 
polling_loop(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------

   debug("polling");

   int result = zmq_poll(state->poll_items, 
                DESCRIPTOR_COUNT, 
                config->zmq_polling_interval); 
   
   // getting a signal here could cause 'Interrupted system call'
   // if we're shutting down, we don't care
   if (halt_signal) return 0;

   check(result != -1, "polling");
   if (result == 0) return 0;

   if (state->poll_items[HEARTBEAT_TIMER].revents != 0) { 
      check((state->poll_items[HEARTBEAT_TIMER].revents & ZMQ_POLLERR) == 0, 
            "heartbeat pollerr");
      check(send_heartbeat(config, state) == 0, "send_heartbeat");
   }

   if (state->poll_items[POSTGRES_CONNECTION].revents != 0) { 
      check((state->poll_items[POSTGRES_CONNECTION].revents & ZMQ_POLLERR) == 0, 
            "heartbeat pollerr");
      check(state->postgres_callback(config, state) == 0, "state->callback");
   }

   return 0;

error:

   return 1;
}

//----------------------------------------------------------------------------
int
main(int argc, char **argv, char **envp) {
//----------------------------------------------------------------------------
   log_info("program starts");
   
   const struct Config * config = load_config();
   check(config != NULL, "load_config");

   struct State * state = create_state(config);
   check(state != NULL, "create_state");

   void *zmq_context = zmq_init(config->zmq_thread_pool_size);
   check(zmq_context != NULL, "initializing zeromq");
   
   check(start_postgres_connection(config, state) == 0, 
         "start_postgres_connection");
   enum POSTGRES_STATUS postgres_status = test_and_set_postgres_polling(state);
   check(postgres_status != POSTGRES_FAILED, "postgres_status");
   state->postgres_callback = postgres_connection_cb;

   check(install_signal_handler() == 0, "install signal handler");

   while (!halt_signal) {
      int result = polling_loop(config, state);
      check(result == 0, "polling_loop")
   } // while
   debug("while loop broken");

   clear_state(state);
   clear_config(config);
   check(zmq_term(zmq_context) == 0, "terminating zeromq")
   log_info("program terminates normally");
   return 0;

error:
   if (state != NULL) clear_state(state);
   if (config != NULL) clear_config(config);
   if (zmq_context != NULL) zmq_term(zmq_context);
   log_info("program terminates with error");
   return 1;
}
