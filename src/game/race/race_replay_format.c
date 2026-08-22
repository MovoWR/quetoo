/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_replay_format.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

enum {
  RACE_REPLAY_OFFSET_VERSION = 4,
  RACE_REPLAY_OFFSET_HEADER_BYTES = 6,
  RACE_REPLAY_OFFSET_FLAGS = 8,
  RACE_REPLAY_OFFSET_TICK_RATE = 12,
  RACE_REPLAY_OFFSET_MODE = 14,
  RACE_REPLAY_OFFSET_PHYSICS_MODE = 15,
  RACE_REPLAY_OFFSET_ELAPSED = 16,
  RACE_REPLAY_OFFSET_FRAME_COUNT = 20,
  RACE_REPLAY_OFFSET_MAP_LENGTH = 24,
  RACE_REPLAY_OFFSET_PLAYER_NAME_LENGTH = 26,
  RACE_REPLAY_OFFSET_RAW_PAYLOAD_LENGTH = 28,
  RACE_REPLAY_OFFSET_STORED_PAYLOAD_LENGTH = 32,
  RACE_REPLAY_OFFSET_RAW_PAYLOAD_CRC = 36,
  RACE_REPLAY_OFFSET_METADATA_CRC = 40,
  RACE_REPLAY_OFFSET_FRAME_SCHEMA = 44,
  RACE_REPLAY_OFFSET_PARAMS_COUNT = 46,
  RACE_REPLAY_OFFSET_PLAYER_UID = 48,
  RACE_REPLAY_OFFSET_REPLAY_ID = 52,
  RACE_REPLAY_OFFSET_RESERVED = 60,
  RACE_REPLAY_STREAM_CHUNK_BYTES = 64u * 1024u
};

typedef struct {
  uint32_t flags;
  uint16_t tick_rate;
  uint8_t mode;
  uint8_t physics_mode;
  uint32_t elapsed_time;
  uint32_t frame_count;
  uint16_t map_length;
  uint16_t player_name_length;
  uint32_t raw_payload_length;
  uint32_t stored_payload_length;
  uint32_t raw_payload_crc;
  uint32_t metadata_crc;
  uint16_t params_count;
  int32_t player_uid;
  uint64_t replay_id;
  uint32_t projectile_event_count;
} race_replay_metadata_t;

_Static_assert(sizeof(float) == 4, "QRPL v1 requires binary32 floats");
_Static_assert(MAX_STATS == 32, "QRPL v1 requires exactly 32 stats");
_Static_assert(MAX_INVENTORY == 64,
               "QRPL v1 requires exactly 64 inventory slots");

static bool Race_Replay_CheckedAdd(size_t a, size_t b, size_t *result) {
  if (!result || a > SIZE_MAX - b) {
    return false;
  }
  *result = a + b;
  return true;
}

static bool Race_Replay_CheckedMultiply(size_t a, size_t b,
                                        size_t *result) {
  if (!result || (a && b > SIZE_MAX / a)) {
    return false;
  }
  *result = a * b;
  return true;
}

static void Race_Replay_WriteU16(uint8_t *output, uint16_t value) {
  output[0] = (uint8_t) value;
  output[1] = (uint8_t) (value >> 8u);
}

static void Race_Replay_WriteI16(uint8_t *output, int16_t value) {
  Race_Replay_WriteU16(output, (uint16_t) value);
}

static void Race_Replay_WriteU32(uint8_t *output, uint32_t value) {
  output[0] = (uint8_t) value;
  output[1] = (uint8_t) (value >> 8u);
  output[2] = (uint8_t) (value >> 16u);
  output[3] = (uint8_t) (value >> 24u);
}

static void Race_Replay_WriteI32(uint8_t *output, int32_t value) {
  Race_Replay_WriteU32(output, (uint32_t) value);
}

static void Race_Replay_WriteU64(uint8_t *output, uint64_t value) {
  Race_Replay_WriteU32(output, (uint32_t) value);
  Race_Replay_WriteU32(output + 4u, (uint32_t) (value >> 32u));
}

static uint16_t Race_Replay_ReadU16(const uint8_t *input) {
  return (uint16_t) input[0] | (uint16_t) ((uint16_t) input[1] << 8u);
}

static int16_t Race_Replay_ReadI16(const uint8_t *input) {
  return (int16_t) Race_Replay_ReadU16(input);
}

static uint32_t Race_Replay_ReadU32(const uint8_t *input) {
  return (uint32_t) input[0] |
         ((uint32_t) input[1] << 8u) |
         ((uint32_t) input[2] << 16u) |
         ((uint32_t) input[3] << 24u);
}

static int32_t Race_Replay_ReadI32(const uint8_t *input) {
  return (int32_t) Race_Replay_ReadU32(input);
}

static uint64_t Race_Replay_ReadU64(const uint8_t *input) {
  return (uint64_t) Race_Replay_ReadU32(input) |
         ((uint64_t) Race_Replay_ReadU32(input + 4u) << 32u);
}

static void Race_Replay_WriteFloat(uint8_t *output, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  Race_Replay_WriteU32(output, bits);
}

