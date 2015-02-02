#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <syslog.h>
#include "configfile.h"

struct Piphoned_Config_ParsedFile g_piphoned_config_info;

static void piphoned_config_parse_file(FILE* p_file, struct Piphoned_Config_ParsedFile* p_info);
static void piphoned_config_parse_ini_separator(const char* line, struct Piphoned_Config_ParsedFile* p_info);
static void piphoned_config_parse_ini_line(const char* line, struct Piphoned_Config_ParsedFile* p_info);
static void piphoned_config_parse_ini_generalline(const char* line, struct Piphoned_Config_ParsedFile* p_info);
static void piphoned_config_parse_ini_proxyline(const char* line, struct Piphoned_Config_ParsedFile* p_info);
static bool piphoned_config_parse_ini_key(const char* line, char* key, char* value);
static void piphoned_config_parsed_file_free(struct Piphoned_Config_ParsedFile* p_file);

enum Piphoned_Config_ParsedFile_ParseState {
  PIPHONED_CONFIG_PARSED_FILE_STOPPED = 0,     /* Parsing finished */
  PIPHONED_CONFIG_PARSED_FILE_STARTING,        /* Parsing just started */
  PIPHONED_CONFIG_PARSED_FILE_PARSING_GENERAL, /* Parsing the [General] section of the config file */
  PIPHONED_CONFIG_PARSED_FILE_PARSING_PROXY    /* Parsing a proxy section of the config file */
};

/* The current state of the config file parser */
static enum Piphoned_Config_ParsedFile_ParseState s_current_parsestate = PIPHONED_CONFIG_PARSED_FILE_STOPPED;

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
void piphoned_config_parse_file(FILE* p_file, struct Piphoned_Config_ParsedFile* p_info)
{
  char line[512];

  p_info->num_proxies = 0; /* At start, we do not have any proxies defined */

  while (!feof(p_file)) {
    memset(line, '\0', 512);
    fgets(line, 512, p_file);

    if (line[0] == '[') /* [section] */
      piphoned_config_parse_ini_separator(line, p_info);
    else
      piphoned_config_parse_ini_line(line, p_info);
  }
}

/**
 * Parses the given line as an INI separator and appends a new, dynamically
 * created ProxyTable to `p_info->proxies` (and increments `num_proxies`).
 */
void piphoned_config_parse_ini_separator(const char* line, struct Piphoned_Config_ParsedFile* p_info)
{
  char sectionname[512];
  size_t length = strlen(line);

  if (p_info->num_proxies >= PIPHONED_MAX_PROXY_NUM) {
    syslog(LOG_CRIT, "The maximum number of proxies (%d) has been reached; ignoring configuration file section '%s'", PIPHONED_MAX_PROXY_NUM, line);
    return;
  }

  memset(sectionname, '\0', 512);
  strncpy(sectionname, line+1, length - 3); /* => max 510 byte, terminating NUL guaranteed; 3 = "]\n\0" */

  if (strcmp(sectionname, "General") == 0) {
    s_current_parsestate = PIPHONED_CONFIG_PARSED_FILE_PARSING_GENERAL;
  }
  else {
    struct Piphoned_Config_ParsedFile_ProxyTable* p_proxytable = (struct Piphoned_Config_ParsedFile_ProxyTable*) malloc(sizeof(struct Piphoned_Config_ParsedFile_ProxyTable));
    memset(p_proxytable, '\0', sizeof(struct Piphoned_Config_ParsedFile_ProxyTable));

    strcpy(p_proxytable->name, sectionname);
    p_info->proxies[p_info->num_proxies++] = p_proxytable;
    /* TODO: Check we don't exceed PIPHONED_MAX_PROXY_NUM */

    s_current_parsestate = PIPHONED_CONFIG_PARSED_FILE_PARSING_PROXY;
  }
}

/**
 * Parses the given line as a setting in the given parser state.
 */
void piphoned_config_parse_ini_line(const char* line, struct Piphoned_Config_ParsedFile* p_info)
{
  if (strlen(line) == 0 || line[0] == '#' || line[0] == '\n') /* Ignore comments and empty lines */
    return;

  switch(s_current_parsestate) {
  case PIPHONED_CONFIG_PARSED_FILE_STARTING:
    syslog(LOG_WARNING, "Ignoring config file line without section: '%s'", line);
    break; /* Ignore anything before the first section is encountered */
  case PIPHONED_CONFIG_PARSED_FILE_PARSING_GENERAL:
    piphoned_config_parse_ini_generalline(line, p_info);
    break;
  case PIPHONED_CONFIG_PARSED_FILE_PARSING_PROXY:
    piphoned_config_parse_ini_proxyline(line, p_info);
    break;
  default:
    syslog(LOG_WARNING, "Encountered config file line in unexpected state %d", s_current_parsestate);
    break;
  }
}

/**
 * Parses the given line as a setting in the [General] section.
 */
