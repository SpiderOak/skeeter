/*----------------------------------------------------------------------------
 * main.c
 * 
 *
 *--------------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <syslog.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <zmq.h>

#include <libpq-fe.h>

#include "bstrlib.h"
#include "command_line.h"
#include "config.h"
#include "dbg_syslog.h"
#include "display_strings.h"
#include "message.h"
#include "signal_handler.h"
#include "state.h"
#ifndef ZMQ_DONTWAIT
#   define ZMQ_DONTWAIT     ZMQ_NOBLOCK
#endif

// http://zguide.zeromq.org/page:all#Suggested-Shim-Macros
#if ZMQ_VERSION_MAJOR == 2
#   define zmq_msg_send(msg,sock,opt) zmq_send (sock, msg, opt)
#   define zmq_msg_recv(msg,sock,opt) zmq_recv (sock, msg, opt)
#   define zmq_ctx_destroy(context) zmq_term(context)
#   define ZMQ_POLL_MSEC    1000        //  zmq_poll is usec
#   define ZMQ_SNDHWM ZMQ_HWM
#   define ZMQ_RCVHWM ZMQ_HWM
#elif ZMQ_VERSION_MAJOR == 3
#   define ZMQ_POLL_MSEC    1           //  zmq_poll is msec
#endif

enum EPOLL_ACTION {
   EPOLL_READ,
   EPOLL_WRITE
};

typedef enum CALLBACK_RESULT {
   CALLBACK_OK,
   CALLBACK_DATABASE_ERROR,
   CALLBACK_ERROR
} CALLBACK_RESULT_TYPE;

// The most epoll events that can be active
// the restart_event and the postgres_event cannot be active at the same time
static const int MAX_EPOLL_EVENTS = 2;

typedef CALLBACK_RESULT_TYPE (* epoll_callback)(const struct Config * config, 
                                                struct State * state);
const char * PROGRAM_NAME = "skeeter";

// forward reference for callbacks
int
start_postgres_connection(const struct Config * config, struct State * state);

//---------------------------------------------------------------------------
// compute the default path to the config file $HOME/.skeeterrc
// return 0 for success, -1 for failure
static int
compute_default_config_path(bstring * config_path) {
//---------------------------------------------------------------------------
   char * home_dir = NULL;

   home_dir = getenv("HOME");
   check(home_dir != NULL, "getenv");
   *config_path = bfromcstr(home_dir);
   check(*config_path != NULL, "bfromcstr");
   check(bcatcstr(*config_path, "/.skeeterrc") == BSTR_OK, "bcatstr home_dir");

   return 0;

error:
   return -1;
}

//---------------------------------------------------------------------------
// utility function for setting up epoll for postgres
// returns 0 on success, -1 on error
static int
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
// find the position of the channel name in config->channel_list
// this is the corresponding position in state->channel_counts
// TODO: if we sort the channel names we can do a binary search
int
_find_channel_index(const struct Config * config, const bstring channel) {
//----------------------------------------------------------------------------
   int i;
   for (i=0; i < config->channel_list->qty; i++) {
      if (bstrcmp(channel, config->channel_list->entry[i]) == 0) {
         return i;
      }
   }
   return -1;
}

//----------------------------------------------------------------------------
CALLBACK_RESULT_TYPE
check_notifications_cb(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   (void) config; // unused
   PGnotify * notification;
   int message_list_size;
   struct bstrList * message_list;
   int channel_index = -1;

   ConnStatusType status = PQstatus(state->postgres_connection);
   check(status == CONNECTION_OK, 
         "Invalid status in callback '%s'", CONN_STATUS[status]);
   
   if (PQconsumeInput(state->postgres_connection) != 1) {
      log_err("PQconsumeInput %s", 
              PQerrorMessage(state->postgres_connection));
      return CALLBACK_DATABASE_ERROR;
   }

   bool more_notifications = true;
   while (more_notifications) {
      notification = PQnotifies(state->postgres_connection);
      if (notification != NULL) {

         // build the message list
         message_list = bstrListCreate();
         check(message_list != NULL, "bstrListCreate");

         message_list_size = (notification->extra == NULL) ? 2 : 3;
         check(bstrListAlloc(message_list, message_list_size) == BSTR_OK,
               "bstrListAlloc");

         // first message: topic
         message_list->entry[0] = bfromcstr(notification->relname);
         check(message_list->entry[0] != NULL, "bfromcstr 0");

         // second message: meta data
         channel_index = _find_channel_index(config, message_list->entry[0]);
         check(channel_index != -1, "channel_index");
         state->channel_counts[channel_index]++;
         debug("%s %ld", 
               notification->relname, 
               state->channel_counts[channel_index]);
         message_list->entry[1] = \
            bformat("timestamp=%d;sequence=%d",
                    time(NULL),
                    state->channel_counts[channel_index]);
         check(message_list->entry[1] != NULL, "bformat");

         // third message: data (if present)
         if (notification->extra != NULL) {
            message_list->entry[2] = bfromcstr(notification->extra);
            check(message_list->entry[2] != NULL, "bfromcstr 2");
         }
         message_list->qty = message_list_size; 

         PQfreemem(notification);

         // publish the message list
         check(publish_message(message_list, state->zmq_pub_socket) == 0,
               "publish_message");

         // clean up the message list
         check(bstrListDestroy(message_list) == BSTR_OK, "bstrListDestroy");

      } else {
         more_notifications = false;
      }
   }

   return CALLBACK_OK;

error:

   return CALLBACK_ERROR;
}

//----------------------------------------------------------------------------
CALLBACK_RESULT_TYPE
check_listen_command_cb(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   (void) config; // unused
   PGresult * result = NULL;
   int ctl_result;

   ConnStatusType status = PQstatus(state->postgres_connection);
   if (status != CONNECTION_OK) { 
      log_err("Invalid status in callback '%s'", CONN_STATUS[status]);
      return CALLBACK_DATABASE_ERROR;
   }
   
   result = PQgetResult(state->postgres_connection);
   if (result == NULL) {
      ctl_result = set_epoll_ctl_for_postgres(EPOLL_READ, 
                                              check_notifications_cb,
                                              state);
      check(ctl_result == 0, "query complete");
   } else {
      PQclear(result);
      if (PQconsumeInput(state->postgres_connection) != 1) { 
         log_err("PQconsumeInput %s", 
                 PQerrorMessage(state->postgres_connection));
         return CALLBACK_DATABASE_ERROR;
      }
   }

   return CALLBACK_OK;

error:

   if (result != NULL) PQclear(result);
   return CALLBACK_ERROR;
}

//----------------------------------------------------------------------------
int
send_listen_command(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   bstring bquery = NULL;
   bstring item = NULL;
   const char * item_str;
   const char * query = NULL;
   int i;

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
CALLBACK_RESULT_TYPE
heartbeat_timer_cb(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   (void) config; // unused
   int message_list_size = 2;
   struct bstrList * message_list;
   uint64_t expiration_count = 0;
   ssize_t bytes_read = read(state->heartbeat_timer_fd, 
                             &expiration_count, 
                             sizeof(expiration_count));
   check(bytes_read == sizeof(expiration_count), "read timerfd");

   // build the message list
   message_list = bstrListCreate();
   check(message_list != NULL, "bstrListCreate");

   check(bstrListAlloc(message_list, message_list_size) == BSTR_OK,
         "bstrListAlloc");

   // first message is topic
   message_list->entry[0] = bfromcstr("heartbeat");
   check(message_list->entry[0] != NULL, "bfromcstr 0");

   // second message is meta data
   state->heartbeat_count++;
   debug("heartbeat %ld", state->heartbeat_count);
   message_list->entry[1] = \
      bformat("timestamp=%d;sequence=%d;connected=%d",
              time(NULL),
              state->heartbeat_count,
              state->postgres_connect_time);
   check(message_list->entry[1] != NULL, "bformat");
   message_list->qty = message_list_size; 

   // publish the message list
   check(publish_message(message_list, state->zmq_pub_socket) == 0,
         "publish_message");

   // clean up the message list
   check(bstrListDestroy(message_list) == BSTR_OK, "bstrListDestroy");

   return CALLBACK_OK;

error:

   return CALLBACK_ERROR;
}

//----------------------------------------------------------------------------
// try to restart the postgres connection
// return 0 on success, 1 on failure
CALLBACK_RESULT_TYPE
restart_timer_cb(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   (void) config; // unused
   int result;
   uint64_t expiration_count = 0;
   ssize_t bytes_read = read(state->restart_timer_fd, 
                             &expiration_count, 
                             sizeof(expiration_count));
   check(bytes_read == sizeof(expiration_count), "read timerfd");
   debug("restart timer fired expiration_count = %ld", expiration_count);

   // turn off the restart timer
   result = epoll_ctl(state->epoll_fd,
                      EPOLL_CTL_DEL,
                      state->restart_timer_fd,
                      &state->restart_timer_event);
   check(result == 0, "epoll restart timer");
   check(close(state->restart_timer_fd) == 0, "close");

   if (start_postgres_connection(config, state) != 0) {
      return CALLBACK_DATABASE_ERROR;
   }

   return CALLBACK_OK;

error:

   return CALLBACK_ERROR;
}

//----------------------------------------------------------------------------
CALLBACK_RESULT_TYPE
postgres_connection_cb(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------

   PostgresPollingStatusType polling_status;
   int ctl_result;

   polling_status = PQconnectPoll(state->postgres_connection);

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
         state->postgres_connect_time = time(NULL);
         check(send_listen_command(config, state) == 0, "send_listen_command");
         ctl_result = set_epoll_ctl_for_postgres(EPOLL_READ, 
                                                 check_listen_command_cb,
                                                 state);
         check(ctl_result == 0, "postgres_connection_cb");
         break;
         
      default:
         log_err("invalid Postgres Polling Status %s postgres_connection_cb",  
                  POLLING_STATUS[polling_status]);
         return CALLBACK_DATABASE_ERROR;
         
   } //switch 

   return CALLBACK_OK;

error:
   return CALLBACK_ERROR;
}


//----------------------------------------------------------------------------
// start the asynchronous connection process
// returns 0 on success, 1 on failure
int
start_postgres_connection(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   PostgresPollingStatusType polling_status;
   int ctl_result;

   state->postgres_connection = \
      PQconnectStartParams(config->postgresql_keywords, 
                           config->postgresql_values, 
                           0);
   check(state->postgres_connection != NULL, "PQconnectStartParams");
   check(PQstatus(state->postgres_connection) != CONNECTION_BAD, 
         "CONNECTION_BAD");

   polling_status = PQconnectPoll(state->postgres_connection);
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

   state->heartbeat_timer_fd = \
      create_and_set_timer(config->heartbeat_interval);
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
                           ZMQ_SNDHWM,
                           &config->pub_socket_hwm,
                           sizeof config->pub_socket_hwm);
   check(result == 0, "zmq_setsockopt");

   log_info("binding PUB socket to '%s'", config->pub_socket_uri);
   check(zmq_bind(state->zmq_pub_socket, config->pub_socket_uri) == 0, 
         "bind %s",
         config->pub_socket_uri);

   return 0;

error:

   if (pub_socket_uri != NULL) bcstrfree(pub_socket_uri);
   return 1;
}

//----------------------------------------------------------------------------
// start the retry timer to re-try connecting to the database
int
set_up_database_retry(const struct Config * config, struct State * state) {
//----------------------------------------------------------------------------
   int result;

   // don't check the state here, our socket fd may be no good
   epoll_ctl(state->epoll_fd,
             EPOLL_CTL_DEL,
             PQsocket(state->postgres_connection),
             &state->postgres_event);
   state->postgres_event.events = 0;

   PQfinish(state->postgres_connection); 
   state->postgres_connection = NULL;
   state->postgres_connect_time = 0;

   state->restart_timer_fd = \
      create_and_set_timer(config->database_retry_interval);
   check(state->restart_timer_fd != -1, "create_and_set_timer");
   state->restart_timer_event.events = EPOLLIN | EPOLLERR;
   state->restart_timer_event.data.ptr = (void *) restart_timer_cb;

   result = epoll_ctl(state->epoll_fd,
                      EPOLL_CTL_ADD,
                      state->restart_timer_fd,
                      &state->restart_timer_event);
   check(result == 0, "epoll restart timer");

   return 0;
error:
   return -1;
}

//----------------------------------------------------------------------------
int
main(int argc, char **argv, char **envp) {
//----------------------------------------------------------------------------
   (void) envp; // unused
   bstring config_path = NULL;
   const struct Config * config = NULL;
   struct State * state = NULL;
   void *zmq_context = NULL;
   int result;
   CALLBACK_RESULT_TYPE callback_result;
   struct epoll_event event_list[MAX_EPOLL_EVENTS];
   int i;

#if defined(NDEBUG)
   openlog(PROGRAM_NAME, LOG_CONS | LOG_PERROR, LOG_USER);
#endif

   log_info("program starts");

   check(parse_command_line(argc, argv, &config_path) == 0, "parse_");
   if (blength(config_path) == 0) {
      check(compute_default_config_path(&config_path) == 0, "default config");
   }

   // initilize our basic structs
   config = load_config(config_path);
   check(config != NULL, "load_config");
   state = create_state(config);
   check(state != NULL, "create_state");

   zmq_context = zmq_init(config->zmq_thread_pool_size);
   check(zmq_context != NULL, "initializing zeromq");
  
   check(initialize_state(config, zmq_context, state) == 0, "initialize_state");

   // start polling the heartbeat timer
   result = epoll_ctl(state->epoll_fd,
                      EPOLL_CTL_ADD,
                      state->heartbeat_timer_fd,
                      &state->heartbeat_timer_event);
   check(result == 0, "epoll heartbeat timer");

   // start postgres connection process
   if (start_postgres_connection(config, state) != 0) { 
      log_err("unable to start posgres connection");
      check(set_up_database_retry(config, state) == 0, "retry");
   }

   // main epoll loop, using callbacks to drive he program
   check(install_signal_handler() == 0, "install signal handler");
   while (!halt_signal) {
      result = epoll_wait(state->epoll_fd,
                          event_list,
                          MAX_EPOLL_EVENTS,
                          config->epoll_timeout * 1000); 
      // we can get 'interrupted system call' from zeromq at shutdown
      // we don't treat it as an error
      check(halt_signal || result != -1, "epoll_wait")
      if (result == 0) {
         continue;
      }
      for (i=0; i < result; i++) {
         check(event_list[i].data.ptr != NULL, "NULL callback");
         callback_result = \
            ((epoll_callback) event_list[i].data.ptr)(config, state);
         if (callback_result == CALLBACK_DATABASE_ERROR) {
            log_err("database error");
            check(set_up_database_retry(config, state) == 0, "retry");
         } else {
            check(callback_result == CALLBACK_OK, "callback");
         } 
      }
   } // while
   debug("while loop broken");

   clear_state(state);
   clear_config(config);
   check(zmq_term(zmq_context) == 0, "terminating zeromq")
   check(bdestroy(config_path) == BSTR_OK, "bdestroy");
   log_info("program terminates normally");
#if defined(NDEBUG)
   closelog();
#endif
   return 0;

error:
   if (state != NULL) clear_state(state);
   if (config != NULL) clear_config(config);
   if (zmq_context != NULL) zmq_term(zmq_context);
   log_info("program terminates with error");
#if defined(NDEBUG)
   closelog();
#endif
   return 1;
}
