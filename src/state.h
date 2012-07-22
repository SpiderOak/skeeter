/*----------------------------------------------------------------------------
 * state.h
 * 
 * State struct and supporting functions
 *--------------------------------------------------------------------------*/
#if !defined(__STATE_H__)
#define __STATE_H__

#include <sys/epoll.h>

#include <libpq-fe.h>

#include "config.h"

struct State {
   int heartbeat_timer_fd;
   struct epoll_event heartbeat_timer_event;

   int restart_timer_fd;
   struct epoll_event restart_timer_event;

   PGconn * postgres_connection;
   time_t postgres_connect_time;
   struct epoll_event postgres_event;

   int epoll_fd;

   void * zmq_pub_socket;

   uint64_t heartbeat_count;

   // parallel array to config.channel_list
   uint64_t * channel_counts;
};

extern struct State *
create_state(const struct Config * config);

extern void
clear_state(struct State * state);

#endif // !defined(__STATE_H__)