static float Race_Replay_ReadFloat(const uint8_t *input) {
  const uint32_t bits = Race_Replay_ReadU32(input);
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

static void Race_Replay_WriteVec3(uint8_t *output, vec3_t value) {
  Race_Replay_WriteFloat(output, value.x);
  Race_Replay_WriteFloat(output + 4u, value.y);
  Race_Replay_WriteFloat(output + 8u, value.z);
}

static vec3_t Race_Replay_ReadVec3(const uint8_t *input) {
  return Vec3(Race_Replay_ReadFloat(input),
              Race_Replay_ReadFloat(input + 4u),
              Race_Replay_ReadFloat(input + 8u));
}

static uint32_t Race_Replay_Crc32(const void *data, size_t length) {
  return (uint32_t) mz_crc32(MZ_CRC32_INIT, data, length);
}

static bool Race_Replay_NameSafe(const char *name, size_t length) {
  if (!name || !length) {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    if (!name[i] || name[i] == '\r' || name[i] == '\n') {
      return false;
    }
  }
  return true;
}

static bool Race_Replay_Vec3Finite(vec3_t value) {
  return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool Race_Replay_ParamsValid(const pm_params_t *params) {
  if (!params) {
    return false;
  }
  const float values[] = {
    params->gravity_water,
    params->accel_ground,
    params->accel_ground_slick,
    params->accel_air,
    params->accel_water,
    params->accel_spectator,
    params->accel_ladder,
    params->friction_ground,
    params->friction_ground_slick,
    params->friction_air,
    params->friction_water,
    params->friction_spectator,
    params->friction_ladder,
    params->speed_ground,
    params->speed_air,
    params->speed_water,
    params->speed_ladder,
    params->speed_spectator,
    params->speed_stop,
    params->speed_jump,
    params->speed_ducked,
    params->speed_duck_stand,
    params->speed_water_jump
  };
  for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++) {
    if (!isfinite(values[i])) {
      return false;
    }
  }
  return true;
}

bool Race_Replay_SampleValid(const race_replay_sample_t *sample) {
  if (!sample || sample->pm_state.type < PM_NORMAL ||
      sample->pm_state.type > PM_FREEZE ||
      !Race_Replay_ParamsValid(&sample->pm_state.params) ||
      !Race_Replay_Vec3Finite(sample->pm_state.origin) ||
      !Race_Replay_Vec3Finite(sample->pm_state.velocity) ||
      !Race_Replay_Vec3Finite(sample->pm_state.view_offset) ||
      !isfinite(sample->pm_state.step_offset) ||
      !Race_Replay_Vec3Finite(sample->pm_state.view_angles) ||
      !Race_Replay_Vec3Finite(sample->pm_state.delta_angles) ||
      !Race_Replay_Vec3Finite(sample->pm_state.hook_position) ||
      !Race_Replay_Vec3Finite(sample->strafe_helper.forward) ||
      !Race_Replay_Vec3Finite(sample->strafe_helper.velocity) ||
      !Race_Replay_Vec3Finite(sample->strafe_helper.wishdir) ||
      !isfinite(sample->strafe_helper.wishspeed) ||
      !isfinite(sample->strafe_helper.accel) ||
      !isfinite(sample->strafe_helper.frametime) ||
      !isfinite(sample->strafe_helper.view_yaw)) {
    return false;
  }
  return true;
}

bool Race_Replay_ProjectileEventValid(
    const race_replay_projectile_event_t *event) {
  return event && event->id &&
         event->kind >= RACE_REPLAY_PROJECTILE_ROCKET &&
         event->kind < RACE_REPLAY_PROJECTILE_KIND_TOTAL &&
         event->operation >= RACE_REPLAY_PROJECTILE_SPAWN &&
         event->operation < RACE_REPLAY_PROJECTILE_OPERATION_TOTAL &&
         Race_Replay_Vec3Finite(event->origin) &&
         Race_Replay_Vec3Finite(event->velocity) &&
         Race_Replay_Vec3Finite(event->normal);
}

static bool Race_Replay_ProjectileEventsValid(
    const race_replay_projectile_event_t *events, const size_t count,
    const size_t capacity, const uint32_t elapsed_time) {
  if (count > capacity || capacity > RACE_REPLAY_MAX_PROJECTILE_EVENTS ||
      (count && !events)) {
    return false;
  }

  struct {
    uint16_t id;
    race_replay_projectile_kind_t kind;
  } active[RACE_REPLAY_MAX_ACTIVE_PROJECTILES];
  size_t active_count = 0u;
  uint32_t previous_time = 0u;
  for (size_t i = 0u; i < count; i++) {
    const race_replay_projectile_event_t *event = events + i;
    if (!Race_Replay_ProjectileEventValid(event) ||
        event->time_ms > elapsed_time ||
        (i && event->time_ms < previous_time)) {
      return false;
    }
    previous_time = event->time_ms;

    size_t active_index = active_count;
    for (size_t j = 0u; j < active_count; j++) {
      if (active[j].id == event->id) {
        active_index = j;
        break;
      }
    }
    if (event->operation == RACE_REPLAY_PROJECTILE_SPAWN) {
      if (active_index != active_count ||
          active_count == RACE_REPLAY_MAX_ACTIVE_PROJECTILES) {
        return false;
      }
      active[active_count].id = event->id;
      active[active_count].kind = event->kind;
      active_count++;
    } else {
      if (active_index == active_count ||
          active[active_index].kind != event->kind) {
        return false;
      }
      active[active_index] = active[--active_count];
    }
  }
  return true;
}

bool Race_Replay_ProfilePlayerUid(const char *profile_uid,
                                  int32_t *player_uid) {
  char canonical[RACE_PROFILE_UID_SIZE];
  if (!player_uid ||
      !Race_Profile_CanonicalizeUid(profile_uid, canonical) ||
      strcmp(profile_uid, canonical)) {
    return false;
  }
  *player_uid = (int32_t) Race_Replay_Crc32(canonical, strlen(canonical));
  return true;
}

bool Race_Replay_Init(race_replay_t *replay,
                      race_replay_sample_t *samples, size_t capacity,
                      race_replay_projectile_event_t *projectile_events,
                      size_t projectile_event_capacity,
                      const char *map, const char *profile_uid,
                      const char *player_name, int32_t player_uid,
                      uint8_t physics_mode) {
  if (!replay || (!samples && capacity) ||
      (!projectile_events && projectile_event_capacity) ||
      capacity > RACE_REPLAY_MAX_FRAMES ||
      projectile_event_capacity > RACE_REPLAY_MAX_PROJECTILE_EVENTS ||
      physics_mode > RACE_REPLAY_MAX_PHYSICS_MODE) {
    return false;
  }

  char canonical_map[RACE_MAP_IDENTITY_SIZE];
  char canonical_uid[RACE_PROFILE_UID_SIZE] = { 0 };
  const size_t player_name_length = player_name ? strlen(player_name) : 0u;
  if (!Race_MapState_CanonicalizeMap(map, canonical_map) ||
      !Race_Replay_NameSafe(player_name, player_name_length) ||
      player_name_length > RACE_REPLAY_PLAYER_NAME_MAX ||
      (profile_uid &&
       (!Race_Profile_CanonicalizeUid(profile_uid, canonical_uid) ||
        strcmp(profile_uid, canonical_uid)))) {
    return false;
  }

  memset(replay, 0, sizeof(*replay));
  memcpy(replay->map, canonical_map, strlen(canonical_map) + 1u);
  memcpy(replay->player_name, player_name, player_name_length + 1u);
  if (profile_uid) {
    memcpy(replay->profile_uid, canonical_uid, sizeof(replay->profile_uid));
  }
  replay->player_uid = player_uid;
  replay->mode = RACE_MODE_RACE;
  replay->physics_mode = physics_mode;
  replay->samples = samples;
  replay->sample_capacity = capacity;
  replay->projectile_events = projectile_events;
  replay->projectile_event_capacity = projectile_event_capacity;
  if (samples && capacity) {
    memset(samples, 0, capacity * sizeof(*samples));
  }
  if (projectile_events && projectile_event_capacity) {
    memset(projectile_events, 0,
           projectile_event_capacity * sizeof(*projectile_events));
  }
  return true;
}

bool Race_Replay_Valid(const race_replay_t *replay) {
  if (!replay || !replay->samples || !replay->sample_count ||
      replay->sample_count > replay->sample_capacity ||
      replay->sample_capacity > RACE_REPLAY_MAX_FRAMES ||
      replay->projectile_event_count > replay->projectile_event_capacity ||
      replay->projectile_event_capacity > RACE_REPLAY_MAX_PROJECTILE_EVENTS ||
      (replay->projectile_event_count && !replay->projectile_events) ||
      !replay->elapsed_time ||
      replay->elapsed_time > RACE_REPLAY_MAX_TIME_MS ||
      replay->mode != RACE_MODE_RACE ||
      replay->physics_mode > RACE_REPLAY_MAX_PHYSICS_MODE) {
    return false;
  }

  char canonical_map[RACE_MAP_IDENTITY_SIZE];
  char canonical_uid[RACE_PROFILE_UID_SIZE];
  const size_t player_name_length = strlen(replay->player_name);
  if (!Race_MapState_CanonicalizeMap(replay->map, canonical_map) ||
      strcmp(replay->map, canonical_map) ||
      !Race_Replay_NameSafe(replay->player_name, player_name_length) ||
      player_name_length > RACE_REPLAY_PLAYER_NAME_MAX ||
      (replay->profile_uid[0] &&
       (!Race_Profile_CanonicalizeUid(replay->profile_uid, canonical_uid) ||
        strcmp(replay->profile_uid, canonical_uid)))) {
    return false;
  }

  uint32_t cumulative_time = 0u;
  for (size_t i = 0; i < replay->sample_count; i++) {
    const race_replay_sample_t *sample = replay->samples + i;
    if (!Race_Replay_SampleValid(sample) ||
        (i && !sample->delta_time_ms) ||
        cumulative_time > UINT32_MAX - sample->delta_time_ms) {
      return false;
    }
    cumulative_time += sample->delta_time_ms;
    if (sample->time_ms != cumulative_time) {
      return false;
    }
  }

  if (!Race_Replay_ProjectileEventsValid(
        replay->projectile_events, replay->projectile_event_count,
        replay->projectile_event_capacity, replay->elapsed_time)) {
    return false;
  }

  const uint32_t difference = cumulative_time > replay->elapsed_time
    ? cumulative_time - replay->elapsed_time
    : replay->elapsed_time - cumulative_time;
  return difference <= RACE_REPLAY_TICK_MSEC;
}

static void Race_Replay_EncodeParams(
  const pm_params_t *params, uint8_t output[RACE_REPLAY_PARAMS_BYTES]) {
  Race_Replay_WriteI16(output, params->gravity);
  const float values[] = {
    params->gravity_water,
    params->accel_ground,
    params->accel_ground_slick,
    params->accel_air,
    params->accel_water,
    params->accel_spectator,
    params->accel_ladder,
    params->friction_ground,
    params->friction_ground_slick,
    params->friction_air,
    params->friction_water,
    params->friction_spectator,
    params->friction_ladder,
    params->speed_ground,
    params->speed_air,
    params->speed_water,
    params->speed_ladder,
    params->speed_spectator,
    params->speed_stop,
    params->speed_jump,
    params->speed_ducked,
    params->speed_duck_stand,
    params->speed_water_jump
  };
  for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++) {
    Race_Replay_WriteFloat(output + 2u + i * sizeof(float), values[i]);
  }
}

