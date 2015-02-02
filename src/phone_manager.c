#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <curl/curl.h>
#include "phone_manager.h"
#include "commandline.h"
#include "configfile.h"

/**
 * Delay to wait between calls to linphone_core_iterate() to prevent
 * the process from grabbing 100% CPU.
 */
#define LINPHONE_WAIT_DELAY 50000

static bool determine_public_ipv4(char* ipv4);
static size_t get_curl_data(void* buf, size_t size, size_t num_members, void* userdata);
static LinphoneProxyConfig* load_linphone_proxy(LinphoneCore* p_linphone);
static void call_state_changed(LinphoneCore* p_linphone, LinphoneCall* p_call, LinphoneCallState cstate, const char *msg);

/**
 * Creates a new PhoneManager. Do not use more than one PhoneManager
 * instance in your program.
 */
struct Piphoned_PhoneManager* piphoned_phonemanager_new()
{
  struct Piphoned_PhoneManager* p_manager = (struct Piphoned_PhoneManager*) malloc(sizeof(struct Piphoned_PhoneManager));
  memset(p_manager, '\0', sizeof(struct Piphoned_PhoneManager));

  while (!determine_public_ipv4(p_manager->ipv4)) {
    syslog(LOG_INFO, "Failed to retrieve public IPv4. Trying again in 20 seconds.");
    sleep(20);
  }

  syslog(LOG_NOTICE, "Determined public IPv4: %s", p_manager->ipv4);

  /* Output linphone logs to stdout if we have stdout (i.e. we are not forking) */
  if (!g_cli_options.daemonize)
    linphone_core_enable_logs(NULL);

  /* Setup linphone callbacks */
  p_manager->vtable.call_state_changed = call_state_changed;

  p_manager->p_linphone = linphone_core_new(&p_manager->vtable, NULL, NULL, NULL);
  linphone_core_set_firewall_policy(p_manager->p_linphone, LinphonePolicyUseNatAddress);
  linphone_core_set_nat_address(p_manager->p_linphone, p_manager->ipv4);

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
  LinphoneProxyConfig* p_proxy = NULL;
  struct timeval timestamp_now;
  struct timeval timestamp_last;

  if (!p_manager)
    return;

  linphone_core_get_default_proxy(p_manager->p_linphone, &p_proxy);

  /* TODO: Allow for multiple proxies */
  linphone_proxy_config_edit(p_proxy);
  linphone_proxy_config_enable_register(p_proxy, FALSE); /* Advises linphone to send deauth request */
  linphone_proxy_config_done(p_proxy);

  /* Allow for the deauthentication requests */
  gettimeofday(&timestamp_last, NULL);
  while (linphone_proxy_config_get_state(p_proxy) != LinphoneRegistrationCleared) {
    linphone_core_iterate(p_manager->p_linphone);
    ms_usleep(LINPHONE_WAIT_DELAY);

    /* If for two minutes nothing happens, terminate anyway. */
    gettimeofday(&timestamp_now, NULL);
    if (timestamp_now.tv_sec - timestamp_last.tv_sec >= 20) {/* Debug: 120 would be correct */
      syslog(LOG_WARNING, "Timeout waiting for SIP proxy to answer unregistration. Quitting anyway.");
      break;
    }
  }

  /* Linphone documentation says we are not allowed to free proxies
   * that have been removed with linphone_core_remove_proxy_config(). */
  linphone_core_destroy(p_manager->p_linphone);
  free(p_manager);
}

/**
 * Loads the configured proxy from the parsed configuration.
 * TODO: Allow for more than one proxy!
 */
bool piphoned_phonemanager_load_proxies(struct Piphoned_PhoneManager* p_manager)
{
  LinphoneProxyConfig* p_proxy = load_linphone_proxy(p_manager->p_linphone);
  if (!p_proxy) {
    return false;
  }

  linphone_core_add_proxy_config(p_manager->p_linphone, p_proxy); /* Side effect: Makes linphone manage the memory of p_proxy */
  linphone_core_set_default_proxy(p_manager->p_linphone, p_proxy); /* First proxy is default proxy */

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

  p_manager->p_call = linphone_core_invite(p_manager->p_linphone, sip_uri);
  if (!p_manager->p_call) {
    syslog(LOG_ERR, "Failed to place call.");
    return;
  }

  syslog(LOG_NOTICE, "Started call to '%s'", sip_uri);
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

/***************************************
 * Private helpers
 ***************************************/

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
  syslog(LOG_INFO, "Using SIP identity for realm %s: %s", p_config->realm, str);

  linphone_proxy_config_set_identity(p_proxy, str);
  linphone_proxy_config_set_server_addr(p_proxy, p_config->server);
  linphone_proxy_config_enable_register(p_proxy, TRUE);

  return p_proxy;
}

void call_state_changed(LinphoneCore* p_linphone, LinphoneCall* p_call, LinphoneCallState cstate, const char *msg)
{
  switch (cstate) {
  case LinphoneCallOutgoingRinging:
    syslog(LOG_DEBUG, "Remote device is ringing.");
    break;
  case LinphoneCallConnected:
    syslog(LOG_DEBUG, "Connection established.");
    break;
  case LinphoneCallEnd:
    syslog(LOG_DEBUG, "Connection closed.");
    break;
  case LinphoneCallError:
    syslog(LOG_WARNING, "Failed to establish call.");
    break;
  default:
    syslog(LOG_DEBUG, "Unhandled notification on call: %i", cstate);
    break;
  }
}
