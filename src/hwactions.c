#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/time.h>
#include <wiringPi.h>
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
static char s_sip_uri[MAX_SIP_URI_LENGTH];
static bool s_phone_is_up = false; /* Has the phone been picked up? Shared resource! */
static struct timeval s_hangup_timestamp;

/* This mutex protects the access to s_is_reading_hwdigit. */
static pthread_mutex_t s_hwdigit_mutex;
/* This mutex protects the access to s_phone_is_up. */
static pthread_mutex_t s_isup_mutex;

static void hangup_callback(int pin, void* arg);
static void dial_action_callback(int pin, void* arg);
static void dial_count_callback(int pin, void* arg);

/**
 * Sets up the callbacks for the interrupts on the Raspberry Pi’s pins.
 */
void piphoned_hwactions_init()
{
  memset(s_sip_uri, '\0', MAX_SIP_URI_LENGTH);
  gettimeofday(&s_hangup_timestamp, NULL);

  /* The grace time values used in this function as the first argument
   * to piphoned_hwactions_triggermonitor_new() describe the timespan
   * in which the lowlevel hardware triggers should be ignored if they
   * happen to fast in a sequence (which happens all the time). The
   * exact values used have been found by trial&error.
   * TODO: Make them configurable? */

  struct Piphoned_HwActions_TriggerMonitor* p_monitor = piphoned_hwactions_triggermonitor_new(100000, g_piphoned_config_info.hangup_pin, hangup_callback, NULL);
  piphoned_hwactions_triggermonitor_setup(p_monitor, INT_EDGE_BOTH);

  sp_trigger_monitors = (struct TriggerMonitorListItem*) malloc(sizeof(struct TriggerMonitorListItem));
  sp_trigger_monitors->p_monitor = p_monitor;
  sp_trigger_monitors->p_next = NULL;

  p_monitor = piphoned_hwactions_triggermonitor_new(100000, g_piphoned_config_info.dial_action_pin, dial_action_callback, NULL);
  piphoned_hwactions_triggermonitor_setup(p_monitor, INT_EDGE_BOTH);

  sp_trigger_monitors->p_next = (struct TriggerMonitorListItem*) malloc(sizeof(struct TriggerMonitorListItem));
  sp_trigger_monitors->p_next->p_monitor = p_monitor;
  sp_trigger_monitors->p_next->p_next = NULL;

  p_monitor = piphoned_hwactions_triggermonitor_new(70000, g_piphoned_config_info.dial_count_pin, dial_count_callback, NULL);
  piphoned_hwactions_triggermonitor_setup(p_monitor, INT_EDGE_FALLING);

  sp_trigger_monitors->p_next->p_next = (struct TriggerMonitorListItem*) malloc(sizeof(struct TriggerMonitorListItem));
  sp_trigger_monitors->p_next->p_next->p_monitor = p_monitor;
  sp_trigger_monitors->p_next->p_next->p_next = NULL;
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
 * Check if the phone is not on the phone base anymore, i.e.
 * it has been picked up. If so, returns true and places any
 * dialed SIP uri inside `uri`, which is guaranteed to be
 * NUL-terminated and start with the sequence "sip:" (without
 * the quotes). `uri` is not touched if the phone is on the
 * base (i.e. when this method returns false). The `auto_domain`
 * setting from the configuration file is automatically appanded
 * after an @ sign.
 *
 * Multiple calls to this method will not change anything
 * unless the phone has actually been picked up/hang up
 * between the calls.
 */
bool piphoned_hwactions_check_pickup(char* uri)
{
  bool result = false;

  pthread_mutex_lock(&s_isup_mutex);
  result = s_phone_is_up;

  /* While the phone is up (i.e. result is `true' here), the logic
   * in the number-reading callbacks is blocked. That is, `s_sip_uri'
   * will not be accessed fromthere. Thus, a mutex protecting `s_sip_uri'
   * is not required, as it is only accessed sequencially in different
   * threads. Even when automated customer service handling is implemented,
   * it probably won’t be done by modifying `s_sip_uri', but rather utilising
   * another variable. */
  if (result) {
    memset(uri, '\0', MAX_SIP_URI_LENGTH);
    sprintf(uri, "sip:%s@%s", s_sip_uri, g_piphoned_config_info.auto_domain);
  }

  pthread_mutex_unlock(&s_isup_mutex);

  return result;
}

/**
 * Checks if the phone is on the base, i.e. has been hung up.
 * Multiple calls to this method will not change anything
 * unless the phone has actually been picked up/hang up
 * between the calls.
 */
bool piphoned_hwactions_check_hangup()
{
  bool result = false;

  pthread_mutex_lock(&s_isup_mutex);
  result = !s_phone_is_up;
  pthread_mutex_unlock(&s_isup_mutex);

  return result;
}

/**
 * Called each time the hangup trigger connects or disconnects. Runs
 * in a separate thread.
 */
void hangup_callback(int pin, void* arg)
{
  struct timeval timestamp;

  pthread_mutex_lock(&s_hwdigit_mutex);
  if (s_is_reading_hwdigit) {
    /* The phone has been picked up WHILE using the ring. This should never happen
     * unless a user can manage this in his brain. */
    pthread_mutex_unlock(&s_hwdigit_mutex);
    syslog(LOG_CRIT, "Don't know how to handle pickup while dialing!");
    exit(5);
    return;
  }
  pthread_mutex_unlock(&s_hwdigit_mutex);

  pthread_mutex_lock(&s_isup_mutex);
  /* Handling the phone is not as clear as one possibly thinks.
   * Hanging up for example may actually trigger the trigger
   * two or three times. We don’t want this to cause a new call. */
  gettimeofday(&timestamp, NULL);
  if (timestamp.tv_sec - s_hangup_timestamp.tv_sec < 3) {
    pthread_mutex_unlock(&s_isup_mutex);
    return;
  }

  if (s_phone_is_up) {
    syslog(LOG_DEBUG, "Phone hung up.");
    /* Reset the SIP URI for the next call. Note that before we set `s_phone_is_up'
     * to `false', `s_sip_uri' won’t be touched by other threads. */
    memset(s_sip_uri, '\0', MAX_SIP_URI_LENGTH);
    s_phone_is_up = false;
  }
  else {
    syslog(LOG_DEBUG, "Phone picked up.");
    s_phone_is_up = true;
  }

  gettimeofday(&s_hangup_timestamp, NULL); /* Update time for check above */
  pthread_mutex_unlock(&s_isup_mutex);
}

static void dial_action_callback(int pin, void* arg)
{
  /* If a user dials while phoning, ignore it for now. It could later
   * be used for automatic customer service handling. */
  pthread_mutex_lock(&s_isup_mutex);
  if (s_phone_is_up) {
    pthread_mutex_unlock(&s_isup_mutex);
    syslog(LOG_NOTICE, "Ignoring attempt to input a digit while the phone is not hung up.");
    return;
  }
  pthread_mutex_unlock(&s_isup_mutex);

  /* Signal start/stop of reading a single digit */
  pthread_mutex_lock(&s_hwdigit_mutex);

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
