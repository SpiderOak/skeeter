/*----------------------------------------------------------------------------
 * state.h
 * 
 * State struct and supporting functions
 *--------------------------------------------------------------------------*/
#if !defined(__STATE_H__)
#define __STATE_H__

#include <sys/epoll.h>

#include <libpq-fe.h>

struct State {
   int heartbeat_timer_fd;
   struct epoll_event heartbeat_timer_event;

   int restart_timer_fd;
   struct epoll_event restart_timer_event;

   PGconn * postgres_connection;
   struct epoll_event postgres_event;

   int epoll_fd;
};

extern struct State *
create_state(void);

extern void
clear_state(struct State * state);

#endif // !defined(__STATE_H__)
