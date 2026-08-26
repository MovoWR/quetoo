/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/race/race_finish_report.h"
#include "game/race/race_leaderboard.h"
#include "game/race/race_map_state.h"
#include "game/race/race_physics.h"
#include "game/race/race_profile.h"
#include "game/race/race_replay_format.h"
#include "game/race/race_replay_transport.h"
#include "game/race/race_settings.h"

#ifdef main
#undef main
#endif

#define RACE_FIXTURE_UID "01234567-89ab-4cde-8f01-23456789abcd"
#define RACE_FIXTURE_DISPLAY_NAME "^1Runner"
#define RACE_FIXTURE_PROFILE_CREDENTIAL \
  "$argon2id$v=19$m=19456,t=2,p=1$" \
  "AAAAAAAAAAAAAAAAAAAAAA$" \
  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
#define RACE_FIXTURE_MAP "edge"
#define RACE_FIXTURE_PROFILE_FILE "profile.profile"
#define RACE_FIXTURE_MAP_STATE_FILE "map.state"
#define RACE_FIXTURE_SETTINGS_FILE "gset.cfg"
#define RACE_FIXTURE_REPLAY_FILE "replay.qrpl"
#define RACE_FIXTURE_WIRE_FILE "wire.bin"
#define RACE_FIXTURE_PATH_SIZE 1024u
#define RACE_FIXTURE_TEXT_SIZE (64u * 1024u)
#define RACE_FIXTURE_REPLAY_SIZE 4096u
#define RACE_FIXTURE_WIRE_SIZE 2048u
#define RACE_FIXTURE_WIRE_RECORD_HEADER_SIZE 8u
#define RACE_FIXTURE_WIRE_RECORDS 6u
#define RACE_FIXTURE_INPUT_STAT 23u
#define RACE_FIXTURE_PROFILE_BYTES 188u
#define RACE_FIXTURE_PROFILE_CRC32 UINT32_C(0x073e83a6)
#define RACE_FIXTURE_MAP_STATE_BYTES 251u
#define RACE_FIXTURE_MAP_STATE_CRC32 UINT32_C(0xfbf704c3)
#define RACE_FIXTURE_SETTINGS_BYTES 316u
#define RACE_FIXTURE_SETTINGS_CRC32 UINT32_C(0xbea7058c)
#define RACE_FIXTURE_REPLAY_ID UINT64_C(0xdba8d53d1dc24cab)
#define RACE_FIXTURE_REPLAY_BYTES 550u
#define RACE_FIXTURE_REPLAY_CRC32 UINT32_C(0x34c8c332)
#define RACE_FIXTURE_WIRE_BYTES 541u
#define RACE_FIXTURE_WIRE_CRC32 UINT32_C(0x73775f65)

typedef struct {
  char profile[RACE_PROFILE_SERIALIZED_MAX];
  size_t profile_length;
  char map_state[RACE_FIXTURE_TEXT_SIZE];
  size_t map_state_length;
  char settings[RACE_FIXTURE_TEXT_SIZE];
  size_t settings_length;
  uint8_t replay[RACE_FIXTURE_REPLAY_SIZE];
  size_t replay_length;
  uint8_t wire[RACE_FIXTURE_WIRE_SIZE];
  size_t wire_length;
} race_portability_fixture_t;

typedef struct {
  race_setting_id_t id;
  const char *value;
} race_fixture_setting_assignment_t;

static const race_fixture_setting_assignment_t race_fixture_settings[] = {
  { RACE_SETTING_CHECKPOINT_FEEDBACK, "silent" },
  { RACE_SETTING_FINISH_CUE_ENABLED, "0" },
  { RACE_SETTING_FINISH_CUE_GAIN, "50" },
  { RACE_SETTING_MAX_VOTES, "5" },
  { RACE_SETTING_VOTE_ALLOW_SPECTATORS, "1" },
  { RACE_SETTING_VOTE_MENU_CHOICES, "4" },
  { RACE_SETTING_VOTE_MENU_DURATION, "15" },
  { RACE_SETTING_VOTING_TIME, "45" },
  { RACE_SETTING_WEAPONS, "0" }
};

static bool Race_FixtureError(const char *message) {
  fprintf(stderr, "RACE_PORTABILITY_FIXTURE_FAIL %s\n",
          message ? message : "unknown");
  return false;
}

#define RACE_FIXTURE_REQUIRE(expression) \
  do { \
    if (!(expression)) { \
      return Race_FixtureError(#expression); \
    } \
  } while (0)

static uint32_t Race_FixtureCrc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; i++) {
    crc ^= bytes[i];
    for (size_t bit = 0; bit < 8u; bit++) {
      crc = (crc >> 1u) ^ (UINT32_C(0xedb88320) &
                           (uint32_t) -(int32_t) (crc & 1u));
    }
  }
  return ~crc;
}

static void Race_FixtureWrite16(uint8_t *output, uint16_t value) {
  output[0] = (uint8_t) value;
  output[1] = (uint8_t) (value >> 8u);
}

static uint16_t Race_FixtureRead16(const uint8_t *input) {
  return (uint16_t) input[0] | (uint16_t) input[1] << 8u;
}

static bool Race_FixtureAppendWireRecord(
    race_portability_fixture_t *fixture, const char tag[4],
    const void *payload, size_t payload_length) {
  RACE_FIXTURE_REQUIRE(fixture && tag && payload && payload_length &&
                       payload_length <= UINT16_MAX);
  RACE_FIXTURE_REQUIRE(fixture->wire_length <= sizeof(fixture->wire) &&
                       sizeof(fixture->wire) - fixture->wire_length >=
                         RACE_FIXTURE_WIRE_RECORD_HEADER_SIZE);
  RACE_FIXTURE_REQUIRE(payload_length <= sizeof(fixture->wire) -
                         fixture->wire_length -
                         RACE_FIXTURE_WIRE_RECORD_HEADER_SIZE);

  uint8_t *record = fixture->wire + fixture->wire_length;
  memcpy(record, tag, 4u);
  Race_FixtureWrite16(record + 4u, (uint16_t) payload_length);
  record[6] = 0u;
  record[7] = 0u;
  memcpy(record + RACE_FIXTURE_WIRE_RECORD_HEADER_SIZE,
         payload, payload_length);
  fixture->wire_length += RACE_FIXTURE_WIRE_RECORD_HEADER_SIZE +
                          payload_length;
  return true;
}

