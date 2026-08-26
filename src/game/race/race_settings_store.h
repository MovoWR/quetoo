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

#define RACE_GSET_MAX_ASSIGNMENTS 128u

typedef struct {
  char name[RACE_SETTING_NAME_SIZE];
  char value[RACE_SETTING_VALUE_SIZE];
} race_gset_assignment_t;

typedef struct {
  size_t count;
  race_gset_assignment_t assignments[RACE_GSET_MAX_ASSIGNMENTS];
} race_gset_document_t;

typedef enum {
  RACE_SETTINGS_STORE_OK,
  RACE_SETTINGS_STORE_MISSING,
  RACE_SETTINGS_STORE_CORRUPT,
  RACE_SETTINGS_STORE_IO_ERROR,
  RACE_SETTINGS_STORE_TOO_LARGE,
  RACE_SETTINGS_STORE_INVALID_ARGUMENT
} race_settings_store_result_t;

void Race_SettingsStore_DocumentInit(race_gset_document_t *document);
const race_gset_assignment_t *Race_SettingsStore_Find(
  const race_gset_document_t *document, const char *name);
bool Race_SettingsStore_Set(const race_gset_document_t *current,
                            const char *name, const char *value,
                            race_gset_document_t *candidate);
bool Race_SettingsStore_Remove(const race_gset_document_t *current,
                               const char *name,
                               race_gset_document_t *candidate);
bool Race_SettingsStore_Equals(const race_gset_document_t *left,
                               const race_gset_document_t *right);

// output_size includes space for the trailing NUL byte. output_length excludes it.
bool Race_SettingsStore_Serialize(const race_gset_document_t *document,
                                  char *output, size_t output_size,
                                  size_t *output_length);
bool Race_SettingsStore_Parse(const void *data, size_t length,
                              race_gset_document_t *document);
race_settings_store_result_t Race_SettingsStore_Load(
  const char *committed, race_gset_document_t *document);
race_settings_store_result_t Race_SettingsStore_Commit(
  const char *committed, const char *candidate,
  const race_gset_document_t *document);

const char *Race_SettingsStore_ResultName(race_settings_store_result_t result);
