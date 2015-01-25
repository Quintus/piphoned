#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include "main.h"

int main(int argc, char* argv[])
{
  /* We need root rights to initialize everything. */
  if (getuid() != 0) {
    fprintf(stderr, "This program has to be run as root. Exiting.\n");
    return 1;
  }

  /***************************************
   * Init logger
   **************************************/

  setlogmask(LOG_UPTO(LOG_DEBUG)); /* TODO: Make user-configurable */
  openlog("piphoned", LOG_CONS | LOG_ODELAY | LOG_PID, LOG_DAEMON);
  syslog(LOG_NOTICE, "Starting up.");

  /***************************************
   * Initializing libraries
   **************************************/
  wiringPiSetup(); /* Requires root */

  /***************************************
   * Config file parsing & initializing
   **************************************/
  piphoned_config_init(); /* sets g_piphoned_config_info */

  /***************************************
   * Cleanup
   **************************************/

  syslog(LOG_NOTICE, "Program finished.");
  closelog();
}
