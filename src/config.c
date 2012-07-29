/*----------------------------------------------------------------------------
 * config.c
 * 
 * configuration from skeeterrc
 *--------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "bstrlib.h"
#include "config.h"
#include "dbg_syslog.h"

int
bstr2int(bstring bstr) {
   char * cstr;
   int value;

   cstr = bstr2cstr(bstr, '?');
   value = atoi(cstr);
   bcstrfree(cstr);
   
   return value;
}
   
long
bstr2long(bstring bstr) {
   char * cstr;
   long value;

   cstr = bstr2cstr(bstr, '?');
   value = atol(cstr);
   bcstrfree(cstr);
   
   return value;
}

//----------------------------------------------------------------------------
// create a postgres keyword entry from a config line that begins wiht the
// postgres prefix
int
postgres_entry(struct Config * config, 
               bstring postgres_prefix,
               int count, 
               struct bstrList * split_list) {
//----------------------------------------------------------------------------
   bstring keyword;

   keyword = bmidstr(split_list->entry[0], 
                     blength(postgres_prefix),
                     blength(split_list->entry[0]));
   check(keyword != NULL, "bmidstr");

   config->postgresql_keywords = realloc(config->postgresql_keywords,
                                         (count+1) * sizeof(char *));
   check_mem(config->postgresql_keywords);
   config->postgresql_keywords[count-1] = bstr2cstr(keyword, '?');
   config->postgresql_keywords[count] = NULL;

   check(bdestroy(keyword) == BSTR_OK, "keyword");

   config->postgresql_values = realloc(config->postgresql_values,
                                       (count+1) * sizeof(char *));
   check_mem(config->postgresql_values);
   config->postgresql_values[count-1] = bstr2cstr(split_list->entry[1], '?');
   debug("value = '%s'", config->postgresql_values[count-1]);
   config->postgresql_values[count] = NULL;
   
   return 0;
error:
   return -1;
}

int 
parse_channel_list(struct Config * config, bstring entry) {
   int i;

   config->channel_list = bsplit(entry, ',');
   check(config->channel_list != NULL, "bsplit");
   for (i=0; i < config->channel_list->qty; i++) {
      check(btrimws(config->channel_list->entry[i]) == BSTR_OK,
            "btrimws");
   }

   return 0;
error:
   return -1;
}

int
set_config_defaults(struct Config * config) {
   // set defaults
   config->zmq_thread_pool_size = 3;
   config->heartbeat_interval = 10;
   config->epoll_timeout = 1;
   config->pub_socket_uri = NULL;
   config->pub_socket_hwm = 5;

   config->postgresql_keywords = malloc(sizeof(char *));
   check_mem(config->postgresql_keywords);
   config->postgresql_keywords[0] = NULL;

   config->postgresql_values = malloc(sizeof(char *));
   check_mem(config->postgresql_values);
   config->postgresql_values[0] = NULL;

   config->channel_list = NULL;

   return 0;

error:
   return -1;
}

//----------------------------------------------------------------------------
// load config from skeeterrc
const struct Config *
load_config(bstring config_path) {
//----------------------------------------------------------------------------
   struct Config * config = NULL;
   FILE * config_stream = NULL;
   struct bStream * config_bstream = NULL;
   const char * config_path_cstr = NULL;
   bstring line = bfromcstr("");;
   int read_result;
   struct bstrList * split_list;
   bstring postgres_prefix = bfromcstr("postgresql-");
   int postgres_count = 0;

   config_path_cstr = bstr2cstr(config_path, '?');
   check(config_path_cstr != NULL, "bstr2cstr");

   config = malloc(sizeof(struct Config)); 
   check_mem(config);
   bzero(config, sizeof(struct Config));

   check(set_config_defaults(config) == 0, "set_config_defaults");

   config_stream = fopen(config_path_cstr, "r");
   check(config_stream != NULL, "fopen(%s)", config_path_cstr);
   config_bstream = bsopen((bNread) fread, config_stream);
   check(config_bstream != NULL, "bsopen");

   for (;;) {
      read_result = bsreadln(line, config_bstream, '\n');
      if (bseof(config_bstream)) break;
      check(read_result == BSTR_OK, "bsreadln");

      // skip blank lines and comments
      switch (bchar(line, 0)) {
         case '\0':
         case ' ':
         case '\n':
         case '#':
            continue;
      }

      if (bstrchrp(line, '=', 0) == BSTR_ERR) {
         log_err("Invalid config line %s", bstr2cstr(line, '?'));
         continue;
      }

      split_list = bsplit(line, '=');
      check(split_list != NULL, "NULL split_list");
      check(split_list->qty == 2, "split error %d", split_list->qty);
      check(btrimws(split_list->entry[0]) == BSTR_OK, "trim[0]")
      check(btrimws(split_list->entry[1]) == BSTR_OK, "trim[1]")

      if (biseqcstr(split_list->entry[0], "zmq_thread_pool_size")) {
         config->zmq_thread_pool_size = bstr2int(split_list->entry[1]);
      } else if (biseqcstr(split_list->entry[0], "pub_socket_uri")) {
         config->pub_socket_uri = bstr2cstr(split_list->entry[1], '?');
      } else if (biseqcstr(split_list->entry[0], "pub_socket_hwm")) {
         config->pub_socket_hwm = bstr2long(split_list->entry[1]);
      } else if (biseqcstr(split_list->entry[0], "epoll_timeout")) {
         config->epoll_timeout = bstr2int(split_list->entry[1]);
      } else if (biseqcstr(split_list->entry[0], "heartbeat_interval")) {
         config->heartbeat_interval = bstr2int(split_list->entry[1]);
      } else if (biseqcstr(split_list->entry[0], "database_retry_interval")) {
         config->database_retry_interval = bstr2int(split_list->entry[1]);
      } else if (bstrncmp(split_list->entry[0], 
                          postgres_prefix, 
                          blength(postgres_prefix)) == 0) {
         postgres_count++;
         check(postgres_entry(config, 
                              postgres_prefix, 
                              postgres_count, 
                              split_list) == 0, 
               "postgres_entry");
      } else if (biseqcstr(split_list->entry[0], "channels")) {
         check(parse_channel_list(config, split_list->entry[1]) == 0,
               "parse_channel_list");
      } else {
         log_err("unknown keyword '%s", bstr2cstr(split_list->entry[0], '?'));
      }

      check(bstrListDestroy(split_list) == BSTR_OK, "bstrListDestroy");
   }

   check(bdestroy(line) == BSTR_OK, "bdestroy(line)");
   check(bdestroy(postgres_prefix) == BSTR_OK, "bdestroy(postgres_prefix");
   check(bsclose(config_bstream) != NULL, "bsclose");
   check(fclose(config_stream) == 0, "fclose");
   check(bcstrfree((char *)config_path_cstr) == BSTR_OK, "bcstrfree");

   return config;

error:
   if (config_bstream != NULL) bsclose(config_bstream);
   if (config_stream != NULL) fclose(config_stream);
   if (config_path_cstr != NULL) bcstrfree((char *)config_path_cstr);

   return NULL;
}

//----------------------------------------------------------------------------
// release resources used by config
// we do this mostly to make it easier to read valgrind output
void
clear_config(const struct Config * config) {
//----------------------------------------------------------------------------
   int i;

   bcstrfree((char *) config->pub_socket_uri); 
   if (config->channel_list != NULL) {
      bstrListDestroy(config->channel_list);
   }
   for (i=0; ;i++) {
      if (config->postgresql_keywords[i] == NULL) break;
      bcstrfree((char *) config->postgresql_keywords[i]);
      bcstrfree((char *) config->postgresql_values[i]);
   }
   free((void *) config->postgresql_keywords);
   free((void *) config->postgresql_values);

   free((void *) config);
}