static bool Race_Replay_DecodeParams(
  const uint8_t input[RACE_REPLAY_PARAMS_BYTES], pm_params_t *params) {
  if (!input || !params) {
    return false;
  }
  params->gravity = Race_Replay_ReadI16(input);
  params->gravity_water = Race_Replay_ReadFloat(input + 2u);
  params->accel_ground = Race_Replay_ReadFloat(input + 6u);
  params->accel_ground_slick = Race_Replay_ReadFloat(input + 10u);
  params->accel_air = Race_Replay_ReadFloat(input + 14u);
  params->accel_water = Race_Replay_ReadFloat(input + 18u);
  params->accel_spectator = Race_Replay_ReadFloat(input + 22u);
  params->accel_ladder = Race_Replay_ReadFloat(input + 26u);
  params->friction_ground = Race_Replay_ReadFloat(input + 30u);
  params->friction_ground_slick = Race_Replay_ReadFloat(input + 34u);
  params->friction_air = Race_Replay_ReadFloat(input + 38u);
  params->friction_water = Race_Replay_ReadFloat(input + 42u);
  params->friction_spectator = Race_Replay_ReadFloat(input + 46u);
  params->friction_ladder = Race_Replay_ReadFloat(input + 50u);
  params->speed_ground = Race_Replay_ReadFloat(input + 54u);
  params->speed_air = Race_Replay_ReadFloat(input + 58u);
  params->speed_water = Race_Replay_ReadFloat(input + 62u);
  params->speed_ladder = Race_Replay_ReadFloat(input + 66u);
  params->speed_spectator = Race_Replay_ReadFloat(input + 70u);
  params->speed_stop = Race_Replay_ReadFloat(input + 74u);
  params->speed_jump = Race_Replay_ReadFloat(input + 78u);
  params->speed_ducked = Race_Replay_ReadFloat(input + 82u);
  params->speed_duck_stand = Race_Replay_ReadFloat(input + 86u);
  params->speed_water_jump = Race_Replay_ReadFloat(input + 90u);
  return Race_Replay_ParamsValid(params);
}

static void Race_Replay_EncodeFrame(
  const race_replay_sample_t *sample, uint16_t params_index,
  uint8_t output[RACE_REPLAY_FRAME_BYTES]) {
  memset(output, 0, RACE_REPLAY_FRAME_BYTES);
  Race_Replay_WriteU16(output, sample->delta_time_ms);
  output[2] = (uint8_t) sample->pm_state.type;
  output[3] = sample->strafe_helper.active ? 1u : 0u;
  Race_Replay_WriteU16(output + 4u, sample->pm_state.flags);
  Race_Replay_WriteU16(output + 6u, sample->pm_state.time);
  Race_Replay_WriteU16(output + 8u, params_index);
  Race_Replay_WriteU16(output + 10u, sample->pm_state.hook_length);
  Race_Replay_WriteVec3(output + 12u, sample->pm_state.origin);
  Race_Replay_WriteVec3(output + 24u, sample->pm_state.velocity);
  Race_Replay_WriteVec3(output + 36u, sample->pm_state.view_offset);
  Race_Replay_WriteFloat(output + 48u, sample->pm_state.step_offset);
  Race_Replay_WriteVec3(output + 52u, sample->pm_state.view_angles);
  Race_Replay_WriteVec3(output + 64u, sample->pm_state.delta_angles);
  Race_Replay_WriteVec3(output + 76u, sample->pm_state.hook_position);
  for (size_t i = 0; i < MAX_STATS; i++) {
    Race_Replay_WriteI16(output + 88u + i * sizeof(int16_t),
                         sample->stats[i]);
  }
  for (size_t i = 0; i < MAX_INVENTORY; i++) {
    Race_Replay_WriteI16(output + 152u + i * sizeof(int16_t),
                         sample->inventory[i]);
  }
  Race_Replay_WriteVec3(output + 280u, sample->strafe_helper.forward);
  Race_Replay_WriteVec3(output + 292u, sample->strafe_helper.velocity);
  Race_Replay_WriteVec3(output + 304u, sample->strafe_helper.wishdir);
  Race_Replay_WriteFloat(output + 316u, sample->strafe_helper.wishspeed);
  Race_Replay_WriteFloat(output + 320u, sample->strafe_helper.accel);
  Race_Replay_WriteFloat(output + 324u, sample->strafe_helper.frametime);
  Race_Replay_WriteFloat(output + 328u, sample->strafe_helper.view_yaw);
}