void piphoned_config_parse_ini_generalline(const char* line, struct Piphoned_Config_ParsedFile* p_info)
{
  char key[512];
  char value[512];

  if (!piphoned_config_parse_ini_key(line, key, value)) {
    syslog(LOG_WARNING, "Ignoring malformed configuration file line '%s'", line);
    return;
  }

  syslog(LOG_DEBUG, "Configuration keypair in [General] section: '%s' => '%s'", key, value);

  if (strcmp(key, "uid") == 0) {
    p_info->uid = atoi(value);
  }
  else if (strcmp(key, "gid") == 0) {
    p_info->gid = atoi(value);
  }
  else if (strcmp(key, "pidfile") == 0) {
    strcpy(p_info->pidfile, value);
  }
  else if (strcmp(key, "hangup_pin") == 0) {
    p_info->hangup_pin = atoi(value);
  }
  else if (strcmp(key, "dial_action_pin") == 0) {
    p_info->dial_action_pin = atoi(value);
  }
  else if (strcmp(key, "dial_count_pin") == 0) {
    p_info->dial_count_pin = atoi(value);
  }
  else if (strcmp(key, "auto_domain") == 0) {
    strcpy(p_info->auto_domain, value);
  }
  else if (strcmp(key, "ring_sound_device") == 0) {
    strcpy(p_info->ring_sound_device, value);
  }
  else if (strcmp(key, "playback_sound_device") == 0) {
    strcpy(p_info->playback_sound_device, value);
  }
  else if (strcmp(key, "capture_sound_device") == 0) {
    strcpy(p_info->capture_sound_device, value);
  }
  else {
    syslog(LOG_ERR, "Ignoring invalid key '%s' in [General] section of configuration file.", key);
  }
}

/**
 * Parses the given line as a setting in a Proxy section.
 */
void piphoned_config_parse_ini_proxyline(const char* line, struct Piphoned_Config_ParsedFile* p_info)
{
  char key[512];
  char value[512];
  struct Piphoned_Config_ParsedFile_ProxyTable* p_proxytable = NULL;

  if (!piphoned_config_parse_ini_key(line, key, value)) {
    syslog(LOG_WARNING, "Ignoring malformed configuration file line '%s'", line);
    return;
  }

  p_proxytable = p_info->proxies[p_info->num_proxies - 1];
  syslog(LOG_DEBUG, "Configuration keypair in [%s] section: '%s' => '%s'", p_proxytable->name, key, value);

  if (strcmp(key, "username") == 0)
    strcpy(p_proxytable->username, value);
  else if (strcmp(key, "password") == 0)
    strcpy(p_proxytable->password, value);
  else if (strcmp(key, "displayname") == 0)
    strcpy(p_proxytable->displayname, value);
  else if (strcmp(key, "server") == 0)
    strcpy(p_proxytable->server, value);
  else if (strcmp(key, "realm") == 0)
    strcpy(p_proxytable->realm, value);
  else
    syslog(LOG_ERR, "Ignoring invalid key '%s' in [%s] section of configuration file.", p_proxytable->name, key);
}

/**
 * Parses a single "key = value" line. The results are placed in the given
 * arguments, where each is required to have a size of at least 512 byte
 * (more will never be needed).
 *
 * \param[in]  line  NUL-terminated line to parse
 * \param[out] key   Receives the key as a NUL-terminated string.
 * \param[out] value Receives the value as a NUL-terminated string.
 *
 * \returns false on parsing failure, true otherwise.
 */
static bool piphoned_config_parse_ini_key(const char* line, char* key, char* value)
{
  char* equalsign = NULL;
  const char* valstart = NULL;
  size_t length = strlen(line);
  size_t i = 0;
  size_t position = 0;

  /* Skip all leading whitespace */
  for(i=0; i < length && line[0] == ' '; line++,i++)
    ;

  if (i == length - 1) {
    syslog(LOG_WARNING, "Encountered line containing only whitespace");
    return false;
  }

  if ((equalsign = strchr(line, '=')) == NULL) {
    syslog(LOG_WARNING, "Encountered line without equal sign (=).");
    return false;
  }

  position = strcspn(line, "=");

  memset(key, '\0', 512);
  memset(value, '\0', 512);

  /********************
   * Part 1: key
   *******************/
  strncpy(key, line, position);

  /* Remove trailing whitespace */
  for(i=strlen(key)-1; key[i] == ' '; i--)
    key[i] = '\0';

  if (strlen(key) == 0) {
    syslog(LOG_WARNING, "Found empty key.");
    return false;
  }

  /********************
   * Part 2: value
   *******************/

  /* Remove leading whitespace */
  length   = strlen(line + position + 1);
  valstart = line + position + 1;
  for(i=0; i < length && valstart[0] == ' '; i++,valstart++)
    ;

  if (i == length - 1) {
    syslog(LOG_WARNING, "Found empty value.");
    return false;
  }

  strcpy(value, valstart);

  /* Remove trailing whitespace */
  for(i=strlen(value)-1; value[i] == ' ' || value[i] == '\n'; i--)
    value[i] = '\0';

  return true;
}

/**
 * Frees all dynamically allocated information related to the configuration.
 * The `p_file` pointer itself stays valid.
 */
void piphoned_config_parsed_file_free(struct Piphoned_Config_ParsedFile* p_file)
{
  while (--p_file->num_proxies >= 0) {
    free(p_file->proxies[p_file->num_proxies]);
  }

  p_file->num_proxies = 0; /* recover -1 */
}
