#ifndef PIPHONED_CONFIGFILE_H
#define PIPHONED_CONFIGFILE_H
#include <linux/limits.h>
#include "config.h"

struct Piphoned_Config_ParsedFile_GeneralTable
{
  int uid;
  int gid;
  char pidfile[PATH_MAX];
};

struct Piphoned_Config_ParsedFile_ProxyTable
{
  char name[512];
};

struct Piphoned_Config_ParsedFile
{
  struct Piphoned_Config_ParsedFile_GeneralTable general;
  struct Piphoned_Config_ParsedFile_ProxyTable* proxies[PIPHONED_MAX_PROXY_NUM];
  int num_proxies;
};

extern struct Piphoned_Config_ParsedFile g_piphoned_config_info;

void piphoned_config_init(const char* configfile);
void piphoned_config_free();

#endif
