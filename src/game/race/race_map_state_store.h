/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "race_map_state.h"

typedef enum {
  RACE_MAP_STATE_STORE_OK,
  RACE_MAP_STATE_STORE_MISSING,
  RACE_MAP_STATE_STORE_CORRUPT,
  RACE_MAP_STATE_STORE_IDENTITY_MISMATCH,
  RACE_MAP_STATE_STORE_IO_ERROR,
  RACE_MAP_STATE_STORE_MEMORY_ERROR,
  RACE_MAP_STATE_STORE_INVALID_ARGUMENT
} race_map_state_store_result_t;

race_map_state_store_result_t Race_MapStateStore_Load(
  const char *committed, const char *expected_map, const char *expected_ruleset,
  race_leaderboard_record_t *records, size_t capacity,
  race_map_state_t *state,
  race_map_state_parse_result_t *parse_result);

race_map_state_store_result_t Race_MapStateStore_Commit(
  const char *committed, const char *candidate,
  const race_map_state_t *state,
  race_map_state_parse_result_t *parse_result);

const char *Race_MapStateStore_ResultName(race_map_state_store_result_t result);
