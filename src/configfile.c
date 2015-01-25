#include <stdio.h>
#include <stdlib.h>
#include "configfile.h"

struct piphoned_config_parsedfile g_piphoned_config_info;

void piphoned_config_init(const char* configfile)
{
  FILE* p_file = fopen(configfile, "r");

  if (!p_file) {
    syslog(LOG_CRIT, "Failed to open config file '%s': %m", configfile);
    exit(2);
  }

  fclose(p_file);
}
