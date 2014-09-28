/*----------------------------------------------------------------------------
 * message.c
 * 
 * publish a zeromq message
 *--------------------------------------------------------------------------*/

#include <zmq.h>

#include "bstrlib.h"
#include "dbg_syslog.h"
#include "zmq_shim.h"

//---------------------------------------------------------------------------
// send a (possibly multipart) message over the pub socket
// all strings are copied to zmq message structures
// it is the responsibility of the caller to clean up message_list
// return 0 for success, -1 for failure
int
publish_message(const struct bstrList * message_list, void * zmq_pub_socket) {
//---------------------------------------------------------------------------
   int i;
   zmq_msg_t message;
   size_t message_size;
   const char * cstr;
   int flag;
   int result;

   for (i=0; i < message_list->qty; i++) {
      message_size = blength(message_list->entry[i]);
      check(zmq_msg_init_size(&message, message_size) == 0,
            "zmq_init_size %d",
            (int) message_size);
      cstr = bstr2cstr(message_list->entry[i], '?');
      check(cstr != NULL, "bstr2cstr");
      memcpy(zmq_msg_data(&message), cstr, message_size);
      check(bcstrfree((char *)cstr) == BSTR_OK, "bcstrfree");
      flag = (i == (message_list->qty)-1) ? 0 : ZMQ_SNDMORE;
      result = zmq_msg_send(&message, zmq_pub_socket, flag);
      check(result != -1, "zmq_send channel_message");
      check(zmq_msg_close(&message) == 0, "close messge");

   }
   return 0;

error:
   return -1;
}