static bool Race_FixturePath(const char *directory, const char *name,
                             char path[RACE_FIXTURE_PATH_SIZE]) {
  if (!directory || !*directory || !name || !*name) {
    return false;
  }
  const int32_t length = snprintf(path, RACE_FIXTURE_PATH_SIZE,
                                  "%s/%s", directory, name);
  return length > 0 && (size_t) length < RACE_FIXTURE_PATH_SIZE;
}

static bool Race_FixtureWriteFile(const char *directory, const char *name,
                                  const void *data, size_t length) {
  char path[RACE_FIXTURE_PATH_SIZE];
  RACE_FIXTURE_REQUIRE(Race_FixturePath(directory, name, path));

  FILE *file = fopen(path, "wb");
  if (!file) {
    return Race_FixtureError("could not open fixture output");
  }

  const bool written = fwrite(data, 1u, length, file) == length;
  const bool closed = fclose(file) == 0;
  RACE_FIXTURE_REQUIRE(written && closed);
  return true;
}

static bool Race_FixtureReadFile(const char *directory, const char *name,
                                 void *data, size_t capacity,
                                 size_t *length) {
  char path[RACE_FIXTURE_PATH_SIZE];
  RACE_FIXTURE_REQUIRE(Race_FixturePath(directory, name, path));

  FILE *file = fopen(path, "rb");
  if (!file) {
    return Race_FixtureError("could not open fixture input");
  }

  bool valid = fseek(file, 0, SEEK_END) == 0;
  const long file_length = valid ? ftell(file) : -1;
  valid = valid && file_length >= 0 && (size_t) file_length <= capacity &&
          fseek(file, 0, SEEK_SET) == 0;
  if (valid) {
    valid = fread(data, 1u, (size_t) file_length, file) ==
            (size_t) file_length;
  }
  valid = fclose(file) == 0 && valid;
  RACE_FIXTURE_REQUIRE(valid);
  *length = (size_t) file_length;
  return true;
}

static race_replay_sample_t Race_FixtureReplaySample(uint32_t time_ms,
                                                      float value) {
  race_replay_sample_t sample = {
    .time_ms = time_ms,
    .delta_time_ms = time_ms ? RACE_REPLAY_TICK_MSEC : 0u,
    .pm_state = {
      .type = PM_HOOK_PULL,
      .flags = (uint16_t) (1u << ((time_ms / 25u) % 8u)),
      .time = (uint16_t) time_ms,
      .params = {
        .gravity = 800,
        .gravity_water = 0.5f,
        .accel_ground = 10.f,
        .accel_ground_slick = 5.f,
        .accel_air = 1.f,
        .accel_water = 4.f,
        .accel_spectator = 6.f,
        .accel_ladder = 8.f,
        .friction_ground = 6.f,
        .friction_ground_slick = 1.f,
        .friction_air = 0.1f,
        .friction_water = 1.f,
        .friction_spectator = 5.f,
        .friction_ladder = 10.f,
        .speed_ground = 320.f,
        .speed_air = 320.f,
        .speed_water = 160.f,
        .speed_ladder = 125.f,
        .speed_spectator = 500.f,
        .speed_stop = 100.f,
        .speed_jump = 270.f,
        .speed_ducked = 100.f,
        .speed_duck_stand = 200.f,
        .speed_water_jump = 350.f
      },
      .step_offset = value,
      .hook_length = (uint16_t) (time_ms + 10u)
    },
    .strafe_helper = {
      .active = time_ms != 0u,
      .wishspeed = value * 5.f,
      .accel = value * 6.f,
      .frametime = 0.025f,
      .view_yaw = value * 7.f
    }
  };

  sample.pm_state.origin = Vec3(value, value + 1.f, value + 2.f);
  sample.pm_state.velocity = Vec3(value * 2.f, value * 2.f + 1.f,
                                  value * 2.f + 2.f);
  sample.pm_state.view_angles = Vec3(value * 3.f, value * 3.f + 1.f,
                                     value * 3.f + 2.f);
  sample.pm_state.view_offset = Vec3(value * 4.f, value * 4.f + 1.f,
                                     value * 4.f + 2.f);
  sample.pm_state.delta_angles = Vec3(value * 5.f, value * 5.f + 1.f,
                                      value * 5.f + 2.f);
  sample.pm_state.hook_position = Vec3(value * 6.f, value * 6.f + 1.f,
                                       value * 6.f + 2.f);
  sample.strafe_helper.forward = Vec3(value, 0.f, 0.f);
  sample.strafe_helper.velocity = sample.pm_state.velocity;
  sample.strafe_helper.wishdir = Vec3(0.f, value, 0.f);

  for (size_t i = 0; i < MAX_STATS; i++) {
    sample.stats[i] = (int16_t) (i + (size_t) value);
  }
  sample.stats[RACE_FIXTURE_INPUT_STAT] = RACE_INPUT_FORMAT_V1 |
                                          RACE_INPUT_FORWARD |
                                          RACE_INPUT_RIGHT |
                                          RACE_INPUT_JUMP;
  for (size_t i = 0; i < MAX_INVENTORY; i++) {
    sample.inventory[i] = (int16_t) (i * 2u + (size_t) value);
  }
  return sample;
}

