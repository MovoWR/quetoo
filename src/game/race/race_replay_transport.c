/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_replay_transport.h"

#include <math.h>
#include <string.h>

#define RACE_REPLAY_STATE_HEADER_BYTES 32u
#define RACE_REPLAY_STATE_SAMPLE_BYTES 28u
#define RACE_REPLAY_TELEMETRY_BYTES 104u
#define RACE_REPLAY_PROJECTILE_HEADER_BYTES 16u
#define RACE_RACELINE_HEADER_BYTES 32u
#define RACE_RACELINE_POINT_BYTES 16u

static void Race_Transport_Write16(uint8_t *output, uint16_t value) {
  output[0] = (uint8_t) value;
  output[1] = (uint8_t) (value >> 8u);
}

static void Race_Transport_Write32(uint8_t *output, uint32_t value) {
  output[0] = (uint8_t) value;
  output[1] = (uint8_t) (value >> 8u);
  output[2] = (uint8_t) (value >> 16u);
  output[3] = (uint8_t) (value >> 24u);
}

static void Race_Transport_Write64(uint8_t *output, uint64_t value) {
  Race_Transport_Write32(output, (uint32_t) value);
  Race_Transport_Write32(output + 4u, (uint32_t) (value >> 32u));
}

static void Race_Transport_WriteFloat(uint8_t *output, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  Race_Transport_Write32(output, bits);
}

static uint16_t Race_Transport_Read16(const uint8_t *input) {
  return (uint16_t) input[0] | (uint16_t) input[1] << 8u;
}

static uint32_t Race_Transport_Read32(const uint8_t *input) {
  return (uint32_t) input[0] |
         (uint32_t) input[1] << 8u |
         (uint32_t) input[2] << 16u |
         (uint32_t) input[3] << 24u;
}

static uint64_t Race_Transport_Read64(const uint8_t *input) {
  return (uint64_t) Race_Transport_Read32(input) |
         (uint64_t) Race_Transport_Read32(input + 4u) << 32u;
}

