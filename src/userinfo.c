#include <string.h>
#include <sys/types.h>
#include <grp.h>
#include "userinfo.h"

/**
 * Returns the UID for the given user name. Returns -1 if there
 * is no user of that name.
 */
uid_t piphoned_userinfo_get_uid(const char* username)
{
  struct passwd* p_passwd = NULL;
  uid_t uid = -1;

  while ((p_passwd = getpwent())) {
    if (strcmp(p_passwd->pw_name, username) == 0) {
      uid = p_passwd->pw_uid;
      break;
    }
  }

  endpwent();
  return uid;
}

/**
 * Returns the GID for the given group name. Returns -1 if there
 * is no group of that name.
 */
gid_t piphoned_userinfo_get_gid(const char* groupname)
{
  struct group* p_group = NULL;
  gid_t gid = -1;

  while ((p_group = getgrent())) {
    if (strcmp(p_group->gr_name, groupname) == 0) {
      gid = p_group->gr_gid;
      break;
    }
  }

  endgrent();
  return gid;
}
