/*----------------------------------------------------------------------------
 * signal_handler.c
 * creatge and install a signal handler to set halt_signal to 1 on
 * SIGKILL or SIGTERM
 *
 *--------------------------------------------------------------------------*/

#include <signal.h>
#include <stdbool.h>

#include "dbg.h"

bool halt_signal = false;

static void
signal_handler(int signal) {
   debug("signal %d", signal);
   halt_signal = true;
}

// install the same signal handler for SIGINT and SIGTERM
int 
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

