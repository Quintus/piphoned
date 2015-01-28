#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <wiringPi.h>
#include <linphone/linphonecore.h>
#include "main.h"
#include "configfile.h"
#include "hwactions.h"

static int mainloop();

int main(int argc, char* argv[])
{
  pid_t childpid = 0;
  pid_t sessionid = 0;
  FILE* file = NULL;
  int retval = 0;

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
  if (childpid < 0) { /* Error */
    syslog(LOG_CRIT, "Fork failed: %m");
    retval = 3;
    goto finish;
  }
  else if (childpid > 0) { /* Parent */
    syslog(LOG_INFO, "Fork successful. Child PID is %d, going to exit parent process.", childpid);
    goto finish;
  }
  /* Child */

  sessionid = setsid();
  if (sessionid < 0) {
    syslog(LOG_CRIT, "Failed to acquire session ID.");
    retval = 3;
    goto finish;
  }

  umask(0137); /* rw-r----- */
  chdir("/");

  file = fopen(g_piphoned_config_info.pidfile, "w");
  if (!file) {
    syslog(LOG_CRIT, "Failed to open PID file '%s': %m", g_piphoned_config_info.pidfile);
    retval = 3;
    goto finish;
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
    retval = 3;
    goto finish;
  }
  if (setuid(g_piphoned_config_info.uid) != 0) {
    syslog(LOG_CRIT, "Failed to drop user privileges: %m. Exiting!");
    retval = 3;
    goto finish;
  }

  /* Paranoid extra security check */
  if (setuid(0) != -1) {
    syslog(LOG_CRIT, "Regained root privileges! Exiting!");
    retval = 3;
    goto finish;
  }

  syslog(LOG_INFO, "Successfully dropped privileges.");

  /***************************************
   * Start of real code
   ***************************************/

  retval = mainloop();

  /***************************************
   * Cleanup
   **************************************/

 finish:
  piphoned_config_free();
  syslog(LOG_NOTICE, "Program finished.");
  closelog();

  return retval;
}

int mainloop()
{
  LinphoneCoreVTable vtable = {0};
  LinphoneCore* p_linphone = NULL;
  char sip_uri[512];

  /* TODO: Setup linphone callbacks */

  /* p_linphone = linphone_core_new(&vtable, NULL, NULL, NULL); */
  piphoned_hwactions_init();

  while(true) {
    /* linphone_core_iterate(p_linphone); */

    memset(sip_uri, '\0', 512);
    if (piphoned_hwactions_has_dialed_uri(sip_uri)) {
      /* piphoned_phone_place_call(p_linphone, sip_uri); */
    }

    ms_usleep(50000);
  }

  syslog(LOG_NOTICE, "Shutting down.");

  /* TODO: Iterate all the proxies and close them down */

  /* linphone_core_destroy(p_linphone); */

  return 0;
}
