/*----------------------------------------------------------------------------
 * main.c
 * 
 *
 *--------------------------------------------------------------------------*/
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

static const long POLLING_INTERVAL = 5 * 1000 * 1000;
enum DESCRIPTOR_INDICES {
   HEARTBEAT_TIMER,
   POSTGRES_CONNECTION,
   DESCRIPTOR_COUNT
};

struct Config {
   int zmq_thread_pool_size;
   time_t heartbeat_period;
   long zmq_polling_interval;
};

struct State {
   int heartbeat_timerfd;
   zmq_pollitem_t poll_items[DESCRIPTOR_COUNT];
};

const struct Config *
load_config() {
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

// release resources used by config
void
clear_config(const struct Config * config) {
   free((void *) config);
}

// create and initialize a timerfd for use with poll
// return the fd on success, -1 on error
static int
create_and_set_timer(time_t timer_period) {

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

struct State *
create_state(const struct Config * config) {

   struct State * state = malloc(sizeof(struct State));
   check_mem(state);
   bzero(state, sizeof(struct State));

   state->heartbeat_timerfd = create_and_set_timer(config->heartbeat_period);
   check(state->heartbeat_timerfd != -1, "create_and_set_timer");

   state->poll_items[HEARTBEAT_TIMER].socket = NULL;
   state->poll_items[HEARTBEAT_TIMER].fd = state->heartbeat_timerfd;
   state->poll_items[HEARTBEAT_TIMER].events = ZMQ_POLLIN;

   state->poll_items[POSTGRES_CONNECTION].socket = NULL;
   state->poll_items[POSTGRES_CONNECTION].fd = 0;
   state->poll_items[POSTGRES_CONNECTION].events = 0;

   return state;

error:

   return NULL;
}

// release resources used by state
void
clear_state(struct State * state) {
   if (state->heartbeat_timerfd != -1) close(state->heartbeat_timerfd);
   free(state);
}

// start the asynchronous connection process
// returns connection handle on success, NULL on failure
PGconn *
start_postgres_connection() {
   const char * keywords[] = {
      "dbname",
      NULL
   };
   const char * values[] = {
      "postgres",
      NULL
   };

   PGconn * connection = PQconnectStartParams(keywords, values, 0);
   check(connection != NULL, "PQconnectStartParams");
   check(PQstatus(connection) != CONNECTION_BAD, "CONNECTION_BAD")

   return connection;

error:
 
   return NULL;
}

int
main(int argc, char **argv, char **envp) {
   log_info("program starts");
   
   const struct Config * config = load_config();
   check(config != NULL, "load_config");

   struct State * state = create_state(config);
   check(state != NULL, "create_state");

   void *zmq_context = zmq_init(config->zmq_thread_pool_size);
   check(zmq_context != NULL, "initializing zeromq");
   
   PGconn * postgres_connection = start_postgres_connection();
   check(postgres_connection != NULL, "start_postgres_connection");
   PostgresPollingStatusType polling_status = \
      PQconnectPoll(postgres_connection);
   check(polling_status == PGRES_POLLING_READING, 
         "polling status = %s", POLLING_STATUS[polling_status]);

   check(install_signal_handler() == 0, "install signal handler");

   while (!halt_signal) {
      debug("polling");

      int result = zmq_poll(state->poll_items, 
                            DESCRIPTOR_COUNT, 
                            config->zmq_polling_interval); 
   
      // getting a signal here could cause 'Interrupted system call'
      // if we're shutting down, we don't care
      if (halt_signal) break;

      check(result != -1, "polling");
      if (result == 0) continue;

      check(state->poll_items[HEARTBEAT_TIMER].revents == ZMQ_POLLIN, 
            "poll result");

      uint64_t expiration_count = 0;
      ssize_t bytes_read = read(state->heartbeat_timerfd, 
                                &expiration_count, 
                                sizeof(expiration_count));
      check(bytes_read == sizeof(expiration_count), "read timerfd");
      debug("timer fired expiration_count = %ld", expiration_count);
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