static float Race_Transport_ReadFloat(const uint8_t *input) {
  const uint32_t bits = Race_Transport_Read32(input);
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

static bool Race_Transport_FiniteVec3(vec3_t vector) {
  return isfinite(vector.x) && isfinite(vector.y) && isfinite(vector.z);
}

static bool Race_Transport_BoundedString(const char *string, size_t maximum,
                                         size_t *length) {
  if (!string) {
    return false;
  }
  size_t len = 0u;
  while (len <= maximum && string[len]) {
    len++;
  }
  if (len > maximum) {
    return false;
  }
  if (length) {
    *length = len;
  }
  return true;
}

bool Race_ReplaySpeed_Ratio(const race_replay_speed_t speed,
                            uint32_t *numerator, uint32_t *denominator) {
  if (!numerator || !denominator) {
    return false;
  }
  switch (speed) {
    case RACE_REPLAY_SPEED_QUARTER:
      *numerator = 1u;
      *denominator = 4u;
      return true;
    case RACE_REPLAY_SPEED_HALF:
      *numerator = 1u;
      *denominator = 2u;
      return true;
    case RACE_REPLAY_SPEED_NORMAL:
      *numerator = 1u;
      *denominator = 1u;
      return true;
    case RACE_REPLAY_SPEED_DOUBLE:
      *numerator = 2u;
      *denominator = 1u;
      return true;
    case RACE_REPLAY_SPEED_QUADRUPLE:
      *numerator = 4u;
      *denominator = 1u;
      return true;
    default:
      return false;
  }
}

bool Race_ReplayGeneration_Newer(const uint32_t candidate,
                                 const uint32_t current) {
  return candidate && (!current || (int32_t) (candidate - current) > 0);
}

static bool Race_ReplayState_Valid(const race_replay_state_message_t *message,
                                   size_t *name_length) {
  if (!message || !message->generation || !message->sequence ||
      message->speed >= RACE_REPLAY_SPEED_TOTAL ||
      (message->flags & ~RACE_REPLAY_STATE_FLAGS_MASK) ||
      !Race_Transport_BoundedString(message->display_name,
                                    RACE_REPLAY_DISPLAY_NAME_MAX,
                                    name_length)) {
    return false;
  }

  const bool active = (message->flags & RACE_REPLAY_STATE_ACTIVE) != 0;
  if (!active) {
    return message->flags == 0u &&
           message->source == RACE_REPLAY_SOURCE_NONE &&
           !message->replay_id && !message->duration_ms &&
           !message->playhead_ms && !message->rank &&
           !*message->display_name && !message->sample_count;
  }

  if (message->source <= RACE_REPLAY_SOURCE_NONE ||
      message->source >= RACE_REPLAY_SOURCE_TOTAL ||
      !message->replay_id || !message->duration_ms ||
      message->playhead_ms > message->duration_ms ||
      message->rank > 15u || !message->sample_count ||
      message->sample_count > RACE_REPLAY_STATE_MAX_SAMPLES ||
      ((message->flags & RACE_REPLAY_STATE_COMPLETED) &&
       (!(message->flags & RACE_REPLAY_STATE_PAUSED) ||
        message->playhead_ms != message->duration_ms))) {
    return false;
  }

  for (size_t i = 0u; i < message->sample_count; i++) {
    const race_replay_pose_sample_t *sample = message->samples + i;
    if (sample->time_ms > message->duration_ms ||
        (i && sample->time_ms <= message->samples[i - 1u].time_ms) ||
        !Race_Transport_FiniteVec3(sample->origin) ||
        !Race_Transport_FiniteVec3(sample->view_angles)) {
      return false;
    }
  }

  return message->samples[0].time_ms <= message->playhead_ms &&
         message->samples[message->sample_count - 1u].time_ms >=
           message->playhead_ms;
}

size_t Race_ReplayState_Encode(const race_replay_state_message_t *message,
                               void *output, const size_t capacity) {
  size_t name_length;
  if (!output || !Race_ReplayState_Valid(message, &name_length)) {
    return 0u;
  }
  const size_t length = RACE_REPLAY_STATE_HEADER_BYTES + name_length +
                        message->sample_count * RACE_REPLAY_STATE_SAMPLE_BYTES;
  if (length > capacity || length > RACE_REPLAY_TRANSPORT_MAX_PAYLOAD) {
    return 0u;
  }

  uint8_t *bytes = output;
  memset(bytes, 0, length);
  bytes[0] = RACE_REPLAY_TRANSPORT_VERSION;
  bytes[1] = message->flags;
  bytes[2] = (uint8_t) message->speed;
  bytes[3] = (uint8_t) message->source;
  Race_Transport_Write32(bytes + 4u, message->generation);
  Race_Transport_Write32(bytes + 8u, message->sequence);
  Race_Transport_Write64(bytes + 12u, message->replay_id);
  Race_Transport_Write32(bytes + 20u, message->duration_ms);
  Race_Transport_Write32(bytes + 24u, message->playhead_ms);
  bytes[28] = message->rank;
  bytes[29] = (uint8_t) name_length;
  bytes[30] = (uint8_t) message->sample_count;
  memcpy(bytes + RACE_REPLAY_STATE_HEADER_BYTES,
         message->display_name, name_length);

  uint8_t *sample_bytes = bytes + RACE_REPLAY_STATE_HEADER_BYTES + name_length;
  for (size_t i = 0u; i < message->sample_count; i++) {
    const race_replay_pose_sample_t *sample = message->samples + i;
    Race_Transport_Write32(sample_bytes, sample->time_ms);
    Race_Transport_WriteFloat(sample_bytes + 4u, sample->origin.x);
    Race_Transport_WriteFloat(sample_bytes + 8u, sample->origin.y);
    Race_Transport_WriteFloat(sample_bytes + 12u, sample->origin.z);
    Race_Transport_WriteFloat(sample_bytes + 16u, sample->view_angles.x);
    Race_Transport_WriteFloat(sample_bytes + 20u, sample->view_angles.y);
    Race_Transport_WriteFloat(sample_bytes + 24u, sample->view_angles.z);
    sample_bytes += RACE_REPLAY_STATE_SAMPLE_BYTES;
  }
  return length;
}

bool Race_ReplayState_Decode(const void *data, const size_t length,
                             race_replay_state_message_t *message) {
  if (!data || !message || length < RACE_REPLAY_STATE_HEADER_BYTES ||
      length > RACE_REPLAY_TRANSPORT_MAX_PAYLOAD) {
    return false;
  }
  const uint8_t *bytes = data;
  const size_t name_length = bytes[29];
  const size_t sample_count = bytes[30];
  if (bytes[0] != RACE_REPLAY_TRANSPORT_VERSION || bytes[31] ||
      name_length > RACE_REPLAY_DISPLAY_NAME_MAX ||
      sample_count > RACE_REPLAY_STATE_MAX_SAMPLES ||
      length != RACE_REPLAY_STATE_HEADER_BYTES + name_length +
                sample_count * RACE_REPLAY_STATE_SAMPLE_BYTES) {
    return false;
  }

  memset(message, 0, sizeof(*message));
  message->flags = bytes[1];
  message->speed = (race_replay_speed_t) bytes[2];
  message->source = (race_replay_source_t) bytes[3];
  message->generation = Race_Transport_Read32(bytes + 4u);
  message->sequence = Race_Transport_Read32(bytes + 8u);
  message->replay_id = Race_Transport_Read64(bytes + 12u);
  message->duration_ms = Race_Transport_Read32(bytes + 20u);
  message->playhead_ms = Race_Transport_Read32(bytes + 24u);
  message->rank = bytes[28];
  memcpy(message->display_name, bytes + RACE_REPLAY_STATE_HEADER_BYTES,
         name_length);
  message->sample_count = sample_count;

  const uint8_t *sample_bytes = bytes + RACE_REPLAY_STATE_HEADER_BYTES +
                                name_length;
  for (size_t i = 0u; i < sample_count; i++) {
    race_replay_pose_sample_t *sample = message->samples + i;
    sample->time_ms = Race_Transport_Read32(sample_bytes);
    sample->origin = Vec3(Race_Transport_ReadFloat(sample_bytes + 4u),
                          Race_Transport_ReadFloat(sample_bytes + 8u),
                          Race_Transport_ReadFloat(sample_bytes + 12u));
    sample->view_angles = Vec3(Race_Transport_ReadFloat(sample_bytes + 16u),
                               Race_Transport_ReadFloat(sample_bytes + 20u),
                               Race_Transport_ReadFloat(sample_bytes + 24u));
    sample_bytes += RACE_REPLAY_STATE_SAMPLE_BYTES;
  }
  return Race_ReplayState_Valid(message, NULL);
}

static bool Race_ReplayTelemetry_Valid(
    const race_replay_telemetry_message_t *message) {
  if (!message || !message->generation || !message->sequence ||
      message->pm_type < PM_NORMAL || message->pm_type > PM_FREEZE ||
      !Race_Transport_FiniteVec3(message->origin) ||
      !Race_Transport_FiniteVec3(message->velocity) ||
      (message->input_flags && !Race_InputFlagsValid(message->input_flags))) {
    return false;
  }

  const race_strafe_sample_t *sample = &message->strafe_helper;
  if (!Race_Transport_FiniteVec3(sample->forward) ||
      !Race_Transport_FiniteVec3(sample->velocity) ||
      !Race_Transport_FiniteVec3(sample->wishdir) ||
      !isfinite(sample->wishspeed) || !isfinite(sample->accel) ||
      !isfinite(sample->frametime) || !isfinite(sample->view_yaw)) {
    return false;
  }
  return !sample->active ||
         (sample->wishspeed >= 0.f && sample->accel >= 0.f &&
          sample->frametime > 0.f);
}

size_t Race_ReplayTelemetry_Encode(
    const race_replay_telemetry_message_t *message,
    void *output, const size_t capacity) {
  if (!output || capacity < RACE_REPLAY_TELEMETRY_BYTES ||
      !Race_ReplayTelemetry_Valid(message)) {
    return 0u;
  }

  uint8_t *bytes = output;
  memset(bytes, 0, RACE_REPLAY_TELEMETRY_BYTES);
  bytes[0] = RACE_REPLAY_TRANSPORT_VERSION;
  bytes[2] = (uint8_t) message->pm_type;
  Race_Transport_Write32(bytes + 4u, message->generation);
  Race_Transport_Write32(bytes + 8u, message->sequence);
  Race_Transport_Write32(bytes + 12u, message->playhead_ms);
  Race_Transport_Write32(bytes + 16u, message->frame_cursor);
  Race_Transport_Write16(bytes + 20u, message->pm_flags);
  Race_Transport_Write16(bytes + 22u, (uint16_t) message->input_flags);
  Race_Transport_WriteFloat(bytes + 24u, message->origin.x);
  Race_Transport_WriteFloat(bytes + 28u, message->origin.y);
  Race_Transport_WriteFloat(bytes + 32u, message->origin.z);
  Race_Transport_WriteFloat(bytes + 36u, message->velocity.x);
  Race_Transport_WriteFloat(bytes + 40u, message->velocity.y);
  Race_Transport_WriteFloat(bytes + 44u, message->velocity.z);
  bytes[48] = message->strafe_helper.active ? 1u : 0u;
  Race_Transport_WriteFloat(bytes + 52u, message->strafe_helper.forward.x);
  Race_Transport_WriteFloat(bytes + 56u, message->strafe_helper.forward.y);
  Race_Transport_WriteFloat(bytes + 60u, message->strafe_helper.forward.z);
  Race_Transport_WriteFloat(bytes + 64u, message->strafe_helper.velocity.x);
  Race_Transport_WriteFloat(bytes + 68u, message->strafe_helper.velocity.y);
  Race_Transport_WriteFloat(bytes + 72u, message->strafe_helper.velocity.z);
  Race_Transport_WriteFloat(bytes + 76u, message->strafe_helper.wishdir.x);
  Race_Transport_WriteFloat(bytes + 80u, message->strafe_helper.wishdir.y);
  Race_Transport_WriteFloat(bytes + 84u, message->strafe_helper.wishdir.z);
  Race_Transport_WriteFloat(bytes + 88u, message->strafe_helper.wishspeed);
  Race_Transport_WriteFloat(bytes + 92u, message->strafe_helper.accel);
  Race_Transport_WriteFloat(bytes + 96u, message->strafe_helper.frametime);
  Race_Transport_WriteFloat(bytes + 100u, message->strafe_helper.view_yaw);
  return RACE_REPLAY_TELEMETRY_BYTES;
}

bool Race_ReplayTelemetry_Decode(
    const void *data, const size_t length,
    race_replay_telemetry_message_t *message) {
  if (!data || !message || length != RACE_REPLAY_TELEMETRY_BYTES) {
    return false;
  }
  const uint8_t *bytes = data;
  if (bytes[0] != RACE_REPLAY_TRANSPORT_VERSION || bytes[1] ||
      bytes[3] || bytes[49] || bytes[50] || bytes[51] || bytes[48] > 1u) {
    return false;
  }

  memset(message, 0, sizeof(*message));
  message->pm_type = (pm_type_t) bytes[2];
  message->generation = Race_Transport_Read32(bytes + 4u);
  message->sequence = Race_Transport_Read32(bytes + 8u);
  message->playhead_ms = Race_Transport_Read32(bytes + 12u);
  message->frame_cursor = Race_Transport_Read32(bytes + 16u);
  message->pm_flags = Race_Transport_Read16(bytes + 20u);
  message->input_flags = (int16_t) Race_Transport_Read16(bytes + 22u);
  message->origin = Vec3(Race_Transport_ReadFloat(bytes + 24u),
                         Race_Transport_ReadFloat(bytes + 28u),
                         Race_Transport_ReadFloat(bytes + 32u));
  message->velocity = Vec3(Race_Transport_ReadFloat(bytes + 36u),
                           Race_Transport_ReadFloat(bytes + 40u),
                           Race_Transport_ReadFloat(bytes + 44u));
  message->strafe_helper.active = bytes[48] != 0u;
  message->strafe_helper.forward = Vec3(
    Race_Transport_ReadFloat(bytes + 52u),
    Race_Transport_ReadFloat(bytes + 56u),
    Race_Transport_ReadFloat(bytes + 60u));
  message->strafe_helper.velocity = Vec3(
    Race_Transport_ReadFloat(bytes + 64u),
    Race_Transport_ReadFloat(bytes + 68u),
    Race_Transport_ReadFloat(bytes + 72u));
  message->strafe_helper.wishdir = Vec3(
    Race_Transport_ReadFloat(bytes + 76u),
    Race_Transport_ReadFloat(bytes + 80u),
    Race_Transport_ReadFloat(bytes + 84u));
  message->strafe_helper.wishspeed = Race_Transport_ReadFloat(bytes + 88u);
  message->strafe_helper.accel = Race_Transport_ReadFloat(bytes + 92u);
  message->strafe_helper.frametime = Race_Transport_ReadFloat(bytes + 96u);
  message->strafe_helper.view_yaw = Race_Transport_ReadFloat(bytes + 100u);
  return Race_ReplayTelemetry_Valid(message);
}

static void Race_ReplayProjectiles_EncodeEvent(
    const race_replay_projectile_event_t *event, uint8_t *bytes) {
  Race_Transport_Write32(bytes, event->time_ms);
  Race_Transport_Write16(bytes + 4u, event->id);
  bytes[6] = (uint8_t) event->kind;
  bytes[7] = (uint8_t) event->operation;
  Race_Transport_WriteFloat(bytes + 8u, event->origin.x);
  Race_Transport_WriteFloat(bytes + 12u, event->origin.y);
  Race_Transport_WriteFloat(bytes + 16u, event->origin.z);
  Race_Transport_WriteFloat(bytes + 20u, event->velocity.x);
  Race_Transport_WriteFloat(bytes + 24u, event->velocity.y);
  Race_Transport_WriteFloat(bytes + 28u, event->velocity.z);
  Race_Transport_WriteFloat(bytes + 32u, event->normal.x);
  Race_Transport_WriteFloat(bytes + 36u, event->normal.y);
  Race_Transport_WriteFloat(bytes + 40u, event->normal.z);
}

static void Race_ReplayProjectiles_DecodeEvent(
    const uint8_t *bytes, race_replay_projectile_event_t *event) {
  *event = (race_replay_projectile_event_t) {
    .time_ms = Race_Transport_Read32(bytes),
    .id = Race_Transport_Read16(bytes + 4u),
    .kind = (race_replay_projectile_kind_t) bytes[6],
    .operation = (race_replay_projectile_operation_t) bytes[7],
    .origin = Vec3(Race_Transport_ReadFloat(bytes + 8u),
                   Race_Transport_ReadFloat(bytes + 12u),
                   Race_Transport_ReadFloat(bytes + 16u)),
    .velocity = Vec3(Race_Transport_ReadFloat(bytes + 20u),
                     Race_Transport_ReadFloat(bytes + 24u),
                     Race_Transport_ReadFloat(bytes + 28u)),
    .normal = Vec3(Race_Transport_ReadFloat(bytes + 32u),
                   Race_Transport_ReadFloat(bytes + 36u),
                   Race_Transport_ReadFloat(bytes + 40u))
  };
}

static bool Race_ReplayProjectiles_EventValid(
    const race_replay_projectile_event_t *event) {
  return event && event->id &&
         event->kind >= RACE_REPLAY_PROJECTILE_ROCKET &&
         event->kind < RACE_REPLAY_PROJECTILE_KIND_TOTAL &&
         event->operation >= RACE_REPLAY_PROJECTILE_SPAWN &&
         event->operation < RACE_REPLAY_PROJECTILE_OPERATION_TOTAL &&
         isfinite(event->origin.x) && isfinite(event->origin.y) &&
         isfinite(event->origin.z) && isfinite(event->velocity.x) &&
         isfinite(event->velocity.y) && isfinite(event->velocity.z) &&
         isfinite(event->normal.x) && isfinite(event->normal.y) &&
         isfinite(event->normal.z);
}

static bool Race_ReplayProjectiles_Valid(
    const race_replay_projectile_message_t *message) {
  if (!message || !message->generation || !message->sequence ||
      message->op < RACE_REPLAY_PROJECTILE_MESSAGE_RESET ||
      message->op > RACE_REPLAY_PROJECTILE_MESSAGE_EVENTS ||
      message->event_count > RACE_REPLAY_PROJECTILE_MAX_MESSAGE_EVENTS) {
    return false;
  }
  if (message->op == RACE_REPLAY_PROJECTILE_MESSAGE_RESET) {
    return !message->event_count;
  }
  if (!message->event_count) {
    return false;
  }
  for (size_t i = 0u; i < message->event_count; i++) {
    const race_replay_projectile_event_t *event = message->events + i;
    if (!Race_ReplayProjectiles_EventValid(event) ||
        event->time_ms > message->playhead_ms ||
        (i && event->time_ms < message->events[i - 1u].time_ms) ||
        (message->op == RACE_REPLAY_PROJECTILE_MESSAGE_SNAPSHOT &&
         event->operation != RACE_REPLAY_PROJECTILE_SPAWN)) {
      return false;
    }
  }
  return true;
}

size_t Race_ReplayProjectiles_Encode(
    const race_replay_projectile_message_t *message,
    void *output, const size_t capacity) {
  if (!output || !Race_ReplayProjectiles_Valid(message)) {
    return 0u;
  }
  const size_t length = RACE_REPLAY_PROJECTILE_HEADER_BYTES +
                        message->event_count *
                          RACE_REPLAY_PROJECTILE_EVENT_BYTES;
  if (length > capacity || length > RACE_REPLAY_TRANSPORT_MAX_PAYLOAD) {
    return 0u;
  }
  uint8_t *bytes = output;
  memset(bytes, 0, length);
  bytes[0] = RACE_REPLAY_TRANSPORT_VERSION;
  bytes[1] = (uint8_t) message->op;
  bytes[2] = message->event_count;
  Race_Transport_Write32(bytes + 4u, message->generation);
  Race_Transport_Write32(bytes + 8u, message->sequence);
  Race_Transport_Write32(bytes + 12u, message->playhead_ms);
  for (size_t i = 0u; i < message->event_count; i++) {
    Race_ReplayProjectiles_EncodeEvent(
      message->events + i,
      bytes + RACE_REPLAY_PROJECTILE_HEADER_BYTES +
        i * RACE_REPLAY_PROJECTILE_EVENT_BYTES);
  }
  return length;
}

bool Race_ReplayProjectiles_Decode(
    const void *data, const size_t length,
    race_replay_projectile_message_t *message) {
  if (!data || !message || length < RACE_REPLAY_PROJECTILE_HEADER_BYTES ||
      length > RACE_REPLAY_TRANSPORT_MAX_PAYLOAD) {
    return false;
  }
  const uint8_t *bytes = data;
  const uint8_t event_count = bytes[2];
  if (bytes[0] != RACE_REPLAY_TRANSPORT_VERSION || bytes[3] ||
      event_count > RACE_REPLAY_PROJECTILE_MAX_MESSAGE_EVENTS ||
      length != RACE_REPLAY_PROJECTILE_HEADER_BYTES +
                event_count * RACE_REPLAY_PROJECTILE_EVENT_BYTES) {
    return false;
  }
  memset(message, 0, sizeof(*message));
  message->op = (race_replay_projectile_message_op_t) bytes[1];
  message->event_count = event_count;
  message->generation = Race_Transport_Read32(bytes + 4u);
  message->sequence = Race_Transport_Read32(bytes + 8u);
  message->playhead_ms = Race_Transport_Read32(bytes + 12u);
  for (size_t i = 0u; i < event_count; i++) {
    Race_ReplayProjectiles_DecodeEvent(
      bytes + RACE_REPLAY_PROJECTILE_HEADER_BYTES +
        i * RACE_REPLAY_PROJECTILE_EVENT_BYTES,
      message->events + i);
  }
  return Race_ReplayProjectiles_Valid(message);
}

static bool Race_Raceline_Valid(const race_raceline_message_t *message) {
  if (!message || !message->generation || !message->sequence ||
      message->op < RACE_RACELINE_MESSAGE_BEGIN ||
      message->op > RACE_RACELINE_MESSAGE_CLEAR) {
    return false;
  }
  if (message->op == RACE_RACELINE_MESSAGE_CLEAR) {
    return message->source == RACE_REPLAY_SOURCE_NONE && !message->rank &&
           !message->replay_id && !message->total_points &&
           !message->first_point && !message->point_count &&
           !message->duration_ms;
  }
  if (message->source <= RACE_REPLAY_SOURCE_NONE ||
      message->source >= RACE_REPLAY_SOURCE_TOTAL ||
      message->rank > 15u || !message->replay_id ||
      message->total_points < 2u ||
      message->total_points > RACE_REPLAY_RACELINE_MAX_POINTS ||
      !message->duration_ms) {
    return false;
  }
  if (message->op == RACE_RACELINE_MESSAGE_BEGIN) {
    return !message->first_point && !message->point_count;
  }
  if (message->op == RACE_RACELINE_MESSAGE_END) {
    return message->first_point == message->total_points &&
           !message->point_count;
  }
  if (!message->point_count ||
      message->point_count > RACE_REPLAY_RACELINE_CHUNK_MAX_POINTS ||
      (size_t) message->first_point + message->point_count >
        message->total_points) {
    return false;
  }
  for (size_t i = 0u; i < message->point_count; i++) {
    if (message->points[i].time_ms > message->duration_ms ||
        (i && message->points[i].time_ms <=
                message->points[i - 1u].time_ms) ||
        !Race_Transport_FiniteVec3(message->points[i].origin)) {
      return false;
    }
  }
  return true;
}

size_t Race_Raceline_Encode(const race_raceline_message_t *message,
                            void *output, const size_t capacity) {
  if (!output || !Race_Raceline_Valid(message)) {
    return 0u;
  }
  const size_t length = RACE_RACELINE_HEADER_BYTES +
                        message->point_count * RACE_RACELINE_POINT_BYTES;
  if (length > capacity || length > RACE_REPLAY_TRANSPORT_MAX_PAYLOAD) {
    return 0u;
  }
  uint8_t *bytes = output;
  memset(bytes, 0, length);
  bytes[0] = RACE_REPLAY_TRANSPORT_VERSION;
  bytes[1] = (uint8_t) message->op;
  bytes[2] = (uint8_t) message->source;
  bytes[3] = message->rank;
  Race_Transport_Write32(bytes + 4u, message->generation);
  Race_Transport_Write32(bytes + 8u, message->sequence);
  Race_Transport_Write64(bytes + 12u, message->replay_id);
  Race_Transport_Write16(bytes + 20u, message->total_points);
  Race_Transport_Write16(bytes + 22u, message->first_point);
  bytes[24] = message->point_count;
  Race_Transport_Write32(bytes + 28u, message->duration_ms);
  uint8_t *point_bytes = bytes + RACE_RACELINE_HEADER_BYTES;
  for (size_t i = 0u; i < message->point_count; i++) {
    Race_Transport_Write32(point_bytes, message->points[i].time_ms);
    Race_Transport_WriteFloat(point_bytes + 4u, message->points[i].origin.x);
    Race_Transport_WriteFloat(point_bytes + 8u, message->points[i].origin.y);
    Race_Transport_WriteFloat(point_bytes + 12u, message->points[i].origin.z);
    point_bytes += RACE_RACELINE_POINT_BYTES;
  }
  return length;
}

bool Race_Raceline_Decode(const void *data, const size_t length,
                          race_raceline_message_t *message) {
  if (!data || !message || length < RACE_RACELINE_HEADER_BYTES ||
      length > RACE_REPLAY_TRANSPORT_MAX_PAYLOAD) {
    return false;
  }
  const uint8_t *bytes = data;
  const size_t point_count = bytes[24];
  if (bytes[0] != RACE_REPLAY_TRANSPORT_VERSION ||
      bytes[25] || bytes[26] || bytes[27] ||
      point_count > RACE_REPLAY_RACELINE_CHUNK_MAX_POINTS ||
      length != RACE_RACELINE_HEADER_BYTES +
                point_count * RACE_RACELINE_POINT_BYTES) {
    return false;
  }
  memset(message, 0, sizeof(*message));
  message->op = (race_raceline_message_op_t) bytes[1];
  message->source = (race_replay_source_t) bytes[2];
  message->rank = bytes[3];
  message->generation = Race_Transport_Read32(bytes + 4u);
  message->sequence = Race_Transport_Read32(bytes + 8u);
  message->replay_id = Race_Transport_Read64(bytes + 12u);
  message->total_points = Race_Transport_Read16(bytes + 20u);
  message->first_point = Race_Transport_Read16(bytes + 22u);
  message->point_count = (uint8_t) point_count;
  message->duration_ms = Race_Transport_Read32(bytes + 28u);
  const uint8_t *point_bytes = bytes + RACE_RACELINE_HEADER_BYTES;
  for (size_t i = 0u; i < point_count; i++) {
    message->points[i].time_ms = Race_Transport_Read32(point_bytes);
    message->points[i].origin = Vec3(
      Race_Transport_ReadFloat(point_bytes + 4u),
      Race_Transport_ReadFloat(point_bytes + 8u),
      Race_Transport_ReadFloat(point_bytes + 12u));
    point_bytes += RACE_RACELINE_POINT_BYTES;
  }
  return Race_Raceline_Valid(message);
}

void Race_ReplayClientCache_Clear(race_replay_client_cache_t *cache) {
  if (cache) {
    memset(cache, 0, sizeof(*cache));
  }
}

static void Race_ReplayClientCache_NewGeneration(
  race_replay_client_cache_t *cache, const uint32_t generation) {
  Race_ReplayClientCache_Clear(cache);
  cache->generation = generation;
}

race_replay_transport_result_t Race_ReplayClientCache_ApplyState(
  race_replay_client_cache_t *cache, const void *data, const size_t length,
  const uint32_t received_time) {
  race_replay_state_message_t message;
  if (!cache || !Race_ReplayState_Decode(data, length, &message)) {
    return RACE_REPLAY_TRANSPORT_MALFORMED;
  }
  if (cache->generation && message.generation != cache->generation) {
    if (!Race_ReplayGeneration_Newer(message.generation, cache->generation)) {
      return RACE_REPLAY_TRANSPORT_STALE;
    }
    Race_ReplayClientCache_NewGeneration(cache, message.generation);
  } else if (!cache->generation) {
    cache->generation = message.generation;
  }
  if (cache->state_sequence &&
      !Race_ReplayGeneration_Newer(message.sequence,
                                   cache->state_sequence)) {
    return RACE_REPLAY_TRANSPORT_STALE;
  }
  cache->state = message;
  cache->state_sequence = message.sequence;
  cache->telemetry_valid = false;
  cache->state_received_time = received_time;
  return RACE_REPLAY_TRANSPORT_APPLIED;
}

race_replay_transport_result_t Race_ReplayClientCache_ApplyTelemetry(
    race_replay_client_cache_t *cache, const void *data,
    const size_t length) {
  race_replay_telemetry_message_t message;
  if (!cache || !Race_ReplayTelemetry_Decode(data, length, &message)) {
    return RACE_REPLAY_TRANSPORT_MALFORMED;
  }
  if (!cache->generation || message.generation != cache->generation ||
      message.sequence != cache->state_sequence ||
      !(cache->state.flags & RACE_REPLAY_STATE_ACTIVE) ||
      message.playhead_ms != cache->state.playhead_ms ||
      message.playhead_ms > cache->state.duration_ms) {
    return RACE_REPLAY_TRANSPORT_STALE;
  }
  cache->telemetry = message;
  cache->telemetry_sequence = message.sequence;
  cache->telemetry_valid = true;
  return RACE_REPLAY_TRANSPORT_APPLIED;
}

race_replay_transport_result_t Race_ReplayClientCache_ApplyProjectiles(
    race_replay_client_cache_t *cache, const void *data,
    const size_t length) {
  race_replay_projectile_message_t message;
  if (!cache || !Race_ReplayProjectiles_Decode(data, length, &message)) {
    return RACE_REPLAY_TRANSPORT_MALFORMED;
  }
  if (cache->generation && message.generation != cache->generation) {
    if (!Race_ReplayGeneration_Newer(message.generation, cache->generation)) {
      return RACE_REPLAY_TRANSPORT_STALE;
    }
    Race_ReplayClientCache_NewGeneration(cache, message.generation);
  } else if (!cache->generation) {
    cache->generation = message.generation;
  }
  if (cache->projectile_sequence &&
      !Race_ReplayGeneration_Newer(message.sequence,
                                   cache->projectile_sequence)) {
    return RACE_REPLAY_TRANSPORT_STALE;
  }

  if (message.op == RACE_REPLAY_PROJECTILE_MESSAGE_RESET) {
    cache->projectile_count = 0u;
    memset(cache->projectiles, 0, sizeof(cache->projectiles));
    cache->projectile_sequence = message.sequence;
    return RACE_REPLAY_TRANSPORT_APPLIED;
  }

  race_replay_projectile_event_t
    active[RACE_REPLAY_MAX_ACTIVE_PROJECTILES];
  size_t active_count = cache->projectile_count;
  memcpy(active, cache->projectiles, active_count * sizeof(*active));
  for (size_t i = 0u; i < message.event_count; i++) {
    const race_replay_projectile_event_t *event = message.events + i;
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
        return RACE_REPLAY_TRANSPORT_MALFORMED;
      }
      active[active_count++] = *event;
    } else {
      if (active_index == active_count ||
          active[active_index].kind != event->kind) {
        return RACE_REPLAY_TRANSPORT_MALFORMED;
      }
      active[active_index] = active[--active_count];
    }
  }

  cache->projectile_count = (uint16_t) active_count;
  memcpy(cache->projectiles, active, active_count * sizeof(*active));
  if (active_count < RACE_REPLAY_MAX_ACTIVE_PROJECTILES) {
    memset(cache->projectiles + active_count, 0,
           (RACE_REPLAY_MAX_ACTIVE_PROJECTILES - active_count) *
             sizeof(*cache->projectiles));
  }
  cache->projectile_sequence = message.sequence;
  return RACE_REPLAY_TRANSPORT_APPLIED;
}

