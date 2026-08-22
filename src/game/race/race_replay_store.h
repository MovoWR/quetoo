/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "race_replay_format.h"

#define RACE_REPLAY_STORE_PATH_SIZE 4096u

typedef enum {
  RACE_REPLAY_STORE_OK,
  RACE_REPLAY_STORE_NOT_FOUND,
  RACE_REPLAY_STORE_CORRUPT,
  RACE_REPLAY_STORE_COLLISION,
  RACE_REPLAY_STORE_IO_ERROR,
  RACE_REPLAY_STORE_MEMORY_ERROR,
  RACE_REPLAY_STORE_INVALID_ARGUMENT
} race_replay_store_result_t;

race_replay_store_result_t Race_ReplayStore_Load(
  const char *committed, const char *expected_map, uint64_t expected_id,
  race_replay_sample_t *samples, size_t capacity,
  race_replay_projectile_event_t *projectile_events,
  size_t projectile_event_capacity,
  race_replay_t *replay, race_replay_parse_result_t *parse_result);

race_replay_store_result_t Race_ReplayStore_Commit(
  const char *directory, const race_replay_t *replay,
  uint64_t *replay_id, bool *newly_created,
  race_replay_parse_result_t *parse_result);

race_replay_store_result_t Race_ReplayStore_Remove(const char *committed);
const char *Race_ReplayStore_ResultName(race_replay_store_result_t result);
