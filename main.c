/*----------------------------------------------------------------------------
 * main.c
 * 
 *
 *--------------------------------------------------------------------------*/
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <zmq.h>

#include <libpq-fe.h>

#include "bstrlib.h"
#include "dbg.h"

static const int ZMQ_THREAD_POOL_SIZE = 3;
static const time_t TIMER_PERIOD = 15;
static const long POLLING_INTERVAL = 5 * 1000 * 1000;
enum DESCRIPTOR_INDICES {
   HEARTBEAT_TIMER,
   DESCRIPTOR_COUNT
};
static int halt_signal = 0;

static void
signal_handler(int signal) {
   debug("signal %d", signal);
   halt_signal = 1;
}

// install the same signal handler for SIGINT and SIGTERM
static int 
install_signal_handler() {

   struct sigaction action;
   action.sa_handler = signal_handler;
   sigemptyset(&action.sa_mask); 
   action.sa_flags = 0;

   check(sigaction(SIGINT, &action, NULL) == 0, "sigaction, SIGINT");
   check(sigaction(SIGTERM, &action, NULL) == 0, "sigaction, SIGTERM");

   return 0;

error:

   return -1;
}

// create and initialize a timerfd for use with poll
// return the fd on success, -1 on error
static int
create_and_set_timer() {

   // create the timer fd
   int timerfd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
   check(timerfd != -1, "timerfd_create");

   // define the firing interval
   struct itimerspec timer_value;
   timer_value.it_interval.tv_sec = TIMER_PERIOD;
   timer_value.it_interval.tv_nsec = 0;
   timer_value.it_value.tv_sec = TIMER_PERIOD; // first expiration
   timer_value.it_value.tv_nsec = 0;

   // set the firing interval
   int result = timerfd_settime(timerfd, 0, &timer_value, NULL);
   check(result == 0, "timerfd_settime");

   return timerfd;

error:

   return -1;
}

int
main(int argc, char **argv, char **envp) {
   log_info("program starts");

   void *zmq_context = zmq_init(ZMQ_THREAD_POOL_SIZE);
   check(zmq_context != NULL, "initializing zeromq");

   int heartbeat_timerfd = create_and_set_timer();
   check(heartbeat_timerfd != -1, "create_and_set_timer");
   
   zmq_pollitem_t poll_items[DESCRIPTOR_COUNT];
   poll_items[HEARTBEAT_TIMER].socket = NULL;
   poll_items[HEARTBEAT_TIMER].fd = heartbeat_timerfd;
   poll_items[HEARTBEAT_TIMER].events = ZMQ_POLLIN;

   check(install_signal_handler() == 0, "install signal handler");

   while (!halt_signal) {
      debug("polling");

      int result = zmq_poll(poll_items, 1, POLLING_INTERVAL); 
   
      // getting a signal here could cause 'Interrupted system call'
      // if we're shutting down, we don't care
      if (halt_signal) break;

      check(result != -1, "polling");
      if (result == 0) continue;

      check(poll_items[HEARTBEAT_TIMER].revents == ZMQ_POLLIN, "poll result");

      uint64_t expiration_count = 0;
      ssize_t bytes_read = read(heartbeat_timerfd, 
                                &expiration_count, 
                                sizeof(expiration_count));
      check(bytes_read == sizeof(expiration_count), "read timerfd");
      debug("timer fired expiration_count = %ld", expiration_count);
   } // while
   debug("while loop broken");

   check(close(heartbeat_timerfd) == 0, "closing timerfd");
   check(zmq_term(zmq_context) == 0, "terminating zeromq")
   log_info("program terminates normally");
   return 0;

error:
   if (heartbeat_timerfd != -1) close(heartbeat_timerfd);
   if (zmq_context != NULL) zmq_term(zmq_context);
   log_info("program terminates with error");
   return 1;
}
