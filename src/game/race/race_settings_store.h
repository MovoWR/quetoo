/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "race_settings.h"

typedef enum {
  RACE_SETTINGS_STORE_OK,
  RACE_SETTINGS_STORE_MISSING,
  RACE_SETTINGS_STORE_CORRUPT,
  RACE_SETTINGS_STORE_IO_ERROR,
  RACE_SETTINGS_STORE_MEMORY_ERROR,
  RACE_SETTINGS_STORE_INVALID_ARGUMENT
} race_settings_store_result_t;

race_settings_store_result_t Race_SettingsStore_Load(
  const char *committed,
  race_setting_scope_t scope, const char *map,
  race_settings_document_t *document,
  race_settings_parse_result_t *parse_result);

race_settings_store_result_t Race_SettingsStore_Commit(
  const char *committed, const char *candidate,
  const race_settings_document_t *document,
  race_settings_parse_result_t *parse_result);

const char *Race_SettingsStore_ResultName(race_settings_store_result_t result);
