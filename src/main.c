#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <wiringPi.h>
#include <linphone/linphonecore.h>
#include "main.h"
#include "configfile.h"
#include "hwactions.h"
#include "commandline.h"

static int mainloop();
void handle_sigterm(int signum);
int command_start();
int command_stop();
int command_restart();

static volatile bool s_stop_mainloop;

int main(int argc, char* argv[])
{
  int retval = 0;

  /* We need root rights to initialize everything. */
  if (getuid() != 0) {
    fprintf(stderr, "This program has to be run as root. Exiting.\n");
    return 1;
  }

  piphoned_commandline_info_from_argv(argc, argv); /* sets up g_cli_options */

  setlogmask(LOG_UPTO(g_cli_options.loglevel));
  openlog("piphoned", LOG_CONS | LOG_ODELAY | LOG_PID, LOG_DAEMON);
  syslog(LOG_DEBUG, "Early startup phase entered.");

  piphoned_config_init(g_cli_options.config_file); /* sets g_piphoned_config_info */

  switch(g_cli_options.command) {
  case PIPHONED_COMMAND_START:
    retval = command_start();
    break;
  case PIPHONED_COMMAND_STOP:
    retval = command_stop();
    break;
  case PIPHONED_COMMAND_RESTART:
    retval = command_restart();
    break;
  default:
    fprintf(stderr, "Invalid command %d. This is a bug.\n", g_cli_options.command);
    return 1;
  }

  syslog(LOG_DEBUG, "Late termination phase ended.");
  closelog();

  return retval;
}

int mainloop()
{
  LinphoneCoreVTable vtable = {0};
  LinphoneCore* p_linphone = NULL;
  char sip_uri[512];

  s_stop_mainloop = false;

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

    if (s_stop_mainloop)
      break;
  }

  syslog(LOG_NOTICE, "Initiating shutdown.");

  /* TODO: Iterate all the proxies and close them down */

  piphoned_hwactions_free();
  /* linphone_core_destroy(p_linphone); */

  return 0;
}

int command_start()
{
  pid_t childpid = 0;
  pid_t sessionid = 0;
  FILE* file = NULL;
  int retval = 0;

  syslog(LOG_NOTICE, "Starting up.");

  /***************************************
   * Initializing libraries
   **************************************/
  wiringPiSetup(); /* Requires root */

  /***************************************
   * Daemonising
   ***************************************/

  if (g_cli_options.daemonize) {
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
   * Signal handlers
   ***************************************/

  /* Debian doesn’t have sigaction() yet as it doesn’t yet
   * implement POSIX.1-2008. */
  /*
  struct sigaction term_signal_info;
  term_signal_info.sa_handler = handle_sigterm;
  term_signal_info.sa_mask = SIGINT;
  if (sigaction(SIGTERM, &term_signal_info, NULL) < 0) {
    syslog(LOG_CRIT, "Failed to setup SIGTERM signal handler: %m");
    goto finish;
  }
  */
  /* So instead, use deprecated signal() for now. */
  if (signal(SIGTERM, handle_sigterm) == SIG_ERR) {
    syslog(LOG_CRIT, "Failed to setup SIGTERM signal handler: %m");
    goto finish;
  }

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

  return retval;
}

int command_stop()
{
  /* TODO */
  printf("Stop.\n");
  return 0;
}
int command_restart()
{
  /* TODO */
  printf("Restart.\n");
  return 0;
}

void handle_sigterm(int signum)
{
  s_stop_mainloop = true;
}
