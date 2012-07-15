/*----------------------------------------------------------------------------
 * command_line.h
 * 
 * parse the command line
 *--------------------------------------------------------------------------*/
#if !defined(__COMMAND_LINE_H__)
#define __COMMAND_LINE_H__

#include "bstrlib.h"


// sets config_path from -c or --config-path
// config_path unchanged if one of these arguments is not present
// returns 0 for success, -1 for failure
extern int
parse_command_line(int argc, char ** argv, bstring * config_path);

#endif // !defined(__COMMAND_LINE_H__)
