#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <sys/stat.h>
#include "phone_manager.h"
#include "commandline.h"
#include "configfile.h"

#define AUTHTOKEN_FILE "/tmp/zrtptoken"

/**
 * Delay to wait between calls to linphone_core_iterate() to prevent
 * the process from grabbing 100% CPU.
 */
#define LINPHONE_WAIT_DELAY 50000

enum Piphoned_CallLogAction {
  PIPHONED_CALL_ACCEPTED = 1,
  PIPHONED_CALL_DECLINED,
  PIPHONED_CALL_OUTGOING,
  PIPHONED_CALL_MISSED,
  PIPHONED_CALL_BUSY
};

static LinphoneProxyConfig* load_linphone_proxy(LinphoneCore* p_linphone, const struct Piphoned_Config_ParsedFile_ProxyTable* p_proxyconfig);
static void call_state_changed(LinphoneCore* p_linphone, LinphoneCall* p_call, LinphoneCallState cstate, const char *msg);
static void call_encryption_changed(LinphoneCore* p_linphone, LinphoneCall* p_call, bool_t is_encrypted, const char* p_authtoken);
static void handle_incoming_call(LinphoneCore* p_linphone, LinphoneCall* p_call);
static void handle_running_streams(LinphoneCore* p_linphone, LinphoneCall* p_call);
static void handle_call_ending(LinphoneCore* p_linphone, LinphoneCall* p_call);
static void log_call(LinphoneCall* p_call, enum Piphoned_CallLogAction action);

/**
 * Creates a new PhoneManager. Do not use more than one PhoneManager
 * instance in your program.
 */
struct Piphoned_PhoneManager* piphoned_phonemanager_new()
{
  struct Piphoned_PhoneManager* p_manager = (struct Piphoned_PhoneManager*) malloc(sizeof(struct Piphoned_PhoneManager));
  memset(p_manager, '\0', sizeof(struct Piphoned_PhoneManager));

  /* Disable ORTP logs if running as a daemon.
   * Otherwise output them to stdout. */
  if (g_cli_options.daemonize)
    linphone_core_set_log_level(0);
  else
    linphone_core_set_log_file(NULL);

  /* Setup linphone callbacks */
  p_manager->vtable.call_state_changed = call_state_changed;
  p_manager->vtable.call_encryption_changed = call_encryption_changed;

  p_manager->p_linphone = linphone_core_new(&p_manager->vtable, NULL, NULL, p_manager);
  /*linphone_core_set_stun_server(p_manager->p_linphone, "stun.linphone.org");
    linphone_core_set_firewall_policy(p_manager->p_linphone, LinphonePolicyUseStun); */

  /* Setup the sound devices */
  if (!linphone_core_sound_device_can_capture(p_manager->p_linphone, g_piphoned_config_info.capture_sound_device)) {
    syslog(LOG_CRIT, "Sound device set as capture device (%s) cannot capture sound!", g_piphoned_config_info.capture_sound_device);
    goto fail;
  }
  if (!linphone_core_sound_device_can_playback(p_manager->p_linphone, g_piphoned_config_info.playback_sound_device)) {
    syslog(LOG_CRIT, "Sound device set as playback device (%s) cannot playback sound!", g_piphoned_config_info.playback_sound_device);
    goto fail;
  }
  if (!linphone_core_sound_device_can_playback(p_manager->p_linphone, g_piphoned_config_info.ring_sound_device)) {
    syslog(LOG_CRIT, "Sound device set as ringer device (%s) cannot playback sound!", g_piphoned_config_info.ring_sound_device);
    goto fail;
  }

  syslog(LOG_INFO, "All sound devices reported as working by liblinphone.");

  linphone_core_set_ringer_device(p_manager->p_linphone, g_piphoned_config_info.ring_sound_device);
  linphone_core_set_playback_device(p_manager->p_linphone, g_piphoned_config_info.playback_sound_device);
  linphone_core_set_capture_device(p_manager->p_linphone, g_piphoned_config_info.capture_sound_device);

  syslog(LOG_INFO, "Ringer device: %s", g_piphoned_config_info.ring_sound_device);
  syslog(LOG_INFO, "Playback device: %s", g_piphoned_config_info.playback_sound_device);
  syslog(LOG_INFO, "Capture device: %s", g_piphoned_config_info.capture_sound_device);

