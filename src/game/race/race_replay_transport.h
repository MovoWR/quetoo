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

#include "race_replay_format.h"

#define RACE_REPLAY_TRANSPORT_VERSION 1u
#define RACE_REPLAY_TRANSPORT_MAX_PAYLOAD 255u
#define RACE_REPLAY_STATE_MAX_SAMPLES 6u
#define RACE_REPLAY_RACELINE_MAX_POINTS 512u
#define RACE_REPLAY_RACELINE_CHUNK_MAX_POINTS 13u
#define RACE_REPLAY_PROJECTILE_MAX_MESSAGE_EVENTS 5u
#define RACE_REPLAY_DISPLAY_NAME_MAX 31u
#define RACE_REPLAY_DISPLAY_NAME_SIZE (RACE_REPLAY_DISPLAY_NAME_MAX + 1u)

#define RACE_REPLAY_STATE_ACTIVE        (1u << 0)
#define RACE_REPLAY_STATE_PAUSED        (1u << 1)
#define RACE_REPLAY_STATE_COMPLETED     (1u << 2)
#define RACE_REPLAY_STATE_DISCONTINUITY (1u << 3)
#define RACE_REPLAY_STATE_FLAGS_MASK    \
  (RACE_REPLAY_STATE_ACTIVE | RACE_REPLAY_STATE_PAUSED | \
   RACE_REPLAY_STATE_COMPLETED | RACE_REPLAY_STATE_DISCONTINUITY)

typedef enum {
  RACE_REPLAY_SPEED_QUARTER,
  RACE_REPLAY_SPEED_HALF,
  RACE_REPLAY_SPEED_NORMAL,
  RACE_REPLAY_SPEED_DOUBLE,
  RACE_REPLAY_SPEED_QUADRUPLE,
  RACE_REPLAY_SPEED_TOTAL
} race_replay_speed_t;

typedef enum {
  RACE_REPLAY_SOURCE_NONE,
  RACE_REPLAY_SOURCE_PERSONAL_BEST,
  RACE_REPLAY_SOURCE_WORLD_RECORD,
  RACE_REPLAY_SOURCE_ID,
  RACE_REPLAY_SOURCE_TOTAL
} race_replay_source_t;

typedef struct {
  uint32_t time_ms;
  vec3_t origin;
  vec3_t view_angles;
} race_replay_pose_sample_t;

typedef struct {
  uint8_t flags;
  race_replay_speed_t speed;
  race_replay_source_t source;
  uint32_t generation;
  uint32_t sequence;
  uint64_t replay_id;
  uint32_t duration_ms;
  uint32_t playhead_ms;
  uint8_t rank;
  char display_name[RACE_REPLAY_DISPLAY_NAME_SIZE];
  race_replay_pose_sample_t samples[RACE_REPLAY_STATE_MAX_SAMPLES];
  size_t sample_count;
} race_replay_state_message_t;

/**
 * @brief Full QRPL v1 training telemetry accompanying one replay state update.
 */
typedef struct {
  uint32_t generation;
  uint32_t sequence;
  uint32_t playhead_ms;
  uint32_t frame_cursor;
  pm_type_t pm_type;
  uint16_t pm_flags;
  vec3_t origin;
  vec3_t velocity;
  int16_t input_flags;
  race_strafe_sample_t strafe_helper;
} race_replay_telemetry_message_t;

typedef enum {
  RACE_REPLAY_PROJECTILE_MESSAGE_RESET = 1,
  RACE_REPLAY_PROJECTILE_MESSAGE_SNAPSHOT,
  RACE_REPLAY_PROJECTILE_MESSAGE_EVENTS
} race_replay_projectile_message_op_t;

typedef struct {
  race_replay_projectile_message_op_t op;
  uint32_t generation;
  uint32_t sequence;
  uint32_t playhead_ms;
  uint8_t event_count;
  race_replay_projectile_event_t
    events[RACE_REPLAY_PROJECTILE_MAX_MESSAGE_EVENTS];
} race_replay_projectile_message_t;

typedef enum {
  RACE_RACELINE_MESSAGE_BEGIN = 1,
  RACE_RACELINE_MESSAGE_CHUNK,
  RACE_RACELINE_MESSAGE_END,
  RACE_RACELINE_MESSAGE_CLEAR
} race_raceline_message_op_t;

