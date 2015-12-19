#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/time.h>
#include <wiringPi.h>
#include <linphone/linphonecore.h>
#include "hwactions.h"
#include "configfile.h"
#include "trigger_monitor.h"

/**
 * Maximum length of a SIP uri.
 * TODO: Make this a compilation option.
 */
#define MAX_SIP_URI_LENGTH 512

/**
 * Keeping track of our allocated TriggerMonitor instances.
 */
struct TriggerMonitorListItem
{
  struct Piphoned_HwActions_TriggerMonitor* p_monitor;
  struct TriggerMonitorListItem* p_next;
};

static struct TriggerMonitorListItem* sp_trigger_monitors = NULL; /*< Keeping track of our allocated TriggerMonitor instances */
static bool s_is_reading_hwdigit = false; /* Are we dialing a digit right now? Shared resource! */
static int s_hwdigit = -1; /* Current dialed digit. Shared, but only sequencially in different threads. */
static char s_sip_uri[MAX_SIP_URI_LENGTH]; /* The full dialed SIP URI. Shared resource! */
static struct timeval s_dial_timestamp;

/* This mutex protects the access to s_is_reading_hwdigit and s_sip_uri. */
static pthread_mutex_t s_hwdigit_mutex;

static void dial_action_callback(int pin, void* arg);
static void dial_count_callback(int pin, void* arg);

/**
 * Sets up the callbacks for the interrupts on the Raspberry Pi’s pins.
 */
void piphoned_hwactions_init()
{
  memset(s_sip_uri, '\0', MAX_SIP_URI_LENGTH);
  gettimeofday(&s_dial_timestamp, NULL);

  pinMode(g_piphoned_config_info.hangup_pin, INPUT);

  /* The grace time values used in this function as the first argument
   * to piphoned_hwactions_triggermonitor_new() describe the timespan
   * in which the lowlevel hardware triggers should be ignored if they
   * happen to fast in a sequence (which happens all the time). The
   * exact values used have been found by trial&error.
   * TODO: Make them configurable? */

  struct Piphoned_HwActions_TriggerMonitor* p_monitor = piphoned_hwactions_triggermonitor_new(100000, g_piphoned_config_info.dial_action_pin, dial_action_callback, NULL);
  piphoned_hwactions_triggermonitor_setup(p_monitor, INT_EDGE_BOTH);

  sp_trigger_monitors = (struct TriggerMonitorListItem*) malloc(sizeof(struct TriggerMonitorListItem));
  sp_trigger_monitors->p_monitor = p_monitor;
  sp_trigger_monitors->p_next = NULL;

  p_monitor = piphoned_hwactions_triggermonitor_new(70000, g_piphoned_config_info.dial_count_pin, dial_count_callback, NULL);
  piphoned_hwactions_triggermonitor_setup(p_monitor, INT_EDGE_FALLING);

  sp_trigger_monitors->p_next = (struct TriggerMonitorListItem*) malloc(sizeof(struct TriggerMonitorListItem));
  sp_trigger_monitors->p_next->p_monitor = p_monitor;
  sp_trigger_monitors->p_next->p_next = NULL;
}

/**
 * Terminates all trigger monitors and frees the allocated resources.
 */
void piphoned_hwactions_free()
{
  struct TriggerMonitorListItem* p_item = sp_trigger_monitors;
  syslog(LOG_DEBUG, "Asking all monitors to terminate.");

  while(p_item->p_next) {
    struct TriggerMonitorListItem* p_next = p_item->p_next;
    piphoned_hwactions_triggermonitor_free(p_item->p_monitor);
    free(p_item);
    p_item = p_next;
  }

  piphoned_hwactions_triggermonitor_free(p_item->p_monitor);
  free(p_item);

  syslog(LOG_DEBUG, "All monitors terminated.");
  sp_trigger_monitors = NULL;
}

/**
 * Checks if the phone is on the base.
 */
bool piphoned_hwactions_is_phone_hung_up()
{
  return digitalRead(g_piphoned_config_info.hangup_pin) == LOW;
}

