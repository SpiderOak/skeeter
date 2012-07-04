/*----------------------------------------------------------------------------
 * config.h
 * 
 * configuration from .skeeterc
 *--------------------------------------------------------------------------*/
#if !defined(__config_h__)
#define __config_h__

#include <stdint.h>
#include <time.h>

#include "bstrlib.h"

struct Config {
   int zmq_thread_pool_size;
   time_t heartbeat_period;
   int epoll_timeout;
   struct bstrList * channel_list;
   bstring pub_socket_uri;
   uint64_t pub_socket_hwm;
};

// load config from skeeterrc
extern const struct Config *
load_config();

// release resources used by config
extern void
clear_config(const struct Config * config);

#endif // !defined(__config_h__)

