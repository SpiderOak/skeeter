/*----------------------------------------------------------------------------
 * signal_handler.h
 * create and install a signal handler to set halt_signal to 1 on
 * SIGKILL or SIGTERM
 *
 *--------------------------------------------------------------------------*/
#if !defined(__SIGNAL_HANDLER_H__)
#define __SIGNAL_HANDLER_H__
#include <stdbool.h>

extern bool halt_signal;

int 
install_signal_handler();

#endif // !defined(__SIGNAL_HANDLER_H__)
