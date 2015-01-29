#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include "commandline.h"

struct Piphoned_Commandline_Info g_cli_options;

static void setup_defaults();
static void process_options(int argc, char* argv[]);
static void print_help(const char* progname);

/**
 * Sets up the global `g_cli_options` variable that contains the
 * parsed commandline arguments.
 */
void piphoned_commandline_info_from_argv(int argc, char* argv[])
{
  setup_defaults();
  process_options(argc, argv);
}

/**
 * Processes the given options and command and fills in
 * `g_cli_options`.
 */
void process_options(int argc, char* argv[])
{
  int option = 0;
  while ((option = getopt(argc, argv, "dhc:l:")) > 0) { /* Single = intended */
    switch(option) {
    case 'd':
      g_cli_options.daemonize = false;
      break;
    case 'h':
      print_help(argv[0]);
      break;
    case 'c':
      g_cli_options.config_file = optarg;
      break;
    case 'l':
      g_cli_options.loglevel = atoi(optarg);
      break;
    default: /* '?' */
      fprintf(stderr, "Invalid option encountered, see -h.\n");
      exit(1);
    }
  }

  if (argc <= 1 || optind >= argc) {
    fprintf(stderr, "No command specified, see -h.\n");
    exit(1);
  }

  if (strcmp(argv[optind], "start") == 0)
    g_cli_options.command = PIPHONED_COMMAND_START;
  else if (strcmp(argv[optind], "stop") == 0)
    g_cli_options.command = PIPHONED_COMMAND_STOP;
  else if (strcmp(argv[optind], "restart") == 0)
    g_cli_options.command = PIPHONED_COMMAND_RESTART;
  else {
    fprintf(stderr, "Invalid command encountered, see -h.\n");
    exit(1);
  }
}

/**
 * Sets up the default values for the global `g_cli_options`
 * variable containing the parsed commandline arguments.
 */
void setup_defaults()
{
  g_cli_options.daemonize = true;
  g_cli_options.config_file = "/etc/piphoned.conf";
  g_cli_options.loglevel = LOG_NOTICE;
}

void print_help(const char* progname)
{
  printf("Usage:\n\
%s [-d] [-c FILE] COMMAND\n\
\n\
Options:\n\
\n\
-d: Do not fork (for debugging)\n\
-c FILE: Use FILE as the config file instead of /etc/piphoned.conf\n\
-l LEVEL: Use LEVEL as the log level. 7 is debug, 0 is basically silence.\n\
\n\
COMMAND may be 'start', 'stop', or 'restart'.\n", progname);
  exit(0);
}