static bool Race_FixtureReplay(race_replay_t *replay,
                               race_replay_sample_t samples[3]) {
  int32_t player_uid;
  RACE_FIXTURE_REQUIRE(Race_Replay_ProfilePlayerUid(RACE_FIXTURE_UID,
                                                     &player_uid));
  RACE_FIXTURE_REQUIRE(Race_Replay_Init(replay, samples, 3u, NULL, 0u,
                                        "maps/Edge.BSP", RACE_FIXTURE_UID,
                                        "Runner", player_uid, 0u));
  replay->elapsed_time = 50u;
  replay->sample_count = 3u;
  samples[0] = Race_FixtureReplaySample(0u, 1.f);
  samples[0].strafe_helper.forward.z = -0.f;
  samples[1] = Race_FixtureReplaySample(25u, 2.f);
  samples[2] = Race_FixtureReplaySample(50u, 3.f);
  RACE_FIXTURE_REQUIRE(Race_Replay_Valid(replay));
  return true;
}

static bool Race_FixtureMapState(race_map_state_t *state,
                                 race_leaderboard_record_t records[1],
                                 uint64_t replay_id) {
  static const uint32_t checkpoints[] = { 15u, 35u };
  static const uint32_t splits[] = { 10u, 30u };

  RACE_FIXTURE_REQUIRE(Race_MapState_Init(
    state, records, 1u, "maps/Edge.BSP",
    RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY));
  state->format = RACE_MAP_STATE_FORMAT_V4;
  state->generation = 42u;
  RACE_FIXTURE_REQUIRE(Race_Leaderboard_RecordInit(
    records, RACE_FIXTURE_UID, RACE_FIXTURE_DISPLAY_NAME, 50u,
    checkpoints, sizeof(checkpoints) / sizeof(checkpoints[0])));
  RACE_FIXTURE_REQUIRE(Race_Leaderboard_RecordSetDate(
    records, UINT64_C(1722470400)));
  RACE_FIXTURE_REQUIRE(Race_Leaderboard_RecordSetSplits(
    records, splits, sizeof(splits) / sizeof(splits[0]),
    UINT64_C(0x123456789abcdef0)));
  RACE_FIXTURE_REQUIRE(Race_Leaderboard_RecordAttachReplay(records,
                                                            replay_id));
  state->record_count = 1u;
  RACE_FIXTURE_REQUIRE(Race_MapState_Valid(state));
  return true;
}

static bool Race_FixtureSettings(char *output, size_t output_size,
                                 size_t *output_length) {
  if (!output || !output_size || !output_length) {
    return false;
  }

  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  RACE_FIXTURE_REQUIRE(catalog && count == RACE_SETTING_TOTAL &&
                       Race_Settings_ValidateCatalog(catalog, count, NULL, 0));

  const int header = snprintf(output, output_size,
                              "# Race global cvar overrides\n");
  RACE_FIXTURE_REQUIRE(header > 0 && (size_t) header < output_size);
  size_t length = (size_t) header;

  for (size_t i = 0; i < sizeof(race_fixture_settings) /
                       sizeof(race_fixture_settings[0]); i++) {
    const race_fixture_setting_assignment_t *assignment =
      race_fixture_settings + i;
    RACE_FIXTURE_REQUIRE((size_t) assignment->id < count);
    const race_setting_descriptor_t *descriptor = catalog + assignment->id;
    char canonical[RACE_SETTING_VALUE_SIZE];
    RACE_FIXTURE_REQUIRE(Race_Settings_CanonicalizeValue(
      descriptor, assignment->value, canonical, sizeof(canonical), NULL, 0));
    const int written = snprintf(output + length, output_size - length,
                                 "set %s \"%s\"\n", descriptor->cvar,
                                 canonical);
    RACE_FIXTURE_REQUIRE(written > 0 && (size_t) written < output_size - length);
    length += (size_t) written;
  }

  *output_length = length;
  return true;
}

