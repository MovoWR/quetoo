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

#include "race_leaderboard.h"

#define RACE_MAP_STATE_MAGIC_V1 "QUETOO_RACE_MAP_STATE_V1"
#define RACE_MAP_STATE_MAGIC_V2 "QUETOO_RACE_MAP_STATE_V2"
#define RACE_MAP_STATE_MAGIC_V3 "QUETOO_RACE_MAP_STATE_V3"
#define RACE_MAP_STATE_MAGIC_V4 "QUETOO_RACE_MAP_STATE_V4"
#define RACE_MAP_STATE_MAGIC RACE_MAP_STATE_MAGIC_V1
#define RACE_MAP_STATE_PUBLICATION_V1 "pending-replay"
#define RACE_MAP_STATE_PUBLICATION_V2 "replay-backed"
#define RACE_MAP_STATE_PUBLICATION RACE_MAP_STATE_PUBLICATION_V1
#define RACE_MAP_STATE_ROOT_DIRECTORY "state"

#define RACE_MAP_IDENTITY_MAX 63u
#define RACE_MAP_IDENTITY_SIZE (RACE_MAP_IDENTITY_MAX + 1u)
#define RACE_MAP_IDENTITY_ENCODED_SIZE (RACE_MAP_IDENTITY_MAX * 2u + 1u)
#define RACE_RULESET_ID_MAX 31u
#define RACE_RULESET_ID_SIZE (RACE_RULESET_ID_MAX + 1u)
#define RACE_MAP_STATE_MAX_RECORDS 4096u
#define RACE_MAP_STATE_MAX_FILE_BYTES (6u * 1024u * 1024u)

typedef enum {
  RACE_MAP_STATE_FORMAT_V1 = 1,
  RACE_MAP_STATE_FORMAT_V2 = 2,
  RACE_MAP_STATE_FORMAT_V3 = 3,
  RACE_MAP_STATE_FORMAT_V4 = 4
} race_map_state_format_t;

typedef struct {
  char map[RACE_MAP_IDENTITY_SIZE];
  char ruleset[RACE_RULESET_ID_SIZE];
  race_map_state_format_t format;
  uint64_t generation;
  race_leaderboard_record_t *records;
  size_t record_count;
  size_t record_capacity;
} race_map_state_t;

typedef enum {
  RACE_MAP_STATE_PARSE_OK,
  RACE_MAP_STATE_PARSE_MALFORMED,
  RACE_MAP_STATE_PARSE_UNKNOWN_VERSION,
  RACE_MAP_STATE_PARSE_LEGACY_UNSUPPORTED,
  RACE_MAP_STATE_PARSE_UNSUPPORTED_RULESET,
  RACE_MAP_STATE_PARSE_CHECKSUM,
  RACE_MAP_STATE_PARSE_TOO_LARGE,
  RACE_MAP_STATE_PARSE_BOUNDS
} race_map_state_parse_result_t;

bool Race_MapState_CanonicalizeMap(const char *input,
                                   char output[RACE_MAP_IDENTITY_SIZE]);
bool Race_MapState_EncodeMap(const char *map,
                             char output[RACE_MAP_IDENTITY_ENCODED_SIZE]);
bool Race_MapState_RulesetValid(const char *ruleset);
bool Race_MapState_Paths(const char *ruleset, const char *map,
                          char *committed, size_t committed_size,
                          char *candidate, size_t candidate_size);

bool Race_MapState_Init(race_map_state_t *state,
                        race_leaderboard_record_t *records, size_t capacity,
                        const char *map, const char *ruleset);
bool Race_MapState_Valid(const race_map_state_t *state);
bool Race_MapState_ReplayBacked(const race_map_state_t *state);
bool Race_MapState_CanPublishReplay(const race_map_state_t *state);
bool Race_MapState_Equals(const race_map_state_t *left,
                          const race_map_state_t *right);

bool Race_MapState_EvaluateCandidate(const race_map_state_t *state,
                                     const race_leaderboard_record_t *candidate,
                                     race_leaderboard_evaluation_t *evaluation);
bool Race_MapState_ApplyCandidate(const race_map_state_t *current,
                                  const race_leaderboard_record_t *candidate,
                                  race_map_state_t *next,
                                  race_leaderboard_evaluation_t *evaluation);

bool Race_MapState_Serialize(const race_map_state_t *state,
                             char *output, size_t output_size,
                             size_t *output_length);
race_map_state_parse_result_t Race_MapState_Parse(
  const void *data, size_t length,
  race_leaderboard_record_t *records, size_t capacity,
  race_map_state_t *state);
const char *Race_MapState_ParseResultName(race_map_state_parse_result_t result);
