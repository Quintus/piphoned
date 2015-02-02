#ifndef LINPHONED_PHONE_MANAGER_H
#define LINPHONED_PHONE_MANAGER_H
#include <stdbool.h>
#include <linphone/linphonecore.h>

struct Piphoned_PhoneManager {
  LinphoneCoreVTable vtable; /*< Linphone callback table */
  LinphoneCore* p_linphone;  /*< Linphone Core object */
  LinphoneCall* p_call;      /*< Current call, if any (NULL otherwise) */
  char ipv4[512];            /*< Our public IPv4 */
  bool is_calling;           /*< Is a call in progress? */
  bool has_incoming_call;    /*< Is an incoming call awaiting acceptance? */
};

struct Piphoned_PhoneManager* piphoned_phonemanager_new();
bool piphoned_phonemanager_load_proxies(struct Piphoned_PhoneManager* ptr);
void piphoned_phonemanager_update(struct Piphoned_PhoneManager* ptr);
void piphoned_phonemanager_place_call(struct Piphoned_PhoneManager* ptr, const char* sip_uri);
void piphoned_phonemanager_stop_call(struct Piphoned_PhoneManager* ptr);
void piphoned_phonemanager_accept_incoming_call(struct Piphoned_PhoneManager* ptr);
void piphoned_phonemanager_decline_incoming_call(struct Piphoned_PhoneManager* ptr);
void piphoned_phonemanager_free(struct Piphoned_PhoneManager* ptr);

#endif
