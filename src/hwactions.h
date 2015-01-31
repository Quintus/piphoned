#ifndef PIPHONED_HWACTIONS_H
#define PIPHONED_HWACTIONS_H
#include <stdbool.h>

void piphoned_hwactions_init();                 /*< Initialize interrupt callbacks. */
void piphoned_hwactions_free();                 /*< Cleanup all the callbacks */
bool piphoned_hwactions_check_pickup(char* uri); /*< Has the phone been picked up? */
bool piphoned_hwactions_check_hangup();         /*< Has the phone been hung up? */

#endif
