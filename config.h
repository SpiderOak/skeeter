/*----------------------------------------------------------------------------
 * config.h
 * 
 * configuration from .skeeterc
 *--------------------------------------------------------------------------*/
#if !defined(__config_h__)
#define __config_h__

#include <time.h>

struct Config {
   int zmq_thread_pool_size;
   time_t heartbeat_period;
   int epoll_timeout;
   struct bstrList * channel_list;
};

// load config from skeeterrc
extern const struct Config *
load_config();

// release resources used by config
extern void
clear_config(const struct Config * config);

#endif // !defined(__config_h__)