static void Race_ReplayClientCache_ClearRaceline(
  race_replay_client_cache_t *cache) {
  cache->raceline_sequence = 0u;
  cache->raceline_source = RACE_REPLAY_SOURCE_NONE;
  cache->raceline_rank = 0u;
  cache->raceline_replay_id = 0u;
  cache->raceline_duration_ms = 0u;
  cache->raceline_total_points = 0u;
  cache->raceline_received_points = 0u;
  cache->raceline_receiving = false;
  cache->raceline_complete = false;
  memset(cache->raceline_points, 0, sizeof(cache->raceline_points));
}

race_replay_transport_result_t Race_ReplayClientCache_ApplyRaceline(
  race_replay_client_cache_t *cache, const void *data, const size_t length) {
  race_raceline_message_t message;
  if (!cache || !Race_Raceline_Decode(data, length, &message)) {
    return RACE_REPLAY_TRANSPORT_MALFORMED;
  }
  if (cache->generation && message.generation != cache->generation) {
    if (!Race_ReplayGeneration_Newer(message.generation, cache->generation)) {
      return RACE_REPLAY_TRANSPORT_STALE;
    }
    Race_ReplayClientCache_NewGeneration(cache, message.generation);
  } else if (!cache->generation) {
    cache->generation = message.generation;
  }

  if (message.op == RACE_RACELINE_MESSAGE_BEGIN) {
    if (cache->raceline_sequence &&
        !Race_ReplayGeneration_Newer(message.sequence,
                                     cache->raceline_sequence)) {
      return RACE_REPLAY_TRANSPORT_STALE;
    }
    Race_ReplayClientCache_ClearRaceline(cache);
    cache->raceline_sequence = message.sequence;
    cache->raceline_source = message.source;
    cache->raceline_rank = message.rank;
    cache->raceline_replay_id = message.replay_id;
    cache->raceline_duration_ms = message.duration_ms;
    cache->raceline_total_points = message.total_points;
    cache->raceline_receiving = true;
    return RACE_REPLAY_TRANSPORT_APPLIED;
  }

  if (message.op == RACE_RACELINE_MESSAGE_CLEAR) {
    if (cache->raceline_sequence &&
        !Race_ReplayGeneration_Newer(message.sequence,
                                     cache->raceline_sequence)) {
      return RACE_REPLAY_TRANSPORT_STALE;
    }
    Race_ReplayClientCache_ClearRaceline(cache);
    cache->raceline_sequence = message.sequence;
    return RACE_REPLAY_TRANSPORT_APPLIED;
  }

  const uint32_t expected_sequence = cache->raceline_sequence + 1u;
  const bool metadata_matches = cache->raceline_receiving &&
    message.sequence == expected_sequence && expected_sequence &&
    message.source == cache->raceline_source &&
    message.rank == cache->raceline_rank &&
    message.replay_id == cache->raceline_replay_id &&
    message.duration_ms == cache->raceline_duration_ms &&
    message.total_points == cache->raceline_total_points;
  if (!metadata_matches) {
    Race_ReplayClientCache_ClearRaceline(cache);
    return RACE_REPLAY_TRANSPORT_MALFORMED;
  }

  if (message.op == RACE_RACELINE_MESSAGE_CHUNK) {
    if (message.first_point != cache->raceline_received_points) {
      Race_ReplayClientCache_ClearRaceline(cache);
      return RACE_REPLAY_TRANSPORT_MALFORMED;
    }
    if (cache->raceline_received_points &&
        message.points[0].time_ms <=
          cache->raceline_points[cache->raceline_received_points - 1u].time_ms) {
      Race_ReplayClientCache_ClearRaceline(cache);
      return RACE_REPLAY_TRANSPORT_MALFORMED;
    }
    memcpy(cache->raceline_points + cache->raceline_received_points,
           message.points,
           message.point_count * sizeof(*message.points));
    cache->raceline_received_points += message.point_count;
    cache->raceline_sequence = message.sequence;
    return RACE_REPLAY_TRANSPORT_APPLIED;
  }

  if (message.first_point != cache->raceline_received_points ||
      cache->raceline_received_points != cache->raceline_total_points) {
    Race_ReplayClientCache_ClearRaceline(cache);
    return RACE_REPLAY_TRANSPORT_MALFORMED;
  }
  cache->raceline_sequence = message.sequence;
  cache->raceline_receiving = false;
  cache->raceline_complete = true;
  return RACE_REPLAY_TRANSPORT_APPLIED;
}

