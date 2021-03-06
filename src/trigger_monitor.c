#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <syslog.h>
#include <wiringPi.h>
#include "interrupt_handler.h"
#include "trigger_monitor.h"

static void monitor_callback(int pin, void* arg);

/**
 * Creates a new TriggerMonitor.
 *
 * \param grace_time If the pin device interrupts very quickly multiple times
 *                   within this number of microseconds, it is treated as a
 *                   single interrupt and the given callback is executed only
 *                   once instead of being executed for each of these quick
 *                   interrupts.
 * \param pin        WiringPi pin to create the monitor for.
 * \param[in] p_callback Function pointer to a callback function that shall be
 *                       run when the pin gets triggered (see also `grace_time`
 *                       above). The callback receives the number of the pin
 *                       triggered and a custom userdata pointer (see below).
 * \param[in] p_userdata This pointer is passed through unchanged to the callback
 *                       function.
 *
 * \returns the new TriggerMonitor instance.
 *
 * \remark Do not use the TriggerMonitor instance for more than one callback.
 */
struct Piphoned_HwActions_TriggerMonitor* piphoned_hwactions_triggermonitor_new(unsigned long grace_time, int pin, void (*p_callback)(int, void*), void* p_userdata)
{
  struct Piphoned_HwActions_TriggerMonitor* p_monitor = (struct Piphoned_HwActions_TriggerMonitor*) malloc(sizeof(struct Piphoned_HwActions_TriggerMonitor));
  memset(p_monitor, '\0', sizeof(struct Piphoned_HwActions_TriggerMonitor));
  p_monitor->grace_time = grace_time;
  p_monitor->p_callback = p_callback;
  p_monitor->p_userdata = p_userdata;
  p_monitor->pin        = pin;
  return p_monitor;
}

/**
 * Frees the TriggerMonitor instance. This method blocks until the underlying
 * thread has terminated.
 */
void piphoned_hwactions_triggermonitor_free(struct Piphoned_HwActions_TriggerMonitor* p_monitor)
{
  if (p_monitor) {
    syslog(LOG_DEBUG, "Stopping and freeing monitor on pin %d", p_monitor->pin);
    piphoned_terminate_pin_interrupt_handler(p_monitor->pin);
    free(p_monitor);
  }
}

/**
 * Starts the monitoring process with the given monitor for the given change.
 *
 * This method kicks off a separate thread that does the actual watching
 * of the pin. Your callback gets run inside this separate thread, so
 * be sure it only accesses shared resources within a mutex.
 *
 * \param p_monitor Sender.
 * \param edgetype One of the `INT_*` parameters also accepted by wiringPiISR().
 *
 * \remark Do not pass the same TriggerMonitor instance to different setup() calls.
 */
void piphoned_hwactions_triggermonitor_setup(struct Piphoned_HwActions_TriggerMonitor* p_monitor, int edgetype)
{
  int pin = p_monitor->pin;

  /* Ensure predictable pin start state */
  syslog(LOG_DEBUG, "Noramlizing pin state on pin %d", pin);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  pinMode(pin, INPUT);

  /* Sometimes an interrupt is triggered right on start. We don't want to count that one. */
  gettimeofday(&p_monitor->timestamp, NULL);
  p_monitor->microseconds_last = p_monitor->timestamp.tv_sec * 1000000 + p_monitor->timestamp.tv_usec;

  syslog(LOG_DEBUG, "Registering triggermonitor callback on pin %d", pin);
  if (!piphoned_handle_pin_interrupt(pin, edgetype, monitor_callback, p_monitor))
    syslog(LOG_ERR, "Failed to setup trigger mointor on pin %d.", pin);
}

/**
 * This is the callback function run on each of the frequent interrupts on the
 * chosen pin. Note how it employs a gracetime mechanism to not call the main
 * callback for all of interrupts that are fired in a very short period of time.
 */
void monitor_callback(int pin, void* arg)
{
  struct Piphoned_HwActions_TriggerMonitor* p_monitor = (struct Piphoned_HwActions_TriggerMonitor*) arg;

  pthread_mutex_lock(&p_monitor->action_mutex);

  gettimeofday(&p_monitor->timestamp, NULL);
  p_monitor->microseconds_now = p_monitor->timestamp.tv_sec * 1000000 + p_monitor->timestamp.tv_usec;

  /* The triggers on the phone hardware trigger multiple times in a very short interval.
   * The following gracetime check prevents the main callback from being called for each
   * of the about 10 triggering actions in a half second. */
  if (p_monitor->microseconds_now - p_monitor->microseconds_last <= p_monitor->grace_time)
    goto unlock_mutex;

  /* Actual action */
  syslog(LOG_DEBUG, "Received relevant unfiltered interrupt on pin %d", pin);
  p_monitor->p_callback(pin, p_monitor->p_userdata);

  /* Update last timestamp so gracetime check works for next time */
  p_monitor->microseconds_last = p_monitor->microseconds_now;

 unlock_mutex:
  pthread_mutex_unlock(&p_monitor->action_mutex);
}