static bool Race_Replay_DecodeFrame(
  const uint8_t input[RACE_REPLAY_FRAME_BYTES], const pm_params_t *params,
  race_replay_sample_t *sample) {
  if (!input || !params || !sample || input[2] > PM_FREEZE || input[3] > 1u) {
    return false;
  }
  memset(sample, 0, sizeof(*sample));
  sample->delta_time_ms = Race_Replay_ReadU16(input);
  sample->pm_state.type = (pm_type_t) input[2];
  sample->strafe_helper.active = input[3] != 0u;
  sample->pm_state.flags = Race_Replay_ReadU16(input + 4u);
  sample->pm_state.time = Race_Replay_ReadU16(input + 6u);
  sample->pm_state.params = *params;
  sample->pm_state.hook_length = Race_Replay_ReadU16(input + 10u);
  sample->pm_state.origin = Race_Replay_ReadVec3(input + 12u);
  sample->pm_state.velocity = Race_Replay_ReadVec3(input + 24u);
  sample->pm_state.view_offset = Race_Replay_ReadVec3(input + 36u);
  sample->pm_state.step_offset = Race_Replay_ReadFloat(input + 48u);
  sample->pm_state.view_angles = Race_Replay_ReadVec3(input + 52u);
  sample->pm_state.delta_angles = Race_Replay_ReadVec3(input + 64u);
  sample->pm_state.hook_position = Race_Replay_ReadVec3(input + 76u);
  for (size_t i = 0; i < MAX_STATS; i++) {
    sample->stats[i] = Race_Replay_ReadI16(
      input + 88u + i * sizeof(int16_t));
  }
  for (size_t i = 0; i < MAX_INVENTORY; i++) {
    sample->inventory[i] = Race_Replay_ReadI16(
      input + 152u + i * sizeof(int16_t));
  }
  sample->strafe_helper.forward = Race_Replay_ReadVec3(input + 280u);
  sample->strafe_helper.velocity = Race_Replay_ReadVec3(input + 292u);
  sample->strafe_helper.wishdir = Race_Replay_ReadVec3(input + 304u);
  sample->strafe_helper.wishspeed = Race_Replay_ReadFloat(input + 316u);
  sample->strafe_helper.accel = Race_Replay_ReadFloat(input + 320u);
  sample->strafe_helper.frametime = Race_Replay_ReadFloat(input + 324u);
  sample->strafe_helper.view_yaw = Race_Replay_ReadFloat(input + 328u);
  return Race_Replay_SampleValid(sample);
}

static void Race_Replay_EncodeProjectileEvent(
    const race_replay_projectile_event_t *event,
    uint8_t output[RACE_REPLAY_PROJECTILE_EVENT_BYTES]) {
  memset(output, 0, RACE_REPLAY_PROJECTILE_EVENT_BYTES);
  Race_Replay_WriteU32(output, event->time_ms);
  Race_Replay_WriteU16(output + 4u, event->id);
  output[6] = (uint8_t) event->kind;
  output[7] = (uint8_t) event->operation;
  Race_Replay_WriteVec3(output + 8u, event->origin);
  Race_Replay_WriteVec3(output + 20u, event->velocity);
  Race_Replay_WriteVec3(output + 32u, event->normal);
}

static bool Race_Replay_DecodeProjectileEvent(
    const uint8_t input[RACE_REPLAY_PROJECTILE_EVENT_BYTES],
    race_replay_projectile_event_t *event) {
  if (!input || !event) {
    return false;
  }
  *event = (race_replay_projectile_event_t) {
    .time_ms = Race_Replay_ReadU32(input),
    .id = Race_Replay_ReadU16(input + 4u),
    .kind = (race_replay_projectile_kind_t) input[6],
    .operation = (race_replay_projectile_operation_t) input[7],
    .origin = Race_Replay_ReadVec3(input + 8u),
    .velocity = Race_Replay_ReadVec3(input + 20u),
    .normal = Race_Replay_ReadVec3(input + 32u)
  };
  return Race_Replay_ProjectileEventValid(event);
}

static void Race_Replay_EncodeHeader(const race_replay_metadata_t *metadata,
                                     uint8_t output[RACE_REPLAY_HEADER_BYTES]) {
  memset(output, 0, RACE_REPLAY_HEADER_BYTES);
  memcpy(output, RACE_REPLAY_MAGIC, 4u);
  Race_Replay_WriteU16(output + RACE_REPLAY_OFFSET_VERSION,
                       RACE_REPLAY_FORMAT_VERSION);
  Race_Replay_WriteU16(output + RACE_REPLAY_OFFSET_HEADER_BYTES,
                       RACE_REPLAY_HEADER_BYTES);
  Race_Replay_WriteU32(output + RACE_REPLAY_OFFSET_FLAGS, metadata->flags);
  Race_Replay_WriteU16(output + RACE_REPLAY_OFFSET_TICK_RATE,
                       metadata->tick_rate);
  output[RACE_REPLAY_OFFSET_MODE] = metadata->mode;
  output[RACE_REPLAY_OFFSET_PHYSICS_MODE] = metadata->physics_mode;
  Race_Replay_WriteU32(output + RACE_REPLAY_OFFSET_ELAPSED,
                       metadata->elapsed_time);
  Race_Replay_WriteU32(output + RACE_REPLAY_OFFSET_FRAME_COUNT,
                       metadata->frame_count);
  Race_Replay_WriteU16(output + RACE_REPLAY_OFFSET_MAP_LENGTH,
                       metadata->map_length);
  Race_Replay_WriteU16(output + RACE_REPLAY_OFFSET_PLAYER_NAME_LENGTH,
                       metadata->player_name_length);
  Race_Replay_WriteU32(output + RACE_REPLAY_OFFSET_RAW_PAYLOAD_LENGTH,
                       metadata->raw_payload_length);
  Race_Replay_WriteU32(output + RACE_REPLAY_OFFSET_STORED_PAYLOAD_LENGTH,
                       metadata->stored_payload_length);
  Race_Replay_WriteU32(output + RACE_REPLAY_OFFSET_RAW_PAYLOAD_CRC,
                       metadata->raw_payload_crc);
  Race_Replay_WriteU32(output + RACE_REPLAY_OFFSET_METADATA_CRC,
                       metadata->metadata_crc);
  Race_Replay_WriteU16(output + RACE_REPLAY_OFFSET_FRAME_SCHEMA,
                       RACE_REPLAY_FRAME_SCHEMA);
  Race_Replay_WriteU16(output + RACE_REPLAY_OFFSET_PARAMS_COUNT,
                       metadata->params_count);
  Race_Replay_WriteI32(output + RACE_REPLAY_OFFSET_PLAYER_UID,
                       metadata->player_uid);
  Race_Replay_WriteU64(output + RACE_REPLAY_OFFSET_REPLAY_ID,
                       metadata->replay_id);
  Race_Replay_WriteU32(output + RACE_REPLAY_OFFSET_RESERVED,
                       metadata->projectile_event_count);
}