  linphone_core_set_media_encryption(p_manager->p_linphone, LinphoneMediaEncryptionZRTP);
  linphone_core_set_media_encryption_mandatory(p_manager->p_linphone, false); /* Drops encryption silently if we don’t communicate it to the user! This has to be done in a callback! */
  linphone_core_set_zrtp_secrets_file(p_manager->p_linphone, g_piphoned_config_info.zrtp_secrets_file);
  syslog(LOG_INFO, "Set preferred encryption method to ZRTP, allowing unencrypted call if unsupported.");

  return p_manager;

 fail:

  linphone_core_destroy(p_manager->p_linphone);
  free(p_manager);
  return NULL;
}

/**
 * Clean up and free the given phone manager instance. This
 * also deauthenticates properly from the SIP server.
 */
void piphoned_phonemanager_free(struct Piphoned_PhoneManager* p_manager)
{
  int i = 0;

  if (!p_manager)
    return;

  for(i=0; i < p_manager->num_proxies; i++) {
    struct timeval timestamp_now;
    struct timeval timestamp_last;
    LinphoneProxyConfig* p_proxy = p_manager->proxies[i];

    linphone_proxy_config_edit(p_proxy);
    linphone_proxy_config_enable_register(p_proxy, FALSE); /* Advises linphone to send deauth request */
    linphone_proxy_config_done(p_proxy);

    /* Allow for the deauthentication requests */
    gettimeofday(&timestamp_last, NULL);
    while (linphone_proxy_config_get_state(p_proxy) != LinphoneRegistrationCleared) {
      linphone_core_iterate(p_manager->p_linphone);
      ms_usleep(LINPHONE_WAIT_DELAY);

      /* If for nothing happens for some time, terminate. */
      gettimeofday(&timestamp_now, NULL);
      if (timestamp_now.tv_sec - timestamp_last.tv_sec >= 20) {
        syslog(LOG_WARNING, "Timeout waiting for SIP proxy %i to answer unregistration. Continuing anyway.", i);
        break;
      }
    }

    /* Linphone documentation says we are not allowed to free proxies
     * that have been removed with linphone_core_remove_proxy_config(). */
    p_manager->proxies[i] = NULL;
  }

  p_manager->num_proxies = 0;
  linphone_core_destroy(p_manager->p_linphone);
  free(p_manager);
}

/**
 * Loads the configured proxy from the parsed configuration.
 * TODO: Allow for more than one proxy!
 */
bool piphoned_phonemanager_load_proxies(struct Piphoned_PhoneManager* p_manager)
{
  int i;

  for(i=0; i < g_piphoned_config_info.num_proxies; i++) {
    struct Piphoned_Config_ParsedFile_ProxyTable* p_config = g_piphoned_config_info.proxies[i];
    LinphoneProxyConfig* p_proxy = load_linphone_proxy(p_manager->p_linphone, p_config);

    if (!p_proxy) {
      if (i == 0) {
        syslog(LOG_ERR, "Default (= first) proxy is misconfigured. Not loading any other proxy configurations.");
        return false;
      }
      else {
        syslog(LOG_WARNING, "Invalid proxy configuration for proxy %d, ignoring.", i + 1);
        continue;
      }
    }

    linphone_core_add_proxy_config(p_manager->p_linphone, p_proxy); /* Side effect: Makes linphone manage the memory of p_proxy */
    p_manager->proxies[p_manager->num_proxies++] = p_proxy;

    /* First proxy is default proxy */
    if (i==0)
      linphone_core_set_default_proxy(p_manager->p_linphone, p_proxy);
  }

  return true;
}

/**
 * Call this once in a mainloop iteration. Instructs linphone to
 * do the necessary communication with the SIP server.
 */
void piphoned_phonemanager_update(struct Piphoned_PhoneManager* p_manager)
{
  linphone_core_iterate(p_manager->p_linphone);
  ms_usleep(LINPHONE_WAIT_DELAY);
}

/**
 * Place a call to the given SIP URI. Does nothing if a call
 * is already in progress.
 *
 * The `is_calling` member of the object struct is set to true when
 * this method returns and will stay so until you call
 * piphoned_phonemanager_stop_call().
 */