uint32_t Race_ReplayState_PresentationTime(
  const race_replay_state_message_t *state, const uint32_t received_time,
  const uint32_t now) {
  if (!state || !(state->flags & RACE_REPLAY_STATE_ACTIVE) ||
      (state->flags & RACE_REPLAY_STATE_PAUSED) || !state->sample_count) {
    return state ? state->playhead_ms : 0u;
  }
  uint32_t numerator, denominator;
  if (!Race_ReplaySpeed_Ratio(state->speed, &numerator, &denominator)) {
    return state->playhead_ms;
  }
  const uint64_t advance = (uint64_t) (now - received_time) * numerator /
                           denominator;
  const uint32_t maximum = Mini(
    state->duration_ms, state->samples[state->sample_count - 1u].time_ms);
  if (advance >= maximum - state->playhead_ms) {
    return maximum;
  }
  return state->playhead_ms + (uint32_t) advance;
}

static float Race_ReplayState_MixAngle(const float from, const float to,
                                       const float fraction) {
  float delta = fmodf(to - from, 360.f);
  if (delta > 180.f) {
    delta -= 360.f;
  } else if (delta < -180.f) {
    delta += 360.f;
  }
  return from + delta * fraction;
}

bool Race_ReplayState_Interpolate(
  const race_replay_state_message_t *state, const uint32_t time_ms,
  race_replay_pose_sample_t *pose) {
  if (!state || !pose || !(state->flags & RACE_REPLAY_STATE_ACTIVE) ||
      !state->sample_count) {
    return false;
  }
  if (time_ms <= state->samples[0].time_ms) {
    *pose = state->samples[0];
    pose->time_ms = time_ms;
    return true;
  }
  const size_t last = state->sample_count - 1u;
  if (time_ms >= state->samples[last].time_ms) {
    *pose = state->samples[last];
    pose->time_ms = time_ms;
    return true;
  }
  size_t next = 1u;
  while (next < state->sample_count &&
         state->samples[next].time_ms < time_ms) {
    next++;
  }
  if (next >= state->sample_count) {
    return false;
  }
  const race_replay_pose_sample_t *from = state->samples + next - 1u;
  const race_replay_pose_sample_t *to = state->samples + next;
  const float fraction = (float) (time_ms - from->time_ms) /
                         (float) (to->time_ms - from->time_ms);
  pose->time_ms = time_ms;
  pose->origin = Vec3_Mix(from->origin, to->origin, fraction);
  pose->view_angles = Vec3(
    Race_ReplayState_MixAngle(from->view_angles.x, to->view_angles.x,
                              fraction),
    Race_ReplayState_MixAngle(from->view_angles.y, to->view_angles.y,
                              fraction),
    Race_ReplayState_MixAngle(from->view_angles.z, to->view_angles.z,
                              fraction));
  return true;
}

