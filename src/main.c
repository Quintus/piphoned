#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <wiringPi.h>
#include "main.h"
#include "configfile.h"

int main(int argc, char* argv[])
{
  pid_t childpid = 0;
  pid_t sessionid = 0;
  FILE* file = NULL;

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
   * Daemonising
   ***************************************/

  childpid = fork();
  if (childpid < 0) {
    syslog(LOG_CRIT, "Fork failed: %m");
    return 3;
  }
  else if (childpid > 0) { /* Parent */
    syslog(LOG_INFO, "Fork successful. Child PID is %d, going to exit parent process.", childpid);
    goto finish;
  }
  /* Child */

  sessionid = setsid();
  if (sessionid < 0) {
    syslog(LOG_CRIT, "Failed to acquire session ID.");
    return 3;
  }

  umask(0137); /* rw-r----- */
  chdir("/");

  file = fopen(g_piphoned_config_info.pidfile, "w");
  if (!file) {
    syslog(LOG_CRIT, "Failed to open PID file '%s': %m", g_piphoned_config_info.pidfile);
    return 3;
  }

  fprintf(file, "%d", getpid());
  fclose(file);

  /* We have no terminal anymore */
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  syslog(LOG_INFO, "Fork setup completed.");

  /***************************************
   * Privilege dropping
   ***************************************/

  if (setgid(g_piphoned_config_info.gid) != 0) {
    syslog(LOG_CRIT, "Failed to drop group privileges: %m. Exiting!");
    return 3;
  }
  if (setuid(g_piphoned_config_info.uid) != 0) {
    syslog(LOG_CRIT, "Failed to drop user privileges: %m. Exiting!");
    return 3;
  }

  /* Paranoid extra security check */
  if (setuid(0) != -1) {
    syslog(LOG_CRIT, "Regained root privileges! Exiting!");
    return 3;
  }

  syslog(LOG_INFO, "Successfully dropped privileges.");

  /***************************************
   * Cleanup
   **************************************/

 finish:
  piphoned_config_free();
  syslog(LOG_NOTICE, "Program finished.");
  closelog();
}
