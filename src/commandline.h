#ifndef PIPHONED_COMMANDLINE_H
#define PIPHONED_COMMANDLINE_H
#include <stdbool.h>

struct Piphoned_CommandlineInfo
{
  bool daemonize; /*< Do we want to fork()? */
};

void piphoned_commandline_info_from_argv(int argc, char* argv[]);

extern struct Piphoned_CommandlineInfo g_cli_options;

#endif