static uint32_t Race_Replay_MetadataCrc(
  const uint8_t header[RACE_REPLAY_HEADER_BYTES],
  const char *map, size_t map_length,
  const char *player_name, size_t player_name_length) {
  uint8_t canonical[RACE_REPLAY_HEADER_BYTES];
  memcpy(canonical, header, sizeof(canonical));
  memset(canonical + RACE_REPLAY_OFFSET_METADATA_CRC, 0, sizeof(uint32_t));
  mz_ulong crc = mz_crc32(MZ_CRC32_INIT, canonical, sizeof(canonical));
  crc = mz_crc32(crc, (const unsigned char *) map, map_length);
  crc = mz_crc32(crc, (const unsigned char *) player_name,
                 player_name_length);
  return (uint32_t) crc;
}

static uint64_t Race_Replay_ReplayId(const race_replay_t *replay,
                                     uint32_t raw_payload_crc) {
  uint8_t identity[4u + RACE_MAP_IDENTITY_MAX + 1u + 1u + 4u + 4u];
  size_t offset = 0u;
  const size_t map_length = strlen(replay->map);
  Race_Replay_WriteI32(identity + offset, replay->player_uid);
  offset += 4u;
  memcpy(identity + offset, replay->map, map_length);
  offset += map_length;
  identity[offset++] = (uint8_t) replay->mode;
  identity[offset++] = replay->physics_mode;
  Race_Replay_WriteU32(identity + offset, replay->elapsed_time);
  offset += 4u;
  Race_Replay_WriteU32(identity + offset, (uint32_t) replay->sample_count);
  offset += 4u;
  mz_ulong identity_crc = mz_crc32(MZ_CRC32_INIT, identity, offset);
  identity_crc = mz_crc32(identity_crc,
                          (const unsigned char *) replay->player_name,
                          strlen(replay->player_name));
  return ((uint64_t) (uint32_t) identity_crc << 32u) | raw_payload_crc;
}

static void Race_Replay_CanonicalizeSignedZero(uint8_t *bytes,
                                                const size_t offset) {
  if ((Race_Replay_ReadU32(bytes + offset) & UINT32_C(0x7fffffff)) == 0u) {
    Race_Replay_WriteU32(bytes + offset, 0u);
  }
}

static void Race_Replay_CanonicalizeParamsSignedZero(
    uint8_t bytes[RACE_REPLAY_PARAMS_BYTES]) {
  for (size_t offset = 2u; offset < RACE_REPLAY_PARAMS_BYTES;
       offset += sizeof(float)) {
    Race_Replay_CanonicalizeSignedZero(bytes, offset);
  }
}

static void Race_Replay_CanonicalizeFrameSignedZero(
    uint8_t bytes[RACE_REPLAY_FRAME_BYTES]) {
  for (size_t offset = 12u; offset < 88u; offset += sizeof(float)) {
    Race_Replay_CanonicalizeSignedZero(bytes, offset);
  }
  for (size_t offset = 280u; offset < RACE_REPLAY_FRAME_BYTES;
       offset += sizeof(float)) {
    Race_Replay_CanonicalizeSignedZero(bytes, offset);
  }
}

static void Race_Replay_CanonicalizeProjectileEventSignedZero(
    uint8_t bytes[RACE_REPLAY_PROJECTILE_EVENT_BYTES]) {
  for (size_t offset = 8u; offset < RACE_REPLAY_PROJECTILE_EVENT_BYTES;
       offset += sizeof(float)) {
    Race_Replay_CanonicalizeSignedZero(bytes, offset);
  }
}

bool Race_Replay_Equals(const race_replay_t *left,
                        const race_replay_t *right) {
  if (!Race_Replay_Valid(left) || !Race_Replay_Valid(right) ||
      strcmp(left->map, right->map) ||
      strcmp(left->player_name, right->player_name) ||
      left->player_uid != right->player_uid || left->mode != right->mode ||
       left->physics_mode != right->physics_mode ||
       left->elapsed_time != right->elapsed_time ||
       left->sample_count != right->sample_count ||
       left->projectile_event_count != right->projectile_event_count ||
      (left->replay_id && right->replay_id &&
       left->replay_id != right->replay_id)) {
    return false;
  }

  for (size_t i = 0; i < left->sample_count; i++) {
    uint8_t left_params[RACE_REPLAY_PARAMS_BYTES];
    uint8_t right_params[RACE_REPLAY_PARAMS_BYTES];
    uint8_t left_frame[RACE_REPLAY_FRAME_BYTES];
    uint8_t right_frame[RACE_REPLAY_FRAME_BYTES];
    Race_Replay_EncodeParams(&left->samples[i].pm_state.params, left_params);
    Race_Replay_EncodeParams(&right->samples[i].pm_state.params, right_params);
    Race_Replay_EncodeFrame(left->samples + i, 0u, left_frame);
    Race_Replay_EncodeFrame(right->samples + i, 0u, right_frame);
    Race_Replay_CanonicalizeParamsSignedZero(left_params);
    Race_Replay_CanonicalizeParamsSignedZero(right_params);
    Race_Replay_CanonicalizeFrameSignedZero(left_frame);
    Race_Replay_CanonicalizeFrameSignedZero(right_frame);
    if (left->samples[i].time_ms != right->samples[i].time_ms ||
        memcmp(left_params, right_params, sizeof(left_params)) ||
        memcmp(left_frame, right_frame, sizeof(left_frame))) {
      return false;
    }
  }
  for (size_t i = 0u; i < left->projectile_event_count; i++) {
    uint8_t left_event[RACE_REPLAY_PROJECTILE_EVENT_BYTES];
    uint8_t right_event[RACE_REPLAY_PROJECTILE_EVENT_BYTES];
    Race_Replay_EncodeProjectileEvent(
      left->projectile_events + i, left_event);
    Race_Replay_EncodeProjectileEvent(
      right->projectile_events + i, right_event);
    Race_Replay_CanonicalizeProjectileEventSignedZero(left_event);
    Race_Replay_CanonicalizeProjectileEventSignedZero(right_event);
    if (memcmp(left_event, right_event, sizeof(left_event))) {
      return false;
    }
  }
  return true;
}

bool Race_Replay_IdString(uint64_t replay_id,
                          char output[RACE_REPLAY_ID_TEXT_SIZE]) {
  if (!replay_id || !output) {
    return false;
  }
  return snprintf(output, RACE_REPLAY_ID_TEXT_SIZE,
                  "%016" PRIx64, replay_id) == RACE_REPLAY_ID_TEXT_LENGTH;
}

bool Race_Replay_Paths(const char *ruleset, const char *map,
                       uint64_t replay_id,
                       char *committed, size_t committed_size,
                       char *candidate, size_t candidate_size) {
  if (!committed || !committed_size || !candidate || !candidate_size) {
    return false;
  }
  char encoded_map[RACE_MAP_IDENTITY_ENCODED_SIZE];
  char id[RACE_REPLAY_ID_TEXT_SIZE];
  if (!Race_MapState_RulesetValid(ruleset) ||
      !Race_MapState_EncodeMap(map, encoded_map) ||
      !Race_Replay_IdString(replay_id, id)) {
    return false;
  }
  const int32_t committed_length = snprintf(
    committed, committed_size,
    RACE_REPLAY_ROOT_DIRECTORY "/%s/%s/replay-%s.ghost",
    ruleset, encoded_map, id);
  const int32_t candidate_length = snprintf(
    candidate, candidate_size,
    RACE_REPLAY_ROOT_DIRECTORY "/%s/%s/replay-%s.candidate",
    ruleset, encoded_map, id);
  return committed_length >= 0 && (size_t) committed_length < committed_size &&
         candidate_length >= 0 && (size_t) candidate_length < candidate_size;
}

