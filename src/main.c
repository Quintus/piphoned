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
#include <curl/curl.h>
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
static LinphoneProxyConfig* load_linphone_proxy(LinphoneCore* p_linphone);
static bool determine_public_ipv4(char* ipv4);
static size_t get_curl_data(void* buf, size_t size, size_t num_members, void* userdata);

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

  /* Library initialisation */
  curl_global_init(CURL_GLOBAL_ALL);

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
  curl_global_cleanup();

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

    /* We have no terminal anymore */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
  }

  umask(0137); /* rw-r----- */
  chdir("/");

  /* If there is a PID file already, there may be something running.
   * Reject start. */
  file = fopen(g_piphoned_config_info.pidfile, "r");
  if (file) {
    syslog(LOG_CRIT, "PID file already exists! Exiting.");
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
  if (signal(SIGINT, handle_sigterm) == SIG_ERR) {
    syslog(LOG_CRIT, "Failed to setup SIGINT signal handler: %m");
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
  LinphoneCoreVTable vtable = {0};
  LinphoneCore* p_linphone = NULL;
  LinphoneProxyConfig* p_proxy = NULL;
  char sip_uri[512]; /* TODO: Use MAX_SIP_URI_LENGTH (which is not global yet, but in hwactions.c...) */
  char ipv4[512];
  bool is_currently_calling = false;
  struct timeval timestamp_now;
  struct timeval timestamp_last;

  s_stop_mainloop = false;

  while (!determine_public_ipv4(ipv4)) {
    syslog(LOG_INFO, "Failed to retrieve public IPv4. Trying again in 20 seconds.");
    sleep(20);
  }

  syslog(LOG_NOTICE, "Determined public IPv4: %s", ipv4);

  /* Output linphone logs to stdout if we have stdout (i.e. we are not forking) */
  if (!g_cli_options.daemonize) {
    linphone_core_enable_logs(NULL);
  }

  /* TODO: Setup linphone callbacks */

  p_linphone = linphone_core_new(&vtable, NULL, NULL, NULL);
  linphone_core_set_firewall_policy(p_linphone, LinphonePolicyUseNatAddress);
  linphone_core_set_nat_address(p_linphone, ipv4);

  p_proxy = load_linphone_proxy(p_linphone);
  if (!p_proxy) {
    syslog(LOG_CRIT, "Failed to load linphone proxy. Exiting.");
    return 4;
  }

  linphone_core_add_proxy_config(p_linphone, p_proxy);
  linphone_core_set_default_proxy(p_linphone, p_proxy); /* First proxy is default proxy */

  piphoned_hwactions_init();

  while(true) {
    linphone_core_iterate(p_linphone);

    if (is_currently_calling) {
      if (piphoned_hwactions_check_hangup()) {
        syslog(LOG_NOTICE, "Terminating call.");
        /* TODO: Linphone stop call */
        is_currently_calling = false;
      }
    }
    else {
      if (piphoned_hwactions_check_pickup(sip_uri)) {
        syslog(LOG_NOTICE, "Dialing SIP URI: %s", sip_uri);
        /* piphoned_phone_place_call(p_linphone, sip_uri); */
        is_currently_calling = true;
      }
    }

    ms_usleep(50000);

    if (s_stop_mainloop)
      break;
  }

  syslog(LOG_NOTICE, "Initiating shutdown.");

  /* TODO: Cater for mulitple proxies */
  linphone_proxy_config_edit(p_proxy);
  linphone_proxy_config_enable_register(p_proxy, FALSE);
  linphone_proxy_config_done(p_proxy);

  /* Send deauthentication request(s) */
  gettimeofday(&timestamp_last, NULL);
  while (linphone_proxy_config_get_state(p_proxy) != LinphoneRegistrationCleared) {
    linphone_core_iterate(p_linphone);

    ms_usleep(50000);

    /* If for two minutes nothing happens, terminate anyway. */
    gettimeofday(&timestamp_now, NULL);
    if (timestamp_now.tv_sec - timestamp_last.tv_sec >= 20) {/* Debug: 120 would be correct */
      syslog(LOG_WARNING, "Timeout waiting for SIP proxy to answer unregistration. Quitting anyway.");
      break;
    }
  }

  /* Linphone documentation says we are not allowed to free proxies
   * that have been removed with linphone_core_remove_proxy_config(). */
  piphoned_hwactions_free();
  linphone_core_destroy(p_linphone);

  return 0;
}

void handle_sigterm(int signum)
{
  s_stop_mainloop = true;
}

LinphoneProxyConfig* load_linphone_proxy(LinphoneCore* p_linphone)
{
  LinphoneProxyConfig* p_proxy = NULL;
  LinphoneAuthInfo* p_auth = NULL;
  struct Piphoned_Config_ParsedFile_ProxyTable* p_config = g_piphoned_config_info.proxies[0];
  char str[PATH_MAX];

  /* TODO: Allow multiple proxies */
  if (g_piphoned_config_info.num_proxies == 0) {
    syslog(LOG_ERR, "No proxies configured.");
    return NULL;
  }
  else if (g_piphoned_config_info.num_proxies > 1) {
    syslog(LOG_ERR, "Cannot handle more than one proxy currently.");
    return NULL;
  }

  p_proxy = linphone_proxy_config_new();
  p_auth  = linphone_auth_info_new(p_config->username,
                                   NULL,
                                   p_config->password,
                                   NULL,
                                   p_config->realm,
                                   NULL /* g_piphoned_config_info.proxies[0]->domain, */);

  linphone_core_add_auth_info(p_linphone, p_auth); /* Side effect: lets linphone-core manage memory of p_auth */

  memset(str, '\0', PATH_MAX);
  sprintf(str, "%s <sip:%s@%s>", p_config->displayname, p_config->username, p_config->server);
  syslog(LOG_INFO, "Using SIP identity for realm %s: %s", str, p_config->realm);

  linphone_proxy_config_set_identity(p_proxy, str);
  linphone_proxy_config_set_server_addr(p_proxy, p_config->server);
  linphone_proxy_config_enable_register(p_proxy, TRUE);

  return p_proxy;
}

size_t get_curl_data(void* buf, size_t size, size_t num_members, void* userdata)
{
  char* ipv4 = (char*) userdata;
  int length = strlen(ipv4);

  memcpy(ipv4 + length, buf, num_members * size);

  return num_members * size;
}

bool determine_public_ipv4(char* ipv4)
{
  CURL* p_handle = NULL;
  char curlerror[CURL_ERROR_SIZE];
  memset(ipv4, '\0', 512);

  p_handle = curl_easy_init();
  curl_easy_setopt(p_handle, CURLOPT_URL, "http://ifconfig.me/ip");
  curl_easy_setopt(p_handle, CURLOPT_WRITEFUNCTION, get_curl_data);
  curl_easy_setopt(p_handle, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(p_handle, CURLOPT_ERRORBUFFER, curlerror);
  curl_easy_setopt(p_handle, CURLOPT_WRITEDATA, ipv4);

  if (curl_easy_perform(p_handle) != CURLE_OK) {
    syslog(LOG_WARNING, "libcurl returned error: %s", curlerror);
    curl_easy_cleanup(p_handle);
    return false;
  }
  else {
    curl_easy_cleanup(p_handle);
    return true;
  }
}
