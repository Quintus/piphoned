#ifndef PIPHONED_CONFIGFILE_H
#define PIPHONED_CONFIGFILE_H

struct piphoned_config_parsedfile
{
  unsigned int uid;
  unsigned int gid;
  char* pidfile;
};

extern struct piphoned_config_parsedfile g_piphoned_config_info;

void piphoned_config_init(const char* configfile);

#endif