static bool Race_FixtureWire(race_portability_fixture_t *fixture) {
  memcpy(fixture->wire, "RQWC", 4u);
  fixture->wire[4] = 1u;
  fixture->wire[5] = RACE_FIXTURE_WIRE_RECORDS;
  fixture->wire[6] = 0u;
  fixture->wire[7] = 0u;
  fixture->wire_length = 8u;

  uint8_t payload[512];
  race_finish_report_t finish = {
    .mode = RACE_MODE_RACE,
    .invalid_flags = 0u,
    .publication_committed = true,
    .new_world_record = true,
    .elapsed_time = 45678u,
    .previous_pb = 47000u,
    .world_record = 45678u,
    .checkpoint_count = 2u,
    .checkpoint_times = { 12345u, 34567u },
    .start_speed = 320.5f,
    .end_speed = 512.25f,
    .top_speed = 888.75f,
    .average_speed = 456.125f
  };
  size_t length = Race_FinishReport_Encode(&finish, payload, sizeof(payload));
  RACE_FIXTURE_REQUIRE(length && Race_FixtureAppendWireRecord(
    fixture, "FINI", payload, length));

  race_replay_state_message_t state = {
    .flags = RACE_REPLAY_STATE_ACTIVE | RACE_REPLAY_STATE_DISCONTINUITY,
    .speed = RACE_REPLAY_SPEED_HALF,
    .source = RACE_REPLAY_SOURCE_WORLD_RECORD,
    .generation = UINT32_C(0x12345678),
    .sequence = UINT32_C(0x89abcdef),
    .replay_id = RACE_FIXTURE_REPLAY_ID,
    .duration_ms = 5000u,
    .playhead_ms = 2500u,
    .rank = 1u,
    .display_name = "^2World Record",
    .samples = {
      {
        .time_ms = 2000u,
        .origin = Vec3(1.25f, -2.5f, 3.75f),
        .view_angles = Vec3(4.5f, 90.25f, -6.75f)
      },
      {
        .time_ms = 2500u,
        .origin = Vec3(10.5f, 20.25f, 30.125f),
        .view_angles = Vec3(-12.5f, 180.5f, 2.25f)
      },
      {
        .time_ms = 3000u,
        .origin = Vec3(-100.75f, 200.5f, 16.25f),
        .view_angles = Vec3(1.5f, 270.75f, -3.25f)
      }
    },
    .sample_count = 3u
  };
  length = Race_ReplayState_Encode(&state, payload, sizeof(payload));
  RACE_FIXTURE_REQUIRE(length && Race_FixtureAppendWireRecord(
    fixture, "STAT", payload, length));

  race_replay_telemetry_message_t telemetry = {
    .generation = state.generation,
    .sequence = state.sequence + 1u,
    .playhead_ms = state.playhead_ms,
    .frame_cursor = 100u,
    .pm_type = PM_HOOK_PULL,
    .pm_flags = UINT16_C(0xa55a),
    .origin = Vec3(10.5f, 20.25f, 30.125f),
    .velocity = Vec3(320.5f, -64.25f, 128.75f),
    .input_flags = RACE_INPUT_FORMAT_V1 | RACE_INPUT_FORWARD |
                   RACE_INPUT_RIGHT | RACE_INPUT_JUMP,
    .strafe_helper = {
      .active = true,
      .forward = Vec3(0.5f, 0.75f, 0.25f),
      .velocity = Vec3(320.5f, -64.25f, 128.75f),
      .wishdir = Vec3(-0.25f, 0.875f, 0.125f),
      .wishspeed = 300.5f,
      .accel = 10.25f,
      .frametime = 0.025f,
      .view_yaw = 123.75f
    }
  };
  length = Race_ReplayTelemetry_Encode(&telemetry, payload, sizeof(payload));
  RACE_FIXTURE_REQUIRE(length && Race_FixtureAppendWireRecord(
    fixture, "TELE", payload, length));

  race_replay_projectile_message_t projectiles = {
    .op = RACE_REPLAY_PROJECTILE_MESSAGE_EVENTS,
    .generation = state.generation,
    .sequence = state.sequence + 2u,
    .playhead_ms = state.playhead_ms,
    .event_count = 2u,
    .events = {
      {
        .time_ms = 2400u,
        .id = UINT16_C(0x1234),
        .kind = RACE_REPLAY_PROJECTILE_ROCKET,
        .operation = RACE_REPLAY_PROJECTILE_SPAWN,
        .origin = Vec3(1.25f, 2.5f, 3.75f),
        .velocity = Vec3(900.5f, -10.25f, 20.75f),
        .normal = Vec3(0.f, 0.f, 1.f)
      },
      {
        .time_ms = 2450u,
        .id = UINT16_C(0x1234),
        .kind = RACE_REPLAY_PROJECTILE_ROCKET,
        .operation = RACE_REPLAY_PROJECTILE_IMPACT,
        .origin = Vec3(45.5f, 6.25f, 7.125f),
        .velocity = Vec3(0.f, 0.f, 0.f),
        .normal = Vec3(-0.5f, 0.25f, 0.75f)
      }
    }
  };
  length = Race_ReplayProjectiles_Encode(
    &projectiles, payload, sizeof(payload));
  RACE_FIXTURE_REQUIRE(length && Race_FixtureAppendWireRecord(
    fixture, "PROJ", payload, length));

  race_raceline_message_t raceline = {
    .op = RACE_RACELINE_MESSAGE_CHUNK,
    .source = RACE_REPLAY_SOURCE_WORLD_RECORD,
    .rank = 1u,
    .generation = state.generation,
    .sequence = state.sequence + 3u,
    .replay_id = RACE_FIXTURE_REPLAY_ID,
    .total_points = 5u,
    .first_point = 1u,
    .point_count = 3u,
    .duration_ms = state.duration_ms,
    .points = {
      { .time_ms = 1000u, .origin = Vec3(1.5f, 2.25f, 3.125f) },
      { .time_ms = 2500u, .origin = Vec3(10.75f, 20.5f, 30.25f) },
      { .time_ms = 4000u, .origin = Vec3(-5.5f, 64.125f, 128.25f) }
    }
  };
  length = Race_Raceline_Encode(&raceline, payload, sizeof(payload));
  RACE_FIXTURE_REQUIRE(length && Race_FixtureAppendWireRecord(
    fixture, "LINE", payload, length));

  race_physics_config_t physics;
  char physics_string[RACE_PHYSICS_CONFIG_STRING_SIZE];
  RACE_FIXTURE_REQUIRE(Race_Physics_ConfigForPresetKey(
    RACE_PHYSICS_PRESET_Q2_V1_KEY, &physics));
  RACE_FIXTURE_REQUIRE(Race_Physics_Encode(&physics, physics_string));
  length = strlen(physics_string) + 1u;
  RACE_FIXTURE_REQUIRE(Race_FixtureAppendWireRecord(
    fixture, "PHYS", physics_string, length));
  return true;
}

