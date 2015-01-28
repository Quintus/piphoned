#ifndef PIPHONED_HWACTIONS_H
#define PIPHONED_HWACTIONS_H
#include <stdbool.h>

void piphoned_hwactions_init();                 /*< Initialize interrupt callbacks. */
void piphoned_hwactions_free();                 /*< Cleanup all the callbacks */
bool piphoned_hwactions_has_hangup_triggered(); /*< Has the hangup trigger been activated? */
bool piphoned_hwactions_dialed_uri(char* uri);  /*< Has an URI been dialed? */

#endif
