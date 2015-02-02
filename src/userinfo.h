#ifndef PIPHONED_USERINFO_H
#define PIPHONED_USERINFO_H
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

uid_t piphoned_userinfo_get_uid(const char* username);
gid_t piphoned_userinfo_get_gid(const char* groupname);

#endif
