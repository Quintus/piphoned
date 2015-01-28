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
 * Keeping track of our allocated TriggerMonitor instances.
 */
struct TriggerMonitorListItem
{
  struct Piphoned_HwActions_TriggerMonitor* p_monitor;
  struct TriggerMonitorListItem* p_next;
};

static bool s_hangup_triggered = false; /*< true if hangup trigger has been triggered */
static struct TriggerMonitorListItem* sp_trigger_monitors = NULL; /*< Keeping track of our allocated TriggerMonitor instances */

static void hangup_callback(int pin, void* arg);

/**
 * Sets up the callbacks for the interrupts on the Raspberry Pi’s pins.
 */
void piphoned_hwactions_init()
{
  /* TODO: Call piphoned_hwactions_triggermonitor_new() and _setup()
   * for the target pins */

  struct Piphoned_HwActions_TriggerMonitor* p_monitor = piphoned_hwactions_triggermonitor_new(100000, g_piphoned_config_info.hangup_pin, hangup_callback, NULL);
  piphoned_hwactions_triggermonitor_setup(p_monitor, INT_EDGE_BOTH);

  struct TriggerMonitorListItem* sp_trigger_monitors = (struct TriggerMonitorListItem*) malloc(sizeof(struct TriggerMonitorListItem));
  sp_trigger_monitors->p_monitor = p_monitor;
  sp_trigger_monitors->p_next = NULL;
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

  piphoned_hwactions_triggermonitor_free(sp_trigger_monitors->p_monitor);
  free(sp_trigger_monitors);

  syslog(LOG_DEBUG, "All monitors terminated.");
  sp_trigger_monitors = NULL;
}

/**
 * Checks if the hangup trigger has been triggered. The variable used internally
 * is a resource that is shared with the interrupt function thread, so this
 * function first acquires the necessary mutex lock, then queries the variable,
 * **resets it to false**, releases the lock, and then returns what was found.
 * That is, after you got true from this function, the state is guaranteed
 * to be "not triggered" again. You never get true by two consecutive calls
 * to this function unless someone pulls the hangup trigger on the phone
 * in that very microsecond between the two calls.
 *
 * Because the function abstracts the threads from you, you don’t have to
 * worry about the underlying interrupt thread.
 *
 * This function makes no difference between releasing the trigger and
 * pressing it. When either has happened, it will return true.
 */
bool piphoned_hwactions_has_hangup_triggered()
{
  bool result = false;

  /*
  pthread_mutex_lock(&s_hangup_mutex);
  result = s_hangup_triggered;
  s_hangup_triggered = false; /* Reset so we will be notified of new hangup triggering /
  pthread_mutex_unlock(&s_hangup_mutex);
  */

  return result;
}

/**
 * Checks if an URI has been dialed, and if so, places it in the passed argument.
 * Returns true if an URI was dialed (and placed in the pointer), false
 * otherwise.
 */
bool piphoned_hwactions_has_dialed_uri(char* uri)
{
  return true;
}

/**
 * Called each time the hangup trigger connects or disconnects. Runs
 * in a separate thread.
 */
void hangup_callback(int pin, void* arg)
{
  syslog(LOG_NOTICE, "Triggered on pin %d!", pin);
  /* s_hangup_triggered = true; */
}
