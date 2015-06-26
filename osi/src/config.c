/******************************************************************************
 *
 *  Copyright (C) 2014 Google, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

#define LOG_TAG "bt_osi_config"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "osi/include/allocator.h"
#include "osi/include/config.h"
#include "osi/include/list.h"
#include "osi/include/log.h"

typedef struct {
  char *key;
  char *value;
} entry_t;

typedef struct {
  char *name;
  list_t *entries;
} section_t;

struct config_t {
  list_t *sections;
};

// Empty definition; this type is aliased to list_node_t.
struct config_section_iter_t {};

static void config_parse(FILE *fp, config_t *config);

static section_t *section_new(const char *name);
static void section_free(void *ptr);
static section_t *section_find(const config_t *config, const char *section);

static entry_t *entry_new(const char *key, const char *value);
static void entry_free(void *ptr);
static entry_t *entry_find(const config_t *config, const char *section, const char *key);

config_t *config_new_empty(void) {
  config_t *config = osi_calloc(sizeof(config_t));
  if (!config) {
    LOG_ERROR(LOG_TAG, "%s unable to allocate memory for config_t.", __func__);
    goto error;
  }

  config->sections = list_new(section_free);
  if (!config->sections) {
    LOG_ERROR(LOG_TAG, "%s unable to allocate list for sections.", __func__);
    goto error;
  }

  return config;

error:;
  config_free(config);
  return NULL;
}

config_t *config_new(const char *filename) {
  assert(filename != NULL);

  config_t *config = config_new_empty();
  if (!config)
    return NULL;

  FILE *fp = fopen(filename, "rt");
  if (!fp) {
    LOG_ERROR(LOG_TAG, "%s unable to open file '%s': %s", __func__, filename, strerror(errno));
    config_free(config);
    return NULL;
  }
  config_parse(fp, config);
  fclose(fp);
  return config;
}

void config_free(config_t *config) {
  if (!config)
    return;

  list_free(config->sections);
  osi_free(config);
}

bool config_has_section(const config_t *config, const char *section) {
  assert(config != NULL);
  assert(section != NULL);

  return (section_find(config, section) != NULL);
}

bool config_has_key(const config_t *config, const char *section, const char *key) {
  assert(config != NULL);
  assert(section != NULL);
  assert(key != NULL);

  return (entry_find(config, section, key) != NULL);
}

int config_get_int(const config_t *config, const char *section, const char *key, int def_value) {
  assert(config != NULL);
  assert(section != NULL);
  assert(key != NULL);

  entry_t *entry = entry_find(config, section, key);
  if (!entry)
    return def_value;

  char *endptr;
  int ret = strtol(entry->value, &endptr, 0);
  return (*endptr == '\0') ? ret : def_value;
}

bool config_get_bool(const config_t *config, const char *section, const char *key, bool def_value) {
  assert(config != NULL);
  assert(section != NULL);
  assert(key != NULL);

  entry_t *entry = entry_find(config, section, key);
  if (!entry)
    return def_value;

  if (!strcmp(entry->value, "true"))
    return true;
  if (!strcmp(entry->value, "false"))
    return false;

  return def_value;
}

const char *config_get_string(const config_t *config, const char *section, const char *key, const char *def_value) {
  assert(config != NULL);
  assert(section != NULL);
  assert(key != NULL);

  entry_t *entry = entry_find(config, section, key);
  if (!entry)
    return def_value;

  return entry->value;
}

void config_set_int(config_t *config, const char *section, const char *key, int value) {
  assert(config != NULL);
  assert(section != NULL);
  assert(key != NULL);

  char value_str[32] = { 0 };
  sprintf(value_str, "%d", value);
  config_set_string(config, section, key, value_str);
}

void config_set_bool(config_t *config, const char *section, const char *key, bool value) {
  assert(config != NULL);
  assert(section != NULL);
  assert(key != NULL);

  config_set_string(config, section, key, value ? "true" : "false");
}

void config_set_string(config_t *config, const char *section, const char *key, const char *value) {
  section_t *sec = section_find(config, section);
  if (!sec) {
    sec = section_new(section);
    list_append(config->sections, sec);
  }

  for (const list_node_t *node = list_begin(sec->entries); node != list_end(sec->entries); node = list_next(node)) {
    entry_t *entry = list_node(node);
    if (!strcmp(entry->key, key)) {
      osi_free(entry->value);
      entry->value = osi_strdup(value);
      return;
    }
  }

  entry_t *entry = entry_new(key, value);
  list_append(sec->entries, entry);
}

bool config_remove_section(config_t *config, const char *section) {
  assert(config != NULL);
  assert(section != NULL);

  section_t *sec = section_find(config, section);
  if (!sec)
    return false;

  return list_remove(config->sections, sec);
}

bool config_remove_key(config_t *config, const char *section, const char *key) {
  assert(config != NULL);
  assert(section != NULL);
  assert(key != NULL);

  section_t *sec = section_find(config, section);
  entry_t *entry = entry_find(config, section, key);
  if (!sec || !entry)
    return false;

  return list_remove(sec->entries, entry);
}

const config_section_node_t *config_section_begin(const config_t *config) {
  assert(config != NULL);
  return (const config_section_node_t *)list_begin(config->sections);
}

const config_section_node_t *config_section_end(const config_t *config) {
  assert(config != NULL);
  return (const config_section_node_t *)list_end(config->sections);
}

const config_section_node_t *config_section_next(const config_section_node_t *node) {
  assert(node != NULL);
  return (const config_section_node_t *)list_next((const list_node_t *)node);
}

const char *config_section_name(const config_section_node_t *node) {
  assert(node != NULL);
  const list_node_t *lnode = (const list_node_t *)node;
  const section_t *section = (const section_t *)list_node(lnode);
  return section->name;
}

bool config_save(const config_t *config, const char *filename) {
  assert(config != NULL);
  assert(filename != NULL);
  assert(*filename != '\0');

  char *temp_filename = osi_calloc(strlen(filename) + 5);
  if (!temp_filename) {
    LOG_ERROR(LOG_TAG, "%s unable to allocate memory for filename.", __func__);
    return false;
  }

  strcpy(temp_filename, filename);
  strcat(temp_filename, ".new");

  FILE *fp = fopen(temp_filename, "wt");
  if (!fp) {
    LOG_ERROR(LOG_TAG, "%s unable to write file '%s': %s", __func__, temp_filename, strerror(errno));
    goto error;
  }

  for (const list_node_t *node = list_begin(config->sections); node != list_end(config->sections); node = list_next(node)) {
    const section_t *section = (const section_t *)list_node(node);
    fprintf(fp, "[%s]\n", section->name);

    for (const list_node_t *enode = list_begin(section->entries); enode != list_end(section->entries); enode = list_next(enode)) {
      const entry_t *entry = (const entry_t *)list_node(enode);
      fprintf(fp, "%s = %s\n", entry->key, entry->value);
    }

    // Only add a separating newline if there are more sections.
    if (list_next(node) != list_end(config->sections))
      fputc('\n', fp);
  }

  fflush(fp);
  fclose(fp);

  if (rename(temp_filename, filename) == -1) {
    LOG_ERROR(LOG_TAG, "%s unable to commit file '%s': %s", __func__, filename, strerror(errno));
    goto error;
  }

  osi_free(temp_filename);
  return true;

error:;
  unlink(temp_filename);
  osi_free(temp_filename);
  return false;
}

static char *trim(char *str) {
  while (isspace(*str))
    ++str;

  if (!*str)
    return str;

  char *end_str = str + strlen(str) - 1;
  while (end_str > str && isspace(*end_str))
    --end_str;

  end_str[1] = '\0';
  return str;
}

static void config_parse(FILE *fp, config_t *config) {
  assert(fp != NULL);
  assert(config != NULL);

  int line_num = 0;
  char line[1024];
  char section[1024];
  strcpy(section, CONFIG_DEFAULT_SECTION);

  while (fgets(line, sizeof(line), fp)) {
    char *line_ptr = trim(line);
    ++line_num;

    // Skip blank and comment lines.
    if (*line_ptr == '\0' || *line_ptr == '#')
      continue;

    if (*line_ptr == '[') {
      size_t len = strlen(line_ptr);
      if (line_ptr[len - 1] != ']') {
        LOG_DEBUG(LOG_TAG, "%s unterminated section name on line %d.", __func__, line_num);
        continue;
      }
      strncpy(section, line_ptr + 1, len - 2);
      section[len - 2] = '\0';
    } else {
      char *split = strchr(line_ptr, '=');
      if (!split) {
        LOG_DEBUG(LOG_TAG, "%s no key/value separator found on line %d.", __func__, line_num);
        continue;
      }

      *split = '\0';
      config_set_string(config, section, trim(line_ptr), trim(split + 1));
    }
  }
}

static section_t *section_new(const char *name) {
  section_t *section = osi_calloc(sizeof(section_t));
  if (!section)
    return NULL;

  section->name = osi_strdup(name);
  section->entries = list_new(entry_free);
  return section;
}

static void section_free(void *ptr) {
  if (!ptr)
    return;

  section_t *section = ptr;
  osi_free(section->name);
  list_free(section->entries);
  osi_free(section);
}

static section_t *section_find(const config_t *config, const char *section) {
  for (const list_node_t *node = list_begin(config->sections); node != list_end(config->sections); node = list_next(node)) {
    section_t *sec = list_node(node);
    if (!strcmp(sec->name, section))
      return sec;
  }

  return NULL;
}

static entry_t *entry_new(const char *key, const char *value) {
  entry_t *entry = osi_calloc(sizeof(entry_t));
  if (!entry)
    return NULL;

  entry->key = osi_strdup(key);
  entry->value = osi_strdup(value);
  return entry;
}

static void entry_free(void *ptr) {
  if (!ptr)
    return;

  entry_t *entry = ptr;
  osi_free(entry->key);
  osi_free(entry->value);
  osi_free(entry);
}

static entry_t *entry_find(const config_t *config, const char *section, const char *key) {
  section_t *sec = section_find(config, section);
  if (!sec)
    return NULL;

  for (const list_node_t *node = list_begin(sec->entries); node != list_end(sec->entries); node = list_next(node)) {
    entry_t *entry = list_node(node);
    if (!strcmp(entry->key, key))
      return entry;
  }

  return NULL;
}
