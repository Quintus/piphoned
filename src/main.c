#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <grp.h>
#include <wiringPi.h>
#include <linphone/linphonecore.h>
#include "main.h"
#include "configfile.h"
#include "hwactions.h"
#include "commandline.h"
#include "phone_manager.h"

enum ZrtpNonceAcception {
  ZRTP_NONCE_UNKNOWN = 0,
  ZRTP_NONCE_WRONG,
  ZRTP_NONCE_OK
};

static int mainloop();
void handle_sigterm(int signum);
void handle_sigusr1(int signum);
int command_start();
int command_stop();
int command_restart();

static volatile bool s_stop_mainloop = false;
static volatile enum ZrtpNonceAcception s_zrtp_sas_ok = ZRTP_NONCE_UNKNOWN;

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

  /* Library initialisation */
  wiringPiSetup(); /* Requires root */

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

  /* Library cleanup */

  piphoned_config_free();
  syslog(LOG_DEBUG, "Late termination phase ended.");
  closelog();

  return retval;
}

int command_start()
{
  pid_t childpid = 0;
  pid_t sessionid = 0;
  FILE* file = NULL;
  int retval = 0;

  syslog(LOG_NOTICE, "Starting up.");

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

    /* We have no terminal anymore. Linphone has a bug when stdout is
     * not available: it goes to 100% CPU usage immediately. Hence, we
     * just make stdout write into /dev/null (rather than close() it),
     * where it can write into whatever it deems useful.  For
     * symmetry, we also put stderr there. */
    close(STDIN_FILENO);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
  }
  else {
    fprintf(stderr, "Not daemonizing. Beware that the standard\n");
    fprintf(stderr, "output and error streams significantly slow\n");
    fprintf(stderr, "down the program. Redirect them to a file or\n");
    fprintf(stderr, "to /dev/null to ensure proper sound decoding.\n");
    sleep(3); /* Give time to read the above */
  }

  umask(0137); /* rw-r----- */
  chdir("/");

  /***************************************
   * Last privileged operations
   ***************************************/

  /* PID file */

  /* If there is a PID file already, there may be something running.
   * Reject start. */
  file = fopen(g_piphoned_config_info.pidfile, "r");
  if (file) {
    syslog(LOG_CRIT, "PID file already exists! Exiting.");
    fprintf(stderr, "PID file already exists, exiting.\n");
    fclose(file);
    retval = 3;
    goto finish;
  }

  file = fopen(g_piphoned_config_info.pidfile, "w");
  if (!file) {
    syslog(LOG_CRIT, "Failed to open PID file '%s': %m", g_piphoned_config_info.pidfile);
    retval = 3;
    goto finish;
  }

  fprintf(file, "%d", getpid());
  fclose(file);

  /* ZRTP secrets file */

  file = fopen(g_piphoned_config_info.zrtp_secrets_file, "a"); /* Creates it if it does not exist */
  if (!file) {
    syslog(LOG_CRIT, "Failed to open ZRTP secrets file '%s': %m", g_piphoned_config_info.zrtp_secrets_file);
    retval = 3;
    goto finish;
  }
  fclose(file);
  /* Too important a file -- protect even from group read rights. Still,
   * we need to be able to write to it later on. */
  chown(g_piphoned_config_info.zrtp_secrets_file, g_piphoned_config_info.uid, g_piphoned_config_info.gid);
  chmod(g_piphoned_config_info.zrtp_secrets_file, S_IRUSR | S_IWUSR);

  syslog(LOG_INFO, "Fork setup completed.");

  /***************************************
   * Privilege dropping
   ***************************************/

  /* On several systems, you have to be in the audio group to access
   * the sound devices. If you aren't liblinphone will just report
   * your sound devices as dysfunctional. */
  if (g_piphoned_config_info.audiogroup == -1) {
    syslog(LOG_WARNING, "The group specified as 'audiogroup' in the configuration file doesn't exist. Assuming membership in this group is not needed for access to sound devices; if sound devices are reported as failing although you know they work, the group might be named different on your system (see 'audiogroup' setting in the configuration file).");
  }
  else {
    gid_t audiogroup[1];
    audiogroup[0] = g_piphoned_config_info.audiogroup;
    syslog(LOG_INFO, "For accessing audio devices, membership in group %d appears to be required. Adding ourselves in.", g_piphoned_config_info.audiogroup);
    setgroups(1, audiogroup);
  }

  if (g_piphoned_config_info.gid < 0) {
    syslog(LOG_CRIT, "Invalid gid specified in configuration file. Exiting!");
    retval = 3;
    goto finish;
  }
  if (g_piphoned_config_info.uid < 0) {
    syslog(LOG_CRIT, "Invalid uid specified in configuration file. Exiting!");
    retval = 3;
    goto finish;
  }

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

  syslog(LOG_INFO, "Successfully dropped privileges to UID %d and GID %d.", g_piphoned_config_info.uid, g_piphoned_config_info.gid);

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
  if (signal(SIGINT, handle_sigterm) == SIG_ERR) {
    syslog(LOG_CRIT, "Failed to setup SIGINT signal handler: %m");
    goto finish;
  }
  if (signal(SIGUSR1, handle_sigusr1) == SIG_ERR) {
    syslog(LOG_CRIT, "Failed to setup SIGUSR1 signal handler: %m");
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
  syslog(LOG_NOTICE, "Program finished.");

  return retval;
}