bool Race_Replay_Serialize(const race_replay_t *replay,
                           void *output, size_t output_size,
                           size_t *output_length, uint64_t *replay_id) {
  if (!Race_Replay_Valid(replay) || !output ||
      output_size < RACE_REPLAY_HEADER_BYTES) {
    return false;
  }

  const size_t frame_count = replay->sample_count;
  size_t max_params_length;
  if (!Race_Replay_CheckedMultiply(frame_count, RACE_REPLAY_PARAMS_BYTES,
                                   &max_params_length)) {
    return false;
  }
  uint8_t *params = malloc(max_params_length);
  uint16_t *frame_params = malloc(frame_count * sizeof(*frame_params));
  if (!params || !frame_params) {
    free(frame_params);
    free(params);
    return false;
  }

  uint16_t params_count = 0u;
  for (size_t i = 0; i < frame_count; i++) {
    uint8_t encoded[RACE_REPLAY_PARAMS_BYTES];
    Race_Replay_EncodeParams(&replay->samples[i].pm_state.params, encoded);
    if (!params_count ||
        memcmp(encoded, params + (params_count - 1u) * RACE_REPLAY_PARAMS_BYTES,
               sizeof(encoded))) {
      memcpy(params + params_count * RACE_REPLAY_PARAMS_BYTES,
             encoded, sizeof(encoded));
      params_count++;
    }
    frame_params[i] = (uint16_t) (params_count - 1u);
  }

  size_t params_length, frames_length, projectile_events_length;
  size_t frames_end, raw_length;
  if (!Race_Replay_CheckedMultiply(params_count, RACE_REPLAY_PARAMS_BYTES,
                                   &params_length) ||
      !Race_Replay_CheckedMultiply(frame_count, RACE_REPLAY_FRAME_BYTES,
                                   &frames_length) ||
      !Race_Replay_CheckedMultiply(replay->projectile_event_count,
                                   RACE_REPLAY_PROJECTILE_EVENT_BYTES,
                                   &projectile_events_length) ||
      !Race_Replay_CheckedAdd(params_length, frames_length, &frames_end) ||
      !Race_Replay_CheckedAdd(frames_end, projectile_events_length,
                              &raw_length) ||
      raw_length > UINT32_MAX) {
    free(frame_params);
    free(params);
    return false;
  }

  uint8_t *raw_payload = malloc(raw_length);
  if (!raw_payload) {
    free(frame_params);
    free(params);
    return false;
  }
  memcpy(raw_payload, params, params_length);
  for (size_t i = 0; i < frame_count; i++) {
    Race_Replay_EncodeFrame(replay->samples + i, frame_params[i],
                            raw_payload + params_length +
                              i * RACE_REPLAY_FRAME_BYTES);
  }
  for (size_t i = 0u; i < replay->projectile_event_count; i++) {
    uint8_t *encoded = raw_payload + frames_end +
                       i * RACE_REPLAY_PROJECTILE_EVENT_BYTES;
    Race_Replay_EncodeProjectileEvent(
      replay->projectile_events + i, encoded);
    Race_Replay_CanonicalizeProjectileEventSignedZero(encoded);
  }
  free(frame_params);
  free(params);

  const size_t map_length = strlen(replay->map);
  const size_t player_name_length = strlen(replay->player_name);
  size_t names_length, payload_offset;
  if (!Race_Replay_CheckedAdd(map_length, player_name_length, &names_length) ||
      !Race_Replay_CheckedAdd(RACE_REPLAY_HEADER_BYTES, names_length,
                              &payload_offset) ||
      payload_offset >= output_size) {
    free(raw_payload);
    return false;
  }

  const size_t bounded_output_size = output_size < RACE_REPLAY_MAX_FILE_BYTES
    ? output_size : RACE_REPLAY_MAX_FILE_BYTES;
  if (payload_offset >= bounded_output_size) {
    free(raw_payload);
    return false;
  }
  uint8_t *bytes = output;
  mz_ulong stored_length = (mz_ulong) (bounded_output_size - payload_offset);
  const int32_t compressed = mz_compress2(
    bytes + payload_offset, &stored_length, raw_payload,
    (mz_ulong) raw_length, 3);
  const uint32_t raw_payload_crc = Race_Replay_Crc32(raw_payload, raw_length);
  free(raw_payload);
  if (compressed != MZ_OK || !stored_length || stored_length > UINT32_MAX ||
      payload_offset + (size_t) stored_length > RACE_REPLAY_MAX_FILE_BYTES) {
    return false;
  }

  race_replay_metadata_t metadata = {
    .flags = RACE_REPLAY_FLAG_DEFLATE,
    .tick_rate = RACE_REPLAY_TICK_RATE,
    .mode = (uint8_t) replay->mode,
    .physics_mode = replay->physics_mode,
    .elapsed_time = replay->elapsed_time,
    .frame_count = (uint32_t) frame_count,
    .map_length = (uint16_t) map_length,
    .player_name_length = (uint16_t) player_name_length,
    .raw_payload_length = (uint32_t) raw_length,
    .stored_payload_length = (uint32_t) stored_length,
    .raw_payload_crc = raw_payload_crc,
    .params_count = params_count,
    .player_uid = replay->player_uid,
    .replay_id = Race_Replay_ReplayId(replay, raw_payload_crc),
    .projectile_event_count = (uint32_t) replay->projectile_event_count
  };
  if (!metadata.replay_id ||
      (replay->replay_id && replay->replay_id != metadata.replay_id)) {
    return false;
  }

  memcpy(bytes + RACE_REPLAY_HEADER_BYTES, replay->map, map_length);
  memcpy(bytes + RACE_REPLAY_HEADER_BYTES + map_length,
         replay->player_name, player_name_length);
  Race_Replay_EncodeHeader(&metadata, bytes);
  metadata.metadata_crc = Race_Replay_MetadataCrc(
    bytes, replay->map, map_length, replay->player_name, player_name_length);
  Race_Replay_EncodeHeader(&metadata, bytes);

  if (output_length) {
    *output_length = payload_offset + (size_t) stored_length;
  }
  if (replay_id) {
    *replay_id = metadata.replay_id;
  }
  return true;
}

