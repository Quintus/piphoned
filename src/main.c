#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include <wiringPi.h>
#include "main.h"
#include "configfile.h"

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
  piphoned_config_init(argv[1]); /* sets g_piphoned_config_info */

  /***************************************
   * Cleanup
   **************************************/

  piphoned_config_free();
  syslog(LOG_NOTICE, "Program finished.");
  closelog();
}
