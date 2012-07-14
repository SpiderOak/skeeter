/*----------------------------------------------------------------------------
 * command_line.c
 * 
 * parse the command line
 *--------------------------------------------------------------------------*/
#include <unistd.h>

#include "bstrlib.h"
#include "dbg.h"

static const char * optstring = "c:";

//----------------------------------------------------------------------------
// sets config_path from -c
// config_path unchanged if one of these arguments is not present
// returns 0 for success, -1 for failure
int
parse_command_line(int argc, char **argv, bstring config_path) {
//----------------------------------------------------------------------------
   int opt;

   check(blength(config_path) == 0, "expecting empty config_path");

   while ((opt = getopt(argc, argv, optstring)) != -1) {
      check(opt == 'c', "unexpectged opt '%d'", opt);
      check(optarg != NULL, "NULL arg");
      check(bcatcstr(config_path, optarg) == BSTR_OK, "bcatcstr");
   }
   
   return 0;
error:
   return -1;
}