static race_replay_parse_result_t Race_Replay_Inflate(
  const uint8_t *stored, size_t stored_length,
  uint8_t *raw, size_t raw_length) {
  mz_stream stream = { 0 };
  if (mz_inflateInit(&stream) != MZ_OK) {
    return RACE_REPLAY_PARSE_MALFORMED;
  }

  race_replay_parse_result_t result = RACE_REPLAY_PARSE_MALFORMED;
  uint8_t overflow;
  for (;;) {
    if (!stream.avail_in && stream.total_in < stored_length) {
      const size_t remaining = stored_length - (size_t) stream.total_in;
      const size_t chunk = remaining < RACE_REPLAY_STREAM_CHUNK_BYTES
        ? remaining : RACE_REPLAY_STREAM_CHUNK_BYTES;
      stream.next_in = stored + stream.total_in;
      stream.avail_in = (uint32_t) chunk;
    }
    if (!stream.avail_out) {
      if (stream.total_out < raw_length) {
        const size_t remaining = raw_length - (size_t) stream.total_out;
        const size_t chunk = remaining < RACE_REPLAY_STREAM_CHUNK_BYTES
          ? remaining : RACE_REPLAY_STREAM_CHUNK_BYTES;
        stream.next_out = raw + stream.total_out;
        stream.avail_out = (uint32_t) chunk;
      } else {
        stream.next_out = &overflow;
        stream.avail_out = sizeof(overflow);
      }
    }

    const mz_ulong previous_in = stream.total_in;
    const mz_ulong previous_out = stream.total_out;
    const int32_t inflated = mz_inflate(&stream, MZ_NO_FLUSH);
    if (stream.total_out > raw_length) {
      result = RACE_REPLAY_PARSE_BOUNDS;
      break;
    }
    if (inflated == MZ_STREAM_END) {
      result = stream.total_in == stored_length && !stream.avail_in &&
               stream.total_out == raw_length
        ? RACE_REPLAY_PARSE_OK : RACE_REPLAY_PARSE_MALFORMED;
      break;
    }
    if (inflated != MZ_OK ||
        (stream.total_in == previous_in && stream.total_out == previous_out)) {
      break;
    }
  }
  if (mz_inflateEnd(&stream) != MZ_OK && result == RACE_REPLAY_PARSE_OK) {
    result = RACE_REPLAY_PARSE_MALFORMED;
  }
  return result;
}