static bool Race_FixtureGenerate(race_portability_fixture_t *fixture) {
  memset(fixture, 0, sizeof(*fixture));

  race_profile_t profile;
  RACE_FIXTURE_REQUIRE(Race_Profile_Init(&profile, RACE_FIXTURE_UID,
                                         RACE_FIXTURE_DISPLAY_NAME));
  RACE_FIXTURE_REQUIRE(Race_Profile_SetCredential(
    &profile, RACE_FIXTURE_PROFILE_CREDENTIAL));
  RACE_FIXTURE_REQUIRE(Race_Profile_Serialize(
    &profile, fixture->profile, sizeof(fixture->profile),
    &fixture->profile_length));

  race_replay_sample_t samples[3];
  race_replay_t replay;
  RACE_FIXTURE_REQUIRE(Race_FixtureReplay(&replay, samples));
  uint64_t replay_id;
  RACE_FIXTURE_REQUIRE(Race_Replay_Serialize(
    &replay, fixture->replay, sizeof(fixture->replay),
    &fixture->replay_length, &replay_id));
  RACE_FIXTURE_REQUIRE(replay_id == RACE_FIXTURE_REPLAY_ID);
  RACE_FIXTURE_REQUIRE(fixture->replay_length == RACE_FIXTURE_REPLAY_BYTES);

  race_leaderboard_record_t records[1];
  race_map_state_t map_state;
  RACE_FIXTURE_REQUIRE(Race_FixtureMapState(&map_state, records, replay_id));
  RACE_FIXTURE_REQUIRE(Race_MapState_Serialize(
    &map_state, fixture->map_state, sizeof(fixture->map_state),
    &fixture->map_state_length));

  RACE_FIXTURE_REQUIRE(Race_FixtureSettings(
    fixture->settings, sizeof(fixture->settings), &fixture->settings_length));
  RACE_FIXTURE_REQUIRE(Race_FixtureWire(fixture));
  return true;
}

static bool Race_FixtureVerifyProfile(const race_portability_fixture_t *fixture) {
  race_profile_t profile;
  RACE_FIXTURE_REQUIRE(Race_Profile_Parse(
    fixture->profile, fixture->profile_length, &profile) ==
    RACE_PROFILE_PARSE_OK);
  RACE_FIXTURE_REQUIRE(strcmp(profile.uid, RACE_FIXTURE_UID) == 0);
  RACE_FIXTURE_REQUIRE(strcmp(profile.display_name,
                              RACE_FIXTURE_DISPLAY_NAME) == 0);
  RACE_FIXTURE_REQUIRE(strcmp(profile.credential,
                              RACE_FIXTURE_PROFILE_CREDENTIAL) == 0);

  char serialized[RACE_PROFILE_SERIALIZED_MAX];
  size_t length;
  RACE_FIXTURE_REQUIRE(Race_Profile_Serialize(
    &profile, serialized, sizeof(serialized), &length));
  RACE_FIXTURE_REQUIRE(length == fixture->profile_length);
  RACE_FIXTURE_REQUIRE(memcmp(serialized, fixture->profile, length) == 0);
  return true;
}

static bool Race_FixtureVerifyReplay(const race_portability_fixture_t *fixture) {
  race_replay_sample_t samples[3];
  race_replay_t replay;
  RACE_FIXTURE_REQUIRE(Race_Replay_Parse(
    fixture->replay, fixture->replay_length, samples,
    sizeof(samples) / sizeof(samples[0]), NULL, 0u, &replay) ==
    RACE_REPLAY_PARSE_OK);
  RACE_FIXTURE_REQUIRE(replay.replay_id == RACE_FIXTURE_REPLAY_ID);
  RACE_FIXTURE_REQUIRE(replay.sample_count == 3u);
  RACE_FIXTURE_REQUIRE(replay.elapsed_time == 50u);
  RACE_FIXTURE_REQUIRE(strcmp(replay.map, RACE_FIXTURE_MAP) == 0);
  RACE_FIXTURE_REQUIRE(strcmp(replay.player_name, "Runner") == 0);

  race_replay_sample_t expected_samples[3];
  race_replay_t expected;
  RACE_FIXTURE_REQUIRE(Race_FixtureReplay(&expected, expected_samples));
  RACE_FIXTURE_REQUIRE(Race_Replay_Equals(&replay, &expected));
  return true;
}

static bool Race_FixtureVerifyMapState(
    const race_portability_fixture_t *fixture) {
  race_leaderboard_record_t records[1];
  race_map_state_t state;
  RACE_FIXTURE_REQUIRE(Race_MapState_Parse(
    fixture->map_state, fixture->map_state_length, records,
    sizeof(records) / sizeof(records[0]), &state) ==
    RACE_MAP_STATE_PARSE_OK);
  RACE_FIXTURE_REQUIRE(state.format == RACE_MAP_STATE_FORMAT_V4);
  RACE_FIXTURE_REQUIRE(state.generation == 42u);
  RACE_FIXTURE_REQUIRE(strcmp(state.map, RACE_FIXTURE_MAP) == 0);
  RACE_FIXTURE_REQUIRE(strcmp(state.ruleset,
                              RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY) == 0);
  RACE_FIXTURE_REQUIRE(state.record_count == 1u);
  RACE_FIXTURE_REQUIRE(strcmp(records[0].uid, RACE_FIXTURE_UID) == 0);
  RACE_FIXTURE_REQUIRE(records[0].elapsed_time == 50u);
  RACE_FIXTURE_REQUIRE(records[0].date_unix_s == UINT64_C(1722470400));
  RACE_FIXTURE_REQUIRE(records[0].replay_id == RACE_FIXTURE_REPLAY_ID);
  RACE_FIXTURE_REQUIRE(records[0].split_layout ==
                       UINT64_C(0x123456789abcdef0));

  char serialized[RACE_FIXTURE_TEXT_SIZE];
  size_t length;
  RACE_FIXTURE_REQUIRE(Race_MapState_Serialize(
    &state, serialized, sizeof(serialized), &length));
  RACE_FIXTURE_REQUIRE(length == fixture->map_state_length);
  RACE_FIXTURE_REQUIRE(memcmp(serialized, fixture->map_state, length) == 0);
  return true;
}

static bool Race_FixtureVerifySettings(
    const race_portability_fixture_t *fixture) {
  char expected[RACE_FIXTURE_TEXT_SIZE];
  size_t length;
  RACE_FIXTURE_REQUIRE(Race_FixtureSettings(expected, sizeof(expected), &length));
  RACE_FIXTURE_REQUIRE(length == fixture->settings_length);
  RACE_FIXTURE_REQUIRE(memcmp(expected, fixture->settings, length) == 0);
  return true;
}