static bool Race_ReplayRaceline_Sample(
  const race_raceline_point_t *points, const size_t count,
  const uint32_t time_ms, vec3_t *output) {
  if (time_ms <= points[0].time_ms) {
    *output = points[0].origin;
    return true;
  }
  if (time_ms >= points[count - 1u].time_ms) {
    *output = points[count - 1u].origin;
    return true;
  }
  size_t next = 1u;
  while (next < count && points[next].time_ms < time_ms) {
    next++;
  }
  if (next >= count) {
    return false;
  }
  const race_raceline_point_t *from = points + next - 1u;
  const race_raceline_point_t *to = points + next;
  const float fraction = (float) (time_ms - from->time_ms) /
                         (float) (to->time_ms - from->time_ms);
  *output = Vec3_Mix(from->origin, to->origin, fraction);
  return true;
}

size_t Race_ReplayRaceline_BuildWindow(
  const race_raceline_point_t *points, const size_t count,
  const uint32_t head_ms, const uint32_t trail_ms, const bool full,
  vec3_t *output, const size_t capacity) {
  if (!points || !output || count < 2u || count > capacity) {
    return 0u;
  }
  for (size_t i = 1u; i < count; i++) {
    if (points[i].time_ms <= points[i - 1u].time_ms) {
      return 0u;
    }
  }
  if (full) {
    for (size_t i = 0u; i < count; i++) {
      output[i] = points[i].origin;
    }
    return count;
  }

  const uint32_t head = Mini(head_ms, points[count - 1u].time_ms);
  const uint32_t tail = head > trail_ms ? head - trail_ms : 0u;
  size_t required = head == tail ? 1u : 2u;
  for (size_t i = 0u; i < count; i++) {
    if (points[i].time_ms > tail && points[i].time_ms < head) {
      required++;
    }
  }
  if (required > capacity) {
    return 0u;
  }
  size_t written = 0u;
  if (!Race_ReplayRaceline_Sample(points, count, tail,
                                  output + written++)) {
    return 0u;
  }
  for (size_t i = 0u; i < count; i++) {
    if (points[i].time_ms > tail && points[i].time_ms < head) {
      output[written++] = points[i].origin;
    }
  }
  if (head != tail &&
      !Race_ReplayRaceline_Sample(points, count, head,
                                  output + written++)) {
    return 0u;
  }
  return written;
}
