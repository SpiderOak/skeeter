/*----------------------------------------------------------------------------
 * zmq_shim.h
 * 
 * http://zguide.zeromq.org/page:all#Suggested-Shim-Macros
 *--------------------------------------------------------------------------*/
#if !defined(__ZMQ_SHIM_H__)
#define __ZMQ_SHIM_H__

#include <zmq.h>

#if ZMQ_VERSION_MAJOR == 2
#   define zmq_msg_send(msg,sock,opt) zmq_send (sock, msg, opt)
#   define zmq_msg_recv(msg,sock,opt) zmq_recv (sock, msg, opt)
#   define zmq_ctx_destroy(context) zmq_term(context)
#   define ZMQ_POLL_MSEC    1000        //  zmq_poll is usec
#   define ZMQ_SNDHWM ZMQ_HWM
#   define ZMQ_RCVHWM ZMQ_HWM
#elif ZMQ_VERSION_MAJOR == 3
#   define ZMQ_POLL_MSEC    1           //  zmq_poll is msec
#endif

#ifndef ZMQ_DONTWAIT
#   define ZMQ_DONTWAIT     ZMQ_NOBLOCK
#endif

#endif // !defined(__ZMQ_SHIM_H__)
