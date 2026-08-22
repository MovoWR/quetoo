/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_map_catalog.h"

#include "shared/shared.h"

#include <stdlib.h>
#include <string.h>

static void Race_MapCatalog_InitEntry(race_map_catalog_entry_t *entry) {
  memset(entry, 0, sizeof(*entry));
  entry->time_limit = -1.f;
}

static bool Race_MapCatalog_Copy(char *output, const size_t output_size,
                                 const char *value) {
  if (!output || !output_size || !value || q_strlen(value) >= output_size) {
    return false;
  }
  q_strlcpy(output, value, output_size);
  return true;
}

static bool Race_MapCatalog_ParseInt(const char *value, int32_t *output) {
  if (!value || !*value || !output) {
    return false;
  }
  char *end;
  const long parsed = strtol(value, &end, 10);
  if (!end || *end || parsed < INT32_MIN || parsed > INT32_MAX) {
    return false;
  }
  *output = (int32_t) parsed;
  return true;
}

static bool Race_MapCatalog_ParseFloat(const char *value, float *output) {
  if (!value || !*value || !output) {
    return false;
  }
  char *end;
  const float parsed = strtof(value, &end);
  if (!end || *end) {
    return false;
  }
  *output = parsed;
  return true;
}

bool Race_MapCatalog_Parse(const char *contents, race_map_catalog_t *catalog) {
  if (!contents || !catalog) {
    return false;
  }

  race_map_catalog_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  race_map_catalog_entry_t entry;
  Race_MapCatalog_InitEntry(&entry);
  bool in_entry = false;

  char token[MAX_STRING_CHARS];
  parser_t parser = Parse_Init(contents, PARSER_ALL_COMMENTS);
  while (Parse_Token(&parser, PARSE_DEFAULT, token, sizeof(token))) {
    if (!q_strcmp(token, "{")) {
      if (in_entry) {
        return false;
      }
      Race_MapCatalog_InitEntry(&entry);
      in_entry = true;
      continue;
    }

    if (!q_strcmp(token, "}")) {
      if (!in_entry) {
        return false;
      }
      in_entry = false;
      if (!*entry.name) {
        continue;
      }
      char canonical[RACE_MAP_IDENTITY_SIZE];
      if (!Race_MapState_CanonicalizeMap(entry.name, canonical) ||
          q_strcmp(entry.name, canonical)) {
        continue;
      }
      if (parsed.count == RACE_MAP_CATALOG_MAX_ENTRIES) {
        return false;
      }
      parsed.entries[parsed.count++] = entry;
      continue;
    }

    if (!in_entry) {
      continue;
    }

    char value[MAX_STRING_CHARS];
    if (!Parse_Token(&parser, PARSE_DEFAULT, value, sizeof(value)) ||
        !q_strcmp(value, "{") || !q_strcmp(value, "}")) {
      return false;
    }

    if (!q_strcmp(token, "name")) {
      if (!Race_MapCatalog_Copy(entry.name, sizeof(entry.name), value)) {
        return false;
      }
    } else if (!q_strcmp(token, "message")) {
      if (!Race_MapCatalog_Copy(entry.title, sizeof(entry.title), value)) {
        return false;
      }
    } else if (!q_strcmp(token, "author")) {
      if (!Race_MapCatalog_Copy(entry.author, sizeof(entry.author), value)) {
        return false;
      }
    } else if (!q_strcmp(token, "description")) {
      if (!Race_MapCatalog_Copy(entry.description,
                                sizeof(entry.description), value)) {
        return false;
      }
    } else if (!q_strcmp(token, "tags")) {
      if (!Race_MapCatalog_Copy(entry.tags, sizeof(entry.tags), value)) {
        return false;
      }
    } else if (!q_strcmp(token, "difficulty")) {
      if (!Race_MapCatalog_ParseInt(value, &entry.difficulty) ||
          entry.difficulty < 1 || entry.difficulty > 10) {
        return false;
      }
    } else if (!q_strcmp(token, "time_limit")) {
      if (!Race_MapCatalog_ParseFloat(value, &entry.time_limit)) {
        return false;
      }
    }
  }

  if (in_entry) {
    return false;
  }
  *catalog = parsed;
  return true;
}

const race_map_catalog_entry_t *Race_MapCatalog_Find(
  const race_map_catalog_t *catalog, const char *name) {
  if (!catalog || !name) {
    return NULL;
  }
  for (size_t i = 0; i < catalog->count; i++) {
    if (!q_strcmp(catalog->entries[i].name, name)) {
      return catalog->entries + i;
    }
  }
  return NULL;
}
