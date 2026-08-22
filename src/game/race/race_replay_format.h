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
#include "race_map_state.h"
#include "race_training.h"

#define RACE_REPLAY_MAGIC "QRPL"
#define RACE_REPLAY_FORMAT_VERSION 1u
#define RACE_REPLAY_HEADER_BYTES 64u
#define RACE_REPLAY_FRAME_SCHEMA 1u
#define RACE_REPLAY_PARAMS_BYTES 94u
#define RACE_REPLAY_FRAME_BYTES 332u
#define RACE_REPLAY_PROJECTILE_EVENT_BYTES 44u
#define RACE_REPLAY_TICK_RATE 40u
#define RACE_REPLAY_TICK_MSEC (1000u / RACE_REPLAY_TICK_RATE)
#define RACE_REPLAY_MAX_TIME_MS 600000u
#define RACE_REPLAY_MAX_FRAMES 24000u
#define RACE_REPLAY_MAX_SAMPLES RACE_REPLAY_MAX_FRAMES
#define RACE_REPLAY_MAX_PROJECTILE_EVENTS (RACE_REPLAY_MAX_FRAMES * 2u)
#define RACE_REPLAY_MAX_ACTIVE_PROJECTILES 256u
#define RACE_REPLAY_MAX_PHYSICS_MODE 1u
#define RACE_REPLAY_FLAG_DEFLATE (1u << 0)
#define RACE_REPLAY_KNOWN_FLAGS RACE_REPLAY_FLAG_DEFLATE
#define RACE_REPLAY_MAX_FILE_BYTES (16u * 1024u * 1024u)
#define RACE_REPLAY_PLAYER_NAME_MAX 31u
#define RACE_REPLAY_PLAYER_NAME_SIZE (RACE_REPLAY_PLAYER_NAME_MAX + 1u)
#define RACE_REPLAY_ID_TEXT_LENGTH 16u
#define RACE_REPLAY_ID_TEXT_SIZE (RACE_REPLAY_ID_TEXT_LENGTH + 1u)
#define RACE_REPLAY_ROOT_DIRECTORY "replays"

/** @brief QRPL v1 strafe-helper snapshot. */
typedef race_strafe_sample_t race_replay_strafe_helper_t;

/**
 * @brief One full QRPL v1 frame plus its derived absolute playback time.
 * @details time_ms is reconstructed from delta_time_ms and is not serialized.
 */
typedef struct {
  uint32_t time_ms;
  uint16_t delta_time_ms;
  pm_state_t pm_state;
  int16_t stats[MAX_STATS];
  int16_t inventory[MAX_INVENTORY];
  race_replay_strafe_helper_t strafe_helper;
} race_replay_sample_t;

typedef race_replay_sample_t race_replay_frame_t;

typedef enum {
  RACE_REPLAY_PROJECTILE_ROCKET = 1,
  RACE_REPLAY_PROJECTILE_HYPERBLASTER,
  RACE_REPLAY_PROJECTILE_KIND_TOTAL
} race_replay_projectile_kind_t;

typedef enum {
  RACE_REPLAY_PROJECTILE_SPAWN = 1,
  RACE_REPLAY_PROJECTILE_IMPACT,
  RACE_REPLAY_PROJECTILE_SILENT_DESPAWN,
  RACE_REPLAY_PROJECTILE_OPERATION_TOTAL
} race_replay_projectile_operation_t;

/** @brief One fixed-size QRPL v1 projectile presentation record. */
typedef struct {
  uint32_t time_ms;
  uint16_t id;
  race_replay_projectile_kind_t kind;
  race_replay_projectile_operation_t operation;
  vec3_t origin;
  vec3_t velocity;
  vec3_t normal;
} race_replay_projectile_event_t;

typedef struct {
  char map[RACE_MAP_IDENTITY_SIZE];
  char player_name[RACE_REPLAY_PLAYER_NAME_SIZE];
  /** Runtime-only current profile identity; QRPL v1 does not serialize it. */
  char profile_uid[RACE_PROFILE_UID_SIZE];
  int32_t player_uid;
  race_mode_t mode;
  uint8_t physics_mode;
  uint32_t elapsed_time;
  race_replay_sample_t *samples;
  size_t sample_count;
  size_t sample_capacity;
  race_replay_projectile_event_t *projectile_events;
  size_t projectile_event_count;
  size_t projectile_event_capacity;
  uint64_t replay_id;
} race_replay_t;

typedef enum {
  RACE_REPLAY_PARSE_OK,
  RACE_REPLAY_PARSE_MALFORMED,
  RACE_REPLAY_PARSE_UNKNOWN_VERSION,
  RACE_REPLAY_PARSE_UNSUPPORTED_SCHEMA,
  RACE_REPLAY_PARSE_UNSUPPORTED_RULESET,
  RACE_REPLAY_PARSE_CHECKSUM,
  RACE_REPLAY_PARSE_TOO_LARGE,
  RACE_REPLAY_PARSE_BOUNDS,
  RACE_REPLAY_PARSE_IDENTITY_MISMATCH
} race_replay_parse_result_t;

bool Race_Replay_ProfilePlayerUid(const char *profile_uid,
                                  int32_t *player_uid);
bool Race_Replay_SampleValid(const race_replay_sample_t *sample);
bool Race_Replay_ProjectileEventValid(
  const race_replay_projectile_event_t *event);
bool Race_Replay_Init(race_replay_t *replay,
                      race_replay_sample_t *samples, size_t capacity,
                      race_replay_projectile_event_t *projectile_events,
                      size_t projectile_event_capacity,
                      const char *map, const char *profile_uid,
                      const char *player_name, int32_t player_uid,
                      uint8_t physics_mode);
bool Race_Replay_Valid(const race_replay_t *replay);
bool Race_Replay_Equals(const race_replay_t *left,
                        const race_replay_t *right);
bool Race_Replay_Paths(const char *ruleset, const char *map,
                       uint64_t replay_id,
                       char *committed, size_t committed_size,
                       char *candidate, size_t candidate_size);
bool Race_Replay_IdString(uint64_t replay_id,
                          char output[RACE_REPLAY_ID_TEXT_SIZE]);

bool Race_Replay_Serialize(const race_replay_t *replay,
                           void *output, size_t output_size,
                           size_t *output_length, uint64_t *replay_id);
race_replay_parse_result_t Race_Replay_Parse(
  const void *data, size_t length,
  race_replay_sample_t *samples, size_t capacity,
  race_replay_projectile_event_t *projectile_events,
  size_t projectile_event_capacity,
  race_replay_t *replay);
const char *Race_Replay_ParseResultName(race_replay_parse_result_t result);