int command_stop()
{
  FILE* p_pidfile = fopen(g_piphoned_config_info.pidfile, "r");
  char pidstr[16];
  int pid = 0;

  if (!p_pidfile) {
    int errcode = errno;

    fprintf(stderr, "Failed to open PID file '%s': %s\n", g_piphoned_config_info.pidfile, strerror(errcode));
    syslog(LOG_CRIT, "Failed to open PID file '%s': %s", g_piphoned_config_info.pidfile, strerror(errcode));

    return 2;
  }

  memset(pidstr, '\0', 16);
  fread(pidstr, 1, 15, p_pidfile); /* Ensure NUL-terminated string */
  fclose(p_pidfile);

  pid = atoi(pidstr);
  printf("Sending SIGTERM to process %d\n", pid);
  syslog(LOG_NOTICE, "Sending SIGTERM to process %d", pid);
  if (kill(pid, SIGTERM) < 0) {
    int errcode = errno;

    fprintf(stderr, "Cannot send SIGTERM to process %d: %s\n", pid, strerror(errcode));
    syslog(LOG_CRIT, "Cannot send SIGTERM to process %d: %s", pid, strerror(errcode));

    return 2;
  }

  /* Wait for the process to exit */
  while(kill(pid, 0) == 0)
    sleep(1);

  /* Clean up PID file. The daemon doesn't have sufficient privileges to do so. */
  unlink(g_piphoned_config_info.pidfile);

  return 0;
}

int command_restart()
{
  int retval = 0;

  retval = command_stop();
  if (retval != 0)
    return retval;

  return command_start();
}

int mainloop()
{
  char sip_uri[512]; /* TODO: Use MAX_SIP_URI_LENGTH (which is not global yet, but in hwactions.c...) */
  struct Piphoned_PhoneManager* p_phonemanager = NULL;

  s_stop_mainloop = false;

  p_phonemanager = piphoned_phonemanager_new();
  if (!p_phonemanager) {
    syslog(LOG_CRIT, "Failed to set up phone manager. Exiting.");
    return 4;
  }
  if (!piphoned_phonemanager_load_proxies(p_phonemanager)) {
    syslog(LOG_CRIT, "Failed to load linphone proxies. Exiting.");
    return 4;
  }

  piphoned_hwactions_init();

  while(true) {
    piphoned_phonemanager_update(p_phonemanager);

    if (p_phonemanager->has_incoming_call) {
      if (!piphoned_hwactions_is_phone_hung_up()) {
        s_zrtp_sas_ok = ZRTP_NONCE_UNKNOWN;
        syslog(LOG_NOTICE, "Accepting call.");
        piphoned_phonemanager_accept_incoming_call(p_phonemanager);
      }
      /* TODO: Find a way to input a call decline on the hardware... */

      /* The termination of an accepted incoming call is exactly
       * the same as of a call that was initiated by us. */
    }
    else {
      if (p_phonemanager->is_calling) {
        if (s_zrtp_sas_ok == ZRTP_NONCE_WRONG) {
          piphoned_phonemanager_reject_zrtp_nonce(p_phonemanager);
        }
        else if (s_zrtp_sas_ok == ZRTP_NONCE_OK) {
          piphoned_phonemanager_accept_zrtp_nonce(p_phonemanager);
        }

        if (piphoned_hwactions_is_phone_hung_up()) {
          syslog(LOG_NOTICE, "Terminating call.");
          piphoned_phonemanager_stop_call(p_phonemanager);
          s_zrtp_sas_ok = ZRTP_NONCE_UNKNOWN; /* Reset for extra safety although not needed strictly */
        }
      }
      else {
        if (!piphoned_hwactions_is_phone_hung_up()) {
          piphoned_hwactions_get_sip_uri(sip_uri);
          syslog(LOG_NOTICE, "Dialing SIP URI: %s", sip_uri);
          s_zrtp_sas_ok = ZRTP_NONCE_UNKNOWN;
          piphoned_phonemanager_place_call(p_phonemanager, sip_uri);
        }
      }
    }

    if (s_stop_mainloop)
      break;
  }

  syslog(LOG_NOTICE, "Initiating shutdown.");
  piphoned_phonemanager_free(p_phonemanager);
  piphoned_hwactions_free();

  return 0;
}

void handle_sigterm(int signum)
{
  s_stop_mainloop = true;
}

void handle_sigusr1(int signum)
{
  s_zrtp_sas_ok = ZRTP_NONCE_OK;
}
