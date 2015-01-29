#ifndef PIPHONED_COMMANDLINE_H
#define PIPHONED_COMMANDLINE_H
#include <stdbool.h>

enum Piphoned_Commandline_Command
{
  PIPHONED_COMMAND_START = 1,
  PIPHONED_COMMAND_STOP,
  PIPHONED_COMMAND_RESTART
};

struct Piphoned_Commandline_Info
{
  bool daemonize;          /*< Do we want to fork()? */
  const char* config_file; /*< Configuration file to load */
  int loglevel;            /*< Syslog log level, from 7 (debug) to 0 (nothing) */

  enum Piphoned_Commandline_Command command; /*< Command to run */
};

void piphoned_commandline_info_from_argv(int argc, char* argv[]);

extern struct Piphoned_Commandline_Info g_cli_options;

#endif
