#ifndef PIPHONED_TRIGGER_MONITOR_H
#define PIPHONED_TRIGGER_MONITOR_H
#include <pthread.h>
#include <sys/time.h>

struct Piphoned_HwActions_TriggerMonitor {
  pthread_mutex_t action_mutex;
  struct timeval timestamp;
  unsigned long microseconds_now;
  unsigned long microseconds_last;
  unsigned long grace_time;
  void (*p_callback)(int, void* arg);
  void* p_userdata;
  int pin;
};

struct Piphoned_HwActions_TriggerMonitor* piphoned_hwactions_triggermonitor_new(unsigned long grace_time, int pin, void (*p_callback)(int, void*), void* p_userdata);
void piphoned_hwactions_triggermonitor_free(struct Piphoned_HwActions_TriggerMonitor* p_monitor);
void piphoned_hwactions_triggermonitor_setup(const struct Piphoned_HwActions_TriggerMonitor* p_monitor, int edgetype);

#endif