void piphoned_phonemanager_place_call(struct Piphoned_PhoneManager* p_manager, const char* sip_uri)
{
  if (p_manager->is_calling) {
    syslog(LOG_WARNING, "Ignoring attempt to call while a call is running.");
    return;
  }
  if (strlen(sip_uri) == 0) {
    syslog(LOG_ERR, "Will not dial an empty SIP URI.");
    p_manager->error_counter++;
    return;
  }
  if (strlen(sip_uri) < 5) { /* Each sip URI must at least have "sip:" at the beginning. */
    syslog(LOG_ERR, "Will not dial incomplete SIP URI.");
    p_manager->error_counter++;
    return;
  }
  if (sip_uri[4] == '@') { /* SIP URI looks like "sip:@foo". This happens surprisingly often. */
    syslog(LOG_ERR, "Will not dial SIP URI without user part.");
    p_manager->error_counter++;
    return;
  }
  if (p_manager->error_counter >= 10) {
    /* There is obviously something going wrong if we get here. */
    syslog(LOG_CRIT, "Received 10 errorneous dialing attempts in a row. Exiting!");
    exit(6);
  }

  p_manager->error_counter = 0; /* Reset for next time */

  p_manager->p_call = linphone_core_invite(p_manager->p_linphone, sip_uri);
  if (!p_manager->p_call) {
    syslog(LOG_ERR, "Failed to place call.");
    return;
  }

  syslog(LOG_NOTICE, "Started call to '%s'", sip_uri);
  log_call(p_manager->p_call, PIPHONED_CALL_OUTGOING);
  linphone_call_ref(p_manager->p_call);

  /* piphoned_phone_place_call(p_linphone, sip_uri); */
  p_manager->is_calling = true;
}

/**
 * Stop a call that is in progress. Does nothing if no call is in
 * progress.
 *
 * The `is_calling` member of the object struct is set to false when
 * this method returns and will stay so until the next call to
 * piphoned_phonemanager_place_call().
 */
void piphoned_phonemanager_stop_call(struct Piphoned_PhoneManager* p_manager)
{
  /* Do nothing if no call is running */
  if (!p_manager->is_calling)
    return;

  /* Terminating a call that has been ended by the other side already should
   * do no harm. */
  linphone_core_terminate_call(p_manager->p_linphone, p_manager->p_call);
  linphone_call_unref(p_manager->p_call);

  p_manager->p_call = NULL;
  p_manager->is_calling = false;
}

/**
 * Accept an incoming call. After this method returns, you can proceed
 * as if the call was initiated by you using piphoned_phonemanager_place_call().
 */
void piphoned_phonemanager_accept_incoming_call(struct Piphoned_PhoneManager* p_manager)
{
  if (!p_manager->has_incoming_call) {
    syslog(LOG_ERR, "Can't accept a call when there is no incoming call.");
    return;
  }
  else if (p_manager->is_calling) {
    syslog(LOG_ERR, "Can't accept a call when another call is running.");
    return;
  }

  log_call(p_manager->p_call, PIPHONED_CALL_ACCEPTED);
  linphone_core_accept_call(p_manager->p_linphone, p_manager->p_call);

  /* Now go into the same state as if the call was initiated by us. */
  p_manager->is_calling = true;
  p_manager->has_incoming_call = false;
}

/**
 * Decline an incoming call. After this method returns, you can proceed
 * as if the call was terminated by you using piphoned_phonemanager_stop_call().
 */
void piphoned_phonemanager_decline_incoming_call(struct Piphoned_PhoneManager* p_manager)
{
  if (!p_manager->has_incoming_call) {
    syslog(LOG_ERR, "Can't decline a call when there is no incoming call.");
    return;
  }
  else if (p_manager->is_calling) {
    syslog(LOG_ERR, "Can't decline a call when another call is running.");
    return;
  }

  log_call(p_manager->p_call, PIPHONED_CALL_DECLINED);
  linphone_core_decline_call(p_manager->p_linphone, p_manager->p_call, LinphoneReasonDeclined);

  /* Now go into the same state as if the call was terminated by us. */
  linphone_call_unref(p_manager->p_call);
  p_manager->has_incoming_call = false;
  p_manager->p_call = NULL;
}

/**
 * Accept the ZRTP SAS authentication nonce string.
 */
void piphoned_phonemanager_accept_zrtp_nonce(struct Piphoned_PhoneManager* p_manager)
{
  if (!p_manager->is_calling)
    return;

  syslog(LOG_NOTICE, "ZRTP SAS accepted.");
  linphone_call_set_authentication_token_verified(p_manager->p_call, true);
}

/**
 * Reject the ZRTP SAS authentication nonce string. This immediately
 * terminates the call.
 */
