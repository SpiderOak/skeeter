/*----------------------------------------------------------------------------
 * main.c
 * 
 *
 *--------------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <zmq.h>

#include <libpq-fe.h>

#include "bstrlib.h"
#include "config.h"
#include "dbg.h"
#include "display_strings.h"
#include "signal_handler.h"
#include "state.h"

enum POSTGRES_STATUS {
   POSTGRES_OK,
   POSTGRES_IN_PROGRESS,
   POSTGRES_FAILED
};

enum EPOLL_ACTION {
   EPOLL_READ,
   EPOLL_WRITE
};

// The most epoll events that can be active
// the restart_event and the postgres_event cannot be active at the same time
static const int MAX_EPOLL_EVENTS = 2;

typedef int (* epoll_callback)(const struct Config * config, 
                               struct State * state);

//---------------------------------------------------------------------------
// utility function for setting up epoll for postgres
// returns 0 on success, -1 on error
int
set_epoll_ctl_for_postgres(enum EPOLL_ACTION action, 
                           epoll_callback callback,
                           struct State * state) {
//---------------------------------------------------------------------------
   int events = \
      action == EPOLL_READ ? EPOLLIN | EPOLLERR : EPOLLOUT | EPOLLERR;
   int op = state->postgres_event.events == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

   state->postgres_event.events = events;
   state->postgres_event.data.ptr = (void *) callback;
   return epoll_ctl(state->epoll_fd,
                    op,
                    PQsocket(state->postgres_connection),
                    &state->postgres_event);
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
   int ctl_result;

   debug("check_listen_commnd_cb");
   ConnStatusType status = PQstatus(state->postgres_connection);
   check(status == CONNECTION_OK, 
         "Invalid status in callback '%s'", CONN_STATUS[status]);
   
   result = PQgetResult(state->postgres_connection);
   if (result == NULL) {
      debug("result = NULL, query complete");
      ctl_result = set_epoll_ctl_for_postgres(EPOLL_READ, 
                                              check_notifications_cb,
                                              state);
      check(ctl_result == 0, "query complete");
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
   bstring item;
   const char * item_str;
   const char * query = NULL;
   int i;

   debug("send_listen_commnd");
   ConnStatusType status = PQstatus(state->postgres_connection);
   check(status == CONNECTION_OK, 
         "Invalid status '%s'", CONN_STATUS[status]);
   
   bquery = bfromcstr("");
   for (i=0; i < config->channel_list->qty; i++) {
      item_str = bstr2cstr(config->channel_list->entry[i], '?');
      check_mem(item_str);
      item = bformat("LISTEN %s;", item_str);
      check(bcstrfree((char *) item_str) == BSTR_OK, "bcstrfree");
      check(bconcat(bquery, item) == BSTR_OK, "bconcat");
      check(bdestroy(item) == BSTR_OK, "bdestroy(item)");
   }
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
// send the heartbeat message
// return 0 on success, 1 on failure
int
heartbeat_timer_cb(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   uint64_t expiration_count = 0;
   ssize_t bytes_read = read(state->heartbeat_timer_fd, 
                             &expiration_count, 
                             sizeof(expiration_count));
   check(bytes_read == sizeof(expiration_count), "read timerfd");
   debug("heartbeat timer fired expiration_count = %ld", expiration_count);

   return 0;

error:

   return 1;
}

//----------------------------------------------------------------------------
// try to restart the postgres connection
// return 0 on success, 1 on failure
int
restart_timer_cb(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   uint64_t expiration_count = 0;
   ssize_t bytes_read = read(state->restart_timer_fd, 
                             &expiration_count, 
                             sizeof(expiration_count));
   check(bytes_read == sizeof(expiration_count), "read timerfd");
   debug("restart timer fired expiration_count = %ld", expiration_count);

   return 0;

error:

   return 1;
}


//----------------------------------------------------------------------------
int
postgres_connection_cb(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------

   PostgresPollingStatusType polling_status;
   int ctl_result;

   polling_status = PQconnectPoll(state->postgres_connection);
   debug("polling status = %s", POLLING_STATUS[polling_status]);

   int op = state->postgres_event.events == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
   switch (polling_status) {
      case PGRES_POLLING_READING:
         ctl_result = set_epoll_ctl_for_postgres(EPOLL_READ, 
                                                 postgres_connection_cb,
                                                 state);
         check(ctl_result == 0, "postgres_connection_cb");
         break;

      case PGRES_POLLING_WRITING:
         ctl_result = set_epoll_ctl_for_postgres(EPOLL_WRITE, 
                                                 postgres_connection_cb,
                                                 state);
         check(ctl_result == 0, "postgres_connection_cb");
         break;

      case PGRES_POLLING_OK:
         check(send_listen_command(config, state) == 0, "send_listen_command");
         ctl_result = set_epoll_ctl_for_postgres(EPOLL_READ, 
                                                 check_listen_command_cb,
                                                 state);
         check(ctl_result == 0, "postgres_connection_cb");
         break;
         
      default:
         sentinel("invalid Postgres Polling Status %s postgres_connection_cb",  
                  POLLING_STATUS[polling_status]);
         
   } //switch 

   return 0;

error:

   return 1;

}

//----------------------------------------------------------------------------
// start the asynchronous connection process
// returns 0 on success, 1 on failure
int
start_postgres_connection(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   PostgresPollingStatusType polling_status;
   int ctl_result;

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

   polling_status = PQconnectPoll(state->postgres_connection);
   debug("polling status = %s", POLLING_STATUS[polling_status]);
   switch (polling_status) {

      case PGRES_POLLING_READING:
         ctl_result = set_epoll_ctl_for_postgres(EPOLL_READ, 
                                                 postgres_connection_cb,
                                                 state);
         check(ctl_result == 0, "start_postgres_connection");
         break;

      case PGRES_POLLING_WRITING:
         ctl_result = set_epoll_ctl_for_postgres(EPOLL_WRITE, 
                                                 postgres_connection_cb,
                                                 state);
         check(ctl_result == 0, "start_postgres_connection");
         break;

      default:
         sentinel("invalid Postgres Polling Status %s",  
                  POLLING_STATUS[polling_status]);
   }

   return 0;

error:
 
   return 1;
}

//----------------------------------------------------------------------------
int
initialize_state(const struct Config * config, 
                 void * zmq_context, 
                 struct State * state) {
//----------------------------------------------------------------------------
   char * pub_socket_uri = NULL;
   int result;

   state->heartbeat_timer_fd = create_and_set_timer(config->heartbeat_period);
   check(state->heartbeat_timer_fd != -1, "create_and_set_timer");
   state->heartbeat_timer_event.events = EPOLLIN | EPOLLERR;
   state->heartbeat_timer_event.data.ptr = (void *) heartbeat_timer_cb;

   state->restart_timer_fd = -1;
   state->restart_timer_event.events = EPOLLIN | EPOLLERR;
   state->restart_timer_event.data.ptr = (void *) restart_timer_cb;

   state->postgres_connection = NULL;
   state->postgres_event.events = 0;
   state->postgres_event.data.ptr = NULL;

   state->epoll_fd = epoll_create(1);
   check(state->epoll_fd != -1, "epoll_create");

   state->zmq_pub_socket = zmq_socket(zmq_context, ZMQ_PUB);
   check(state->zmq_pub_socket != NULL, "zmq_socket");
   
   result = zmq_setsockopt(state->zmq_pub_socket,
                           ZMQ_HWM,
                           &config->pub_socket_hwm,
                           sizeof config->pub_socket_hwm);
   check(result == 0, "zmq_setsockopt");

   pub_socket_uri = bstr2cstr(config->pub_socket_uri, '?');
   check(pub_socket_uri != NULL, "pub_socket_uri");

   check(zmq_bind(state->zmq_pub_socket, pub_socket_uri) == 0, 
         "bind %s",
         pub_socket_uri);

   bcstrfree(pub_socket_uri);
   return 0;

error:

   if (pub_socket_uri != NULL) bcstrfree(pub_socket_uri);
   return 1;
}

//----------------------------------------------------------------------------
int
main(int argc, char **argv, char **envp) {
//----------------------------------------------------------------------------
   int ctl_result;
   struct epoll_event event_list[MAX_EPOLL_EVENTS];
   int wait_result;
   int i;
   int callback_result;

   log_info("program starts");
  
   // initilize our basic structs
   const struct Config * config = load_config();
   check(config != NULL, "load_config");

   struct State * state = create_state();
   check(state != NULL, "create_state");

   void *zmq_context = zmq_init(config->zmq_thread_pool_size);
   check(zmq_context != NULL, "initializing zeromq");
  
   check(initialize_state(config, zmq_context, state) == 0, "initialize_state");

   // start polling the heartbeat timer
   ctl_result = epoll_ctl(state->epoll_fd,
                          EPOLL_CTL_ADD,
                          state->heartbeat_timer_fd,
                          &state->heartbeat_timer_event);
   check(ctl_result != -1, "epoll heartbeat timer");

   // start postgres connection process
   check(start_postgres_connection(config, state) == 0, 
         "start_postgres_connection");

   // main epoll loop, using callbacks to drive he program
   check(install_signal_handler() == 0, "install signal handler");
   while (!halt_signal) {
      wait_result = epoll_wait(state->epoll_fd,
                               event_list,
                               MAX_EPOLL_EVENTS,
                               config->epoll_timeout); 
      check(wait_result != -1, "epoll_wait")
      if (wait_result == 0) {
         debug("poll timeout");
         continue;
      }
      for (i=0; i < wait_result; i++) {
         check(event_list[i].data.ptr != NULL, "NULL callback");
         callback_result = \
            ((epoll_callback) event_list[i].data.ptr)(config, state);
         check(callback_result == 0, "callback");
      }
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
