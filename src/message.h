/*----------------------------------------------------------------------------
 * message.h
 * 
 * publish a zeromq message
 *--------------------------------------------------------------------------*/
#if !defined(__MESSAGE__H__)
#define __MESSAGE__H__

#include "bstrlib.h"

// send a (possibly multipart) message over the pub socket
// all stringws are copied to zmq message structures
// it is the responsibility of the caller to clean up message_list
// return 0 for success, -1 for failure
int
publish_message(const struct bstrList * message_list, void * zmq_pub_socket);

#endif // !defined(__MESSAGE__H__)