static bool Race_FixtureNextWireRecord(
    const race_portability_fixture_t *fixture, size_t *offset,
    const char tag[4], const uint8_t **payload, size_t *payload_length) {
  RACE_FIXTURE_REQUIRE(fixture && offset && tag && payload && payload_length);
  RACE_FIXTURE_REQUIRE(*offset <= fixture->wire_length &&
                       fixture->wire_length - *offset >=
                         RACE_FIXTURE_WIRE_RECORD_HEADER_SIZE);

  const uint8_t *record = fixture->wire + *offset;
  const size_t length = Race_FixtureRead16(record + 4u);
  RACE_FIXTURE_REQUIRE(memcmp(record, tag, 4u) == 0 &&
                       !record[6] && !record[7] && length &&
                       length <= fixture->wire_length - *offset -
                         RACE_FIXTURE_WIRE_RECORD_HEADER_SIZE);
  *payload = record + RACE_FIXTURE_WIRE_RECORD_HEADER_SIZE;
  *payload_length = length;
  *offset += RACE_FIXTURE_WIRE_RECORD_HEADER_SIZE + length;
  return true;
}

static bool Race_FixtureVerifyFinishWire(const uint8_t *payload,
                                         size_t length) {
  race_finish_report_t finish;
  RACE_FIXTURE_REQUIRE(Race_FinishReport_Decode(payload, length, &finish));
  RACE_FIXTURE_REQUIRE(finish.mode == RACE_MODE_RACE &&
                       finish.invalid_flags == 0u &&
                       finish.publication_committed &&
                       finish.new_world_record &&
                       finish.elapsed_time == 45678u &&
                       finish.world_record == 45678u &&
                       finish.checkpoint_count == 2u &&
                       finish.checkpoint_times[0] == 12345u &&
                       finish.checkpoint_times[1] == 34567u);
  uint8_t encoded[512];
  const size_t encoded_length = Race_FinishReport_Encode(
    &finish, encoded, sizeof(encoded));
  RACE_FIXTURE_REQUIRE(encoded_length == length &&
                       memcmp(encoded, payload, length) == 0);
  return true;
}

static bool Race_FixtureVerifyStateWire(const uint8_t *payload,
                                        size_t length) {
  race_replay_state_message_t state;
  RACE_FIXTURE_REQUIRE(Race_ReplayState_Decode(payload, length, &state));
  RACE_FIXTURE_REQUIRE(state.flags ==
                         (RACE_REPLAY_STATE_ACTIVE |
                          RACE_REPLAY_STATE_DISCONTINUITY) &&
                       state.source == RACE_REPLAY_SOURCE_WORLD_RECORD &&
                       state.replay_id == RACE_FIXTURE_REPLAY_ID &&
                       state.playhead_ms == 2500u &&
                       state.sample_count == 3u &&
                       !strcmp(state.display_name, "^2World Record"));
  uint8_t encoded[512];
  const size_t encoded_length = Race_ReplayState_Encode(
    &state, encoded, sizeof(encoded));
  RACE_FIXTURE_REQUIRE(encoded_length == length &&
                       memcmp(encoded, payload, length) == 0);
  return true;
}

static bool Race_FixtureVerifyTelemetryWire(const uint8_t *payload,
                                            size_t length) {
  race_replay_telemetry_message_t telemetry;
  RACE_FIXTURE_REQUIRE(Race_ReplayTelemetry_Decode(
    payload, length, &telemetry));
  RACE_FIXTURE_REQUIRE(telemetry.generation == UINT32_C(0x12345678) &&
                       telemetry.pm_type == PM_HOOK_PULL &&
                       telemetry.input_flags ==
                         (RACE_INPUT_FORMAT_V1 | RACE_INPUT_FORWARD |
                          RACE_INPUT_RIGHT | RACE_INPUT_JUMP) &&
                       telemetry.strafe_helper.active &&
                       telemetry.strafe_helper.frametime == 0.025f);
  uint8_t encoded[512];
  const size_t encoded_length = Race_ReplayTelemetry_Encode(
    &telemetry, encoded, sizeof(encoded));
  RACE_FIXTURE_REQUIRE(encoded_length == length &&
                       memcmp(encoded, payload, length) == 0);
  return true;
}

static bool Race_FixtureVerifyProjectilesWire(const uint8_t *payload,
                                              size_t length) {
  race_replay_projectile_message_t projectiles;
  RACE_FIXTURE_REQUIRE(Race_ReplayProjectiles_Decode(
    payload, length, &projectiles));
  RACE_FIXTURE_REQUIRE(
    projectiles.op == RACE_REPLAY_PROJECTILE_MESSAGE_EVENTS &&
    projectiles.event_count == 2u &&
    projectiles.events[0].operation == RACE_REPLAY_PROJECTILE_SPAWN &&
    projectiles.events[1].operation == RACE_REPLAY_PROJECTILE_IMPACT &&
    projectiles.events[1].time_ms == 2450u);
  uint8_t encoded[512];
  const size_t encoded_length = Race_ReplayProjectiles_Encode(
    &projectiles, encoded, sizeof(encoded));
  RACE_FIXTURE_REQUIRE(encoded_length == length &&
                       memcmp(encoded, payload, length) == 0);
  return true;
}

