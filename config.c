/*----------------------------------------------------------------------------
 * config.c
 * 
 * configuration from skeeterrc
 *--------------------------------------------------------------------------*/
#include <stdlib.h>
#include <strings.h>

#include "bstrlib.h"
#include "config.h"
#include "dbg.h"

//----------------------------------------------------------------------------
// load config from skeeterrc
const struct Config *
load_config() {
//----------------------------------------------------------------------------
   // TODO: load config from skeeterrc
   const char * input_strings[] = {
      "channel1",
      "channel2",
      "channel3",
      NULL
   };

   int i;
   struct Config * config = malloc(sizeof(struct Config)); 
   check_mem(config);
   bzero(config, sizeof(struct Config));

   config->zmq_thread_pool_size = 3;
   config->heartbeat_period = 15;
   config->epoll_timeout = 5 * 1000;

   config->channel_list = bstrListCreate();
   check_mem(config->channel_list);

   for (i=0; input_strings[i] != NULL; i++) {
      check(bstrListAlloc(config->channel_list, i+1) == BSTR_OK,
            "bstrListAlloc");
      config->channel_list->entry[i] = bfromcstr(input_strings[i]);
      config->channel_list->qty += 1;
   }

   return config;

error:

   return NULL;
}

//----------------------------------------------------------------------------
// release resources used by config
void
clear_config(const struct Config * config) {
//----------------------------------------------------------------------------
   if (config->channel_list != NULL) {
      bstrListDestroy(config->channel_list);
   }
   free((void *) config);
}

