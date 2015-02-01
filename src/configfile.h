#ifndef PIPHONED_CONFIGFILE_H
#define PIPHONED_CONFIGFILE_H
#include <linux/limits.h>
#include "config.h"

/**
 * Configuration data for a single proxy.
 */
struct Piphoned_Config_ParsedFile_ProxyTable
{
  char name[512];        /*< Name of the ini section */
  char username[512];    /*< Username for authentication */
  char password[512];    /*< Password for authentication */
  char displayname[512]; /*< How your name is displayed to other users */
  char server[PATH_MAX]; /*< SIP server to connect to */
  char realm[PATH_MAX];  /*< Realm the SIP server asks for */
};

/**
 * The results of parsing the configuration file.
 */
struct Piphoned_Config_ParsedFile
{
  int uid;                /*< User ID to run as */
  int gid;                /*< Group ID to run as */
  char pidfile[PATH_MAX]; /*< PID file to write to */
  int hangup_pin;         /*< Pin to wait for hangup interrupt on */
  int dial_action_pin;    /*< Pin to check for start/stop number dialing */
  int dial_count_pin;     /*< Pin to check for the actual digits dialed */
  char auto_domain[PATH_MAX]; /*< Domain to append to numbers dialed */

  struct Piphoned_Config_ParsedFile_ProxyTable* proxies[PIPHONED_MAX_PROXY_NUM]; /*< Configuration for the proxies */
  int num_proxies; /*< Number of proxy configs in `proxies` */
};

/**
 * Global variable containing the parsed configuration file.
 */
extern struct Piphoned_Config_ParsedFile g_piphoned_config_info;

void piphoned_config_init(const char* configfile); /*< Read the given configuration file */
void piphoned_config_free(); /*< Free all dynamically allocated configuration settings */

#endif