static bool Race_FixtureVerifyRacelineWire(const uint8_t *payload,
                                           size_t length) {
  race_raceline_message_t raceline;
  RACE_FIXTURE_REQUIRE(Race_Raceline_Decode(payload, length, &raceline));
  RACE_FIXTURE_REQUIRE(raceline.op == RACE_RACELINE_MESSAGE_CHUNK &&
                       raceline.source == RACE_REPLAY_SOURCE_WORLD_RECORD &&
                       raceline.replay_id == RACE_FIXTURE_REPLAY_ID &&
                       raceline.total_points == 5u &&
                       raceline.first_point == 1u &&
                       raceline.point_count == 3u &&
                       raceline.points[1].time_ms == 2500u);
  uint8_t encoded[512];
  const size_t encoded_length = Race_Raceline_Encode(
    &raceline, encoded, sizeof(encoded));
  RACE_FIXTURE_REQUIRE(encoded_length == length &&
                       memcmp(encoded, payload, length) == 0);
  return true;
}

static bool Race_FixtureVerifyPhysicsWire(const uint8_t *payload,
                                          size_t length) {
  RACE_FIXTURE_REQUIRE(length > 1u &&
                       length <= RACE_PHYSICS_CONFIG_STRING_SIZE &&
                       payload[length - 1u] == '\0' &&
                       strlen((const char *) payload) + 1u == length);
  race_physics_config_t physics;
  RACE_FIXTURE_REQUIRE(Race_Physics_Decode(
    (const char *) payload, &physics) == RACE_PHYSICS_PARSE_OK);
  race_physics_config_t expected;
  RACE_FIXTURE_REQUIRE(Race_Physics_ConfigForPresetKey(
    RACE_PHYSICS_PRESET_Q2_V1_KEY, &expected));
  RACE_FIXTURE_REQUIRE(Race_Physics_ConfigEquals(&physics, &expected));
  char encoded[RACE_PHYSICS_CONFIG_STRING_SIZE];
  RACE_FIXTURE_REQUIRE(Race_Physics_Encode(&physics, encoded) &&
                       strlen(encoded) + 1u == length &&
                       memcmp(encoded, payload, length) == 0);
  return true;
}

static bool Race_FixtureVerifyWire(const race_portability_fixture_t *fixture) {
  RACE_FIXTURE_REQUIRE(fixture->wire_length >= 8u &&
                       !memcmp(fixture->wire, "RQWC", 4u) &&
                       fixture->wire[4] == 1u &&
                       fixture->wire[5] == RACE_FIXTURE_WIRE_RECORDS &&
                       !fixture->wire[6] && !fixture->wire[7]);

  size_t offset = 8u;
  const uint8_t *payload;
  size_t length;
  RACE_FIXTURE_REQUIRE(Race_FixtureNextWireRecord(
    fixture, &offset, "FINI", &payload, &length));
  RACE_FIXTURE_REQUIRE(Race_FixtureVerifyFinishWire(payload, length));
  RACE_FIXTURE_REQUIRE(Race_FixtureNextWireRecord(
    fixture, &offset, "STAT", &payload, &length));
  RACE_FIXTURE_REQUIRE(Race_FixtureVerifyStateWire(payload, length));
  RACE_FIXTURE_REQUIRE(Race_FixtureNextWireRecord(
    fixture, &offset, "TELE", &payload, &length));
  RACE_FIXTURE_REQUIRE(Race_FixtureVerifyTelemetryWire(payload, length));
  RACE_FIXTURE_REQUIRE(Race_FixtureNextWireRecord(
    fixture, &offset, "PROJ", &payload, &length));
  RACE_FIXTURE_REQUIRE(Race_FixtureVerifyProjectilesWire(payload, length));
  RACE_FIXTURE_REQUIRE(Race_FixtureNextWireRecord(
    fixture, &offset, "LINE", &payload, &length));
  RACE_FIXTURE_REQUIRE(Race_FixtureVerifyRacelineWire(payload, length));
  RACE_FIXTURE_REQUIRE(Race_FixtureNextWireRecord(
    fixture, &offset, "PHYS", &payload, &length));
  RACE_FIXTURE_REQUIRE(Race_FixtureVerifyPhysicsWire(payload, length));
  RACE_FIXTURE_REQUIRE(offset == fixture->wire_length);

  const uint32_t crc = Race_FixtureCrc32(
    fixture->wire, fixture->wire_length);
  if (fixture->wire_length != RACE_FIXTURE_WIRE_BYTES ||
      crc != RACE_FIXTURE_WIRE_CRC32) {
    fprintf(stderr, "RACE_PORTABILITY_WIRE_ACTUAL bytes=%zu crc32=%08x\n",
            fixture->wire_length, crc);

    size_t record_offset = 8u;
    while (record_offset + RACE_FIXTURE_WIRE_RECORD_HEADER_SIZE <=
           fixture->wire_length) {
      const uint8_t *record = fixture->wire + record_offset;
      const size_t record_length = Race_FixtureRead16(record + 4u);
      fprintf(stderr, "RACE_PORTABILITY_WIRE_RECORD tag=%.4s bytes=%zu\n",
              (const char *) record, record_length);
      if (record_length > fixture->wire_length - record_offset -
                            RACE_FIXTURE_WIRE_RECORD_HEADER_SIZE) {
        break;
      }
      record_offset += RACE_FIXTURE_WIRE_RECORD_HEADER_SIZE + record_length;
    }
    return false;
  }
  return true;
}

