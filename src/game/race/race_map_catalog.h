/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "race_map_state.h"

#define RACE_MAP_CATALOG_MAX_ENTRIES 256u

typedef struct {
  char name[RACE_MAP_IDENTITY_SIZE];
  char title[64];
  char author[96];
  char description[256];
  char tags[128];
  int32_t difficulty;
  float time_limit;
} race_map_catalog_entry_t;

typedef struct race_map_catalog_s {
  race_map_catalog_entry_t entries[RACE_MAP_CATALOG_MAX_ENTRIES];
  size_t count;
} race_map_catalog_t;

bool Race_MapCatalog_Parse(const char *contents, race_map_catalog_t *catalog);
const race_map_catalog_entry_t *Race_MapCatalog_Find(
  const race_map_catalog_t *catalog, const char *name);
