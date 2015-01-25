#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "configfile.h"

struct Piphoned_Config_ParsedFile g_piphoned_config_info;

static void piphoned_config_parse_file(FILE* p_file, Piphoned_Config_ParsedFile* p_info);
static void piphoned_config_parse_ini_separator(const char* line, Piphoned_Config_ParsedFile* p_info);
static void piphoned_config_parse_ini_line(const char* line, Piphoned_Config_ParsedFile* p_info);
static void piphoned_config_parsed_file_free(Piphoned_Config_ParsedFile* p_file);

enum Piphoned_Config_ParsedFile_ParseState {
  PIPHONED_CONFIG_PARSED_FILE_STOPPED = 0,     /* Parsing finished */
  PIPHONED_CONFIG_PARSED_FILE_STARTING,        /* Parsing just started */
  PIPHONED_CONFIG_PARSED_FILE_PARSING_GENERAL, /* Parsing the [General] section of the config file */
  PIPHONED_CONFIG_PARSED_FILE_PARSING_PROXY    /* Parsing a proxy section of the config file */
};

/* The current state of the config file parser */
static Piphoned_Config_ParsedFile_ParseState s_current_parsestate = PIPHONED_CONFIG_PARSED_FILE_STOPPED;

/**
 * Initialize the global configuration info. When this function is called,
 * it populates the global variable `g_piphoned_config_info` with the
 * values found in the given configuration file.
 */
void piphoned_config_init(const char* configfile)
{
  FILE* p_file = fopen(configfile, "r");

  if (!p_file) {
    syslog(LOG_CRIT, "Failed to open config file '%s': %m. Exiting!", configfile);
    exit(2);
  }

  s_current_parsestate = PIPHONED_CONFIG_PARSED_FILE_STARTING;
  piphoned_config_parse_file(p_file, &g_piphoned_config_info);
  s_current_parsestate = PIPHONED_CONFIG_PARSED_FILE_STOPPED;

  fclose(p_file);
}

/**
 * Frees the dynamically allocated parts of the global configuration info.
 */
void piphoned_config_free()
{
  piphoned_config_parsed_file_free(&g_piphoned_config_info);
}

/**
 * Parses the given FILE* as a configuration file and stores the information
 * found in `p_info`. This function creates dynamically allocated information
 * that has to be freed with piphoned_config_parsed_file_free().
 */
void piphoned_config_parse_file(FILE* p_file, Piphoned_Config_ParsedFile* p_info)
{
  char line[512];

  p_info->num_proxies = 0; /* At start, we do not have any proxies defined */

  while (!feof(p_file)) {
    memset(line, '\0', 512);
    fgets(line, 512, p_file);

    if (line[0] == '[') /* [section] */
      piphoned_config_parse_ini_separator(line, p_info);
    else
      piphone_config_parse_ini_line(line, p_info);
  }
}

/**
 * Parses the given line as an INI separator and appends a new, dynamically
 * created GeneralTable to `p_info->proxies` (and increments `num_proxies`).
 */
void piphoned_config_parse_ini_separator(const char* line, Piphoned_Config_ParsedFile* p_info)
{
  char sectionname[512];
  size_t length = strlen(line);

  if (p_info->num_proxies >= PIPHONED_MAX_PROXY_NUM) {
    syslog(LOG_CRIT, "The maximum number of proxies (%d) has been reached; ignoring configuration file section '%s'", PIPHONED_MAX_PROXY_NUM, line);
    return;
  }

  memset(sectionname, '\0', 512);
  strncpy(sectionname, line[1], length - 3); /* => max 510 byte, terminating NUL guaranteed; 3 = "]\n\0" */

  if (strcmp(sectionname, "General") == 0) {
    s_current_state = PIPHONE_CONFIG_PARSED_FILE_PARSING_GENERAL;
  }
  else {
    struct Piphoned_Config_ParsedFile_ProxyTable* p_proxytable = (Piphoned_Config_ParsedFile_ProxyTable*) malloc(sizeof(Piphoned_Config_ParsedFile_ProxyTable));
    memset(p_proxytable, '\0', sizeof(Piphoned_Config_ParsedFile_ProxyTable));

    strcpy(p_proxytable->name, sectionname);
    p_info->proxies[p_info->num_proxies++] = p_proxytable;

    s_current_state = PIPHONE_CONFIG_PARSED_FILE_PARSING_PROXY;
  }
}

/**
 * Parses the given line as a setting in the given parser state.
 */
void piphoned_config_parse_ini_line(const char* line, Piphoned_Config_ParsedFile* p_info)
{
  if (line[0] == '#' || line[0] == '\n') /* Ignore comments and empty lines */
    return;

  /* TODO: HIER! */
}

/**
 * Frees all dynamically allocated information related to the configuration.
 * The `p_file` pointer itself stays valid.
 */
void piphoned_config_parsed_file_free(Piphone_Config_ParsedFile* p_file)
{
  while (--p_file->num_proxies >= 0) {
    free(p_file->proxies[p_file->num_proxies]);
  }
}
