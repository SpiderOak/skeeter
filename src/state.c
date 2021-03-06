/*----------------------------------------------------------------------------
 * state.c
 * 
 * State struct and supporting functions
 *--------------------------------------------------------------------------*/
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <zmq.h>

#include "dbg_syslog.h"
#include "state.h"

//----------------------------------------------------------------------------
struct State *
create_state(const struct Config * config) {
//----------------------------------------------------------------------------
   struct State * state = malloc(sizeof(struct State));
   check_mem(state);
   bzero(state, sizeof(struct State));

   state->heartbeat_timer_fd = -1;

   state->restart_timer_fd = -1;

   state->postgres_connection = NULL;
   state->postgres_connect_time = 0;

   state->epoll_fd = -1;

   state->zmq_pub_socket = NULL;

   state->heartbeat_count = 0;

   state->channel_counts = calloc(config->channel_list->qty, sizeof(uint64_t));
   check_mem(state->channel_counts);

   return state;

error:

   return NULL;
}


//----------------------------------------------------------------------------
// release resources used by state
void
clear_state(struct State * state) {
//----------------------------------------------------------------------------
   if (state->heartbeat_timer_fd != -1) close(state->heartbeat_timer_fd);
   if (state->restart_timer_fd != -1) close(state->restart_timer_fd);
   if (state->postgres_connection != NULL) {
      PQfinish(state->postgres_connection); 
   }
   if (state->epoll_fd != -1) close(state->epoll_fd);
   if (state->zmq_pub_socket != NULL) zmq_close(state->zmq_pub_socket);
   free(state->channel_counts);
   free(state);
}

