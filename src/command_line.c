/*----------------------------------------------------------------------------
 * command_line.c
 * 
 * parse the command line
 *--------------------------------------------------------------------------*/
#include <unistd.h>

#include "bstrlib.h"
#include "dbg_syslog.h"

static const char * optstring = "c:";

//----------------------------------------------------------------------------
// sets config_path from -c
// returns 0 for success, -1 for failure
int
parse_command_line(int argc, char **argv, bstring *config_path) {
//----------------------------------------------------------------------------
   int opt;

   while ((opt = getopt(argc, argv, optstring)) != -1) {
      check(opt == 'c', "unexpectged opt '%d'", opt);
      check(optarg != NULL, "NULL arg");
      *config_path = bfromcstr(optarg);
      check(*config_path != NULL, "bfromcstr");
   }
   
   return 0;
error:
   return -1;
}
