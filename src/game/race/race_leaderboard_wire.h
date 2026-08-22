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

#include "shared/shared.h"
#include "race_leaderboard.h"

#define RACE_LEADERBOARD_CONFIG_VERSION "v1"
#define RACE_LEADERBOARD_MAX_NAME_BYTES 31u
#define RACE_LEADERBOARD_CONFIG_MAX_BYTES \
  (6u + RACE_LEADERBOARD_TOP_MAX * \
    (3u + RACE_LEADERBOARD_MAX_NAME_BYTES + 10u + 20u))

typedef struct {
  char name[RACE_LEADERBOARD_MAX_NAME_BYTES + 1u];
  uint32_t time_ms;
  uint64_t date_unix_s;
} race_leaderboard_wire_entry_t;

bool Race_LeaderboardWire_Encode(
  const race_leaderboard_wire_entry_t *entries, size_t count,
  char *output, size_t output_size);
bool Race_LeaderboardWire_Decode(
  const char *source, race_leaderboard_wire_entry_t *entries,
  size_t capacity, size_t *count);