static bool Race_FixtureVerify(const race_portability_fixture_t *fixture) {
  RACE_FIXTURE_REQUIRE(sizeof(float) == 4u && FLT_RADIX == 2);
  RACE_FIXTURE_REQUIRE(fixture->profile_length == RACE_FIXTURE_PROFILE_BYTES);
  RACE_FIXTURE_REQUIRE(Race_FixtureCrc32(
    fixture->profile, fixture->profile_length) == RACE_FIXTURE_PROFILE_CRC32);
  RACE_FIXTURE_REQUIRE(fixture->map_state_length ==
                       RACE_FIXTURE_MAP_STATE_BYTES);
  RACE_FIXTURE_REQUIRE(Race_FixtureCrc32(
    fixture->map_state, fixture->map_state_length) ==
    RACE_FIXTURE_MAP_STATE_CRC32);
  RACE_FIXTURE_REQUIRE(fixture->settings_length ==
                       RACE_FIXTURE_SETTINGS_BYTES);
  RACE_FIXTURE_REQUIRE(Race_FixtureCrc32(
    fixture->settings, fixture->settings_length) ==
    RACE_FIXTURE_SETTINGS_CRC32);
  RACE_FIXTURE_REQUIRE(fixture->replay_length == RACE_FIXTURE_REPLAY_BYTES);
  RACE_FIXTURE_REQUIRE(Race_FixtureCrc32(
    fixture->replay, fixture->replay_length) == RACE_FIXTURE_REPLAY_CRC32);
  RACE_FIXTURE_REQUIRE(Race_FixtureVerifyProfile(fixture));
  RACE_FIXTURE_REQUIRE(Race_FixtureVerifyReplay(fixture));
  RACE_FIXTURE_REQUIRE(Race_FixtureVerifyMapState(fixture));
  RACE_FIXTURE_REQUIRE(Race_FixtureVerifySettings(fixture));
  RACE_FIXTURE_REQUIRE(Race_FixtureVerifyWire(fixture));
  return true;
}

static bool Race_FixtureWrite(const char *directory,
                              const race_portability_fixture_t *fixture) {
  RACE_FIXTURE_REQUIRE(Race_FixtureWriteFile(
    directory, RACE_FIXTURE_PROFILE_FILE,
    fixture->profile, fixture->profile_length));
  RACE_FIXTURE_REQUIRE(Race_FixtureWriteFile(
    directory, RACE_FIXTURE_MAP_STATE_FILE,
    fixture->map_state, fixture->map_state_length));
  RACE_FIXTURE_REQUIRE(Race_FixtureWriteFile(
    directory, RACE_FIXTURE_SETTINGS_FILE,
    fixture->settings, fixture->settings_length));
  RACE_FIXTURE_REQUIRE(Race_FixtureWriteFile(
    directory, RACE_FIXTURE_REPLAY_FILE,
    fixture->replay, fixture->replay_length));
  RACE_FIXTURE_REQUIRE(Race_FixtureWriteFile(
    directory, RACE_FIXTURE_WIRE_FILE,
    fixture->wire, fixture->wire_length));
  return true;
}

static bool Race_FixtureRead(const char *directory,
                             race_portability_fixture_t *fixture) {
  memset(fixture, 0, sizeof(*fixture));
  RACE_FIXTURE_REQUIRE(Race_FixtureReadFile(
    directory, RACE_FIXTURE_PROFILE_FILE,
    fixture->profile, sizeof(fixture->profile), &fixture->profile_length));
  RACE_FIXTURE_REQUIRE(Race_FixtureReadFile(
    directory, RACE_FIXTURE_MAP_STATE_FILE,
    fixture->map_state, sizeof(fixture->map_state), &fixture->map_state_length));
  RACE_FIXTURE_REQUIRE(Race_FixtureReadFile(
    directory, RACE_FIXTURE_SETTINGS_FILE,
    fixture->settings, sizeof(fixture->settings), &fixture->settings_length));
  RACE_FIXTURE_REQUIRE(Race_FixtureReadFile(
    directory, RACE_FIXTURE_REPLAY_FILE,
    fixture->replay, sizeof(fixture->replay), &fixture->replay_length));
  RACE_FIXTURE_REQUIRE(Race_FixtureReadFile(
    directory, RACE_FIXTURE_WIRE_FILE,
    fixture->wire, sizeof(fixture->wire), &fixture->wire_length));
  return true;
}

static void Race_FixturePrint(const race_portability_fixture_t *fixture) {
  printf("RACE_PORTABILITY_FIXTURE_PASS "
         "profile_bytes=%zu profile_crc32=%08x "
         "map_state_bytes=%zu map_state_crc32=%08x "
         "settings_bytes=%zu settings_crc32=%08x "
         "replay_bytes=%zu replay_crc32=%08x "
         "wire_bytes=%zu wire_crc32=%08x "
         "replay_id=%016llx\n",
         fixture->profile_length,
         Race_FixtureCrc32(fixture->profile, fixture->profile_length),
         fixture->map_state_length,
         Race_FixtureCrc32(fixture->map_state, fixture->map_state_length),
         fixture->settings_length,
         Race_FixtureCrc32(fixture->settings, fixture->settings_length),
         fixture->replay_length,
         Race_FixtureCrc32(fixture->replay, fixture->replay_length),
         fixture->wire_length,
         Race_FixtureCrc32(fixture->wire, fixture->wire_length),
         (unsigned long long) RACE_FIXTURE_REPLAY_ID);
}

static void Race_FixtureUsage(const char *program) {
  fprintf(stderr, "Usage: %s --write DIRECTORY | --verify DIRECTORY\n",
          program ? program : "race_portability_fixture");
}

int main(int argc, char **argv) {
  if (argc != 3 || (strcmp(argv[1], "--write") &&
                    strcmp(argv[1], "--verify"))) {
    Race_FixtureUsage(argc > 0 ? argv[0] : NULL);
    return EXIT_FAILURE;
  }

  race_portability_fixture_t fixture;
  if (!strcmp(argv[1], "--write")) {
    if (!Race_FixtureGenerate(&fixture) || !Race_FixtureVerify(&fixture) ||
        !Race_FixtureWrite(argv[2], &fixture)) {
      return EXIT_FAILURE;
    }
  } else if (!Race_FixtureRead(argv[2], &fixture) ||
             !Race_FixtureVerify(&fixture)) {
    return EXIT_FAILURE;
  }

  Race_FixturePrint(&fixture);
  return EXIT_SUCCESS;
}