void piphoned_phonemanager_reject_zrtp_nonce(struct Piphoned_PhoneManager* p_manager)
{
  if (!p_manager->is_calling)
    return;

  syslog(LOG_WARNING, "ZRTP SAS rejected. Terminating call immediately.");
  linphone_call_set_authentication_token_verified(p_manager->p_call, false);
  piphoned_phonemanager_stop_call(p_manager);
}

/***************************************
 * Private helpers
 ***************************************/

/**
 * Load a linphone proxy from the configuration file and return it.
 *
 * Currently only works for the first proxy in the configuration file.
 */
LinphoneProxyConfig* load_linphone_proxy(LinphoneCore* p_linphone, const struct Piphoned_Config_ParsedFile_ProxyTable* p_proxyconfig)
{
  LinphoneProxyConfig* p_proxy = NULL;
  LinphoneAuthInfo* p_auth = NULL;
  char str[PATH_MAX];

  p_proxy = linphone_proxy_config_new();
  p_auth  = linphone_auth_info_new(p_proxyconfig->username,
                                   NULL,
                                   p_proxyconfig->password,
                                   NULL,
                                   p_proxyconfig->realm); /* linphone >= 3.8 requires an additional domain paramater that can be set to NULL */

  linphone_core_add_auth_info(p_linphone, p_auth); /* Side effect: lets linphone-core manage memory of p_auth */

  memset(str, '\0', PATH_MAX);
  sprintf(str, "\"%s\" <sip:%s@%s>", p_proxyconfig->displayname, p_proxyconfig->username, p_proxyconfig->server);
  syslog(LOG_INFO, "Using SIP identity for realm %s: %s", p_proxyconfig->realm, str);

  linphone_proxy_config_set_identity(p_proxy, str);
  linphone_proxy_config_set_server_addr(p_proxy, p_proxyconfig->server);
  linphone_proxy_config_enable_register(p_proxy, TRUE);

  if (p_proxyconfig->use_publish)
    linphone_proxy_config_enable_publish(p_proxy, TRUE);

  return p_proxy;
}

/**
 * Linphone callback called when something of importance related to a call
 * happens.
 */
void call_state_changed(LinphoneCore* p_linphone, LinphoneCall* p_call, LinphoneCallState cstate, const char *msg)
{
  switch (cstate) {
  case LinphoneCallOutgoingRinging:
    syslog(LOG_DEBUG, "Remote device is ringing.");
    break;
  case LinphoneCallConnected:
    syslog(LOG_DEBUG, "Connection established.");
    break;
  case LinphoneCallStreamsRunning:
    handle_running_streams(p_linphone, p_call);
  case LinphoneCallEnd:
    handle_call_ending(p_linphone, p_call);
    break;
  case LinphoneCallError:
    syslog(LOG_WARNING, "Failed to establish call.");
    break;
  case LinphoneCallIncomingReceived:
    handle_incoming_call(p_linphone, p_call);
  default:
    syslog(LOG_DEBUG, "Unhandled notification on call: %i", cstate);
    break;
  }
}

/**
 * Linphone callback called when the encryption state of a call changes.
 */
static void call_encryption_changed(LinphoneCore* p_linphone, LinphoneCall* p_call, bool_t is_encrypted, const char* p_authtoken)
{
  struct Piphoned_PhoneManager* p_manager = (struct Piphoned_PhoneManager*) linphone_core_get_user_data(p_linphone);

  if (is_encrypted)
    syslog(LOG_NOTICE, "*** Encryption enabled ***");
  else
    syslog(LOG_NOTICE, "*** Encryption disabled ***");

  if (p_authtoken) {
    FILE* p_file = fopen(AUTHTOKEN_FILE, "w");
    if (!p_file) {
      syslog(LOG_ERR, "Failed to open authtoken file %s: %m", AUTHTOKEN_FILE);
      syslog(LOG_ERR, "Terminating call for security reasons.");
      piphoned_phonemanager_stop_call(p_manager); /* Emergency termination */
    }

    fprintf(p_file, "ZRTP SAS token: >%s<\n", p_authtoken);
    fclose(p_file);
    syslog(LOG_NOTICE, "ZRTP authtoken written to %s.", AUTHTOKEN_FILE);
  }
}

/**
 * Set up state for an incoming call so that the mainloop can accept it
 * later on.
 */
