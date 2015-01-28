#include <unistd.h>
#include "commandline.h"

struct Piphoned_CommandlineInfo g_cli_options;

static void setup_defaults();

void piphoned_commandline_info_from_argv(int argc, char* argv[])
{
  setup_defaults();

  /* TODO */
}

void setup_defaults()
{
  g_cli_options.daemonize = true;
}
