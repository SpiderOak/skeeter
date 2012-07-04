/*----------------------------------------------------------------------------
 * state.c
 * 
 * State struct and supporting functions
 *--------------------------------------------------------------------------*/
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include "dbg.h"
#include "state.h"

//----------------------------------------------------------------------------
struct State *
create_state() {
//----------------------------------------------------------------------------
   struct State * state = malloc(sizeof(struct State));
   check_mem(state);
   bzero(state, sizeof(struct State));

   state->heartbeat_timer_fd = -1;

   state->restart_timer_fd = -1;

   state->postgres_connection = NULL;

   state->epoll_fd = -1;

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
   free(state);
}