void handle_incoming_call(LinphoneCore* p_linphone, LinphoneCall* p_call)
{
  struct Piphoned_PhoneManager* p_manager = (struct Piphoned_PhoneManager*) linphone_core_get_user_data(p_linphone);
  char* straddr = linphone_call_get_remote_address_as_string(p_call);
  syslog(LOG_NOTICE, "Incoming call from %s", straddr);
  ms_free(straddr);

  /* Deny calls while busy. We can’t have two calls at once. */
  if (p_manager->is_calling) {
    syslog(LOG_NOTICE, "Denying incoming call while another call is active.");
    log_call(p_call, PIPHONED_CALL_BUSY);
    linphone_core_decline_call(p_linphone, p_call, LinphoneReasonBusy);
    return;
  }

  p_manager->p_call = p_call;
  p_manager->has_incoming_call = true;
  linphone_call_ref(p_call);
}

/**
 * Log some information about the now running call.
 */
void handle_running_streams(LinphoneCore* p_linphone, LinphoneCall* p_call)
{
  const LinphoneCallParams* p_params = linphone_call_get_current_params(p_call);
  LinphoneMediaEncryption enc = linphone_call_params_get_media_encryption(p_params);

  switch(enc) {
  case LinphoneMediaEncryptionNone:
    syslog(LOG_INFO, "Encryption is disabled.");
    break;
  case LinphoneMediaEncryptionSRTP:
    syslog(LOG_INFO, "Using SRTP encryption.");
    break;
  case LinphoneMediaEncryptionZRTP:
    syslog(LOG_INFO, "Using ZRTP encryption.");
    break;
  default:
    syslog(LOG_WARNING, "Unknown encryption.");
    break;
  }
}

/**
 * Clean up state after a call has ended. This is mainly used for the case
 * where the mainloop didn’t accept a call (i.e. the call was missed by
 * the user), where state would screw up if we didn’t cleaned it up.
 */
void handle_call_ending(LinphoneCore* p_linphone, LinphoneCall* p_call)
{
  struct Piphoned_PhoneManager* p_manager = (struct Piphoned_PhoneManager*) linphone_core_get_user_data(p_linphone);
  struct stat s;

  if (p_manager->has_incoming_call) {
    syslog(LOG_NOTICE, "Call not accepted. Resetting to normal state.");
    log_call(p_call, PIPHONED_CALL_MISSED);

    /* If we didn't do this, the mainloop would be tricked into trying
     * to accept a call that doesn't exist anymore when the user in
     * reality wanted to start a totally unrelated new call. */
    p_manager->has_incoming_call = false;
    linphone_call_unref(p_manager->p_call);
    p_manager->p_call = NULL;
  }

  /* Remove any ZRTP SAS nonce file if that was an encrypted call. */
  if (stat(AUTHTOKEN_FILE, &s) == 0) {
    unlink(AUTHTOKEN_FILE);
  }

  syslog(LOG_DEBUG, "Connection closed.");
}

/**
 * Logging helper function for writing the call log file.
 */
void log_call(LinphoneCall* p_call, enum Piphoned_CallLogAction action)
{
  char timestamp[512];
  char* sip_uri = linphone_call_get_remote_address_as_string(p_call);
  time_t t = time(NULL);
  struct tm* time = localtime(&t);

  memset(timestamp, '\0', 512);
  strftime(timestamp, 512, "%Y-%m-%dT%H:%M:%S%z", time);

  switch(action) {
  case PIPHONED_CALL_ACCEPTED:
    fprintf(g_piphoned_config_info.p_calllogfile, "%s ACCEPTED %s\n", timestamp, sip_uri);
    break;
  case PIPHONED_CALL_DECLINED:
    fprintf(g_piphoned_config_info.p_calllogfile, "%s DECLINED %s\n", timestamp, sip_uri);
    break;
  case PIPHONED_CALL_OUTGOING:
    fprintf(g_piphoned_config_info.p_calllogfile, "%s OUTGOING %s\n", timestamp, sip_uri);
    break;
  case PIPHONED_CALL_MISSED:
    fprintf(g_piphoned_config_info.p_calllogfile, "%s MISSED %s\n", timestamp, sip_uri);
    break;
  case PIPHONED_CALL_BUSY:
    fprintf(g_piphoned_config_info.p_calllogfile, "%s BUSY %s\n", timestamp, sip_uri);
    break;
  default:
    fprintf(g_piphoned_config_info.p_calllogfile, "%s UNKNOWN %s\n", timestamp, sip_uri);
    break;
  }

  ms_free(sip_uri);
}