race_replay_parse_result_t Race_Replay_Parse(
  const void *data, size_t length,
  race_replay_sample_t *samples, size_t capacity,
  race_replay_projectile_event_t *projectile_events,
  size_t projectile_event_capacity,
  race_replay_t *replay) {
  if (!data || !replay || (!samples && capacity) ||
      (!projectile_events && projectile_event_capacity)) {
    return RACE_REPLAY_PARSE_MALFORMED;
  }
  memset(replay, 0, sizeof(*replay));
  if (length > RACE_REPLAY_MAX_FILE_BYTES) {
    return RACE_REPLAY_PARSE_TOO_LARGE;
  }
  if (length < RACE_REPLAY_HEADER_BYTES ||
      capacity > RACE_REPLAY_MAX_FRAMES ||
      projectile_event_capacity > RACE_REPLAY_MAX_PROJECTILE_EVENTS) {
    return RACE_REPLAY_PARSE_BOUNDS;
  }

  const uint8_t *bytes = data;
  if (memcmp(bytes, RACE_REPLAY_MAGIC, 4u)) {
    return RACE_REPLAY_PARSE_MALFORMED;
  }
  if (Race_Replay_ReadU16(bytes + RACE_REPLAY_OFFSET_VERSION) !=
      RACE_REPLAY_FORMAT_VERSION) {
    return RACE_REPLAY_PARSE_UNKNOWN_VERSION;
  }
  if (Race_Replay_ReadU16(bytes + RACE_REPLAY_OFFSET_HEADER_BYTES) !=
        RACE_REPLAY_HEADER_BYTES ||
      Race_Replay_ReadU32(bytes + RACE_REPLAY_OFFSET_FLAGS) !=
        RACE_REPLAY_FLAG_DEFLATE ||
      Race_Replay_ReadU16(bytes + RACE_REPLAY_OFFSET_FRAME_SCHEMA) !=
        RACE_REPLAY_FRAME_SCHEMA) {
    return RACE_REPLAY_PARSE_UNSUPPORTED_SCHEMA;
  }

  race_replay_metadata_t metadata = {
    .flags = Race_Replay_ReadU32(bytes + RACE_REPLAY_OFFSET_FLAGS),
    .tick_rate = Race_Replay_ReadU16(bytes + RACE_REPLAY_OFFSET_TICK_RATE),
    .mode = bytes[RACE_REPLAY_OFFSET_MODE],
    .physics_mode = bytes[RACE_REPLAY_OFFSET_PHYSICS_MODE],
    .elapsed_time = Race_Replay_ReadU32(bytes + RACE_REPLAY_OFFSET_ELAPSED),
    .frame_count = Race_Replay_ReadU32(bytes + RACE_REPLAY_OFFSET_FRAME_COUNT),
    .map_length = Race_Replay_ReadU16(bytes + RACE_REPLAY_OFFSET_MAP_LENGTH),
    .player_name_length = Race_Replay_ReadU16(
      bytes + RACE_REPLAY_OFFSET_PLAYER_NAME_LENGTH),
    .raw_payload_length = Race_Replay_ReadU32(
      bytes + RACE_REPLAY_OFFSET_RAW_PAYLOAD_LENGTH),
    .stored_payload_length = Race_Replay_ReadU32(
      bytes + RACE_REPLAY_OFFSET_STORED_PAYLOAD_LENGTH),
    .raw_payload_crc = Race_Replay_ReadU32(
      bytes + RACE_REPLAY_OFFSET_RAW_PAYLOAD_CRC),
    .metadata_crc = Race_Replay_ReadU32(
      bytes + RACE_REPLAY_OFFSET_METADATA_CRC),
    .params_count = Race_Replay_ReadU16(
      bytes + RACE_REPLAY_OFFSET_PARAMS_COUNT),
    .player_uid = Race_Replay_ReadI32(bytes + RACE_REPLAY_OFFSET_PLAYER_UID),
    .replay_id = Race_Replay_ReadU64(bytes + RACE_REPLAY_OFFSET_REPLAY_ID),
    .projectile_event_count = Race_Replay_ReadU32(
      bytes + RACE_REPLAY_OFFSET_RESERVED)
  };
  if (metadata.mode != RACE_MODE_RACE) {
    return RACE_REPLAY_PARSE_UNSUPPORTED_RULESET;
  }

  size_t params_length, frames_length, projectile_events_length;
  size_t frames_end, expected_raw_length, names_length;
  size_t payload_offset, expected_length;
  if (metadata.tick_rate != RACE_REPLAY_TICK_RATE ||
      metadata.physics_mode > RACE_REPLAY_MAX_PHYSICS_MODE ||
      !metadata.elapsed_time ||
      metadata.elapsed_time > RACE_REPLAY_MAX_TIME_MS ||
      !metadata.frame_count || metadata.frame_count > capacity ||
      metadata.frame_count > RACE_REPLAY_MAX_FRAMES ||
      metadata.projectile_event_count > projectile_event_capacity ||
      metadata.projectile_event_count > RACE_REPLAY_MAX_PROJECTILE_EVENTS ||
      !metadata.params_count || metadata.params_count > metadata.frame_count ||
      !metadata.map_length || metadata.map_length > RACE_MAP_IDENTITY_MAX ||
      !metadata.player_name_length ||
      metadata.player_name_length > RACE_REPLAY_PLAYER_NAME_MAX ||
      !metadata.stored_payload_length || !metadata.replay_id ||
      !Race_Replay_CheckedMultiply(metadata.params_count,
                                   RACE_REPLAY_PARAMS_BYTES,
                                   &params_length) ||
      !Race_Replay_CheckedMultiply(metadata.frame_count,
                                   RACE_REPLAY_FRAME_BYTES,
                                   &frames_length) ||
      !Race_Replay_CheckedMultiply(metadata.projectile_event_count,
                                   RACE_REPLAY_PROJECTILE_EVENT_BYTES,
                                   &projectile_events_length) ||
      !Race_Replay_CheckedAdd(params_length, frames_length,
                              &frames_end) ||
      !Race_Replay_CheckedAdd(frames_end, projectile_events_length,
                              &expected_raw_length) ||
      expected_raw_length != metadata.raw_payload_length ||
      !Race_Replay_CheckedAdd(metadata.map_length,
                              metadata.player_name_length, &names_length) ||
      !Race_Replay_CheckedAdd(RACE_REPLAY_HEADER_BYTES, names_length,
                              &payload_offset) ||
      !Race_Replay_CheckedAdd(payload_offset, metadata.stored_payload_length,
                              &expected_length) ||
      expected_length != length ||
      metadata.stored_payload_length >
        mz_compressBound(metadata.raw_payload_length)) {
    return RACE_REPLAY_PARSE_BOUNDS;
  }

  const char *map_bytes = (const char *) bytes + RACE_REPLAY_HEADER_BYTES;
  const char *player_name_bytes = map_bytes + metadata.map_length;
  if (!Race_Replay_NameSafe(map_bytes, metadata.map_length) ||
      !Race_Replay_NameSafe(player_name_bytes, metadata.player_name_length)) {
    return RACE_REPLAY_PARSE_MALFORMED;
  }
  char map[RACE_MAP_IDENTITY_SIZE];
  char canonical_map[RACE_MAP_IDENTITY_SIZE];
  memcpy(map, map_bytes, metadata.map_length);
  map[metadata.map_length] = '\0';
  if (!Race_MapState_CanonicalizeMap(map, canonical_map) ||
      strcmp(map, canonical_map)) {
    return RACE_REPLAY_PARSE_MALFORMED;
  }
  char player_name[RACE_REPLAY_PLAYER_NAME_SIZE];
  memcpy(player_name, player_name_bytes, metadata.player_name_length);
  player_name[metadata.player_name_length] = '\0';

  if (Race_Replay_MetadataCrc(bytes, map, metadata.map_length,
                              player_name, metadata.player_name_length) !=
      metadata.metadata_crc) {
    return RACE_REPLAY_PARSE_CHECKSUM;
  }

  uint8_t *raw_payload = malloc(metadata.raw_payload_length);
  if (!raw_payload) {
    return RACE_REPLAY_PARSE_TOO_LARGE;
  }
  race_replay_parse_result_t result = Race_Replay_Inflate(
    bytes + payload_offset, metadata.stored_payload_length,
    raw_payload, metadata.raw_payload_length);
  if (result != RACE_REPLAY_PARSE_OK) {
    free(raw_payload);
    return result;
  }
  if (Race_Replay_Crc32(raw_payload, metadata.raw_payload_length) !=
      metadata.raw_payload_crc) {
    free(raw_payload);
    return RACE_REPLAY_PARSE_CHECKSUM;
  }

  if (!Race_Replay_Init(replay, samples, capacity,
                        projectile_events, projectile_event_capacity,
                        map, NULL, player_name,
                        metadata.player_uid, metadata.physics_mode)) {
    free(raw_payload);
    return RACE_REPLAY_PARSE_BOUNDS;
  }
  replay->elapsed_time = metadata.elapsed_time;
  replay->sample_count = metadata.frame_count;
  replay->projectile_event_count = metadata.projectile_event_count;
  replay->replay_id = metadata.replay_id;

  for (size_t i = 0; i < metadata.params_count; i++) {
    pm_params_t params;
    if (!Race_Replay_DecodeParams(raw_payload +
                                    i * RACE_REPLAY_PARAMS_BYTES,
                                  &params)) {
      free(raw_payload);
      return RACE_REPLAY_PARSE_MALFORMED;
    }
  }

  uint32_t cumulative_time = 0u;
  for (size_t i = 0; i < metadata.frame_count; i++) {
    const uint8_t *frame = raw_payload + params_length +
                           i * RACE_REPLAY_FRAME_BYTES;
    const uint16_t params_index = Race_Replay_ReadU16(frame + 8u);
    pm_params_t params;
    if (params_index >= metadata.params_count ||
        !Race_Replay_DecodeParams(raw_payload +
                                    params_index * RACE_REPLAY_PARAMS_BYTES,
                                  &params) ||
        !Race_Replay_DecodeFrame(frame, &params, samples + i) ||
        (i && !samples[i].delta_time_ms) ||
        cumulative_time > UINT32_MAX - samples[i].delta_time_ms) {
      free(raw_payload);
      return RACE_REPLAY_PARSE_MALFORMED;
    }
    cumulative_time += samples[i].delta_time_ms;
    samples[i].time_ms = cumulative_time;
  }
  for (size_t i = 0u; i < metadata.projectile_event_count; i++) {
    const uint8_t *encoded = raw_payload + frames_end +
                             i * RACE_REPLAY_PROJECTILE_EVENT_BYTES;
    if (!Race_Replay_DecodeProjectileEvent(
          encoded, projectile_events + i)) {
      free(raw_payload);
      return RACE_REPLAY_PARSE_MALFORMED;
    }
  }
  free(raw_payload);

  if (Race_Replay_ReplayId(replay, metadata.raw_payload_crc) !=
      metadata.replay_id) {
    return RACE_REPLAY_PARSE_IDENTITY_MISMATCH;
  }
  return Race_Replay_Valid(replay)
    ? RACE_REPLAY_PARSE_OK : RACE_REPLAY_PARSE_MALFORMED;
}

const char *Race_Replay_ParseResultName(race_replay_parse_result_t result) {
  switch (result) {
    case RACE_REPLAY_PARSE_OK:
      return "ok";
    case RACE_REPLAY_PARSE_MALFORMED:
      return "malformed";
    case RACE_REPLAY_PARSE_UNKNOWN_VERSION:
      return "unknown version";
    case RACE_REPLAY_PARSE_UNSUPPORTED_SCHEMA:
      return "unsupported schema";
    case RACE_REPLAY_PARSE_UNSUPPORTED_RULESET:
      return "unsupported ruleset";
    case RACE_REPLAY_PARSE_CHECKSUM:
      return "checksum mismatch";
    case RACE_REPLAY_PARSE_TOO_LARGE:
      return "too large";
    case RACE_REPLAY_PARSE_BOUNDS:
      return "bounds exceeded";
    case RACE_REPLAY_PARSE_IDENTITY_MISMATCH:
      return "identity mismatch";
  }
  return "unknown";
}