typedef struct {
  uint32_t time_ms;
  vec3_t origin;
} race_raceline_point_t;

typedef struct {
  race_raceline_message_op_t op;
  race_replay_source_t source;
  uint8_t rank;
  uint32_t generation;
  uint32_t sequence;
  uint64_t replay_id;
  uint16_t total_points;
  uint16_t first_point;
  uint8_t point_count;
  uint32_t duration_ms;
  race_raceline_point_t points[RACE_REPLAY_RACELINE_CHUNK_MAX_POINTS];
} race_raceline_message_t;

typedef struct {
  uint32_t generation;
  uint32_t state_sequence;
  uint32_t telemetry_sequence;
  uint32_t projectile_sequence;
  uint32_t raceline_sequence;
  race_replay_state_message_t state;
  race_replay_telemetry_message_t telemetry;
  uint32_t state_received_time;
  race_replay_projectile_event_t
    projectiles[RACE_REPLAY_MAX_ACTIVE_PROJECTILES];
  uint16_t projectile_count;
  race_replay_source_t raceline_source;
  uint8_t raceline_rank;
  uint64_t raceline_replay_id;
  uint32_t raceline_duration_ms;
  uint16_t raceline_total_points;
  uint16_t raceline_received_points;
  race_raceline_point_t raceline_points[RACE_REPLAY_RACELINE_MAX_POINTS];
  bool raceline_receiving;
  bool raceline_complete;
  bool telemetry_valid;
} race_replay_client_cache_t;

typedef enum {
  RACE_REPLAY_TRANSPORT_APPLIED,
  RACE_REPLAY_TRANSPORT_STALE,
  RACE_REPLAY_TRANSPORT_MALFORMED
} race_replay_transport_result_t;

bool Race_ReplaySpeed_Ratio(race_replay_speed_t speed,
                            uint32_t *numerator, uint32_t *denominator);
bool Race_ReplayGeneration_Newer(uint32_t candidate, uint32_t current);

size_t Race_ReplayState_Encode(const race_replay_state_message_t *message,
                               void *output, size_t capacity);
bool Race_ReplayState_Decode(const void *data, size_t length,
                             race_replay_state_message_t *message);
size_t Race_ReplayTelemetry_Encode(
  const race_replay_telemetry_message_t *message,
  void *output, size_t capacity);
bool Race_ReplayTelemetry_Decode(
  const void *data, size_t length,
  race_replay_telemetry_message_t *message);
size_t Race_ReplayProjectiles_Encode(
  const race_replay_projectile_message_t *message,
  void *output, size_t capacity);
bool Race_ReplayProjectiles_Decode(
  const void *data, size_t length,
  race_replay_projectile_message_t *message);
size_t Race_Raceline_Encode(const race_raceline_message_t *message,
                            void *output, size_t capacity);
bool Race_Raceline_Decode(const void *data, size_t length,
                          race_raceline_message_t *message);

void Race_ReplayClientCache_Clear(race_replay_client_cache_t *cache);
race_replay_transport_result_t Race_ReplayClientCache_ApplyState(
  race_replay_client_cache_t *cache,
  const void *data, size_t length, uint32_t received_time);
race_replay_transport_result_t Race_ReplayClientCache_ApplyTelemetry(
  race_replay_client_cache_t *cache,
  const void *data, size_t length);
race_replay_transport_result_t Race_ReplayClientCache_ApplyProjectiles(
  race_replay_client_cache_t *cache,
  const void *data, size_t length);
race_replay_transport_result_t Race_ReplayClientCache_ApplyRaceline(
  race_replay_client_cache_t *cache, const void *data, size_t length);

uint32_t Race_ReplayState_PresentationTime(
  const race_replay_state_message_t *state,
  uint32_t received_time, uint32_t now);
bool Race_ReplayState_Interpolate(
  const race_replay_state_message_t *state, uint32_t time_ms,
  race_replay_pose_sample_t *pose);

size_t Race_ReplayRaceline_BuildWindow(
  const race_raceline_point_t *points, size_t count, uint32_t head_ms,
  uint32_t trail_ms, bool full, vec3_t *output, size_t capacity);
