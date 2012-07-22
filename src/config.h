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

static const size_t MAX_POSTGRESQL_OPTIONS = 20;

struct Config {
   int zmq_thread_pool_size;
   const char *  pub_socket_uri;
   uint64_t pub_socket_hwm;

   int epoll_timeout;
   time_t heartbeat_interval;

   time_t database_retry_interval;
   const char ** postgresql_keywords;
   const char ** postgresql_values;
   struct bstrList * channel_list;
};

// load config from skeeterrc
extern const struct Config *
load_config(bstring config_path);

// release resources used by config
extern void
clear_config(const struct Config * config);

#endif // !defined(__config_h__)