/**
 * Get a dialed SIP URI which is guaranteed to be NUL-terminated and
 * start with the sequence "sip:" (without the quotes). The
 * `auto_domain` setting from the configuration file is automatically
 * appanded after an @ sign.
 *
 * The URI is reset afterwards, i.e. if you call this function again
 * immediately, you’ll get a zero-length string.
 *
 * `target` has to be at least MAX_SIP_URI length bytes long and is
 * guaranteed to be NUL-terminated on return.
 */
void piphoned_hwactions_get_sip_uri(char* target)
{
  memset(target, '\0', MAX_SIP_URI_LENGTH);

  pthread_mutex_lock(&s_hwdigit_mutex);
  sprintf(target, "sip:%s@%s", s_sip_uri, g_piphoned_config_info.auto_domain);
  memset(s_sip_uri, '\0', MAX_SIP_URI_LENGTH);
  pthread_mutex_unlock(&s_hwdigit_mutex);
}

static void dial_action_callback(int pin, void* arg)
{
  /* If a user dials while phoning, ignore it for now. It could later
   * be used for automatic customer service handling. */
  if (!piphoned_hwactions_is_phone_hung_up()) {
    syslog(LOG_NOTICE, "Ignoring attempt to input a digit while the phone is not hung up.");
    return;
  }

  /* Signal start/stop of reading a single digit */
  pthread_mutex_lock(&s_hwdigit_mutex);

  /* Sometimes there is an interrupt on the dial action pin although it shouldn't.
   * Nobody touched it, it just happens. In that case s_reading_digit is set to
   * true, as if someone started inputting a digit. However, digit input will
   * most likely never take longer than a few seconds, hence, if we detect that
   * since the start of the inputting a suspiciously large number of seconds
   * has passed, we ignore that and instead treat the new interrupt as the
   * start of a digit. */
  if (s_is_reading_hwdigit) {
    struct timeval timestamp;
    gettimeofday(&timestamp, NULL);
    if (timestamp.tv_sec - s_dial_timestamp.tv_sec > 10) {
      syslog(LOG_WARNING, "Suspiciously large time difference between the last two digit input interrupts detected. Treating this interrupt as a digit start instead of a digit end.");
      s_is_reading_hwdigit = false;
    }
  }

  if (s_is_reading_hwdigit) {
    int length = 0;

    /* Signal end; also blocks possible unexpected post-calls in dial_count_callback() */
    syslog(LOG_DEBUG, "End of digit.");
    s_is_reading_hwdigit = false;

    /* Check we don’t exceed maxmium length of string (-> segfault) */
    length = strlen(s_sip_uri);
    if (length >= MAX_SIP_URI_LENGTH) {
      syslog(LOG_ERR, "Reached maximum length of SIP URI (%d). Ignoring new digit %d.", MAX_SIP_URI_LENGTH, s_hwdigit);
      return;
    }

    /* Append to the URI string, which is NUL-filled for empty digits already. */
    sprintf(s_sip_uri + length, "%d", s_hwdigit);
  }
  else {
    syslog(LOG_DEBUG, "Start of digit.");
    s_hwdigit = 0; /* Reset for getting a new digit */
    s_is_reading_hwdigit = true;
  }

  gettimeofday(&s_dial_timestamp, NULL);
  pthread_mutex_unlock(&s_hwdigit_mutex);
}

/**
 * This callback function is called once for each number of the
 * dialpad that passes when releasing the ring. By counting
 * the times, we get to know the actual number that was
 * dialed.
 */
static void dial_count_callback(int pin, void* arg)
{
  /* If this gets triggered while we are not dialing, ignore it. */
  pthread_mutex_lock(&s_hwdigit_mutex);
  if (!s_is_reading_hwdigit) {
    pthread_mutex_unlock(&s_hwdigit_mutex);
    return;
  }
  pthread_mutex_unlock(&s_hwdigit_mutex);

  /* Count one digit up. 10 counts as zero (last digit on hardware numpad). */
  if (++s_hwdigit >= 10)
    s_hwdigit = 0;
}
