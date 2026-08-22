/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_settings_store.h"

#include <stdlib.h>
#include <string.h>

#include "race_persistence.h"

static race_settings_store_result_t Race_SettingsStore_Parse(
  const void *serialized, size_t serialized_length,
  race_setting_scope_t scope, const char *map,
  race_settings_document_t *document,
  race_settings_parse_result_t *parse_result) {
  const race_settings_parse_result_t parsed = Race_Settings_Parse(
    serialized, serialized_length, scope, map, document);
  if (parse_result) {
    *parse_result = parsed;
  }
  return parsed == RACE_SETTINGS_PARSE_OK
    ? RACE_SETTINGS_STORE_OK
    : RACE_SETTINGS_STORE_CORRUPT;
}

race_settings_store_result_t Race_SettingsStore_Load(
  const char *committed,
  race_setting_scope_t scope, const char *map,
  race_settings_document_t *document,
  race_settings_parse_result_t *parse_result) {
  if (parse_result) {
    *parse_result = RACE_SETTINGS_PARSE_OK;
  }

  race_settings_document_t empty;
  if (!committed || !*committed || !document ||
      !Race_Settings_DocumentInit(&empty, scope, map, 0)) {
    return RACE_SETTINGS_STORE_INVALID_ARGUMENT;
  }

  char *serialized = malloc(RACE_SETTINGS_MAX_FILE_BYTES + 1u);
  if (!serialized) {
    return RACE_SETTINGS_STORE_MEMORY_ERROR;
  }

  size_t serialized_length;
  const race_persistence_result_t read = Race_Persistence_Read(
    committed, serialized, RACE_SETTINGS_MAX_FILE_BYTES, &serialized_length);
  if (read == RACE_PERSISTENCE_NOT_FOUND) {
    *document = empty;
    free(serialized);
    return RACE_SETTINGS_STORE_MISSING;
  }
  if (read != RACE_PERSISTENCE_OK) {
    if (read == RACE_PERSISTENCE_TOO_LARGE && parse_result) {
      *parse_result = RACE_SETTINGS_PARSE_TOO_LARGE;
    }
    free(serialized);
    return read == RACE_PERSISTENCE_TOO_LARGE
      ? RACE_SETTINGS_STORE_CORRUPT
      : RACE_SETTINGS_STORE_IO_ERROR;
  }

  race_settings_document_t parsed_document;
  const race_settings_store_result_t result = Race_SettingsStore_Parse(
    serialized, serialized_length, scope, map,
    &parsed_document, parse_result);
  if (result == RACE_SETTINGS_STORE_OK) {
    *document = parsed_document;
  }

  free(serialized);
  return result;
}

race_settings_store_result_t Race_SettingsStore_Commit(
  const char *committed, const char *candidate,
  const race_settings_document_t *document,
  race_settings_parse_result_t *parse_result) {
  if (parse_result) {
    *parse_result = RACE_SETTINGS_PARSE_OK;
  }
  if (!committed || !*committed || !candidate || !*candidate ||
      !strcmp(committed, candidate) ||
      !Race_Settings_DocumentValid(document, true)) {
    return RACE_SETTINGS_STORE_INVALID_ARGUMENT;
  }

  char *serialized = malloc(RACE_SETTINGS_MAX_FILE_BYTES + 1u);
  if (!serialized) {
    return RACE_SETTINGS_STORE_MEMORY_ERROR;
  }

  size_t serialized_length;
  if (!Race_Settings_Serialize(document, serialized,
                               RACE_SETTINGS_MAX_FILE_BYTES + 1u,
                               &serialized_length)) {
    free(serialized);
    return RACE_SETTINGS_STORE_INVALID_ARGUMENT;
  }

  race_persistence_result_t persisted = Race_Persistence_WriteCandidate(
    candidate, serialized, serialized_length);
  if (persisted != RACE_PERSISTENCE_OK) {
    free(serialized);
    return RACE_SETTINGS_STORE_IO_ERROR;
  }

  size_t verified_length;
  persisted = Race_Persistence_Read(candidate, serialized,
                                    RACE_SETTINGS_MAX_FILE_BYTES,
                                    &verified_length);
  if (persisted != RACE_PERSISTENCE_OK) {
    free(serialized);
    return RACE_SETTINGS_STORE_IO_ERROR;
  }

  race_settings_document_t verified;
  const char *map = document->scope == RACE_SETTING_SCOPE_MAP
    ? document->map
    : NULL;
  race_settings_store_result_t result = Race_SettingsStore_Parse(
    serialized, verified_length, document->scope, map,
    &verified, parse_result);
  if (result != RACE_SETTINGS_STORE_OK ||
      !Race_Settings_DocumentEquals(document, &verified)) {
    free(serialized);
    return result == RACE_SETTINGS_STORE_OK
      ? RACE_SETTINGS_STORE_CORRUPT
      : result;
  }

  persisted = Race_Persistence_Promote(candidate, committed);
  free(serialized);
  return persisted == RACE_PERSISTENCE_OK
    ? RACE_SETTINGS_STORE_OK
    : RACE_SETTINGS_STORE_IO_ERROR;
}

const char *Race_SettingsStore_ResultName(race_settings_store_result_t result) {
  switch (result) {
    case RACE_SETTINGS_STORE_OK:
      return "ok";
    case RACE_SETTINGS_STORE_MISSING:
      return "missing";
    case RACE_SETTINGS_STORE_CORRUPT:
      return "corrupt";
    case RACE_SETTINGS_STORE_IO_ERROR:
      return "I/O error";
    case RACE_SETTINGS_STORE_MEMORY_ERROR:
      return "memory error";
    case RACE_SETTINGS_STORE_INVALID_ARGUMENT:
      return "invalid argument";
  }
  return "unknown";
}
