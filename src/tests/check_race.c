/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <check.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#define Race_TestGetCwd _getcwd
#define Race_TestGetPid _getpid
#define Race_TestMkdir(path) _mkdir(path)
#define Race_TestRmdir(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define Race_TestGetCwd getcwd
#define Race_TestGetPid getpid
#define Race_TestMkdir(path) mkdir(path, 0700)
#define Race_TestRmdir(path) rmdir(path)
#endif

#include "cg_race_presentation.h"
#include "cg_input_viewer_math.h"
#include "cg_strafe_helper_math.h"
#include "race_admin.h"
#include "race_admin_store.h"
#include "race_finish_report.h"
#include "race_logic.h"
#include "race_leaderboard.h"
#include "race_leaderboard_wire.h"
#include "race_map_state.h"
#include "race_map_state_store.h"
#include "race_persistence.h"
#include "race_physics.h"
#include "race_publication.h"
#include "race_profile.h"
#include "race_replay_format.h"
#include "race_replay_playback.h"
#include "race_replay_record.h"
#include "race_replay_store.h"
#include "race_replay_transport.h"
#include "race_settings.h"
#include "race_settings_store.h"
#include "race_vote.h"
#include "race_vote_menu.h"
#include "miniz.h"
#include "race_wire.h"

#define RACE_TEST_LENGTHOF(a) (sizeof(a) / sizeof(*(a)))

void Race_MapBrowser_AddTests(TCase *tcase);

#define RACE_TEST_UID_A "01234567-89ab-4cde-8f01-23456789abcd"
#define RACE_TEST_UID_B "fedcba98-7654-4321-a012-fedcba987654"
#define RACE_TEST_PATH_SIZE 1024
// g_stat_t slot serialized by QRPL v1 for the Race input contract.
#define RACE_TEST_INPUT_STAT 23u
#define RACE_TEST_HEALTH_STAT 7u
#define RACE_TEST_ADMIN_CAPABILITIES_STAT 28u
_Static_assert(RACE_TEST_INPUT_STAT < MAX_STATS,
               "Race input stat must fit QRPL v1 stats");

static char race_test_directory[RACE_TEST_PATH_SIZE];
static char race_test_committed[RACE_TEST_PATH_SIZE];
static char race_test_candidate[RACE_TEST_PATH_SIZE];
static char race_test_map_committed[RACE_TEST_PATH_SIZE];
static char race_test_map_candidate[RACE_TEST_PATH_SIZE];
static char race_test_admin_committed[RACE_TEST_PATH_SIZE];
static char race_test_admin_candidate[RACE_TEST_PATH_SIZE];
static char race_test_replay_directory[RACE_TEST_PATH_SIZE];
static char race_test_replay_committed[RACE_TEST_PATH_SIZE];
static char race_test_replay_candidate[RACE_TEST_PATH_SIZE];
static uint32_t race_test_generation;
static race_physics_config_t race_test_physics_provided;
static race_physics_parse_result_t race_test_physics_provider_result;
static size_t race_test_physics_provider_calls;

static race_physics_parse_result_t Race_TestPhysicsProvider(
  race_physics_config_t *config) {
  race_test_physics_provider_calls++;
  if (config) {
    *config = race_test_physics_provided;
  }
  return race_test_physics_provider_result;
}

static void Race_TestPersistenceRemove(const char *path) {
  if (path && *path) {
    remove(path);
  }
}

static void Race_TestPersistenceSetup(void) {
  char current[RACE_TEST_PATH_SIZE];
  ck_assert_ptr_nonnull(Race_TestGetCwd(current, sizeof(current)));

  snprintf(race_test_directory, sizeof(race_test_directory),
           "%s/check_race_persistence_%ld_%u",
           current, (long) Race_TestGetPid(), race_test_generation++);

  ck_assert_int_eq(Race_TestMkdir(race_test_directory), 0);

  snprintf(race_test_committed, sizeof(race_test_committed),
           "%s/committed.profile", race_test_directory);
  snprintf(race_test_candidate, sizeof(race_test_candidate),
           "%s/candidate.profile", race_test_directory);
  snprintf(race_test_map_committed, sizeof(race_test_map_committed),
           "%s/committed.state", race_test_directory);
  snprintf(race_test_map_candidate, sizeof(race_test_map_candidate),
           "%s/candidate.state", race_test_directory);
  snprintf(race_test_admin_committed, sizeof(race_test_admin_committed),
           "%s/admins.db", race_test_directory);
  snprintf(race_test_admin_candidate, sizeof(race_test_admin_candidate),
           "%s/admins.candidate", race_test_directory);
  snprintf(race_test_replay_directory, sizeof(race_test_replay_directory),
           "%s/replays", race_test_directory);
  ck_assert_int_eq(Race_TestMkdir(race_test_replay_directory), 0);
  race_test_replay_committed[0] = '\0';
  race_test_replay_candidate[0] = '\0';
}

static void Race_TestPersistenceTeardown(void) {
  Race_TestPersistenceRemove(race_test_candidate);
  Race_TestPersistenceRemove(race_test_committed);
  Race_TestPersistenceRemove(race_test_map_candidate);
  Race_TestPersistenceRemove(race_test_map_committed);
  Race_TestPersistenceRemove(race_test_admin_candidate);
  Race_TestPersistenceRemove(race_test_admin_committed);
  Race_TestPersistenceRemove(race_test_replay_candidate);
  Race_TestPersistenceRemove(race_test_replay_committed);
  ck_assert_int_eq(Race_TestRmdir(race_test_replay_directory), 0);
  ck_assert_int_eq(Race_TestRmdir(race_test_directory), 0);

  race_test_candidate[0] = '\0';
  race_test_committed[0] = '\0';
  race_test_map_candidate[0] = '\0';
  race_test_map_committed[0] = '\0';
  race_test_admin_candidate[0] = '\0';
  race_test_admin_committed[0] = '\0';
  race_test_replay_directory[0] = '\0';
  race_test_replay_committed[0] = '\0';
  race_test_replay_candidate[0] = '\0';
  race_test_directory[0] = '\0';
}

static void Race_TestPersistenceRead(const char *path,
                                     char *buffer, size_t capacity,
                                     const char *expected) {
  size_t length;
  ck_assert_int_eq(Race_Persistence_Read(path, buffer, capacity, &length),
                   RACE_PERSISTENCE_OK);
  ck_assert_uint_eq(length, strlen(expected));
  ck_assert_int_eq(memcmp(buffer, expected, length), 0);
}

static race_course_t Race_TestCourse(const int32_t *checkpoints, size_t count) {
  race_course_t course;
  Race_Course_Reset(&course);
  Race_Course_AddFinish(&course);

  for (size_t i = 0; i < count; i++) {
    Race_Course_AddCheckpoint(&course, checkpoints[i]);
  }

  Race_Course_Validate(&course);
  return course;
}

static void Race_TestUid(size_t index, char uid[RACE_PROFILE_UID_SIZE]) {
  snprintf(uid, RACE_PROFILE_UID_SIZE,
           "00000000-0000-4000-8000-%012llx",
           (unsigned long long) index);
}

static race_leaderboard_record_t Race_TestRecord(const char *uid,
                                                 const char *display_name,
                                                 uint32_t elapsed_time,
                                                 const uint32_t *splits,
                                                 size_t split_count) {
  race_leaderboard_record_t record;
  ck_assert(Race_Leaderboard_RecordInit(&record, uid, display_name,
                                        elapsed_time, splits, split_count));
  return record;
}

static race_replay_sample_t Race_TestReplaySample(uint32_t time_ms,
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
  sample.stats[RACE_TEST_INPUT_STAT] = RACE_INPUT_FORMAT_V1 |
                                       RACE_INPUT_FORWARD |
                                       RACE_INPUT_RIGHT |
                                       RACE_INPUT_JUMP;
  for (size_t i = 0; i < MAX_INVENTORY; i++) {
    sample.inventory[i] = (int16_t) (i * 2u + (size_t) value);
  }
  return sample;
}

static race_replay_t Race_TestReplay(race_replay_sample_t *samples,
                                     size_t capacity) {
  ck_assert_uint_ge(capacity, 3u);
  race_replay_t replay;
  int32_t player_uid;
  ck_assert(Race_Replay_ProfilePlayerUid(RACE_TEST_UID_A, &player_uid));
  ck_assert(Race_Replay_Init(&replay, samples, capacity, NULL, 0u,
                             "maps/Edge.BSP", RACE_TEST_UID_A, "Runner",
                             player_uid, 0u));
  replay.elapsed_time = 50u;
  replay.sample_count = 3u;
  replay.samples[0] = Race_TestReplaySample(0u, 1.f);
  replay.samples[1] = Race_TestReplaySample(25u, 2.f);
  replay.samples[2] = Race_TestReplaySample(50u, 3.f);
  ck_assert(Race_Replay_Valid(&replay));
  return replay;
}

static race_replay_projectile_event_t Race_TestProjectileEvent(
    const uint32_t time_ms, const uint16_t id,
    const race_replay_projectile_kind_t kind,
    const race_replay_projectile_operation_t operation) {
  return (race_replay_projectile_event_t) {
    .time_ms = time_ms,
    .id = id,
    .kind = kind,
    .operation = operation,
    .origin = Vec3((float) id, (float) time_ms, 3.f),
    .velocity = Vec3(800.f, 16.f * id, -4.f),
    .normal = operation == RACE_REPLAY_PROJECTILE_IMPACT
      ? Vec3(0.f, 0.f, 1.f) : Vec3_Zero()
  };
}

static race_replay_state_message_t Race_TestReplayState(
  const uint32_t generation, const uint32_t sequence) {
  race_replay_state_message_t state = {
    .flags = RACE_REPLAY_STATE_ACTIVE,
    .speed = RACE_REPLAY_SPEED_NORMAL,
    .source = RACE_REPLAY_SOURCE_WORLD_RECORD,
    .generation = generation,
    .sequence = sequence,
    .replay_id = UINT64_C(0x123456789abcdef0),
    .duration_ms = 50u,
    .playhead_ms = 25u,
    .rank = 1u,
    .sample_count = 3u
  };
  snprintf(state.display_name, sizeof(state.display_name), "%s", "Runner");
  state.samples[0] = (race_replay_pose_sample_t) {
    .time_ms = 0u,
    .origin = Vec3(0.f, 1.f, 2.f),
    .view_angles = Vec3(0.f, 350.f, 0.f)
  };
  state.samples[1] = (race_replay_pose_sample_t) {
    .time_ms = 25u,
    .origin = Vec3(25.f, 26.f, 27.f),
    .view_angles = Vec3(10.f, 10.f, 20.f)
  };
  state.samples[2] = (race_replay_pose_sample_t) {
    .time_ms = 50u,
    .origin = Vec3(50.f, 51.f, 52.f),
    .view_angles = Vec3(20.f, 20.f, 40.f)
  };
  return state;
}

START_TEST(_Race_TrainingInputContract) {
  const pm_cmd_t forward_left_jump = {
    .forward = 1,
    .right = -1,
    .up = 1,
    .buttons = BUTTON_ATTACK | BUTTON_HOOK | BUTTON_WALK
  };
  const int16_t forward_flags = Race_InputFlags(&forward_left_jump);
  ck_assert_int_eq(forward_flags,
                   RACE_INPUT_FORMAT_V1 |
                     RACE_INPUT_FORWARD | RACE_INPUT_LEFT |
                     RACE_INPUT_JUMP | RACE_INPUT_ATTACK |
                     RACE_INPUT_HOOK | RACE_INPUT_WALK);
  ck_assert(Race_InputFlagsValid(forward_flags));

  const pm_cmd_t back_right_crouch = {
    .forward = -1,
    .right = 1,
    .up = -1
  };
  ck_assert_int_eq(Race_InputFlags(&back_right_crouch),
                   RACE_INPUT_FORMAT_V1 |
                     RACE_INPUT_BACK | RACE_INPUT_RIGHT |
                     RACE_INPUT_CROUCH);
  ck_assert_int_eq(Race_InputFlags(NULL), RACE_INPUT_FORMAT_V1);
  ck_assert(Race_InputFlagsValid(RACE_INPUT_FORMAT_V1));
  ck_assert(!Race_InputFlagsValid(0));
  ck_assert(!Race_InputFlagsValid(RACE_INPUT_FORWARD));
} END_TEST

START_TEST(_Race_InputViewerLegacyContract) {
  ck_assert(!Cg_InputViewer_ShouldCapturePredictionCommand(0u, 0u));
  ck_assert(!Cg_InputViewer_ShouldCapturePredictionCommand(0u, 1u));
  ck_assert(Cg_InputViewer_ShouldCapturePredictionCommand(0u, 2u));
  ck_assert(!Cg_InputViewer_ShouldCapturePredictionCommand(1u, 2u));

  const int16_t local = RACE_INPUT_FORMAT_V1 |
                        RACE_INPUT_FORWARD | RACE_INPUT_ATTACK;
  const int16_t chase = RACE_INPUT_FORMAT_V1 |
                        RACE_INPUT_LEFT | RACE_INPUT_CROUCH;
  const int16_t replay = RACE_INPUT_FORMAT_V1 |
                         RACE_INPUT_RIGHT | RACE_INPUT_JUMP;

  cg_input_viewer_state_t state = Cg_InputViewer_Select(
    local, chase, replay, true, true);
  ck_assert_int_eq(state.source, CG_INPUT_VIEWER_REPLAY);
  ck_assert_int_eq(state.flags, replay);
  ck_assert(state.valid);

  state = Cg_InputViewer_Select(local, chase, replay, false, true);
  ck_assert_int_eq(state.source, CG_INPUT_VIEWER_CHASE);
  ck_assert_int_eq(state.flags, chase);
  ck_assert(state.valid);

  state = Cg_InputViewer_Select(local, chase, replay, false, false);
  ck_assert_int_eq(state.source, CG_INPUT_VIEWER_LOCAL);
  ck_assert_int_eq(state.flags, local);
  ck_assert(state.valid);

  state = Cg_InputViewer_Select(local, chase, 0, true, true);
  ck_assert_int_eq(state.source, CG_INPUT_VIEWER_REPLAY);
  ck_assert(!state.valid);

  ck_assert(Cg_InputViewer_Visible(true, true, false, false, false));
  ck_assert(!Cg_InputViewer_Visible(false, true, false, false, false));
  ck_assert(!Cg_InputViewer_Visible(true, false, false, false, false));
  ck_assert(!Cg_InputViewer_Visible(true, true, true, false, false));
  ck_assert(!Cg_InputViewer_Visible(true, true, false, true, false));
  ck_assert(!Cg_InputViewer_Visible(true, true, false, false, true));

  // §7 geometry: 46px chevrons, 7px gaps, 32px between groups.
  ck_assert_int_eq(Cg_InputViewer_ChevronsHeight(46, 7), 99);
  ck_assert_int_eq(Cg_InputViewer_ChevronsWidth(46, 7), 152);
  ck_assert_int_eq(Cg_InputViewer_ClusterWidth(46, 7, 32, 96, 64), 376);

  // Halving the scale halves the cluster.
  ck_assert_int_eq(Cg_InputViewer_ChevronsHeight(23, 4), 50);
  ck_assert_int_eq(Cg_InputViewer_ChevronsWidth(23, 4), 77);
} END_TEST

START_TEST(_Race_StrafeHelperLegacyMath) {
  ck_assert(!Cg_StrafeHelper_ShouldObservePredictionCommand(0u, 0u));
  ck_assert(!Cg_StrafeHelper_ShouldObservePredictionCommand(0u, 1u));
  ck_assert(Cg_StrafeHelper_ShouldObservePredictionCommand(0u, 2u));
  ck_assert(!Cg_StrafeHelper_ShouldObservePredictionCommand(1u, 2u));

  cg_strafe_helper_state_t state = { 0 };
  ck_assert(Cg_StrafeHelper_Update(
    &state, Vec3(1.f, 0.f, 0.f), Vec3(320.f, 0.f, 0.f),
    Vec3(0.f, 1.f, 0.f), 320.f, 1.f, 0.025f, 0.f));
  ck_assert_float_eq_tol(state.raw.velocity_norm, 320.f, 0.001f);
  ck_assert_float_eq_tol(state.raw.velocity_yaw, 0.f, 0.001f);
  ck_assert_float_eq_tol(state.raw.view_yaw, 0.f, 0.001f);
  ck_assert(isfinite(state.raw.angle_optimal));
  ck_assert(isfinite(state.raw.angle_diff));

  ck_assert(!Cg_StrafeHelper_Update(
    &state, Vec3(1.f, 0.f, 0.f), Vec3(320.f, 0.f, 0.f),
    Vec3(1.f, 0.f, 0.f), 320.f, 1.f, 0.025f, 0.f));
  ck_assert_float_eq(state.raw.velocity_norm, 0.f);

  cg_strafe_helper_dynamic_trend_t trend = { 0 };
  ck_assert_int_eq(Cg_StrafeHelper_UpdateDynamicTrend(&trend, 300.f, 0u),
                   CG_STRAFE_HELPER_SPEED_NEUTRAL);
  ck_assert_int_eq(Cg_StrafeHelper_UpdateDynamicTrend(&trend, 301.f, 1u),
                   CG_STRAFE_HELPER_SPEED_GAIN);
  ck_assert_int_eq(Cg_StrafeHelper_UpdateDynamicTrend(
                     &trend, 301.f,
                     1u + CG_STRAFE_HELPER_DYNAMIC_COLOR_HOLD_MILLIS),
                   CG_STRAFE_HELPER_SPEED_NEUTRAL);
  ck_assert_int_eq(Cg_StrafeHelper_UpdateDynamicTrend(&trend, 299.f, 102u),
                   CG_STRAFE_HELPER_SPEED_LOSS);
} END_TEST

typedef struct {
  bool replay_ok;
  bool map_ok;
  bool remove_ok;
  bool newly_created;
  char order[4];
  size_t order_length;
} race_test_publication_t;

static bool Race_TestPublicationReplay(void *data, bool *newly_created) {
  race_test_publication_t *test = data;
  test->order[test->order_length++] = 'R';
  *newly_created = test->newly_created;
  return test->replay_ok;
}

static bool Race_TestPublicationMap(void *data) {
  race_test_publication_t *test = data;
  test->order[test->order_length++] = 'M';
  return test->map_ok;
}

static bool Race_TestPublicationRemove(void *data) {
  race_test_publication_t *test = data;
  test->order[test->order_length++] = 'D';
  return test->remove_ok;
}

static race_publication_result_t Race_TestPublicationCommit(
  race_test_publication_t *test) {
  const race_publication_ops_t ops = {
    .commit_replay = Race_TestPublicationReplay,
    .commit_map_state = Race_TestPublicationMap,
    .remove_replay = Race_TestPublicationRemove,
    .context = test
  };
  return Race_Publication_Commit(&ops);
}

static race_vote_request_t Race_TestVoteRequest(race_vote_type_t type,
                                                uint16_t eligible_count) {
  race_vote_request_t request = {
    .type = type,
    .initiator = { .slot = 0, .connection_id = 100u },
    .start_time = 1000u,
    .duration = 30000u,
    .max_clients = 8u
  };
  for (uint16_t slot = 0; slot < eligible_count; slot++) {
    request.eligible_connection_ids[slot] = 100u + slot;
  }
  if (type == RACE_VOTE_TYPE_MAP) {
    snprintf(request.target.map, sizeof(request.target.map), "%s", "race-test");
  } else if (type == RACE_VOTE_TYPE_KICK) {
    request.target.kick = (race_vote_identity_t) {
      .slot = 2,
      .connection_id = 102u
    };
  } else if (type == RACE_VOTE_TYPE_PHYSICS) {
    snprintf(request.target.physics, sizeof(request.target.physics), "%s",
             "q2-v1");
  }
  return request;
}

START_TEST(_Race_VoteEligibilityMathAndCooldown) {
  ck_assert(Race_Vote_IsEligible(true, false, false, true, RACE_MODE_RACE));
  ck_assert(Race_Vote_IsEligible(true, false, false, true, RACE_MODE_PRACTICE));
  ck_assert(!Race_Vote_IsEligible(true, false, true, true, RACE_MODE_RACE));
  ck_assert(!Race_Vote_IsEligible(true, true, false, true, RACE_MODE_RACE));
  ck_assert(!Race_Vote_IsEligible(true, false, false, false, RACE_MODE_RACE));
  ck_assert(!Race_Vote_IsEligible(false, false, false, true, RACE_MODE_RACE));

  ck_assert_uint_eq(Race_Vote_RequiredQuorum(0), 0);
  ck_assert_uint_eq(Race_Vote_RequiredYes(0), 0);
  ck_assert_uint_eq(Race_Vote_RequiredQuorum(1), 1);
  ck_assert_uint_eq(Race_Vote_RequiredYes(1), 1);
  ck_assert_uint_eq(Race_Vote_RequiredQuorum(2), 1);
  ck_assert_uint_eq(Race_Vote_RequiredYes(2), 2);
  ck_assert_uint_eq(Race_Vote_RequiredQuorum(3), 2);
  ck_assert_uint_eq(Race_Vote_RequiredYes(3), 2);
  ck_assert_uint_eq(Race_Vote_RequiredQuorum(4), 2);
  ck_assert_uint_eq(Race_Vote_RequiredYes(4), 3);

  ck_assert_int_eq(Race_Vote_StartAvailability(1000u, 0u, 0u, 3u),
                   RACE_VOTE_START_AVAILABLE);
  ck_assert_int_eq(Race_Vote_StartAvailability(1000u, 2000u, 1u, 3u),
                   RACE_VOTE_START_COOLDOWN);
  ck_assert_int_eq(Race_Vote_StartAvailability(2000u, 2000u, 1u, 3u),
                   RACE_VOTE_START_AVAILABLE);
  ck_assert_int_eq(Race_Vote_StartAvailability(1000u, 0u, 3u, 3u),
                   RACE_VOTE_START_LIMIT);
  ck_assert_int_eq(Race_Vote_StartAvailability(1000u, 0u, 0u, 0u),
                   RACE_VOTE_START_LIMIT);
  ck_assert(Race_Vote_TimeReached(10u, UINT32_MAX - 5u));
  ck_assert_uint_eq(Race_Vote_TimeRemaining(1000u, 2000u), 1000u);
  ck_assert_uint_eq(Race_Vote_TimeRemaining(2000u, 2000u), 0u);
} END_TEST

START_TEST(_Race_VoteCreationAndBallots) {
  race_vote_state_t vote = { 0 };
  race_vote_request_t request = Race_TestVoteRequest(RACE_VOTE_TYPE_MAP, 4u);
  ck_assert(Race_Vote_Begin(&vote, &request));
  ck_assert(vote.active);
  ck_assert_uint_eq(vote.generation, 1u);
  ck_assert_uint_eq(vote.eligible_count, 4u);
  ck_assert_uint_eq(vote.yes_count, 1u); // initiator auto-yes
  ck_assert(!Race_Vote_Begin(&vote, &request)); // one active vote

  const race_vote_identity_t initiator = { .slot = 0, .connection_id = 100u };
  const race_vote_identity_t voter1 = { .slot = 1, .connection_id = 101u };
  const race_vote_identity_t voter2 = { .slot = 2, .connection_id = 102u };
  const race_vote_identity_t voter3 = { .slot = 3, .connection_id = 103u };
  ck_assert(Race_Vote_CanCast(&vote, initiator));
  ck_assert(Race_Vote_CanCast(&vote, voter1));
  ck_assert(!Race_Vote_CanCast(
    &vote, (race_vote_identity_t) { .slot = 1, .connection_id = 999u }));
  ck_assert(!Race_Vote_CanCast(
    &vote, (race_vote_identity_t) { .slot = 4, .connection_id = 104u }));
  ck_assert_int_eq(Race_Vote_Cast(&vote, voter1, RACE_VOTE_BALLOT_YES),
                   RACE_VOTE_CAST_ACCEPTED);
  const uint16_t unchanged_yes = vote.yes_count;
  const uint16_t unchanged_no = vote.no_count;
  ck_assert_int_eq(Race_Vote_Cast(&vote, voter1, RACE_VOTE_BALLOT_YES),
                   RACE_VOTE_CAST_UNCHANGED);
  ck_assert_uint_eq(vote.yes_count, unchanged_yes);
  ck_assert_uint_eq(vote.no_count, unchanged_no);
  ck_assert_uint_eq(vote.yes_count, 2u);
  ck_assert_int_eq(Race_Vote_Cast(&vote, voter1, RACE_VOTE_BALLOT_NO),
                   RACE_VOTE_CAST_CHANGED);
  ck_assert_uint_eq(vote.yes_count, 1u);
  ck_assert_uint_eq(vote.no_count, 1u);
  ck_assert_int_eq(Race_Vote_Cast(
                     &vote, (race_vote_identity_t) {
                       .slot = 4, .connection_id = 104u
                     }, RACE_VOTE_BALLOT_YES),
                   RACE_VOTE_CAST_INELIGIBLE);
  ck_assert_int_eq(Race_Vote_Cast(&vote, voter2, RACE_VOTE_BALLOT_YES),
                   RACE_VOTE_CAST_ACCEPTED);
  ck_assert_int_eq(Race_Vote_Evaluate(&vote, 1001u),
                   RACE_VOTE_OUTCOME_PENDING);
  ck_assert_int_eq(Race_Vote_Cast(&vote, voter3, RACE_VOTE_BALLOT_NO),
                   RACE_VOTE_CAST_ACCEPTED);
  ck_assert_int_eq(Race_Vote_Evaluate(&vote, 1001u),
                   RACE_VOTE_OUTCOME_FAILED_THRESHOLD); // exact 2-2 tie

  race_vote_state_t completed;
  ck_assert(Race_Vote_Complete(&vote, RACE_VOTE_OUTCOME_FAILED_THRESHOLD,
                               &completed));
  ck_assert(!vote.active);
  ck_assert(!Race_Vote_CanCast(&vote, voter1));
  ck_assert(!Race_Vote_Complete(&vote, RACE_VOTE_OUTCOME_FAILED_THRESHOLD,
                                NULL));
  ck_assert_int_eq(Race_Vote_Cast(&vote, voter1, RACE_VOTE_BALLOT_YES),
                   RACE_VOTE_CAST_INACTIVE);
} END_TEST

START_TEST(_Race_VoteOutcomesAndConnections) {
  race_vote_state_t vote = { 0 };
  race_vote_request_t request = Race_TestVoteRequest(RACE_VOTE_TYPE_KICK, 3u);
  ck_assert(Race_Vote_Begin(&vote, &request));
  ck_assert_int_eq(Race_Vote_Cast(
                     &vote, (race_vote_identity_t) {
                       .slot = 2, .connection_id = 102u
                     }, RACE_VOTE_BALLOT_YES),
                   RACE_VOTE_CAST_ACCEPTED); // kick target may vote
  ck_assert_int_eq(Race_Vote_Evaluate(&vote, 1001u),
                   RACE_VOTE_OUTCOME_PENDING); // legacy waits for every ballot
  ck_assert_int_eq(Race_Vote_Cast(
                     &vote, (race_vote_identity_t) {
                       .slot = 1, .connection_id = 101u
                     }, RACE_VOTE_BALLOT_NO),
                   RACE_VOTE_CAST_ACCEPTED);
  ck_assert_int_eq(Race_Vote_Evaluate(&vote, 1001u),
                   RACE_VOTE_OUTCOME_PASSED);

  ck_assert(Race_Vote_Complete(&vote, RACE_VOTE_OUTCOME_PASSED, NULL));
  request = Race_TestVoteRequest(RACE_VOTE_TYPE_MAP, 4u);
  ck_assert(Race_Vote_Begin(&vote, &request));
  ck_assert_int_eq(Race_Vote_Evaluate(&vote, vote.deadline),
                   RACE_VOTE_OUTCOME_FAILED_QUORUM);

  ck_assert(Race_Vote_RemoveVoter(
    &vote, (race_vote_identity_t) { .slot = 3, .connection_id = 103u }));
  ck_assert_uint_eq(vote.eligible_count, 3u);
  ck_assert_int_eq(Race_Vote_Cast(
                     &vote, (race_vote_identity_t) {
                       .slot = 3, .connection_id = 999u
                     }, RACE_VOTE_BALLOT_YES),
                   RACE_VOTE_CAST_INELIGIBLE); // reconnect/new connection
  ck_assert(Race_Vote_RemoveVoter(
    &vote, (race_vote_identity_t) { .slot = 0, .connection_id = 100u }));
  ck_assert_uint_eq(vote.yes_count, 0u); // initiator disconnect removes auto-yes
  ck_assert_uint_eq(vote.eligible_count, 2u);
  ck_assert_int_eq(Race_Vote_Evaluate(&vote, 1001u),
                   RACE_VOTE_OUTCOME_PENDING);
  ck_assert(Race_Vote_RemoveVoter(
    &vote, (race_vote_identity_t) { .slot = 1, .connection_id = 101u }));
  ck_assert(Race_Vote_RemoveVoter(
    &vote, (race_vote_identity_t) { .slot = 2, .connection_id = 102u }));
  ck_assert_int_eq(Race_Vote_Evaluate(&vote, 1001u),
                   RACE_VOTE_OUTCOME_FAILED_NO_ELIGIBLE);

  race_vote_state_t empty = { 0 };
  request = Race_TestVoteRequest(RACE_VOTE_TYPE_MAP, 0u);
  ck_assert(!Race_Vote_Begin(&empty, &request));
} END_TEST

START_TEST(_Race_VoteExecutionOnce) {
  race_vote_state_t vote = { 0 }, completed;
  ck_assert_uint_eq(Race_Vote_TimeRemainingSeconds(1000u, 1000u), 0u);
  ck_assert_uint_eq(Race_Vote_TimeRemainingSeconds(1000u, 1001u), 1u);
  ck_assert_uint_eq(Race_Vote_TimeRemainingSeconds(1000u, 1999u), 1u);
  ck_assert_uint_eq(Race_Vote_TimeRemainingSeconds(1000u, 2000u), 1u);
  ck_assert_uint_eq(Race_Vote_TimeRemainingSeconds(1000u, 2001u), 2u);

  race_vote_request_t request = Race_TestVoteRequest(RACE_VOTE_TYPE_MAP, 3u);
  ck_assert(Race_Vote_Begin(&vote, &request));
  ck_assert_int_eq(Race_Vote_Cast(
                     &vote, (race_vote_identity_t) {
                       .slot = 1, .connection_id = 101u
                     }, RACE_VOTE_BALLOT_YES),
                   RACE_VOTE_CAST_ACCEPTED);
  ck_assert_int_eq(Race_Vote_Evaluate(&vote, 1001u),
                   RACE_VOTE_OUTCOME_PENDING);
  ck_assert(!Race_Vote_MarkPassed(&vote, 1001u));
  ck_assert_int_eq(Race_Vote_Cast(
                     &vote, (race_vote_identity_t) {
                       .slot = 2, .connection_id = 102u
                     }, RACE_VOTE_BALLOT_NO),
                   RACE_VOTE_CAST_ACCEPTED);
  ck_assert_int_eq(Race_Vote_Evaluate(&vote, 1001u),
                   RACE_VOTE_OUTCOME_PASSED);
  ck_assert(Race_Vote_MarkPassed(&vote, 1001u));
  ck_assert(vote.active);
  ck_assert(vote.passed);
  ck_assert(!Race_Vote_CanCast(
    &vote, (race_vote_identity_t) { .slot = 1, .connection_id = 101u }));
  ck_assert_uint_eq(Race_Vote_TimeRemaining(1001u, vote.deadline), 0u);
  ck_assert_uint_eq(Race_Vote_TimeRemainingSeconds(1001u, vote.deadline), 0u);
  ck_assert_int_eq(Race_Vote_Evaluate(&vote, 1002u),
                   RACE_VOTE_OUTCOME_PASSED);
  ck_assert_int_eq(Race_Vote_Cast(
                     &vote, (race_vote_identity_t) {
                       .slot = 1, .connection_id = 101u
                     }, RACE_VOTE_BALLOT_NO),
                   RACE_VOTE_CAST_INACTIVE);
  ck_assert(!Race_Vote_RemoveVoter(
    &vote, (race_vote_identity_t) { .slot = 1, .connection_id = 101u }));
  ck_assert(!Race_Vote_MarkPassed(&vote, 1002u));

  uint32_t action_count = 0u;
  if (Race_Vote_Complete(&vote, RACE_VOTE_OUTCOME_PASSED, &completed)) {
    action_count++;
  }
  if (Race_Vote_Complete(&vote, RACE_VOTE_OUTCOME_PASSED, &completed)) {
    action_count++;
  }
  ck_assert_uint_eq(action_count, 1u);
  ck_assert_str_eq(completed.target.map, "race-test");

  request = Race_TestVoteRequest(RACE_VOTE_TYPE_KICK, 3u);
  ck_assert(Race_Vote_Begin(&vote, &request));
  ck_assert(!vote.passed);
  ck_assert(Race_Vote_IdentityEqual(vote.target.kick,
                                    request.target.kick));
  ck_assert_int_eq(Race_Vote_Cast(
                     &vote, (race_vote_identity_t) {
                       .slot = 1, .connection_id = 101u
                     }, RACE_VOTE_BALLOT_NO),
                   RACE_VOTE_CAST_ACCEPTED);
  ck_assert_int_eq(Race_Vote_Cast(
                     &vote, (race_vote_identity_t) {
                       .slot = 2, .connection_id = 102u
                     }, RACE_VOTE_BALLOT_NO),
                   RACE_VOTE_CAST_ACCEPTED);
  ck_assert_int_eq(Race_Vote_Evaluate(&vote, 1001u),
                   RACE_VOTE_OUTCOME_FAILED_THRESHOLD);
  ck_assert(Race_Vote_Complete(&vote, RACE_VOTE_OUTCOME_FAILED_THRESHOLD,
                                NULL));
  ck_assert_uint_eq(action_count, 1u); // failure performed no mutation

  request = Race_TestVoteRequest(RACE_VOTE_TYPE_MAP, 3u);
  ck_assert(Race_Vote_Begin(&vote, &request));
  ck_assert(Race_Vote_Complete(
    &vote, RACE_VOTE_OUTCOME_CANCELLED_INTERMISSION, NULL));
  ck_assert(!vote.active);
} END_TEST

START_TEST(_Race_VotePhysicsTarget) {
  race_vote_state_t vote = { 0 };
  const race_vote_request_t request =
    Race_TestVoteRequest(RACE_VOTE_TYPE_PHYSICS, 1u);
  ck_assert(Race_Vote_Begin(&vote, &request));
  ck_assert_int_eq(vote.type, RACE_VOTE_TYPE_PHYSICS);
  ck_assert_str_eq(vote.target.physics, "q2-v1");
  ck_assert_int_eq(Race_Vote_Evaluate(&vote, 1001u),
                   RACE_VOTE_OUTCOME_PASSED);
} END_TEST

START_TEST(_Race_VoteMenuBallotsAndSpectators) {
  race_vote_menu_state_t menu;
  Race_VoteMenu_Init(&menu);
  const char *choices[] = { "race-one", "race-two", "race-three" };
  ck_assert(Race_VoteMenu_Begin(&menu, choices, lengthof(choices), 8u,
                                1000u, 20000u, false));
  ck_assert(menu.active);
  ck_assert_uint_eq(menu.deadline, 21000u);
  ck_assert(Race_VoteMenu_CanCast(&menu, 0u, false));
  ck_assert(!Race_VoteMenu_CanCast(&menu, 1u, true));
  ck_assert(!Race_VoteMenu_CanCast(&menu, 8u, false));
  ck_assert(!Race_VoteMenu_Begin(&menu, choices, lengthof(choices), 8u,
                                 1000u, 20000u, false));
  ck_assert_int_eq(Race_VoteMenu_Cast(&menu, 0u, false, 1u),
                   RACE_VOTE_MENU_CAST_ACCEPTED);
  const uint16_t unchanged_votes = menu.choices[0].votes;
  ck_assert_int_eq(Race_VoteMenu_Cast(&menu, 0u, false, 1u),
                   RACE_VOTE_MENU_CAST_UNCHANGED);
  ck_assert_uint_eq(menu.choices[0].votes, unchanged_votes);
  ck_assert_int_eq(Race_VoteMenu_Cast(&menu, 0u, false, 2u),
                   RACE_VOTE_MENU_CAST_CHANGED);
  ck_assert_uint_eq(menu.choices[0].votes, 0u);
  ck_assert_uint_eq(menu.choices[1].votes, 1u);
  ck_assert_int_eq(Race_VoteMenu_Cast(&menu, 1u, true, 1u),
                   RACE_VOTE_MENU_CAST_SPECTATOR);
  ck_assert_int_eq(Race_VoteMenu_Cast(&menu, 1u, false, 4u),
                   RACE_VOTE_MENU_CAST_INVALID_CHOICE);
  ck_assert(Race_VoteMenu_RemoveVoter(&menu, 0u));
  ck_assert_uint_eq(menu.choices[1].votes, 0u);
  ck_assert(!Race_VoteMenu_RemoveVoter(&menu, 0u));
} END_TEST

START_TEST(_Race_VoteMenuDeadlineAndTieResolution) {
  race_vote_menu_state_t menu;
  Race_VoteMenu_Init(&menu);
  const char *choices[] = { "race-one", "race-two", "race-three" };
  ck_assert(Race_VoteMenu_Begin(&menu, choices, lengthof(choices), 8u,
                                UINT32_MAX - 1000u, 2000u, true));
  ck_assert(Race_VoteMenu_CanCast(&menu, 0u, true));
  ck_assert_uint_eq(Race_VoteMenu_TimeRemaining(&menu, UINT32_MAX - 500u),
                    1500u);
  ck_assert(!Race_VoteMenu_Expired(&menu, UINT32_MAX - 500u));
  ck_assert(Race_VoteMenu_Expired(&menu, 999u));
  ck_assert_int_eq(Race_VoteMenu_Cast(&menu, 0u, true, 1u),
                   RACE_VOTE_MENU_CAST_ACCEPTED);
  ck_assert_int_eq(Race_VoteMenu_Cast(&menu, 1u, false, 2u),
                   RACE_VOTE_MENU_CAST_ACCEPTED);
  uint8_t tied[RACE_VOTE_MENU_MAX_CHOICES];
  uint16_t winning_votes = 0u;
  ck_assert_uint_eq(Race_VoteMenu_TiedWinners(&menu, tied, &winning_votes),
                    2u);
  ck_assert_uint_eq(winning_votes, 1u);
  ck_assert_uint_eq(tied[0], 0u);
  ck_assert_uint_eq(tied[1], 1u);
  uint8_t winner = UINT8_MAX;
  ck_assert(Race_VoteMenu_Resolve(&menu, 1u, &winner, &winning_votes));
  ck_assert_uint_eq(winner, 1u);
  ck_assert(!Race_VoteMenu_Resolve(&menu, 2u, &winner, &winning_votes));

  Race_VoteMenu_Init(&menu);
  ck_assert(Race_VoteMenu_Begin(&menu, choices, lengthof(choices), 8u,
                                0u, 1000u, true));
  ck_assert_uint_eq(Race_VoteMenu_TiedWinners(&menu, tied, &winning_votes),
                    0u);
} END_TEST

static race_map_state_t Race_TestMapState(race_leaderboard_record_t *records,
                                          size_t capacity,
                                          uint64_t generation) {
  race_map_state_t state;
  ck_assert(Race_MapState_Init(&state, records, capacity, "maps/Edge.BSP",
                               RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY));
  state.generation = generation;
  return state;
}

static uint32_t Race_TestSettingsCrc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; i++) {
    crc ^= bytes[i];
    for (size_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                          (uint32_t) -(int32_t) (crc & 1u));
    }
  }
  return ~crc;
}

static size_t Race_TestSettingsReplace(char *data, size_t length,
                                       size_t capacity,
                                       const char *needle,
                                       const char *replacement) {
  char *found = strstr(data, needle);
  ck_assert_ptr_nonnull(found);

  const size_t needle_length = strlen(needle);
  const size_t replacement_length = strlen(replacement);
  const size_t offset = (size_t) (found - data);
  ck_assert_msg(length - needle_length + replacement_length < capacity,
                "Replacement exceeds settings test buffer");
  memmove(found + replacement_length, found + needle_length,
          length - offset - needle_length + 1u);
  memcpy(found, replacement, replacement_length);
  return length - needle_length + replacement_length;
}

static void Race_TestSettingsUpdateChecksum(char *data, size_t length) {
  char *checksum = strstr(data, "crc=");
  ck_assert_ptr_nonnull(checksum);
  const size_t offset = (size_t) (checksum - data);
  ck_assert_uint_eq(offset + 13u, length);
  snprintf(checksum + 4, 9, "%08x", Race_TestSettingsCrc32(data, offset));
  checksum[12] = '\n';
  checksum[13] = '\0';
}

START_TEST(_Race_CourseValidation) {
  race_course_t finishless;
  Race_Course_Reset(&finishless);
  ck_assert(!Race_Course_Validate(&finishless));

  race_course_t course = Race_TestCourse(NULL, 0);
  ck_assert(course.valid);
  ck_assert_uint_eq(course.checkpoint_count, 0);

  const int32_t one[] = { 1 };
  course = Race_TestCourse(one, RACE_TEST_LENGTHOF(one));
  ck_assert(course.valid);
  ck_assert_uint_eq(course.checkpoint_count, 1);

  const int32_t contiguous[] = { 1, 2, 3 };
  course = Race_TestCourse(contiguous, RACE_TEST_LENGTHOF(contiguous));
  ck_assert(course.valid);
  ck_assert_uint_eq(course.checkpoint_count, 3);

  const int32_t duplicate_middle[] = { 1, 2, 2, 3 };
  course = Race_TestCourse(duplicate_middle, RACE_TEST_LENGTHOF(duplicate_middle));
  ck_assert(course.valid);
  ck_assert_uint_eq(course.checkpoint_count, 3);

  const int32_t duplicate_first[] = { 1, 1, 2, 3 };
  course = Race_TestCourse(duplicate_first, RACE_TEST_LENGTHOF(duplicate_first));
  ck_assert(course.valid);
  ck_assert_uint_eq(course.checkpoint_count, 3);

  const int32_t missing_first[] = { 2 };
  course = Race_TestCourse(missing_first, RACE_TEST_LENGTHOF(missing_first));
  ck_assert(!course.valid);

  const int32_t missing_middle[] = { 1, 3 };
  course = Race_TestCourse(missing_middle, RACE_TEST_LENGTHOF(missing_middle));
  ck_assert(!course.valid);

  const int32_t missing_third[] = { 1, 2, 4 };
  course = Race_TestCourse(missing_third, RACE_TEST_LENGTHOF(missing_third));
  ck_assert(!course.valid);

  const int32_t malformed[] = { 1, 0 };
  course = Race_TestCourse(malformed, RACE_TEST_LENGTHOF(malformed));
  ck_assert(!course.valid);
  ck_assert(course.malformed);
} END_TEST

START_TEST(_Race_MaximumCourse) {
  race_course_t course;
  Race_Course_Reset(&course);
  Race_Course_AddFinish(&course);

  for (int32_t checkpoint = 1; checkpoint <= RACE_MAX_CHECKPOINTS; checkpoint++) {
    ck_assert(Race_Course_AddCheckpoint(&course, checkpoint));
  }

  ck_assert(Race_Course_Validate(&course));
  ck_assert_uint_eq(course.checkpoint_count, RACE_MAX_CHECKPOINTS);

  Race_Course_Reset(&course);
  Race_Course_AddFinish(&course);
  ck_assert(!Race_Course_AddCheckpoint(&course, RACE_MAX_CHECKPOINTS + 1));
  ck_assert(!Race_Course_Validate(&course));
} END_TEST

START_TEST(_Race_OptionalCatalogValidation) {
  race_course_t course;
  Race_Course_Reset(&course);
  Race_Course_AddFinish(&course);
  Race_Course_AddFinish(&course);
  ck_assert_uint_eq(course.finish_count, 2u);

  Race_Course_AddStart(&course);
  Race_Course_AddStart(&course);
  ck_assert_uint_eq(course.start_count, 2u);

  ck_assert(Race_Course_AddSplit(&course, 1));
  ck_assert(Race_Course_AddSplit(&course, 2));
  ck_assert(Race_Course_AddStage(&course, 2));
  ck_assert(Race_Course_AddStage(&course, 3));
  ck_assert(Race_Course_Validate(&course));
  ck_assert(course.splits_valid);
  ck_assert_uint_eq(course.split_count, 2u);
  ck_assert(course.stages_valid);
  ck_assert_uint_eq(course.stage_count, 3u);

  Race_Course_Reset(&course);
  Race_Course_AddFinish(&course);
  ck_assert(Race_Course_AddSplit(&course, 2));
  ck_assert(Race_Course_Validate(&course));
  ck_assert(!course.splits_valid);
  ck_assert(course.valid);

  Race_Course_Reset(&course);
  Race_Course_AddFinish(&course);
  ck_assert(!Race_Course_AddStage(&course, 1));
  ck_assert(Race_Course_Validate(&course));
  ck_assert(!course.stages_valid);
  ck_assert(course.valid);

  Race_Course_InvalidateBarrier(&course);
  ck_assert(!Race_Course_Validate(&course));
} END_TEST

START_TEST(_Race_Progression) {
  race_run_t run;
  Race_Run_Reset(&run);

  ck_assert(!Race_Run_Start(&run, false, 100));
  ck_assert_int_eq(run.state, RACE_RUN_IDLE);

  ck_assert(Race_Run_Start(&run, true, 100));
  ck_assert_int_eq(run.state, RACE_RUN_ACTIVE);
  ck_assert_uint_eq(run.checkpoint_count, 0);

  ck_assert(!Race_Run_Checkpoint(&run, 3, 2, 120));
  ck_assert_uint_eq(run.checkpoint_count, 0);

  ck_assert(Race_Run_Checkpoint(&run, 3, 1, 125));
  ck_assert_uint_eq(run.checkpoint_times[0], 25);
  ck_assert(!Race_Run_Checkpoint(&run, 3, 1, 130));

  ck_assert(!Race_Run_Finish(&run, 3, 140));
  ck_assert_int_eq(run.state, RACE_RUN_ACTIVE);

  ck_assert(Race_Run_Checkpoint(&run, 3, 2, 150));
  ck_assert(!Race_Run_Checkpoint(&run, 3, 2, 151));
  ck_assert(Race_Run_Checkpoint(&run, 3, 3, 175));
  ck_assert(Race_Run_Finish(&run, 3, 200));
  ck_assert_int_eq(run.state, RACE_RUN_FINISHED);
  ck_assert_uint_eq(run.elapsed_time, 100);
} END_TEST

START_TEST(_Race_SplitAndStageProgression) {
  race_run_t run;
  ck_assert(Race_Run_Start(&run, true, 100u));
  ck_assert_uint_eq(run.stage, 1u);

  ck_assert(!Race_Run_Split(&run, 3u, 2u, 120u));
  ck_assert(Race_Run_Split(&run, 3u, 1u, 125u));
  ck_assert_uint_eq(run.split_count, 1u);
  ck_assert_uint_eq(run.split_times[0], 25u);
  ck_assert(!Race_Run_Split(&run, 3u, 1u, 126u));
  ck_assert(Race_Run_Split(&run, 3u, 2u, 150u));

  ck_assert(!Race_Run_Stage(&run, 3u, 3u, 160u));
  ck_assert(Race_Run_Stage(&run, 3u, 2u, 175u));
  ck_assert_uint_eq(run.stage, 2u);
  ck_assert_uint_eq(run.stage_times[0], 75u);
  ck_assert(Race_Run_Stage(&run, 3u, 3u, 200u));
  ck_assert_uint_eq(run.stage_times[1], 100u);

  Race_Run_Reset(&run);
  ck_assert_uint_eq(run.split_count, 0u);
  ck_assert_uint_eq(run.stage, 0u);
} END_TEST

START_TEST(_Race_BarrierPolicies) {
  ck_assert(!Race_CheckpointGateSatisfied(1u, 2u, RACE_GATE_AT_LEAST, false));
  ck_assert(Race_CheckpointGateSatisfied(2u, 2u, RACE_GATE_AT_LEAST, false));
  ck_assert(Race_CheckpointGateSatisfied(3u, 2u, RACE_GATE_AT_LEAST, false));
  ck_assert(Race_CheckpointGateSatisfied(2u, 2u, RACE_GATE_EXACT, false));
  ck_assert(!Race_CheckpointGateSatisfied(3u, 2u, RACE_GATE_EXACT, false));
  ck_assert(Race_CheckpointGateSatisfied(1u, 2u, RACE_GATE_AT_LEAST, true));

  const vec3_t east = Vec3(1.f, 0.f, 0.f);
  ck_assert(Race_OneWayDirectionAllowed(Vec3(10.f, 0.f, 0.f), east, .001f));
  ck_assert(!Race_OneWayDirectionAllowed(Vec3(-10.f, 0.f, 0.f), east, .001f));
  ck_assert(!Race_OneWayDirectionAllowed(Vec3(0.f, 0.f, 10.f), east, .001f));
  ck_assert(!Race_OneWayDirectionAllowed(Vec3(.0001f, 0.f, 0.f), east, .001f));
} END_TEST

START_TEST(_Race_StartPolicies) {
  race_start_mode_t mode = RACE_START_JUMP;
  ck_assert(Race_StartMode_Parse(NULL, &mode));
  ck_assert_int_eq(mode, RACE_START_TOUCH);
  ck_assert(Race_StartMode_Parse("touch", &mode));
  ck_assert_int_eq(mode, RACE_START_TOUCH);
  ck_assert(Race_StartMode_Parse("exit", &mode));
  ck_assert_int_eq(mode, RACE_START_EXIT);
  ck_assert(Race_StartMode_Parse("jump", &mode));
  ck_assert_int_eq(mode, RACE_START_JUMP);
  ck_assert(!Race_StartMode_Parse("invalid", &mode));

  ck_assert(Race_StartJumpEdge(1, 0));
  ck_assert(Race_StartJumpEdge(1, -1));
  ck_assert(!Race_StartJumpEdge(1, 1));
  ck_assert(!Race_StartJumpEdge(0, 0));
  ck_assert(Race_StartExitTransition(RACE_START_EXIT, false));
  ck_assert(!Race_StartExitTransition(RACE_START_EXIT, true));
  ck_assert(!Race_StartExitTransition(RACE_START_JUMP, false));
} END_TEST

START_TEST(_Race_InvalidStartPreservesAttempt) {
  race_run_t run;
  Race_Run_Reset(&run);

  ck_assert(Race_Run_Start(&run, true, 100));
  ck_assert(Race_Run_Checkpoint(&run, 1, 1, 125));
  Race_Run_MarkInvalid(&run, RACE_INVALID_NOCLIP);

  ck_assert(!Race_Run_Start(&run, false, 200));
  ck_assert_int_eq(run.state, RACE_RUN_ACTIVE);
  ck_assert_uint_eq(run.start_time, 100);
  ck_assert_uint_eq(run.checkpoint_count, 1);
  ck_assert_int_eq(run.invalid_flags, RACE_INVALID_NOCLIP);

  ck_assert(Race_Run_Finish(&run, 1, 250));
  ck_assert(!Race_Run_Start(&run, false, 300));
  ck_assert_int_eq(run.state, RACE_RUN_FINISHED);
  ck_assert_uint_eq(run.elapsed_time, 150);
} END_TEST

START_TEST(_Race_RunMetadata) {
  race_run_t run;
  ck_assert(Race_Run_Start(&run, true, 100));

  run.mode = RACE_MODE_RACE;
  run.start_speed = 320.f;
  Race_Run_ObserveSpeed(&run, 300.f);
  Race_Run_ObserveSpeed(&run, 340.f);
  ck_assert_float_eq_tol(run.current_speed, 340.f, 0.001f);
  ck_assert_float_eq_tol(run.top_speed, 340.f, 0.001f);
  ck_assert_uint_eq(run.speed_samples, 2);
  ck_assert_float_eq_tol(Race_Run_AverageSpeed(&run), 320.f, 0.001f);

  ck_assert(Race_Run_IsValid(&run));
  Race_Run_MarkInvalid(&run, RACE_INVALID_NOCLIP);
  Race_Run_MarkInvalid(&run, RACE_INVALID_REPLAY_CAPACITY);
  ck_assert(!Race_Run_IsValid(&run));
  ck_assert_int_eq(run.invalid_flags,
                   RACE_INVALID_NOCLIP | RACE_INVALID_REPLAY_CAPACITY);

  ck_assert(Race_Run_Finish(&run, 0, 200));
  Race_Run_ObserveSpeed(&run, 500.f);
  ck_assert_uint_eq(run.speed_samples, 2);
  ck_assert_float_eq_tol(run.top_speed, 340.f, 0.001f);

  Race_Run_Reset(&run);
  ck_assert_int_eq(run.mode, RACE_MODE_RACE);
  ck_assert_int_eq(run.invalid_flags, RACE_INVALID_NONE);
  ck_assert_float_eq_tol(run.top_speed, 0.f, 0.001f);
  ck_assert_float_eq_tol(Race_Run_AverageSpeed(&run), 0.f, 0.001f);
} END_TEST

START_TEST(_Race_FinishReportWireV1) {
  race_finish_report_t report = {
    .mode = RACE_MODE_RACE,
    .invalid_flags = RACE_INVALID_NOCLIP,
    .elapsed_time = 12345u,
    .previous_pb = 13000u,
    .world_record = 11000u,
    .checkpoint_count = 3u,
    .checkpoint_times = { 3000u, 6500u, 9000u },
    .start_speed = 320.f,
    .end_speed = 480.f,
    .top_speed = 725.f,
    .average_speed = 412.5f
  };
  uint8_t bytes[RACE_FINISH_REPORT_MAX_BYTES];
  const size_t length = Race_FinishReport_Encode(
    &report, bytes, sizeof(bytes));
  ck_assert_uint_eq(length, 38u + 3u * sizeof(uint32_t));
  ck_assert_uint_eq(bytes[0], RACE_FINISH_REPORT_VERSION);
  ck_assert_uint_eq(bytes[1], RACE_MODE_RACE);

  race_finish_report_t parsed;
  ck_assert(Race_FinishReport_Decode(bytes, length, &parsed));
  ck_assert_int_eq(parsed.mode, report.mode);
  ck_assert_uint_eq(parsed.invalid_flags, report.invalid_flags);
  ck_assert_uint_eq(parsed.elapsed_time, report.elapsed_time);
  ck_assert_uint_eq(parsed.previous_pb, report.previous_pb);
  ck_assert_uint_eq(parsed.world_record, report.world_record);
  ck_assert_uint_eq(parsed.checkpoint_count, report.checkpoint_count);
  ck_assert_uint_eq(parsed.checkpoint_times[2], 9000u);
  ck_assert_float_eq_tol(parsed.start_speed, 320.f, 0.001f);
  ck_assert_float_eq_tol(parsed.end_speed, 480.f, 0.001f);
  ck_assert_float_eq_tol(parsed.top_speed, 725.f, 0.001f);
  ck_assert_float_eq_tol(parsed.average_speed, 412.5f, 0.001f);

  ck_assert_uint_eq(Race_FinishReport_Encode(
    &report, bytes, length - 1u), 0u);
  ck_assert(!Race_FinishReport_Decode(bytes, length - 1u, &parsed));

  bytes[0]++;
  ck_assert(!Race_FinishReport_Decode(bytes, length, &parsed));
  bytes[0] = RACE_FINISH_REPORT_VERSION;
  bytes[16] = RACE_MAX_CHECKPOINTS + 1u;
  bytes[17] = 0u;
  ck_assert(!Race_FinishReport_Decode(bytes, length, &parsed));

  report.checkpoint_times[1] = 2000u;
  ck_assert_uint_eq(Race_FinishReport_Encode(
    &report, bytes, sizeof(bytes)), 0u);
  report.checkpoint_times[1] = 6500u;
  report.elapsed_time = RACE_FINISH_REPORT_MAX_TIME_MS + 1u;
  ck_assert_uint_eq(Race_FinishReport_Encode(
    &report, bytes, sizeof(bytes)), 0u);
  report.elapsed_time = 12345u;
  report.mode = RACE_MODE_TOTAL;
  ck_assert_uint_eq(Race_FinishReport_Encode(
    &report, bytes, sizeof(bytes)), 0u);
} END_TEST

START_TEST(_Race_ResetAndRestart) {
  race_run_t run;

  ck_assert(Race_Run_Start(&run, true, 100));
  ck_assert(Race_Run_Checkpoint(&run, 1, 1, 125));

  Race_Run_Reset(&run);
  ck_assert_int_eq(run.state, RACE_RUN_IDLE);
  ck_assert_uint_eq(run.checkpoint_count, 0);
  ck_assert_uint_eq(run.start_time, 0);

  ck_assert(Race_Run_Start(&run, true, 500));
  ck_assert_uint_eq(run.start_time, 500);
  ck_assert(Race_Run_Checkpoint(&run, 1, 1, 525));
  ck_assert(Race_Run_Finish(&run, 1, 550));
  ck_assert_uint_eq(run.elapsed_time, 50);
} END_TEST

START_TEST(_Race_ZeroCheckpointCourse) {
  race_run_t run;

  ck_assert(Race_Run_Start(&run, true, 100));
  ck_assert(Race_Run_Finish(&run, 0, 125));
  ck_assert_uint_eq(run.elapsed_time, 25);
} END_TEST

START_TEST(_Race_ClientIndependence) {
  race_run_t first;
  race_run_t second;

  ck_assert(Race_Run_Start(&first, true, 100));
  ck_assert(Race_Run_Start(&second, true, 200));
  ck_assert(Race_Run_Checkpoint(&first, 2, 1, 125));

  ck_assert_uint_eq(first.checkpoint_count, 1);
  ck_assert_uint_eq(second.checkpoint_count, 0);
  ck_assert_uint_eq(first.start_time, 100);
  ck_assert_uint_eq(second.start_time, 200);

  Race_Run_Reset(&first);
  ck_assert_int_eq(first.state, RACE_RUN_IDLE);
  ck_assert_int_eq(second.state, RACE_RUN_ACTIVE);
} END_TEST

START_TEST(_Race_TriggerDebounce) {
  uint32_t first = UINT32_MAX;
  uint32_t second = UINT32_MAX;

  ck_assert(!Race_Trigger_Debounced(&first, 1000, 500));
  ck_assert(Race_Trigger_Debounced(&first, 1499, 500));
  ck_assert(!Race_Trigger_Debounced(&first, 1500, 500));

  ck_assert(!Race_Trigger_Debounced(&second, 1100, 500));
  ck_assert(Race_Trigger_Debounced(&second, 1200, 500));
  ck_assert(!Race_Trigger_Debounced(&first, 2000, 500));
} END_TEST

START_TEST(_Race_Modes) {
  race_run_t run;
  Race_Run_Reset(&run);

  race_mode_t mode = RACE_MODE_RACE;
  ck_assert_int_eq(mode, 0);

  ck_assert(Race_Mode_Transition(&mode, &run, RACE_MODE_PRACTICE));
  ck_assert_int_eq(mode, RACE_MODE_PRACTICE);
  ck_assert_int_eq(run.state, RACE_RUN_IDLE);

  ck_assert(Race_Run_Start(&run, true, 100));
  ck_assert(Race_Mode_Transition(&mode, &run, RACE_MODE_RACE));
  ck_assert_int_eq(mode, RACE_MODE_RACE);
  ck_assert_int_eq(run.state, RACE_RUN_IDLE);
  ck_assert_uint_eq(run.start_time, 0);

  ck_assert(Race_Mode_Transition(&mode, &run, RACE_MODE_SPECTATOR));
  ck_assert_int_eq(mode, RACE_MODE_SPECTATOR);
  ck_assert(Race_Mode_Transition(&mode, &run, RACE_MODE_PRACTICE));
  ck_assert_int_eq(mode, RACE_MODE_PRACTICE);
  ck_assert(Race_Mode_Transition(&mode, &run, RACE_MODE_SPECTATOR));
  ck_assert(Race_Mode_Transition(&mode, &run, RACE_MODE_RACE));

  ck_assert(!Race_Mode_Transition(&mode, &run, RACE_MODE_RACE));
  ck_assert(!Race_Mode_Transition(&mode, &run, RACE_MODE_TOTAL));

  ck_assert(!Race_Mode_AllowsHook(RACE_MODE_RACE));
  ck_assert(Race_Mode_AllowsHook(RACE_MODE_PRACTICE));
  ck_assert(!Race_Mode_AllowsHook(RACE_MODE_SPECTATOR));
  ck_assert(!Race_Mode_AllowsHook(RACE_MODE_TOTAL));
} END_TEST

START_TEST(_Race_DamagePolicy) {
  int32_t damage = 100;
  int32_t knockback = 75;

  ck_assert(Race_DamagePolicy(false, true, false, &damage, &knockback));
  ck_assert_int_eq(damage, 100);
  ck_assert_int_eq(knockback, 75);

  damage = 100;
  knockback = 75;
  ck_assert(Race_DamagePolicy(true, false, false, &damage, &knockback));
  ck_assert_int_eq(damage, 0);
  ck_assert_int_eq(knockback, 75);

  damage = 100;
  knockback = 75;
  ck_assert(Race_DamagePolicy(true, true, true, &damage, &knockback));
  ck_assert_int_eq(damage, 0);
  ck_assert_int_eq(knockback, 75);

  damage = 100;
  knockback = 75;
  ck_assert(!Race_DamagePolicy(true, true, false, &damage, &knockback));
  ck_assert_int_eq(damage, 0);
  ck_assert_int_eq(knockback, 0);
} END_TEST

START_TEST(_Race_ModeClientIndependence) {
  race_mode_t first_mode = RACE_MODE_RACE;
  race_mode_t second_mode = RACE_MODE_RACE;
  race_run_t first_run;
  race_run_t second_run;

  ck_assert(Race_Run_Start(&first_run, true, 100));
  ck_assert(Race_Run_Start(&second_run, true, 200));
  ck_assert(Race_Mode_Transition(&first_mode, &first_run, RACE_MODE_PRACTICE));

  ck_assert_int_eq(first_mode, RACE_MODE_PRACTICE);
  ck_assert_int_eq(first_run.state, RACE_RUN_IDLE);
  ck_assert_int_eq(second_mode, RACE_MODE_RACE);
  ck_assert_int_eq(second_run.state, RACE_RUN_ACTIVE);
  ck_assert_uint_eq(second_run.start_time, 200);
} END_TEST

START_TEST(_Race_AutoStart) {
  race_run_t run;
  Race_Run_Reset(&run);

  ck_assert(Race_Run_ShouldAutoStart(RACE_MODE_RACE, &run, true, true, 1, 0, 0));
  ck_assert(Race_Run_ShouldAutoStart(RACE_MODE_RACE, &run, true, true, 0, -1, 0));
  ck_assert(Race_Run_ShouldAutoStart(RACE_MODE_RACE, &run, true, true, 0, 0, 1));
  ck_assert(Race_Run_ShouldAutoStart(RACE_MODE_RACE, &run, true, true, 0, 0, -1));
  ck_assert(Race_Run_ShouldAutoStart(RACE_MODE_PRACTICE, &run, true, true, 1, 0, 0));

  ck_assert(!Race_Run_ShouldAutoStart(RACE_MODE_SPECTATOR, &run, true, true, 1, 0, 0));
  ck_assert(!Race_Run_ShouldAutoStart(RACE_MODE_RACE, &run, true, true, 0, 0, 0));
  ck_assert(!Race_Run_ShouldAutoStart(RACE_MODE_RACE, &run, false, true, 1, 0, 0));
  ck_assert(!Race_Run_ShouldAutoStart(RACE_MODE_RACE, &run, true, false, 1, 0, 0));

  ck_assert(Race_Run_Start(&run, true, 100));
  ck_assert(!Race_Run_ShouldAutoStart(RACE_MODE_RACE, &run, true, true, 1, 0, 0));
  ck_assert_uint_eq(run.start_time, 100);

  ck_assert(Race_Run_Finish(&run, 0, 125));
  ck_assert(!Race_Run_ShouldAutoStart(RACE_MODE_RACE, &run, true, true, 1, 0, 0));
} END_TEST

START_TEST(_Race_Elapsed) {
  race_run_t run;
  Race_Run_Reset(&run);
  ck_assert_uint_eq(Race_Run_Elapsed(&run, 500), 0);

  ck_assert(Race_Run_Start(&run, true, 100));
  ck_assert_uint_eq(Race_Run_Elapsed(&run, 225), 125);

  ck_assert(Race_Run_Finish(&run, 0, 250));
  ck_assert_uint_eq(Race_Run_Elapsed(&run, 1000), 150);

  Race_Run_Reset(&run);
  ck_assert_uint_eq(Race_Run_Elapsed(&run, 1000), 0);
} END_TEST

START_TEST(_Race_WireElapsed) {
  const uint32_t values[] = {
    0u, 1u, UINT32_C(0x7fff), UINT32_C(0x8000), UINT32_C(0xffff),
    UINT32_C(0x10000), UINT32_C(0x12345678), UINT32_MAX
  };

  for (size_t i = 0; i < RACE_TEST_LENGTHOF(values); i++) {
    const int16_t low = Race_WireElapsedLow(values[i]);
    const int16_t high = Race_WireElapsedHigh(values[i]);
    ck_assert_uint_eq(Race_WireElapsed(low, high), values[i]);
  }
} END_TEST

START_TEST(_Race_StoredSpawn) {
  race_stored_spawn_t spawn = { 0 };
  const vec3_t stored_origin = Vec3(128.f, -64.f, 96.f);
  const vec3_t stored_angles = Vec3(-10.f, 135.f, 0.f);
  vec3_t origin = Vec3_Zero();
  vec3_t angles = Vec3_Zero();

  ck_assert(!Race_StoredSpawn_Capture(&spawn, RACE_MODE_RACE, false, true,
                                      stored_origin, stored_angles));
  ck_assert(!Race_StoredSpawn_Capture(&spawn, RACE_MODE_PRACTICE, true, true,
                                      stored_origin, stored_angles));
  ck_assert(!Race_StoredSpawn_Capture(&spawn, RACE_MODE_PRACTICE, false, false,
                                      stored_origin, stored_angles));
  ck_assert(!Race_StoredSpawn_Get(&spawn, RACE_MODE_PRACTICE, false, &origin, &angles));

  // Capture has no grounded input: storing while airborne is intentionally valid.
  ck_assert(Race_StoredSpawn_Capture(&spawn, RACE_MODE_PRACTICE, false, true,
                                     stored_origin, stored_angles));
  ck_assert(!Race_StoredSpawn_Get(&spawn, RACE_MODE_RACE, false, &origin, &angles));
  ck_assert(!Race_StoredSpawn_Get(&spawn, RACE_MODE_PRACTICE, true, &origin, &angles));
  ck_assert(Race_StoredSpawn_Get(&spawn, RACE_MODE_PRACTICE, false, &origin, &angles));
  ck_assert(Vec3_Equal(origin, stored_origin));
  ck_assert(Vec3_Equal(angles, stored_angles));

  Race_StoredSpawn_Clear(&spawn);
  ck_assert(!Race_StoredSpawn_Get(&spawn, RACE_MODE_PRACTICE, false, &origin, &angles));
} END_TEST

START_TEST(_Race_StoredSpawnClientIndependence) {
  race_stored_spawn_t first = { 0 };
  race_stored_spawn_t second = { 0 };
  vec3_t origin, angles;

  ck_assert(Race_StoredSpawn_Capture(&first, RACE_MODE_PRACTICE, false, true,
                                     Vec3(1.f, 2.f, 3.f), Vec3(4.f, 5.f, 6.f)));
  ck_assert(Race_StoredSpawn_Capture(&second, RACE_MODE_PRACTICE, false, true,
                                     Vec3(10.f, 20.f, 30.f), Vec3(40.f, 50.f, 60.f)));

  Race_StoredSpawn_Clear(&first);
  ck_assert(!Race_StoredSpawn_Get(&first, RACE_MODE_PRACTICE, false, &origin, &angles));
  ck_assert(Race_StoredSpawn_Get(&second, RACE_MODE_PRACTICE, false, &origin, &angles));
  ck_assert(Vec3_Equal(origin, Vec3(10.f, 20.f, 30.f)));
  ck_assert(Vec3_Equal(angles, Vec3(40.f, 50.f, 60.f)));
} END_TEST

START_TEST(_Race_MovementCollisionPolicy) {
  const int32_t game_player_mask = Race_MovementClipMask(CONTENTS_MASK_CLIP_PLAYER);
  const int32_t cgame_prediction_mask = Race_MovementClipMask(CONTENTS_MASK_CLIP_PLAYER);
  const int32_t game_ai_mask = Race_MovementClipMask(CONTENTS_MASK_CLIP_MONSTER);

  ck_assert_int_eq(game_player_mask, cgame_prediction_mask);
  ck_assert_int_eq(game_player_mask & CONTENTS_MONSTER, 0);
  ck_assert_int_eq(game_player_mask, CONTENTS_MASK_CLIP_CORPSE);
  ck_assert_int_ne(game_player_mask & CONTENTS_PLAYER_CLIP, 0);
  ck_assert_int_ne(game_player_mask & CONTENTS_SOLID, 0);
  ck_assert_int_ne(game_ai_mask & CONTENTS_MONSTER_CLIP, 0);
  ck_assert_int_eq(game_ai_mask & CONTENTS_MONSTER, 0);
} END_TEST

START_TEST(_Race_PresentationLabels) {
  ck_assert_str_eq(Cg_Race_ModeLabel(RACE_MODE_RACE), "Race");
  ck_assert_str_eq(Cg_Race_ModeLabel(RACE_MODE_PRACTICE), "Practice");
  ck_assert_str_eq(Cg_Race_ModeLabel(RACE_MODE_SPECTATOR), "Spectator");
  ck_assert_str_eq(Cg_Race_ModeLabel(RACE_MODE_TOTAL), "Unknown");

  ck_assert_str_eq(Cg_Race_RunStateLabel(RACE_RUN_IDLE), "Idle");
  ck_assert_str_eq(Cg_Race_RunStateLabel(RACE_RUN_ACTIVE), "Running");
  ck_assert_str_eq(Cg_Race_RunStateLabel(RACE_RUN_FINISHED), "Finished");
  ck_assert_str_eq(Cg_Race_RunStateLabel(RACE_RUN_TOTAL), "Unknown");
} END_TEST

START_TEST(_Race_PresentationFormatting) {
  char elapsed[32];

  Cg_Race_FormatElapsed(0u, elapsed, sizeof(elapsed));
  ck_assert_str_eq(elapsed, "0:00.000");

  Cg_Race_FormatElapsed(2725u, elapsed, sizeof(elapsed));
  ck_assert_str_eq(elapsed, "0:02.725");

  Cg_Race_FormatElapsed(61025u, elapsed, sizeof(elapsed));
  ck_assert_str_eq(elapsed, "1:01.025");

  Cg_Race_FormatElapsed(UINT32_MAX, elapsed, sizeof(elapsed));
  ck_assert_str_eq(elapsed, "71582:47.295");

  ck_assert_uint_eq(Cg_Race_CheckpointProgress(0u, 3u), 0u);
  ck_assert_uint_eq(Cg_Race_CheckpointProgress(2u, 3u), 2u);
  ck_assert_uint_eq(Cg_Race_CheckpointProgress(4u, 3u), 3u);
  ck_assert_uint_eq(Cg_Race_CheckpointProgress(64u, 64u), 64u);
} END_TEST

START_TEST(_Race_PresentationMarkers) {
  cg_race_marker_descriptor_t marker;

  ck_assert(Cg_Race_DescribeMarker("trigger_race_start", false, 0, &marker));
  ck_assert_int_eq(marker.type, CG_RACE_MARKER_START);

  ck_assert(Cg_Race_DescribeMarker("trigger_race_cp", true, 2, &marker));
  ck_assert_int_eq(marker.type, CG_RACE_MARKER_CHECKPOINT);
  ck_assert_uint_eq(marker.checkpoint, 2u);

  cg_race_marker_descriptor_t duplicate;
  ck_assert(Cg_Race_DescribeMarker("trigger_race_cp", true, 2, &duplicate));
  ck_assert_int_eq(duplicate.type, marker.type);
  ck_assert_uint_eq(duplicate.checkpoint, marker.checkpoint);

  ck_assert(Cg_Race_DescribeMarker("trigger_race_finish", false, 0, &marker));
  ck_assert_int_eq(marker.type, CG_RACE_MARKER_FINISH);

  ck_assert(!Cg_Race_DescribeMarker("trigger_race_cp", false, 2, &marker));
  ck_assert(!Cg_Race_DescribeMarker("trigger_race_cp", true, 0, &marker));
  ck_assert(!Cg_Race_DescribeMarker("trigger_race_cp", true,
                                    RACE_MAX_CHECKPOINTS + 1, &marker));
  ck_assert(!Cg_Race_DescribeMarker("trigger_multiple", true, 2, &marker));
  ck_assert(!Cg_Race_DescribeMarker(NULL, true, 2, &marker));
  ck_assert(!Cg_Race_DescribeMarker("trigger_race_start", false, 0, NULL));
} END_TEST

START_TEST(_Race_PresentationSpeed) {
  ck_assert_int_eq(Cg_Race_HorizontalSpeed(Vec3_Zero()), 0);
  ck_assert_int_eq(Cg_Race_HorizontalSpeed(Vec3(3.f, 4.f, 1000.f)), 5);
  ck_assert_int_eq(Cg_Race_HorizontalSpeed(Vec3(400.4f, 0.f, 0.f)), 400);
  ck_assert_int_eq(Cg_Race_HorizontalSpeed(Vec3(400.6f, 0.f, -500.f)), 401);
} END_TEST

START_TEST(_Race_HudLayoutVisibilityAndClimb) {
  const cg_race_hud_layout_t race = Cg_Race_HudLayout(1920, 1080, 64);
  const cg_race_hud_layout_t practice = Cg_Race_HudLayout(1024, 768, 128);

  ck_assert_int_eq(race.bar_y, 1016);
  ck_assert_int_eq(race.mode_x + 32, 960);
  ck_assert_int_gt(race.status_width, 0);
  ck_assert_int_eq(practice.bar_y, 704);
  ck_assert_int_eq(practice.mode_x + 64, 512);
  ck_assert_int_gt(practice.status_width, 0);

  ck_assert(Cg_Race_RunHudVisible(true, false, false, false, false, false));
  ck_assert(Cg_Race_RunHudVisible(true, false, false, false, true, true));
  ck_assert(!Cg_Race_RunHudVisible(false, false, false, false, false, false));
  ck_assert(!Cg_Race_RunHudVisible(true, true, false, false, false, false));
  ck_assert(!Cg_Race_RunHudVisible(true, false, true, false, false, false));
  ck_assert(!Cg_Race_RunHudVisible(true, false, false, true, false, false));
  ck_assert(!Cg_Race_RunHudVisible(true, false, false, false, true, false));

  ck_assert_int_eq(Cg_Race_ClimbState(31.99f), CG_RACE_CLIMB_READY);
  ck_assert_int_eq(Cg_Race_ClimbState(32.f), CG_RACE_CLIMB_CLOSER);
  ck_assert_int_eq(Cg_Race_ClimbState(63.99f), CG_RACE_CLIMB_CLOSER);
  ck_assert_int_eq(Cg_Race_ClimbState(64.f), CG_RACE_CLIMB_TOO_FAR);
  ck_assert_str_eq(Cg_Race_ClimbLabel(CG_RACE_CLIMB_READY), "CLIMB");
  ck_assert_str_eq(Cg_Race_ClimbLabel(CG_RACE_CLIMB_CLOSER), "CLOSER");
  ck_assert_str_eq(Cg_Race_ClimbLabel(CG_RACE_CLIMB_TOO_FAR), "TOO FAR");
} END_TEST

START_TEST(_Race_ProfileIdentity) {
  char canonical[RACE_PROFILE_UID_SIZE];
  ck_assert(Race_Profile_CanonicalizeUid(
    "01234567-89AB-4CDE-8F01-23456789ABCD", canonical));
  ck_assert_str_eq(canonical, RACE_TEST_UID_A);

  race_profile_t original;
  race_profile_t renamed;
  race_profile_t same_name;
  ck_assert(Race_Profile_Init(&original, RACE_TEST_UID_A, "Runner"));
  ck_assert(Race_Profile_Init(&renamed, RACE_TEST_UID_A, "Renamed"));
  ck_assert(Race_Profile_Init(&same_name, RACE_TEST_UID_B, "Runner"));
  ck_assert_str_eq(original.uid, renamed.uid);
  ck_assert_str_ne(original.uid, same_name.uid);

  char original_path[128], original_candidate[128];
  char renamed_path[128], renamed_candidate[128];
  char other_path[128], other_candidate[128];
  ck_assert(Race_Profile_Paths(original.uid,
                               original_path, sizeof(original_path),
                               original_candidate, sizeof(original_candidate)));
  ck_assert(Race_Profile_Paths(renamed.uid,
                               renamed_path, sizeof(renamed_path),
                               renamed_candidate, sizeof(renamed_candidate)));
  ck_assert(Race_Profile_Paths(same_name.uid,
                               other_path, sizeof(other_path),
                               other_candidate, sizeof(other_candidate)));
  ck_assert_str_eq(original_path, renamed_path);
  ck_assert_str_ne(original_path, other_path);
} END_TEST

START_TEST(_Race_ProfileIdentityPolicy) {
  char uid[RACE_PROFILE_UID_SIZE];

  ck_assert(!Race_Profile_CanonicalizeUid(NULL, uid));
  ck_assert(!Race_Profile_CanonicalizeUid("", uid));
  ck_assert(!Race_Profile_CanonicalizeUid("../01234567-89ab-4cde-8f01-23456789abcd", uid));
  ck_assert(!Race_Profile_CanonicalizeUid("01234567\\89ab-4cde-8f01-23456789abcd", uid));
  ck_assert(!Race_Profile_CanonicalizeUid("01234567-89ab-3cde-8f01-23456789abcd", uid));
  ck_assert(!Race_Profile_CanonicalizeUid("01234567-89ab-4cde-7f01-23456789abcd", uid));
  ck_assert(!Race_Profile_CanonicalizeUid("01234567-89ab-4cde-8f01-23456789abcd0", uid));

  char maximum_name[RACE_PROFILE_NAME_SIZE];
  memset(maximum_name, 'x', RACE_PROFILE_NAME_MAX);
  maximum_name[RACE_PROFILE_NAME_MAX] = '\0';

  race_profile_t profile;
  ck_assert(Race_Profile_Init(&profile, RACE_TEST_UID_A, maximum_name));
  ck_assert_uint_eq(strlen(profile.display_name), RACE_PROFILE_NAME_MAX);

  char oversized_name[RACE_PROFILE_NAME_SIZE + 1];
  memset(oversized_name, 'x', RACE_PROFILE_NAME_SIZE);
  oversized_name[RACE_PROFILE_NAME_SIZE] = '\0';
  ck_assert(!Race_Profile_Init(&profile, RACE_TEST_UID_A, oversized_name));
} END_TEST

START_TEST(_Race_ProfilePathSafety) {
  char committed[128], candidate[128];
  ck_assert(Race_Profile_Paths(RACE_TEST_UID_A,
                               committed, sizeof(committed),
                               candidate, sizeof(candidate)));
  ck_assert_str_eq(committed,
                   "profiles/" RACE_TEST_UID_A ".profile");
  ck_assert_str_eq(candidate,
                   "profiles/" RACE_TEST_UID_A ".candidate");
  ck_assert_ptr_null(strstr(committed, ".."));
  ck_assert_ptr_null(strchr(committed, '\\'));
  ck_assert_ptr_null(strstr(committed, "Runner"));

  ck_assert(!Race_Profile_Paths("../../escape",
                                committed, sizeof(committed),
                                candidate, sizeof(candidate)));
  ck_assert(!Race_Profile_Paths("01234567/89ab-4cde-8f01-23456789abcd",
                                committed, sizeof(committed),
                                candidate, sizeof(candidate)));
} END_TEST

START_TEST(_Race_ProfileSerialization) {
  race_profile_t profile;
  ck_assert(Race_Profile_Init(&profile, RACE_TEST_UID_A, "^1Runner"));

  char first[RACE_PROFILE_SERIALIZED_MAX];
  char second[RACE_PROFILE_SERIALIZED_MAX];
  size_t first_length, second_length;
  ck_assert(Race_Profile_Serialize(&profile, first, sizeof(first), &first_length));
  ck_assert(Race_Profile_Serialize(&profile, second, sizeof(second), &second_length));
  ck_assert_uint_eq(first_length, second_length);
  ck_assert_int_eq(memcmp(first, second, first_length), 0);
  ck_assert_str_eq(first,
                   "RACE_PROFILE_V1\n"
                   "uid=" RACE_TEST_UID_A "\n"
                   "name=5e3152756e6e6572\n");

  race_profile_t parsed;
  ck_assert_int_eq(Race_Profile_Parse(first, first_length, &parsed),
                   RACE_PROFILE_PARSE_OK);
  ck_assert_str_eq(parsed.uid, profile.uid);
  ck_assert_str_eq(parsed.display_name, profile.display_name);
} END_TEST

START_TEST(_Race_ProfileSerializationBounds) {
  race_profile_t profile;
  ck_assert(Race_Profile_Init(&profile, RACE_TEST_UID_A, ""));

  char serialized[RACE_PROFILE_SERIALIZED_MAX];
  size_t serialized_length;
  ck_assert(Race_Profile_Serialize(&profile, serialized, sizeof(serialized),
                                   &serialized_length));

  race_profile_t parsed;
  ck_assert_int_eq(Race_Profile_Parse(serialized, serialized_length, &parsed),
                   RACE_PROFILE_PARSE_OK);
  ck_assert_str_eq(parsed.display_name, "");

  char maximum_name[RACE_PROFILE_NAME_SIZE];
  memset(maximum_name, 'z', RACE_PROFILE_NAME_MAX);
  maximum_name[RACE_PROFILE_NAME_MAX] = '\0';
  ck_assert(Race_Profile_Init(&profile, RACE_TEST_UID_A, maximum_name));
  ck_assert(Race_Profile_Serialize(&profile, serialized, sizeof(serialized),
                                   &serialized_length));
  ck_assert_int_eq(Race_Profile_Parse(serialized, serialized_length, &parsed),
                   RACE_PROFILE_PARSE_OK);
  ck_assert_str_eq(parsed.display_name, maximum_name);

  const char opaque_name[] = { (char) 0xc3, '(', '\0' };
  ck_assert(Race_Profile_Init(&profile, RACE_TEST_UID_A, opaque_name));
  ck_assert(Race_Profile_Serialize(&profile, serialized, sizeof(serialized),
                                   &serialized_length));
  ck_assert_int_eq(Race_Profile_Parse(serialized, serialized_length, &parsed),
                   RACE_PROFILE_PARSE_OK);
  ck_assert_int_eq(memcmp(parsed.display_name, opaque_name, sizeof(opaque_name)), 0);
} END_TEST

START_TEST(_Race_ProfileMalformedInput) {
  static const char *malformed[] = {
    "",
    "RACE_PROFILE_V1",
    "RACE_PROFILE_V1\nuid=" RACE_TEST_UID_A "\n",
    "RACE_PROFILE_V1\nname=52\nuid=" RACE_TEST_UID_A "\n",
    "RACE_PROFILE_V1\nuid=" RACE_TEST_UID_A "\nname=5\n",
    "RACE_PROFILE_V1\nuid=" RACE_TEST_UID_A "\nname=GG\n",
    "RACE_PROFILE_V1\nuid=" RACE_TEST_UID_A "\nname=00\n",
    "RACE_PROFILE_V1\nuid=01234567-89AB-4CDE-8F01-23456789ABCD\nname=52\n",
    "RACE_PROFILE_V1\nuid=" RACE_TEST_UID_A "\nuid=" RACE_TEST_UID_A "\nname=52\n",
    "RACE_PROFILE_V1\nuid=" RACE_TEST_UID_A "\nname=52\nunknown=value\n",
    "RACE_PROFILES_V1\nuid=" RACE_TEST_UID_A "\nname=52\n"
  };

  race_profile_t parsed;
  for (size_t i = 0; i < RACE_TEST_LENGTHOF(malformed); i++) {
    ck_assert_int_eq(Race_Profile_Parse(malformed[i], strlen(malformed[i]), &parsed),
                     RACE_PROFILE_PARSE_MALFORMED);
  }

  static const char unknown_version[] =
    "RACE_PROFILE_V2\nuid=" RACE_TEST_UID_A "\nname=52\n";
  ck_assert_int_eq(Race_Profile_Parse(unknown_version, strlen(unknown_version), &parsed),
                   RACE_PROFILE_PARSE_UNKNOWN_VERSION);

  char oversized[RACE_PROFILE_SERIALIZED_MAX + 1];
  memset(oversized, 'x', sizeof(oversized));
  ck_assert_int_eq(Race_Profile_Parse(oversized, sizeof(oversized), &parsed),
                   RACE_PROFILE_PARSE_TOO_LARGE);

  static const char embedded_nul[] =
    "RACE_PROFILE_V1\nuid=" RACE_TEST_UID_A "\nname=52\n\0trailing";
  ck_assert_int_eq(Race_Profile_Parse(embedded_nul, sizeof(embedded_nul) - 1, &parsed),
                   RACE_PROFILE_PARSE_MALFORMED);
} END_TEST

START_TEST(_Race_PersistenceCandidatePromotion) {
  char buffer[64];
  size_t length;

  ck_assert_int_eq(Race_Persistence_Read(race_test_committed, buffer, sizeof(buffer), &length),
                   RACE_PERSISTENCE_NOT_FOUND);
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_candidate, "stale", 5),
                   RACE_PERSISTENCE_OK);
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_candidate, "fresh", 5),
                   RACE_PERSISTENCE_OK);
  Race_TestPersistenceRead(race_test_candidate, buffer, sizeof(buffer), "fresh");

  ck_assert_int_eq(Race_Persistence_Promote(race_test_candidate, race_test_committed),
                   RACE_PERSISTENCE_OK);
  ck_assert_int_eq(Race_Persistence_Read(race_test_candidate, buffer, sizeof(buffer), &length),
                   RACE_PERSISTENCE_NOT_FOUND);
  Race_TestPersistenceRead(race_test_committed, buffer, sizeof(buffer), "fresh");
} END_TEST

START_TEST(_Race_PersistenceRealPathBounds) {
  static const char virtual_path[] = "race/profiles/runner.profile";
  static const char real_path[] = "/write/root/race/profiles/runner.profile";
  char copied[sizeof(real_path)];

  ck_assert(Race_Persistence_CopyRealPath(virtual_path, real_path,
                                          copied, sizeof(copied)));
  ck_assert_str_eq(copied, real_path);

  static const char windows_path[] =
    "C:\\write\\root\\race/profiles/runner.profile";
  char windows_copy[sizeof(windows_path)];
  ck_assert(Race_Persistence_CopyRealPath(virtual_path, windows_path,
                                          windows_copy,
                                          sizeof(windows_copy)));
  ck_assert_str_eq(windows_copy, windows_path);

  ck_assert(!Race_Persistence_CopyRealPath(
    virtual_path, "/write/root/race/profiles/runner.pro",
    copied, sizeof(copied)));
  ck_assert(!Race_Persistence_CopyRealPath(
    virtual_path, "/write/root/not-race/profiles/runner.profile",
    copied, sizeof(copied)));
  ck_assert(!Race_Persistence_CopyRealPath(
    virtual_path, real_path, copied, strlen(real_path)));
  ck_assert(!Race_Persistence_CopyRealPath(
    virtual_path, virtual_path, copied, sizeof(copied)));
} END_TEST

START_TEST(_Race_PersistenceFailures) {
  char buffer[64];
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_committed, "committed", 9),
                   RACE_PERSISTENCE_OK);

  char unavailable[RACE_TEST_PATH_SIZE];
  snprintf(unavailable, sizeof(unavailable), "%s/missing/candidate", race_test_directory);
  ck_assert_int_eq(Race_Persistence_WriteCandidate(unavailable, "replacement", 11),
                   RACE_PERSISTENCE_IO_ERROR);
  Race_TestPersistenceRead(race_test_committed, buffer, sizeof(buffer), "committed");

  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_candidate, "replacement", 11),
                   RACE_PERSISTENCE_OK);
  ck_assert_int_eq(remove(race_test_candidate), 0);
  ck_assert_int_eq(Race_Persistence_Promote(race_test_candidate, race_test_committed),
                   RACE_PERSISTENCE_NOT_FOUND);
  Race_TestPersistenceRead(race_test_committed, buffer, sizeof(buffer), "committed");
} END_TEST

START_TEST(_Race_PersistenceBoundsAndCorruption) {
  static const char malformed[] = "not a profile";
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_committed,
                                                   malformed, sizeof(malformed) - 1),
                   RACE_PERSISTENCE_OK);

  char buffer[RACE_PROFILE_SERIALIZED_MAX];
  size_t length;
  ck_assert_int_eq(Race_Persistence_Read(race_test_committed,
                                        buffer, sizeof(buffer), &length),
                   RACE_PERSISTENCE_OK);

  race_profile_t profile;
  ck_assert_int_eq(Race_Profile_Parse(buffer, length, &profile),
                   RACE_PROFILE_PARSE_MALFORMED);
  Race_TestPersistenceRead(race_test_committed, buffer, sizeof(buffer), malformed);

  char oversized[RACE_PROFILE_SERIALIZED_MAX + 1];
  memset(oversized, 'x', sizeof(oversized));
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_candidate,
                                                   oversized, sizeof(oversized)),
                   RACE_PERSISTENCE_OK);
  ck_assert_int_eq(Race_Persistence_Read(race_test_candidate,
                                        buffer, sizeof(buffer), &length),
                   RACE_PERSISTENCE_TOO_LARGE);
  ck_assert_int_eq(Race_Persistence_Read(race_test_directory,
                                        buffer, sizeof(buffer), &length),
                   RACE_PERSISTENCE_NOT_REGULAR);
} END_TEST

START_TEST(_Race_PersistenceReconnectAndRename) {
  race_profile_t first;
  ck_assert(Race_Profile_Init(&first, RACE_TEST_UID_A, "First name"));

  char serialized[RACE_PROFILE_SERIALIZED_MAX];
  size_t serialized_length;
  ck_assert(Race_Profile_Serialize(&first, serialized, sizeof(serialized),
                                   &serialized_length));
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_candidate,
                                                   serialized, serialized_length),
                   RACE_PERSISTENCE_OK);
  ck_assert_int_eq(Race_Persistence_Promote(race_test_candidate, race_test_committed),
                   RACE_PERSISTENCE_OK);

  char loaded[RACE_PROFILE_SERIALIZED_MAX];
  size_t loaded_length;
  ck_assert_int_eq(Race_Persistence_Read(race_test_committed,
                                        loaded, sizeof(loaded), &loaded_length),
                   RACE_PERSISTENCE_OK);

  race_profile_t reconnected;
  ck_assert_int_eq(Race_Profile_Parse(loaded, loaded_length, &reconnected),
                   RACE_PROFILE_PARSE_OK);
  ck_assert_str_eq(reconnected.uid, first.uid);

  ck_assert(Race_Profile_SetDisplayName(&reconnected, "Second name"));
  ck_assert(Race_Profile_Serialize(&reconnected, serialized, sizeof(serialized),
                                   &serialized_length));
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_candidate,
                                                   serialized, serialized_length),
                   RACE_PERSISTENCE_OK);
  ck_assert_int_eq(Race_Persistence_Promote(race_test_candidate, race_test_committed),
                   RACE_PERSISTENCE_OK);

  ck_assert_int_eq(Race_Persistence_Read(race_test_committed,
                                        loaded, sizeof(loaded), &loaded_length),
                   RACE_PERSISTENCE_OK);
  race_profile_t restarted;
  ck_assert_int_eq(Race_Profile_Parse(loaded, loaded_length, &restarted),
                   RACE_PROFILE_PARSE_OK);
  ck_assert_str_eq(restarted.uid, first.uid);
  ck_assert_str_eq(restarted.display_name, "Second name");
} END_TEST

START_TEST(_Race_LeaderboardPbWrAndIdentity) {
  race_leaderboard_record_t records[4] = { 0 };
  size_t count = 0;
  race_leaderboard_evaluation_t evaluation;

  const uint32_t first_splits[] = { 300u, 700u };
  race_leaderboard_record_t first = Race_TestRecord(RACE_TEST_UID_A, "Runner",
                                                    1000u, first_splits,
                                                    RACE_TEST_LENGTHOF(first_splits));
  ck_assert(Race_Leaderboard_Evaluate(records, count, RACE_TEST_LENGTHOF(records),
                                      &first, &evaluation));
  ck_assert(evaluation.valid);
  ck_assert(evaluation.would_accept);
  ck_assert(evaluation.personal_best);
  ck_assert(evaluation.world_record);
  ck_assert(evaluation.first_completion);
  ck_assert(evaluation.top);
  ck_assert_uint_eq(evaluation.top_rank, 1u);
  ck_assert(Race_Leaderboard_Apply(records, &count, RACE_TEST_LENGTHOF(records),
                                   &first, &evaluation));
  ck_assert_uint_eq(count, 1u);

  race_leaderboard_record_t tied = Race_TestRecord(RACE_TEST_UID_A, "Renamed",
                                                   1000u, first_splits,
                                                   RACE_TEST_LENGTHOF(first_splits));
  ck_assert(Race_Leaderboard_Evaluate(records, count, RACE_TEST_LENGTHOF(records),
                                      &tied, &evaluation));
  ck_assert(!evaluation.would_accept);
  ck_assert(!evaluation.personal_best);
  ck_assert(!Race_Leaderboard_Apply(records, &count, RACE_TEST_LENGTHOF(records),
                                    &tied, &evaluation));
  ck_assert_str_eq(records[0].display_name, "Runner");

  race_leaderboard_record_t slower = Race_TestRecord(RACE_TEST_UID_A, "Renamed",
                                                     1100u, first_splits,
                                                     RACE_TEST_LENGTHOF(first_splits));
  ck_assert(Race_Leaderboard_Evaluate(records, count, RACE_TEST_LENGTHOF(records),
                                      &slower, &evaluation));
  ck_assert(!evaluation.would_accept);

  const uint32_t faster_splits[] = { 250u, 600u };
  race_leaderboard_record_t faster = Race_TestRecord(RACE_TEST_UID_A, "Renamed",
                                                     900u, faster_splits,
                                                     RACE_TEST_LENGTHOF(faster_splits));
  ck_assert(Race_Leaderboard_Apply(records, &count, RACE_TEST_LENGTHOF(records),
                                   &faster, &evaluation));
  ck_assert(evaluation.personal_best);
  ck_assert(evaluation.world_record);
  ck_assert(!evaluation.first_completion);
  ck_assert_uint_eq(count, 1u);
  ck_assert_str_eq(records[0].display_name, "Renamed");

  race_leaderboard_record_t same_name = Race_TestRecord(RACE_TEST_UID_B, "Renamed",
                                                        900u, faster_splits,
                                                        RACE_TEST_LENGTHOF(faster_splits));
  ck_assert(Race_Leaderboard_Apply(records, &count, RACE_TEST_LENGTHOF(records),
                                   &same_name, &evaluation));
  ck_assert(evaluation.personal_best);
  ck_assert(!evaluation.world_record);
  ck_assert_uint_eq(evaluation.top_rank, 2u);
  ck_assert_uint_eq(count, 2u);
  ck_assert_ptr_nonnull(Race_Leaderboard_Find(records, count, RACE_TEST_UID_A));
  ck_assert_ptr_nonnull(Race_Leaderboard_Find(records, count, RACE_TEST_UID_B));

  ck_assert(!Race_Leaderboard_RecordInit(&same_name, NULL, "Unregistered",
                                         1000u, NULL, 0));
  ck_assert(!Race_Leaderboard_RecordInit(&same_name, "", "Unregistered",
                                         1000u, NULL, 0));
} END_TEST

START_TEST(_Race_LeaderboardTopBoundAndUpdate) {
  race_leaderboard_record_t records[20] = { 0 };
  size_t count = 0;

  for (size_t i = 0; i < 16; i++) {
    char uid[RACE_PROFILE_UID_SIZE];
    Race_TestUid(i, uid);
    race_leaderboard_record_t record = Race_TestRecord(uid, "Runner",
                                                       1000u + (uint32_t) i,
                                                       NULL, 0);
    ck_assert(Race_Leaderboard_Apply(records, &count, RACE_TEST_LENGTHOF(records),
                                     &record, NULL));
  }

  const race_leaderboard_record_t *top[RACE_LEADERBOARD_TOP_MAX];
  ck_assert_uint_eq(Race_Leaderboard_Top(records, count, top,
                                         RACE_TEST_LENGTHOF(top)),
                    RACE_LEADERBOARD_TOP_MAX);
  ck_assert_uint_eq(top[0]->elapsed_time, 1000u);
  ck_assert_uint_eq(top[RACE_LEADERBOARD_TOP_MAX - 1]->elapsed_time, 1014u);

  char slow_uid[RACE_PROFILE_UID_SIZE];
  Race_TestUid(15, slow_uid);
  race_leaderboard_record_t improved = Race_TestRecord(slow_uid, "Improved",
                                                       500u, NULL, 0);
  race_leaderboard_evaluation_t evaluation;
  ck_assert(Race_Leaderboard_Apply(records, &count, RACE_TEST_LENGTHOF(records),
                                   &improved, &evaluation));
  ck_assert(evaluation.world_record);
  ck_assert_uint_eq(evaluation.top_rank, 1u);
  ck_assert_uint_eq(count, 16u);

  ck_assert_uint_eq(Race_Leaderboard_Top(records, count, top,
                                         RACE_TEST_LENGTHOF(top)),
                    RACE_LEADERBOARD_TOP_MAX);
  ck_assert_str_eq(top[0]->uid, slow_uid);
  ck_assert_uint_eq(top[0]->elapsed_time, 500u);
  ck_assert_uint_eq(top[RACE_LEADERBOARD_TOP_MAX - 1]->elapsed_time, 1013u);

  race_leaderboard_record_t full_records[1] = { 0 };
  size_t full_count = 0;
  race_leaderboard_record_t first = Race_TestRecord(RACE_TEST_UID_A, "A", 1000u,
                                                    NULL, 0);
  race_leaderboard_record_t second = Race_TestRecord(RACE_TEST_UID_B, "B", 900u,
                                                     NULL, 0);
  ck_assert(Race_Leaderboard_Apply(full_records, &full_count, 1u, &first, NULL));
  ck_assert(Race_Leaderboard_Evaluate(full_records, full_count, 1u,
                                      &second, &evaluation));
  ck_assert(!evaluation.would_accept);
  ck_assert(!Race_Leaderboard_Apply(full_records, &full_count, 1u, &second, NULL));
} END_TEST

START_TEST(_Race_MapIdentityAndPaths) {
  ck_assert(Race_MapState_RulesetValid(RACE_PHYSICS_PRESET_Q2_V1_KEY));
  ck_assert(Race_MapState_RulesetValid(
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1_KEY));
  ck_assert(Race_MapState_RulesetValid(
    RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY));
  ck_assert(!Race_MapState_RulesetValid("../q2-v1"));
  ck_assert(!Race_MapState_RulesetValid("Q2-V1"));

  char canonical[RACE_MAP_IDENTITY_SIZE];
  ck_assert(Race_MapState_CanonicalizeMap("Edge", canonical));
  ck_assert_str_eq(canonical, "edge");
  ck_assert(Race_MapState_CanonicalizeMap("maps/EDGE.bsp", canonical));
  ck_assert_str_eq(canonical, "edge");
  ck_assert(Race_MapState_CanonicalizeMap("race/Long-Jump_1.BSP", canonical));
  ck_assert_str_eq(canonical, "race/long-jump_1");

  static const char *unsafe[] = {
    "", "/edge", "edge/", "edge//other", "../edge", "race/../edge",
    "race/./edge", "C:/edge", "race\\edge", "race edge", "maps/.bsp"
  };
  for (size_t i = 0; i < RACE_TEST_LENGTHOF(unsafe); i++) {
    ck_assert(!Race_MapState_CanonicalizeMap(unsafe[i], canonical));
  }

  char committed[256], candidate[256];
  ck_assert(Race_MapState_Paths(RACE_PHYSICS_PRESET_Q2_V1_KEY,
                                 "maps/EDGE.bsp",
                                 committed, sizeof(committed),
                                 candidate, sizeof(candidate)));
  ck_assert_str_eq(committed,
                   RACE_MAP_STATE_ROOT_DIRECTORY "/q2-v1/65646765.state");
  ck_assert_str_eq(candidate,
                   RACE_MAP_STATE_ROOT_DIRECTORY "/q2-v1/65646765.candidate");
  ck_assert_ptr_null(strstr(committed, ".."));
  ck_assert_ptr_null(strchr(committed, '\\'));
  ck_assert(!Race_MapState_Paths("../q2-v1", "edge",
                                  committed, sizeof(committed),
                                  candidate, sizeof(candidate)));
} END_TEST

START_TEST(_Race_MapStateGeneration) {
  race_leaderboard_record_t current_records[4] = { 0 };
  race_leaderboard_record_t next_records[4] = { 0 };
  race_map_state_t current = Race_TestMapState(current_records,
                                               RACE_TEST_LENGTHOF(current_records), 0u);
  race_map_state_t next = {
    .records = next_records,
    .record_capacity = RACE_TEST_LENGTHOF(next_records)
  };

  race_leaderboard_record_t record = Race_TestRecord(RACE_TEST_UID_A, "Runner",
                                                     1000u, NULL, 0);
  race_leaderboard_evaluation_t evaluation;
  ck_assert(Race_MapState_ApplyCandidate(&current, &record, &next, &evaluation));
  ck_assert_uint_eq(next.generation, 1u);
  ck_assert_uint_eq(next.record_count, 1u);

  race_leaderboard_record_t tied = Race_TestRecord(RACE_TEST_UID_A, "Runner",
                                                   1000u, NULL, 0);
  current = next;
  next.records = current_records;
  next.record_capacity = RACE_TEST_LENGTHOF(current_records);
  ck_assert(!Race_MapState_ApplyCandidate(&current, &tied, &next, &evaluation));
  ck_assert(!evaluation.would_accept);

  race_leaderboard_record_t faster = Race_TestRecord(RACE_TEST_UID_A, "Renamed",
                                                     900u, NULL, 0);
  ck_assert(Race_MapState_ApplyCandidate(&current, &faster, &next, &evaluation));
  ck_assert_uint_eq(next.generation, 2u);
  ck_assert_uint_eq(next.record_count, 1u);

  next.generation = UINT64_MAX;
  current.records = next_records;
  current.record_capacity = RACE_TEST_LENGTHOF(next_records);
  ck_assert(!Race_MapState_ApplyCandidate(&next, &faster, &current, &evaluation));
} END_TEST

START_TEST(_Race_MapStateReplayBackedTransition) {
  race_leaderboard_record_t current_records[4] = { 0 };
  race_leaderboard_record_t next_records[4] = { 0 };
  race_map_state_t current = Race_TestMapState(
    current_records, RACE_TEST_LENGTHOF(current_records), 0u);
  race_map_state_t next = {
    .records = next_records,
    .record_capacity = RACE_TEST_LENGTHOF(next_records)
  };

  race_leaderboard_record_t candidate = Race_TestRecord(
    RACE_TEST_UID_A, "Runner", 1000u, NULL, 0u);
  const uint32_t analytical_splits[] = { 400u, 800u };
  ck_assert(Race_Leaderboard_RecordSetSplits(
    &candidate, analytical_splits, RACE_TEST_LENGTHOF(analytical_splits),
    UINT64_C(0x123456789abcdef0)));
  ck_assert(Race_Leaderboard_RecordSetDate(&candidate, UINT64_C(1722470400)));
  ck_assert(Race_Leaderboard_RecordAttachReplay(
    &candidate, UINT64_C(0x123456789abcdef0)));
  ck_assert(Race_MapState_CanPublishReplay(&current));
  ck_assert(Race_MapState_ApplyCandidate(&current, &candidate, &next, NULL));
  ck_assert(Race_MapState_ReplayBacked(&next));
  ck_assert_uint_eq(next.records[0].replay_id,
                    UINT64_C(0x123456789abcdef0));
  ck_assert_uint_eq(next.records[0].split_count, 2u);

  char serialized[4096];
  size_t serialized_length;
  ck_assert(Race_MapState_Serialize(&next, serialized, sizeof(serialized),
                                    &serialized_length));
  ck_assert_ptr_nonnull(strstr(serialized, RACE_MAP_STATE_MAGIC_V4));
  ck_assert_ptr_nonnull(strstr(serialized,
                              "publication=" RACE_MAP_STATE_PUBLICATION_V2));

  race_leaderboard_record_t parsed_records[4];
  race_map_state_t parsed;
  ck_assert_int_eq(Race_MapState_Parse(
                     serialized, serialized_length, parsed_records,
                     RACE_TEST_LENGTHOF(parsed_records), &parsed),
                   RACE_MAP_STATE_PARSE_OK);
  ck_assert(Race_MapState_Equals(&next, &parsed));
  ck_assert_uint_eq(parsed.records[0].split_times[1], 800u);
  ck_assert_uint_eq(parsed.records[0].split_layout,
                    UINT64_C(0x123456789abcdef0));

  race_leaderboard_record_t pending_records[4] = { 0 };
  race_map_state_t pending = Race_TestMapState(
    pending_records, RACE_TEST_LENGTHOF(pending_records), 1u);
  pending.records[0] = Race_TestRecord(RACE_TEST_UID_B, "Pending", 1100u,
                                       NULL, 0u);
  pending.record_count = 1u;
  ck_assert(!Race_MapState_CanPublishReplay(&pending));
  next.records = next_records;
  next.record_capacity = RACE_TEST_LENGTHOF(next_records);
  ck_assert(!Race_MapState_ApplyCandidate(&pending, &candidate, &next, NULL));
} END_TEST

START_TEST(_Race_MapStateV2Compatibility) {
  race_leaderboard_record_t records[4] = { 0 };
  race_map_state_t state = Race_TestMapState(
    records, RACE_TEST_LENGTHOF(records), 1u);
  state.format = RACE_MAP_STATE_FORMAT_V2;
  records[0] = Race_TestRecord(RACE_TEST_UID_A, "Runner", 1000u, NULL, 0u);
  ck_assert(Race_Leaderboard_RecordAttachReplay(
    records, UINT64_C(0x123456789abcdef0)));
  state.record_count = 1u;

  char serialized[4096];
  size_t serialized_length;
  ck_assert(Race_MapState_Serialize(&state, serialized, sizeof(serialized),
                                    &serialized_length));
  ck_assert_ptr_nonnull(strstr(serialized, RACE_MAP_STATE_MAGIC_V2));

  race_leaderboard_record_t parsed_records[4] = { 0 };
  race_map_state_t parsed;
  ck_assert_int_eq(Race_MapState_Parse(
                     serialized, serialized_length, parsed_records,
                     RACE_TEST_LENGTHOF(parsed_records), &parsed),
                   RACE_MAP_STATE_PARSE_OK);
  ck_assert_int_eq(parsed.format, RACE_MAP_STATE_FORMAT_V2);
  ck_assert_uint_eq(parsed.records[0].date_unix_s, 0u);
  ck_assert_uint_eq(parsed.records[0].replay_id,
                    UINT64_C(0x123456789abcdef0));
  ck_assert(Race_MapState_Equals(&state, &parsed));
} END_TEST

START_TEST(_Race_MapStateV3Compatibility) {
  race_leaderboard_record_t records[2] = { 0 };
  race_map_state_t state = Race_TestMapState(
    records, RACE_TEST_LENGTHOF(records), 1u);
  state.format = RACE_MAP_STATE_FORMAT_V3;
  records[0] = Race_TestRecord(RACE_TEST_UID_A, "Runner", 1000u, NULL, 0u);
  ck_assert(Race_Leaderboard_RecordSetDate(records, UINT64_C(1722470400)));
  ck_assert(Race_Leaderboard_RecordAttachReplay(
    records, UINT64_C(0x123456789abcdef0)));
  state.record_count = 1u;

  char serialized[4096];
  size_t serialized_length;
  ck_assert(Race_MapState_Serialize(&state, serialized, sizeof(serialized),
                                    &serialized_length));
  ck_assert_ptr_nonnull(strstr(serialized, RACE_MAP_STATE_MAGIC_V3));

  race_leaderboard_record_t parsed_records[2] = { 0 };
  race_map_state_t parsed;
  ck_assert_int_eq(Race_MapState_Parse(
                     serialized, serialized_length, parsed_records,
                     RACE_TEST_LENGTHOF(parsed_records), &parsed),
                   RACE_MAP_STATE_PARSE_OK);
  ck_assert_int_eq(parsed.format, RACE_MAP_STATE_FORMAT_V3);
  ck_assert_uint_eq(parsed.records[0].date_unix_s, UINT64_C(1722470400));
  ck_assert_uint_eq(parsed.records[0].split_count, 0u);
  ck_assert(Race_MapState_Equals(&state, &parsed));
} END_TEST

START_TEST(_Race_LeaderboardWireV1) {
  const race_leaderboard_wire_entry_t source[] = {
    { .name = "^1World\\Runner", .time_ms = 1000u,
      .date_unix_s = UINT64_C(1722470400) },
    { .name = "Practice\nName", .time_ms = 1250u,
      .date_unix_s = UINT64_C(1722556800) }
  };
  char wire[MAX_STRING_CHARS];
  ck_assert(Race_LeaderboardWire_Encode(
    source, RACE_TEST_LENGTHOF(source), wire, sizeof(wire)));
  ck_assert_str_eq(wire,
                   "v1\\2\\^1World Runner\\1000\\1722470400"
                   "\\Practice Name\\1250\\1722556800");

  race_leaderboard_wire_entry_t parsed[RACE_LEADERBOARD_TOP_MAX];
  size_t count;
  ck_assert(Race_LeaderboardWire_Decode(
    wire, parsed, RACE_TEST_LENGTHOF(parsed), &count));
  ck_assert_uint_eq(count, RACE_TEST_LENGTHOF(source));
  ck_assert_str_eq(parsed[0].name, "^1World Runner");
  ck_assert_uint_eq(parsed[0].time_ms, 1000u);
  ck_assert_uint_eq(parsed[0].date_unix_s, UINT64_C(1722470400));
  ck_assert_str_eq(parsed[1].name, "Practice Name");

  ck_assert(!Race_LeaderboardWire_Decode(
    "v2\\0", parsed, RACE_TEST_LENGTHOF(parsed), &count));
  ck_assert(!Race_LeaderboardWire_Decode(
    "v1\\1\\Runner\\0\\1722470400",
    parsed, RACE_TEST_LENGTHOF(parsed), &count));
  ck_assert(!Race_LeaderboardWire_Decode(
    "v1\\2\\Slow\\2000\\0\\Fast\\1000\\0",
    parsed, RACE_TEST_LENGTHOF(parsed), &count));
} END_TEST

START_TEST(_Race_MapStateRoundTripAndBounds) {
  race_leaderboard_record_t records[4] = { 0 };
  race_map_state_t state = Race_TestMapState(records, RACE_TEST_LENGTHOF(records), 1u);

  char empty[4096];
  size_t empty_length;
  ck_assert(Race_MapState_Serialize(&state, empty, sizeof(empty), &empty_length));

  race_leaderboard_record_t parsed_empty_records[4];
  race_map_state_t parsed_empty;
  ck_assert_int_eq(Race_MapState_Parse(empty, empty_length,
                                       parsed_empty_records,
                                       RACE_TEST_LENGTHOF(parsed_empty_records),
                                       &parsed_empty),
                   RACE_MAP_STATE_PARSE_OK);
  ck_assert(Race_MapState_Equals(&state, &parsed_empty));

  uint32_t splits[RACE_MAX_CHECKPOINTS];
  for (size_t i = 0; i < RACE_MAX_CHECKPOINTS; i++) {
    splits[i] = (uint32_t) i + 1u;
  }
  records[0] = Race_TestRecord(RACE_TEST_UID_A, "^1Runner", 1000u,
                               splits, RACE_MAX_CHECKPOINTS);
  state.record_count = 1u;
  strcpy(state.ruleset, RACE_PHYSICS_PRESET_Q2_V1_KEY);

  char serialized[16384];
  char repeated[16384];
  size_t serialized_length, repeated_length;
  ck_assert(Race_MapState_Serialize(&state, serialized, sizeof(serialized),
                                    &serialized_length));

  race_leaderboard_record_t parsed_records[16];
  race_map_state_t parsed;
  ck_assert_int_eq(Race_MapState_Parse(serialized, serialized_length,
                                       parsed_records,
                                       RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_OK);
  ck_assert(Race_MapState_Equals(&state, &parsed));
  ck_assert_uint_eq(parsed.records[0].checkpoint_count, RACE_MAX_CHECKPOINTS);

  ck_assert(Race_MapState_Serialize(&parsed, repeated, sizeof(repeated),
                                    &repeated_length));
  ck_assert_uint_eq(serialized_length, repeated_length);
  ck_assert_int_eq(memcmp(serialized, repeated, serialized_length), 0);

  char *oversized = malloc(RACE_MAP_STATE_MAX_FILE_BYTES + 1u);
  ck_assert_ptr_nonnull(oversized);
  memset(oversized, 'x', RACE_MAP_STATE_MAX_FILE_BYTES + 1u);
  ck_assert_int_eq(Race_MapState_Parse(oversized,
                                       RACE_MAP_STATE_MAX_FILE_BYTES + 1u,
                                       parsed_records,
                                       RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_TOO_LARGE);
  free(oversized);
} END_TEST

START_TEST(_Race_MapStateMalformedInput) {
  race_leaderboard_record_t records[4] = { 0 };
  race_map_state_t state = Race_TestMapState(records, RACE_TEST_LENGTHOF(records), 1u);
  records[0] = Race_TestRecord(RACE_TEST_UID_A, "Runner", 1000u, NULL, 0);
  records[1] = Race_TestRecord(RACE_TEST_UID_B, "Runner", 1100u, NULL, 0);
  state.record_count = 2u;

  char original[8192];
  size_t original_length;
  ck_assert(Race_MapState_Serialize(&state, original, sizeof(original),
                                    &original_length));

  race_leaderboard_record_t parsed_records[16];
  race_map_state_t parsed;
  char malformed[8192];

  memcpy(malformed, original, original_length + 1u);
  malformed[strlen(RACE_MAP_STATE_MAGIC) - 1u] = '9';
  ck_assert_int_eq(Race_MapState_Parse(malformed, original_length,
                                       parsed_records, RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_UNKNOWN_VERSION);

  static const char legacy[] = "# RACE_MAP_STATE_V1\n";
  ck_assert_int_eq(Race_MapState_Parse(legacy, sizeof(legacy) - 1u,
                                       parsed_records, RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_LEGACY_UNSUPPORTED);

  memcpy(malformed, original, original_length + 1u);
  char *ruleset = strstr(malformed,
                         "ruleset=" RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY);
  ck_assert_ptr_nonnull(ruleset);
  ruleset[8] = '/';
  ck_assert_int_eq(Race_MapState_Parse(malformed, original_length,
                                       parsed_records, RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_UNSUPPORTED_RULESET);

  memcpy(malformed, original, original_length + 1u);
  char *generation = strstr(malformed, "generation=1");
  ck_assert_ptr_nonnull(generation);
  generation[strlen("generation=")] = '0';
  ck_assert_int_eq(Race_MapState_Parse(malformed, original_length,
                                       parsed_records, RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_MALFORMED);

  memcpy(malformed, original, original_length + 1u);
  char *count = strstr(malformed, "records=2");
  ck_assert_ptr_nonnull(count);
  count[strlen("records=")] = '9';
  ck_assert_int_eq(Race_MapState_Parse(malformed, original_length,
                                       parsed_records, RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_MALFORMED);

  memcpy(malformed, original, original_length + 1u);
  char *first_row = strstr(malformed, "record=");
  ck_assert_ptr_nonnull(first_row);
  first_row[strlen("record=")] = 'g';
  ck_assert_int_eq(Race_MapState_Parse(malformed, original_length,
                                       parsed_records, RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_MALFORMED);

  memcpy(malformed, original, original_length + 1u);
  first_row = strstr(malformed, "record=");
  char *second_row = strstr(first_row + strlen("record="), "record=");
  ck_assert_ptr_nonnull(second_row);
  memcpy(second_row + strlen("record="), RACE_TEST_UID_A, RACE_PROFILE_UID_LENGTH);
  ck_assert_int_eq(Race_MapState_Parse(malformed, original_length,
                                       parsed_records, RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_MALFORMED);

  memcpy(malformed, original, original_length + 1u);
  first_row = strstr(malformed, "record=");
  second_row = strstr(first_row + strlen("record="), "record=");
  memcpy(second_row + strlen("record="), "00000000", 8u);
  ck_assert_int_eq(Race_MapState_Parse(malformed, original_length,
                                       parsed_records, RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_MALFORMED);

  memcpy(malformed, original, original_length + 1u);
  first_row = strstr(malformed, "record=");
  char *encoded_name = strchr(first_row, '|');
  ck_assert_ptr_nonnull(encoded_name);
  encoded_name++;
  encoded_name[0] = encoded_name[0] == '5' ? '4' : '5';
  ck_assert_int_eq(Race_MapState_Parse(malformed, original_length,
                                       parsed_records, RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_CHECKSUM);

  ck_assert_int_eq(Race_MapState_Parse(original, original_length - 1u,
                                       parsed_records, RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_MALFORMED);

  memcpy(malformed, original, original_length);
  malformed[original_length] = 'x';
  ck_assert_int_eq(Race_MapState_Parse(malformed, original_length + 1u,
                                       parsed_records, RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_MALFORMED);

  static const char bad_bound[] =
    RACE_MAP_STATE_MAGIC "\n"
    "map=65646765\n"
    "ruleset=" RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY "\n"
    "publication=" RACE_MAP_STATE_PUBLICATION "\n"
    "generation=1\n"
    "records=4097\n";
  ck_assert_int_eq(Race_MapState_Parse(bad_bound, sizeof(bad_bound) - 1u,
                                       parsed_records, RACE_TEST_LENGTHOF(parsed_records),
                                       &parsed),
                   RACE_MAP_STATE_PARSE_MALFORMED);
} END_TEST

START_TEST(_Race_MapStateMaximumRecords) {
  race_leaderboard_record_t *records = calloc(RACE_MAP_STATE_MAX_RECORDS,
                                               sizeof(*records));
  race_leaderboard_record_t *parsed_records = calloc(RACE_MAP_STATE_MAX_RECORDS,
                                                      sizeof(*parsed_records));
  char *serialized = malloc(RACE_MAP_STATE_MAX_FILE_BYTES + 1u);
  ck_assert_ptr_nonnull(records);
  ck_assert_ptr_nonnull(parsed_records);
  ck_assert_ptr_nonnull(serialized);

  race_map_state_t state;
  ck_assert(Race_MapState_Init(
    &state, records, RACE_MAP_STATE_MAX_RECORDS, "edge",
    RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY));
  state.generation = 1u;

  char maximum_name[RACE_PROFILE_NAME_SIZE];
  memset(maximum_name, 'x', RACE_PROFILE_NAME_MAX);
  maximum_name[RACE_PROFILE_NAME_MAX] = '\0';

  uint32_t splits[RACE_MAX_CHECKPOINTS];
  for (size_t i = 0; i < RACE_MAX_CHECKPOINTS; i++) {
    splits[i] = (uint32_t) i + 1u;
  }

  for (size_t i = 0; i < RACE_MAP_STATE_MAX_RECORDS; i++) {
    char uid[RACE_PROFILE_UID_SIZE];
    Race_TestUid(i, uid);
    records[i] = Race_TestRecord(uid, maximum_name,
                                 RACE_LEADERBOARD_MAX_TIME_MS,
                                 splits, RACE_MAX_CHECKPOINTS);
  }
  state.record_count = RACE_MAP_STATE_MAX_RECORDS;

  size_t serialized_length;
  ck_assert(Race_MapState_Serialize(&state, serialized,
                                    RACE_MAP_STATE_MAX_FILE_BYTES + 1u,
                                    &serialized_length));
  ck_assert_uint_lt(serialized_length, RACE_MAP_STATE_MAX_FILE_BYTES);

  race_map_state_t parsed;
  ck_assert_int_eq(Race_MapState_Parse(serialized, serialized_length,
                                       parsed_records, RACE_MAP_STATE_MAX_RECORDS,
                                       &parsed),
                   RACE_MAP_STATE_PARSE_OK);
  ck_assert(Race_MapState_Equals(&state, &parsed));

  const race_leaderboard_record_t *top[RACE_LEADERBOARD_TOP_MAX];
  ck_assert_uint_eq(Race_Leaderboard_Top(parsed.records, parsed.record_count,
                                         top, RACE_TEST_LENGTHOF(top)),
                    RACE_LEADERBOARD_TOP_MAX);
  ck_assert_str_eq(top[0]->uid, records[0].uid);
  ck_assert_str_eq(top[RACE_LEADERBOARD_TOP_MAX - 1]->uid,
                   records[RACE_LEADERBOARD_TOP_MAX - 1].uid);

  free(serialized);
  free(parsed_records);
  free(records);
} END_TEST

START_TEST(_Race_ReplayFormatRoundTripAndIdentity) {
  race_replay_sample_t samples[4];
  race_replay_t replay = Race_TestReplay(samples, RACE_TEST_LENGTHOF(samples));
  replay.samples[0].strafe_helper.forward.z = -0.f;

  uint8_t serialized[4096];
  size_t serialized_length;
  uint64_t replay_id;
  ck_assert(Race_Replay_Serialize(&replay, serialized, sizeof(serialized),
                                  &serialized_length, &replay_id));
  // Cross-architecture QRPL v1 golden fixture. The ID incorporates the raw
  // canonical payload CRC; the length also fixes miniz's stored byte stream.
  ck_assert_uint_eq(replay_id, UINT64_C(0xdba8d53d1dc24cab));
  ck_assert_uint_eq(serialized_length, 550u);
  ck_assert_int_eq(memcmp(serialized, RACE_REPLAY_MAGIC, 4u), 0);
  ck_assert_uint_eq(serialized[4], RACE_REPLAY_FORMAT_VERSION);
  ck_assert_uint_eq(serialized[6], RACE_REPLAY_HEADER_BYTES);
  ck_assert_uint_eq(serialized[8], RACE_REPLAY_FLAG_DEFLATE);
  ck_assert_uint_eq(serialized[12], RACE_REPLAY_TICK_RATE);
  ck_assert_uint_eq(serialized[14], RACE_MODE_RACE);
  ck_assert_uint_eq(serialized[44], RACE_REPLAY_FRAME_SCHEMA);
  static const uint8_t expected_replay_id[] = {
    0xabu, 0x4cu, 0xc2u, 0x1du, 0x3du, 0xd5u, 0xa8u, 0xdbu
  };
  ck_assert_int_eq(memcmp(serialized + 52u, expected_replay_id,
                          sizeof(expected_replay_id)), 0);
  ck_assert_uint_eq(serialized[60], 0u);
  const uint32_t raw_length = (uint32_t) serialized[28] |
                              ((uint32_t) serialized[29] << 8u) |
                              ((uint32_t) serialized[30] << 16u) |
                              ((uint32_t) serialized[31] << 24u);
  const uint32_t stored_length = (uint32_t) serialized[32] |
                                 ((uint32_t) serialized[33] << 8u) |
                                 ((uint32_t) serialized[34] << 16u) |
                                 ((uint32_t) serialized[35] << 24u);
  ck_assert_uint_eq(raw_length,
                    RACE_REPLAY_PARAMS_BYTES +
                      replay.sample_count * RACE_REPLAY_FRAME_BYTES);
  ck_assert_uint_lt(stored_length, raw_length);
  const uint16_t map_length = (uint16_t) serialized[24] |
                              (uint16_t) ((uint16_t) serialized[25] << 8u);
  const uint16_t player_name_length = (uint16_t) serialized[26] |
                                      (uint16_t) ((uint16_t) serialized[27] << 8u);
  const size_t payload_offset = RACE_REPLAY_HEADER_BYTES + map_length +
                                player_name_length;
  uint8_t raw_payload[2048];
  mz_ulong inflated_length = sizeof(raw_payload);
  ck_assert_int_eq(mz_uncompress(raw_payload, &inflated_length,
                                 serialized + payload_offset,
                                 stored_length), MZ_OK);
  ck_assert_uint_eq(inflated_length, raw_length);
  ck_assert_uint_eq(raw_payload[0], 0x20u);
  ck_assert_uint_eq(raw_payload[1], 0x03u);
  const uint8_t *first_frame = raw_payload + RACE_REPLAY_PARAMS_BYTES;
  const uint8_t *second_frame = first_frame + RACE_REPLAY_FRAME_BYTES;
  ck_assert_uint_eq(first_frame[0], 0u);
  ck_assert_uint_eq(first_frame[2], PM_HOOK_PULL);
  ck_assert_uint_eq(first_frame[3], 0u);
  ck_assert_uint_eq(first_frame[291], 0x80u);
  ck_assert_uint_eq(first_frame[4], 1u);
  ck_assert_uint_eq(first_frame[10], 10u);
  ck_assert_uint_eq(second_frame[0], RACE_REPLAY_TICK_MSEC);
  ck_assert_uint_eq(second_frame[3], 1u);
  ck_assert_uint_eq(second_frame[8], 0u);
  ck_assert_uint_eq(second_frame[88], 2u);
  ck_assert_uint_eq(second_frame[152], 2u);
  ck_assert_uint_eq(second_frame[134],
                    replay.samples[1].stats[RACE_TEST_INPUT_STAT] & 0xff);
  ck_assert_uint_eq(second_frame[135],
                    (uint16_t) replay.samples[1].stats[RACE_TEST_INPUT_STAT] >> 8u);
  ck_assert_uint_eq(second_frame[3], 1u);

  race_replay_sample_t parsed_samples[4];
  race_replay_t parsed;
  ck_assert_int_eq(Race_Replay_Parse(
                     serialized, serialized_length, parsed_samples,
                     RACE_TEST_LENGTHOF(parsed_samples), NULL, 0u, &parsed),
                   RACE_REPLAY_PARSE_OK);
  ck_assert(!signbit(parsed.samples[0].strafe_helper.forward.z));
  replay.replay_id = replay_id;
  ck_assert(Race_Replay_Equals(&replay, &parsed));

  uint8_t repeated[4096];
  size_t repeated_length;
  uint64_t repeated_id;
  parsed.samples[0].strafe_helper.forward.z = -0.f;
  ck_assert(Race_Replay_Serialize(&parsed, repeated, sizeof(repeated),
                                  &repeated_length, &repeated_id));
  ck_assert_uint_eq(repeated_id, replay_id);
  ck_assert_uint_eq(repeated_length, serialized_length);
  ck_assert_int_eq(memcmp(repeated, serialized, serialized_length), 0);

  char id[RACE_REPLAY_ID_TEXT_SIZE];
  ck_assert(Race_Replay_IdString(replay_id, id));
  ck_assert_uint_eq(strlen(id), RACE_REPLAY_ID_TEXT_LENGTH);
  char committed[256], candidate[256];
  ck_assert(Race_Replay_Paths(RACE_PHYSICS_PRESET_Q2_V1_KEY,
                               "maps/EDGE.bsp", replay_id,
                               committed, sizeof(committed),
                               candidate, sizeof(candidate)));
  ck_assert_ptr_nonnull(strstr(committed, "/q2-v1/"));
  ck_assert_ptr_nonnull(strstr(committed, "/65646765/"));
  ck_assert_ptr_nonnull(strstr(committed, id));
  ck_assert_ptr_nonnull(strstr(committed, "replay-"));
  ck_assert_ptr_nonnull(strstr(committed, ".ghost"));
  ck_assert_ptr_nonnull(strstr(candidate, ".candidate"));
} END_TEST

START_TEST(_Race_ReplayFormatRejectsCorruptionAndBounds) {
  race_replay_sample_t samples[4];
  race_replay_t replay = Race_TestReplay(samples, RACE_TEST_LENGTHOF(samples));
  uint8_t serialized[4097];
  size_t serialized_length;
  ck_assert(Race_Replay_Serialize(&replay, serialized, sizeof(serialized),
                                  &serialized_length, NULL));

  race_replay_sample_t parsed_samples[4];
  race_replay_t parsed;
  uint8_t malformed[4097];
  memcpy(malformed, serialized, serialized_length);
  malformed[36] ^= 1u;
  const size_t metadata_length = RACE_REPLAY_HEADER_BYTES +
                                 strlen(replay.map) +
                                 strlen(replay.player_name);
  uint8_t metadata[RACE_REPLAY_HEADER_BYTES + RACE_MAP_IDENTITY_SIZE +
                   RACE_REPLAY_PLAYER_NAME_SIZE];
  memcpy(metadata, malformed, metadata_length);
  memset(metadata + 40u, 0, 4u);
  const uint32_t metadata_crc = Race_TestSettingsCrc32(
    metadata, metadata_length);
  for (size_t i = 0u; i < 4u; i++) {
    malformed[40u + i] = (uint8_t) (metadata_crc >> (i * 8u));
  }
  ck_assert_int_eq(Race_Replay_Parse(
                     malformed, serialized_length, parsed_samples,
                     RACE_TEST_LENGTHOF(parsed_samples), NULL, 0u, &parsed),
                   RACE_REPLAY_PARSE_CHECKSUM);

  memcpy(malformed, serialized, serialized_length);
  malformed[4] = 2u;
  ck_assert_int_eq(Race_Replay_Parse(
                     malformed, serialized_length, parsed_samples,
                     RACE_TEST_LENGTHOF(parsed_samples), NULL, 0u, &parsed),
                   RACE_REPLAY_PARSE_UNKNOWN_VERSION);

  memcpy(malformed, serialized, serialized_length);
  malformed[44] = 2u;
  ck_assert_int_eq(Race_Replay_Parse(
                     malformed, serialized_length, parsed_samples,
                     RACE_TEST_LENGTHOF(parsed_samples), NULL, 0u, &parsed),
                   RACE_REPLAY_PARSE_UNSUPPORTED_SCHEMA);

  memcpy(malformed, serialized, serialized_length);
  malformed[14] = RACE_MODE_PRACTICE;
  ck_assert_int_eq(Race_Replay_Parse(
                     malformed, serialized_length, parsed_samples,
                     RACE_TEST_LENGTHOF(parsed_samples), NULL, 0u, &parsed),
                   RACE_REPLAY_PARSE_UNSUPPORTED_RULESET);

  memcpy(malformed, serialized, serialized_length);
  malformed[60] = 1u;
  ck_assert_int_eq(Race_Replay_Parse(
                     malformed, serialized_length, parsed_samples,
                     RACE_TEST_LENGTHOF(parsed_samples), NULL, 0u, &parsed),
                   RACE_REPLAY_PARSE_BOUNDS);

  memcpy(malformed, serialized, serialized_length);
  malformed[serialized_length] = 0u;
  ck_assert_int_eq(Race_Replay_Parse(
                     malformed, serialized_length + 1u, parsed_samples,
                     RACE_TEST_LENGTHOF(parsed_samples), NULL, 0u, &parsed),
                   RACE_REPLAY_PARSE_BOUNDS);
  ck_assert_int_eq(Race_Replay_Parse(
                     serialized, serialized_length, parsed_samples, 2u,
                     NULL, 0u, &parsed),
                   RACE_REPLAY_PARSE_BOUNDS);

  replay.samples[1].pm_state.origin.x = NAN;
  ck_assert(!Race_Replay_Valid(&replay));
} END_TEST

START_TEST(_Race_ReplayProjectileFormatAndLifecycle) {
  race_replay_sample_t samples[4];
  race_replay_projectile_event_t events[4];
  race_replay_t replay = Race_TestReplay(samples, RACE_TEST_LENGTHOF(samples));
  replay.projectile_events = events;
  replay.projectile_event_capacity = RACE_TEST_LENGTHOF(events);
  replay.projectile_event_count = RACE_TEST_LENGTHOF(events);
  events[0] = Race_TestProjectileEvent(
    0u, 1u, RACE_REPLAY_PROJECTILE_ROCKET,
    RACE_REPLAY_PROJECTILE_SPAWN);
  events[1] = Race_TestProjectileEvent(
    25u, 2u, RACE_REPLAY_PROJECTILE_HYPERBLASTER,
    RACE_REPLAY_PROJECTILE_SPAWN);
  events[2] = Race_TestProjectileEvent(
    25u, 1u, RACE_REPLAY_PROJECTILE_ROCKET,
    RACE_REPLAY_PROJECTILE_IMPACT);
  events[3] = Race_TestProjectileEvent(
    50u, 2u, RACE_REPLAY_PROJECTILE_HYPERBLASTER,
    RACE_REPLAY_PROJECTILE_SILENT_DESPAWN);
  ck_assert(Race_Replay_Valid(&replay));

  uint8_t serialized[4096];
  size_t serialized_length;
  uint64_t replay_id;
  ck_assert(Race_Replay_Serialize(
    &replay, serialized, sizeof(serialized),
    &serialized_length, &replay_id));
  ck_assert_uint_eq(serialized[60], 4u);
  ck_assert_uint_eq(serialized[61], 0u);
  ck_assert_uint_eq(serialized[62], 0u);
  ck_assert_uint_eq(serialized[63], 0u);

  race_replay_sample_t parsed_samples[4];
  race_replay_projectile_event_t parsed_events[4];
  race_replay_t parsed;
  ck_assert_int_eq(Race_Replay_Parse(
                     serialized, serialized_length,
                     parsed_samples, RACE_TEST_LENGTHOF(parsed_samples),
                     parsed_events, RACE_TEST_LENGTHOF(parsed_events),
                     &parsed),
                   RACE_REPLAY_PARSE_OK);
  replay.replay_id = replay_id;
  ck_assert(Race_Replay_Equals(&replay, &parsed));
  ck_assert_uint_eq(parsed.projectile_event_count, 4u);
  ck_assert_uint_eq(parsed.projectile_events[2].time_ms, 25u);
  ck_assert_uint_eq(parsed.projectile_events[2].id, 1u);
  ck_assert_int_eq(parsed.projectile_events[2].operation,
                   RACE_REPLAY_PROJECTILE_IMPACT);

  uint8_t repeated[4096];
  size_t repeated_length;
  uint64_t repeated_id;
  ck_assert(Race_Replay_Serialize(
    &parsed, repeated, sizeof(repeated),
    &repeated_length, &repeated_id));
  ck_assert_uint_eq(repeated_id, replay_id);
  ck_assert_uint_eq(repeated_length, serialized_length);
  ck_assert_int_eq(memcmp(repeated, serialized, serialized_length), 0);

  events[3].time_ms = 24u;
  ck_assert(!Race_Replay_Valid(&replay));
  events[3].time_ms = 50u;
  events[3].id = 3u;
  ck_assert(!Race_Replay_Valid(&replay));
  events[3].id = 2u;
  events[1].id = 1u;
  ck_assert(!Race_Replay_Valid(&replay));
  events[1].id = 2u;
  events[3].operation = RACE_REPLAY_PROJECTILE_IMPACT;
  events[3].normal.x = NAN;
  ck_assert(!Race_Replay_Valid(&replay));
  events[3].normal = Vec3(0.f, 1.f, 0.f);
  ck_assert(Race_Replay_Valid(&replay));

  replay.projectile_event_count = 3u;
  ck_assert(Race_Replay_Valid(&replay));
  ck_assert_int_eq(Race_Replay_Parse(
                     serialized, serialized_length,
                     parsed_samples, RACE_TEST_LENGTHOF(parsed_samples),
                     NULL, 0u, &parsed),
                   RACE_REPLAY_PARSE_BOUNDS);
} END_TEST

START_TEST(_Race_ReplayRecordingCadenceAndLimits) {
  race_replay_recording_t recording = { 0 };
  const uint32_t start = UINT32_MAX - 10u;
  ck_assert(Race_ReplayRecording_Start(&recording, "edge", RACE_TEST_UID_A,
                                       "Runner", 42, 0u, start));
  race_replay_sample_t sample = Race_TestReplaySample(0u, 1.f);
  ck_assert(Race_ReplayRecording_Capture(&recording, start, &sample));
  sample = Race_TestReplaySample(0u, 2.f);
  ck_assert(Race_ReplayRecording_Capture(&recording, start, &sample));
  ck_assert_uint_eq(recording.replay.sample_count, 1u);
  ck_assert_float_eq(recording.replay.samples[0].pm_state.origin.x, 2.f);
  sample = Race_TestReplaySample(0u, 3.f);
  ck_assert(Race_ReplayRecording_Capture(&recording, start + 25u, &sample));
  sample = Race_TestReplaySample(0u, 4.f);
  ck_assert(Race_ReplayRecording_Finish(&recording, start + 50u, 50u,
                                        &sample));
  ck_assert_uint_eq(recording.replay.sample_count, 3u);
  ck_assert_uint_eq(recording.replay.samples[2].time_ms, 50u);
  Race_ReplayRecording_Destroy(&recording);

  ck_assert(Race_ReplayRecording_Start(&recording, "edge", RACE_TEST_UID_A,
                                       "Runner", 42, 0u, 1000u));
  for (size_t i = 0; i < RACE_REPLAY_MAX_SAMPLES; i++) {
    sample = Race_TestReplaySample(0u, (float) i);
    ck_assert(Race_ReplayRecording_Capture(
      &recording, 1000u + (uint32_t) i * RACE_REPLAY_TICK_MSEC, &sample));
  }
  sample = Race_TestReplaySample(0u, 999.f);
  ck_assert(Race_ReplayRecording_Finish(
    &recording, 1000u + RACE_REPLAY_MAX_TIME_MS,
    RACE_REPLAY_MAX_TIME_MS, &sample));
  ck_assert_uint_eq(recording.replay.sample_count, RACE_REPLAY_MAX_SAMPLES);
  ck_assert_uint_eq(recording.replay.samples[RACE_REPLAY_MAX_SAMPLES - 1u].time_ms,
                    RACE_REPLAY_MAX_TIME_MS);
  Race_ReplayRecording_Destroy(&recording);

  ck_assert(Race_ReplayRecording_Start(&recording, "edge", RACE_TEST_UID_A,
                                       "Runner", 42, 0u, 0u));
  sample = Race_TestReplaySample(0u, 1.f);
  sample.pm_state.velocity.y = NAN;
  ck_assert(!Race_ReplayRecording_Capture(&recording, 0u, &sample));
  ck_assert(recording.invalid);
  Race_ReplayRecording_Destroy(&recording);
} END_TEST

START_TEST(_Race_ReplayPlaybackClock) {
  race_replay_clock_t clock;
  Race_ReplayClock_Init(&clock, UINT32_MAX - 10u);
  ck_assert_int_eq(clock.speed, RACE_REPLAY_SPEED_NORMAL);
  ck_assert_int_eq(Race_ReplayClock_Advance(&clock, 5u, 100u),
                   RACE_REPLAY_ADVANCE_MOVED);
  ck_assert_uint_eq(clock.playhead_ms, 16u);

  ck_assert(Race_ReplayClock_SetSpeed(
    &clock, RACE_REPLAY_SPEED_QUARTER, 5u));
  ck_assert_int_eq(Race_ReplayClock_Advance(&clock, 6u, 100u),
                   RACE_REPLAY_ADVANCE_NONE);
  ck_assert_uint_eq(clock.rate_remainder, 1u);
  ck_assert_int_eq(Race_ReplayClock_Advance(&clock, 9u, 100u),
                   RACE_REPLAY_ADVANCE_MOVED);
  ck_assert_uint_eq(clock.playhead_ms, 17u);

  ck_assert(Race_ReplayClock_SetPaused(&clock, true, 10u));
  ck_assert_int_eq(Race_ReplayClock_Advance(&clock, 1000u, 100u),
                   RACE_REPLAY_ADVANCE_NONE);
  ck_assert_uint_eq(clock.playhead_ms, 17u);
  ck_assert(Race_ReplayClock_SetPaused(&clock, false, 1000u));
  ck_assert(Race_ReplayClock_SetSpeed(
    &clock, RACE_REPLAY_SPEED_QUADRUPLE, 1000u));
  ck_assert_int_eq(Race_ReplayClock_Advance(&clock, 1021u, 100u),
                   RACE_REPLAY_ADVANCE_COMPLETED);
  ck_assert_uint_eq(clock.playhead_ms, 100u);
  ck_assert(clock.paused);
  ck_assert(clock.completed);
  ck_assert(!Race_ReplayClock_SetPaused(&clock, false, 1021u));

  ck_assert(Race_ReplayClock_Seek(&clock, 500u, 100u, 1100u));
  ck_assert_uint_eq(clock.playhead_ms, 100u);
  ck_assert(clock.completed);
  ck_assert(Race_ReplayClock_Seek(&clock, 20u, 100u, 1200u));
  ck_assert_uint_eq(clock.playhead_ms, 20u);
  ck_assert(clock.paused);
  ck_assert(!clock.completed);
  ck_assert(Race_ReplayClock_Restart(&clock, 1300u));
  ck_assert_uint_eq(clock.playhead_ms, 0u);
  ck_assert(!clock.paused);
  ck_assert_int_eq(clock.speed, RACE_REPLAY_SPEED_QUADRUPLE);

  ck_assert(!Race_ReplayClock_ShiftSpeed(&clock, 1, 1300u));
  ck_assert(Race_ReplayClock_ShiftSpeed(&clock, -1, 1300u));
  ck_assert_int_eq(clock.speed, RACE_REPLAY_SPEED_DOUBLE);
  ck_assert(Race_ReplayClock_ShiftSpeed(&clock, -1, 1300u));
  ck_assert_int_eq(clock.speed, RACE_REPLAY_SPEED_NORMAL);
  ck_assert(!Race_ReplayClock_ShiftSpeed(&clock, 0, 1300u));

  ck_assert_uint_eq(Race_ReplayClock_OffsetTarget(1000u, -5000, 10000u), 0u);
  ck_assert_uint_eq(Race_ReplayClock_OffsetTarget(9000u, 5000, 10000u),
                    10000u);
  ck_assert_uint_eq(Race_ReplayClock_OffsetTarget(5000u, -2000, 10000u),
                    3000u);
  ck_assert(!Race_ReplayClock_SetSpeed(
    &clock, RACE_REPLAY_SPEED_TOTAL, 1300u));

  ck_assert(Race_ReplayPlayback_LoadAllowed(0u, false, 0u));
  ck_assert(!Race_ReplayPlayback_LoadAllowed(
    1000u, true, 1000u + RACE_REPLAY_LOAD_COOLDOWN_MSEC - 1u));
  ck_assert(Race_ReplayPlayback_LoadAllowed(
    1000u, true, 1000u + RACE_REPLAY_LOAD_COOLDOWN_MSEC));
  ck_assert(!Race_ReplayPlayback_LoadAllowed(
    UINT32_MAX - 100u, true,
    RACE_REPLAY_LOAD_COOLDOWN_MSEC - 102u));
  ck_assert(Race_ReplayPlayback_LoadAllowed(
    UINT32_MAX - 100u, true,
    RACE_REPLAY_LOAD_COOLDOWN_MSEC - 101u));

  bool attack_released = false;
  ck_assert(!Race_ReplayPlayback_AttackExit(&attack_released, true));
  ck_assert(!Race_ReplayPlayback_AttackExit(&attack_released, false));
  ck_assert(attack_released);
  ck_assert(Race_ReplayPlayback_AttackExit(&attack_released, true));

  attack_released = true;
  ck_assert(Race_ReplayPlayback_AttackExit(&attack_released, true));
  ck_assert(!Race_ReplayPlayback_AttackExit(NULL, true));
} END_TEST

START_TEST(_Race_ReplaySelection) {
  const uint32_t split = 25u;
  race_leaderboard_record_t records[] = {
    Race_TestRecord(RACE_TEST_UID_A, "Alpha", 200u, &split, 1u),
    Race_TestRecord("11111111-2222-4333-8444-555555555555",
                    "Charlie", 150u, &split, 1u),
    Race_TestRecord(RACE_TEST_UID_B, "Bravo", 100u, &split, 1u)
  };
  ck_assert(Race_Leaderboard_RecordAttachReplay(
    records + 0u, UINT64_C(0xaaaaaaaaaaaaaaaa)));
  ck_assert(Race_Leaderboard_RecordAttachReplay(
    records + 1u, UINT64_C(0xcccccccccccccccc)));
  ck_assert(Race_Leaderboard_RecordAttachReplay(
    records + 2u, UINT64_C(0xbbbbbbbbbbbbbbbb)));

  race_leaderboard_record_t selected;
  size_t rank = 0u;
  ck_assert(Race_ReplaySelection_Select(
    records, RACE_TEST_LENGTHOF(records),
    RACE_REPLAY_SOURCE_WORLD_RECORD, NULL, 0u, &selected, &rank));
  ck_assert_str_eq(selected.uid, RACE_TEST_UID_B);
  ck_assert_uint_eq(rank, 1u);
  ck_assert(Race_ReplaySelection_Select(
    records, RACE_TEST_LENGTHOF(records),
    RACE_REPLAY_SOURCE_PERSONAL_BEST, RACE_TEST_UID_A, 0u,
    &selected, &rank));
  ck_assert_uint_eq(selected.replay_id, UINT64_C(0xaaaaaaaaaaaaaaaa));
  ck_assert_uint_eq(rank, 3u);
  ck_assert(Race_ReplaySelection_Select(
    records, RACE_TEST_LENGTHOF(records), RACE_REPLAY_SOURCE_ID,
    NULL, UINT64_C(0xcccccccccccccccc), &selected, &rank));
  ck_assert_str_eq(selected.display_name, "Charlie");
  ck_assert_uint_eq(rank, 2u);
  ck_assert(!Race_ReplaySelection_Select(
    records, RACE_TEST_LENGTHOF(records), RACE_REPLAY_SOURCE_ID,
    NULL, UINT64_C(0xdddddddddddddddd), &selected, &rank));
  ck_assert(!Race_ReplaySelection_Select(
    records, RACE_TEST_LENGTHOF(records),
    RACE_REPLAY_SOURCE_PERSONAL_BEST,
    "99999999-9999-4999-8999-999999999999", 0u, &selected, &rank));
  ck_assert(!Race_ReplaySelection_Select(
    records, RACE_TEST_LENGTHOF(records), RACE_REPLAY_SOURCE_NONE,
    NULL, 0u, &selected, &rank));
} END_TEST

START_TEST(_Race_ReplayPlaybackSamplingAndInterpolation) {
  race_replay_sample_t samples[4];
  const race_replay_t replay = Race_TestReplay(
    samples, RACE_TEST_LENGTHOF(samples));
  race_replay_pose_sample_t window[RACE_REPLAY_STATE_MAX_SAMPLES];
  size_t cursor = SIZE_MAX;
  ck_assert_uint_eq(Race_ReplayPlayback_Window(
                      &replay, 25u, window,
                      RACE_TEST_LENGTHOF(window), &cursor),
                    2u);
  ck_assert_uint_eq(cursor, 1u);
  ck_assert_uint_eq(window[0].time_ms, 25u);
  ck_assert_uint_eq(window[1].time_ms, 50u);
  ck_assert_uint_eq(Race_ReplayPlayback_Window(
                      &replay, 50u, window,
                      RACE_TEST_LENGTHOF(window), &cursor),
                    1u);
  ck_assert_uint_eq(cursor, 2u);
  ck_assert_uint_eq(Race_ReplayPlayback_Window(
                      &replay, 51u, window,
                      RACE_TEST_LENGTHOF(window), NULL),
                    0u);

  uint32_t target = UINT32_MAX;
  ck_assert(Race_ReplayPlayback_StepTarget(&replay, 0u, 1, &target));
  ck_assert_uint_eq(target, 25u);
  ck_assert(Race_ReplayPlayback_StepTarget(&replay, 25u, -1, &target));
  ck_assert_uint_eq(target, 0u);
  ck_assert(!Race_ReplayPlayback_StepTarget(&replay, 0u, -1, &target));
  ck_assert(!Race_ReplayPlayback_StepTarget(&replay, 50u, 1, &target));

  race_replay_state_message_t state = Race_TestReplayState(1u, 1u);
  race_replay_pose_sample_t pose;
  ck_assert(Race_ReplayState_Interpolate(&state, 12u, &pose));
  ck_assert(fabsf(pose.origin.x - 12.f) < 0.001f);
  ck_assert(fabsf(pose.view_angles.y - 359.6f) < 0.001f);
  ck_assert(Race_ReplayState_Interpolate(&state, 25u, &pose));
  ck_assert(fabsf(pose.origin.x - 25.f) < 0.001f);
  ck_assert(Race_ReplayState_Interpolate(&state, 50u, &pose));
  ck_assert(fabsf(pose.origin.x - 50.f) < 0.001f);
  ck_assert_uint_eq(Race_ReplayState_PresentationTime(
                      &state, 1000u, 1010u),
                    35u);
  state.flags |= RACE_REPLAY_STATE_PAUSED;
  ck_assert_uint_eq(Race_ReplayState_PresentationTime(
                      &state, 1000u, 5000u),
                    25u);
} END_TEST

START_TEST(_Race_ReplayViewerState) {
  player_state_t viewer = {
    .client = 7u,
    .entity = 42,
    .stats = {
      [RACE_TEST_ADMIN_CAPABILITIES_STAT] = 0x1234
    }
  };
  race_replay_sample_t sample = {
    .pm_state = {
      .type = PM_SPECTATOR,
      .origin = Vec3(128.f, -64.f, 256.f),
      .velocity = Vec3(320.f, 40.f, -12.f),
      .view_angles = Vec3(-8.f, 135.f, 0.f),
      .view_offset = Vec3(0.f, 0.f, 22.f)
    },
    .stats = {
      [RACE_TEST_HEALTH_STAT] = 75,
      [RACE_TEST_ADMIN_CAPABILITIES_STAT] = -1
    },
    .inventory = {
      [3] = 9
    }
  };

  Race_ReplayPlayback_ApplyViewerState(
    &viewer, &sample, RACE_TEST_ADMIN_CAPABILITIES_STAT);

  ck_assert_uint_eq(viewer.client, 7u);
  ck_assert_int_eq(viewer.entity, 42);
  ck_assert_int_eq(
    viewer.stats[RACE_TEST_ADMIN_CAPABILITIES_STAT], 0x1234);
  ck_assert_int_eq(viewer.stats[RACE_TEST_HEALTH_STAT], 75);
  ck_assert_int_eq(viewer.inventory[3], 9);
  ck_assert_int_eq(viewer.pm_state.type, PM_SPECTATOR);
  ck_assert(fabsf(viewer.pm_state.origin.x - 128.f) < 0.001f);
  ck_assert(fabsf(viewer.pm_state.origin.y + 64.f) < 0.001f);
  ck_assert(fabsf(viewer.pm_state.view_angles.y - 135.f) < 0.001f);
} END_TEST

START_TEST(_Race_ReplayStateTransportAndLifecycle) {
  uint8_t payload[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD];
  uint8_t oversized[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD + 1u] = { 0 };
  race_replay_state_message_t state = Race_TestReplayState(1u, 1u);
  const size_t length = Race_ReplayState_Encode(
    &state, payload, sizeof(payload));
  ck_assert_uint_eq(length, 32u + strlen(state.display_name) + 3u * 28u);

  race_replay_state_message_t decoded;
  ck_assert(Race_ReplayState_Decode(payload, length, &decoded));
  ck_assert(!Race_ReplayState_Decode(oversized, sizeof(oversized), &decoded));
  ck_assert_uint_eq(decoded.generation, state.generation);
  ck_assert_uint_eq(decoded.sequence, state.sequence);
  ck_assert_uint_eq(decoded.replay_id, state.replay_id);
  ck_assert_str_eq(decoded.display_name, state.display_name);
  ck_assert_uint_eq(decoded.sample_count, state.sample_count);
  ck_assert(fabsf(decoded.samples[1].origin.y - 26.f) < 0.001f);

  race_replay_client_cache_t cache;
  Race_ReplayClientCache_Clear(&cache);
  ck_assert_int_eq(Race_ReplayClientCache_ApplyState(
                     &cache, payload, length, 1000u),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  ck_assert_uint_eq(cache.state_received_time, 1000u);
  ck_assert_int_eq(Race_ReplayClientCache_ApplyState(
                     &cache, payload, length, 2000u),
                   RACE_REPLAY_TRANSPORT_STALE);
  ck_assert_uint_eq(cache.state_received_time, 1000u);

  state.sequence = 2u;
  size_t next_length = Race_ReplayState_Encode(
    &state, payload, sizeof(payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyState(
                     &cache, payload, next_length, 1025u),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  state = Race_TestReplayState(2u, 1u);
  next_length = Race_ReplayState_Encode(&state, payload, sizeof(payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyState(
                     &cache, payload, next_length, 2000u),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  ck_assert_uint_eq(cache.generation, 2u);
  ck_assert_uint_eq(cache.state_sequence, 1u);
  ck_assert_uint_eq(cache.raceline_received_points, 0u);

  state = Race_TestReplayState(1u, 99u);
  next_length = Race_ReplayState_Encode(&state, payload, sizeof(payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyState(
                     &cache, payload, next_length, 3000u),
                   RACE_REPLAY_TRANSPORT_STALE);

  ck_assert(!Race_ReplayState_Decode(payload, next_length - 1u, &decoded));
  const uint8_t saved_version = payload[0];
  payload[0] = 99u;
  ck_assert(!Race_ReplayState_Decode(payload, next_length, &decoded));
  payload[0] = saved_version;
  payload[31] = 1u;
  ck_assert(!Race_ReplayState_Decode(payload, next_length, &decoded));
  payload[31] = 0u;
  const size_t name_length = payload[29];
  const size_t second_origin = 32u + name_length + 28u + 4u;
  payload[second_origin + 0u] = 0x00u;
  payload[second_origin + 1u] = 0x00u;
  payload[second_origin + 2u] = 0xc0u;
  payload[second_origin + 3u] = 0x7fu;
  ck_assert(!Race_ReplayState_Decode(payload, next_length, &decoded));

  state = Race_TestReplayState(3u, 1u);
  memset(state.display_name, 'x', RACE_REPLAY_DISPLAY_NAME_MAX);
  state.display_name[RACE_REPLAY_DISPLAY_NAME_MAX] = '\0';
  state.sample_count = RACE_REPLAY_STATE_MAX_SAMPLES;
  for (size_t i = 0u; i < state.sample_count; i++) {
    state.samples[i] = (race_replay_pose_sample_t) {
      .time_ms = (uint32_t) i * 10u,
      .origin = Vec3((float) i, 0.f, 0.f),
      .view_angles = Vec3(0.f, (float) i, 0.f)
    };
  }
  state.duration_ms = 50u;
  state.playhead_ms = 25u;
  ck_assert_uint_eq(Race_ReplayState_Encode(
                      &state, payload, sizeof(payload)),
                    231u);
  state.sample_count = 0u;
  ck_assert_uint_eq(Race_ReplayState_Encode(
                      &state, payload, sizeof(payload)),
                    0u);
  ck_assert(Race_ReplayGeneration_Newer(1u, UINT32_MAX));
  ck_assert(!Race_ReplayGeneration_Newer(UINT32_MAX, 1u));

  Race_ReplayClientCache_Clear(&cache);
  ck_assert_uint_eq(cache.generation, 0u);
  ck_assert_uint_eq(cache.state.sample_count, 0u);
  ck_assert(!cache.telemetry_valid);
  ck_assert(!cache.raceline_complete);
} END_TEST

START_TEST(_Race_ReplayTelemetryTransportAndLifecycle) {
  uint8_t state_payload[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD];
  uint8_t telemetry_payload[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD];
  race_replay_state_message_t state = Race_TestReplayState(7u, 3u);
  const size_t state_length = Race_ReplayState_Encode(
    &state, state_payload, sizeof(state_payload));
  ck_assert_uint_gt(state_length, 0u);

  race_replay_client_cache_t cache;
  Race_ReplayClientCache_Clear(&cache);
  ck_assert_int_eq(Race_ReplayClientCache_ApplyState(
                     &cache, state_payload, state_length, 1000u),
                   RACE_REPLAY_TRANSPORT_APPLIED);

  race_replay_telemetry_message_t telemetry = {
    .generation = state.generation,
    .sequence = state.sequence,
    .playhead_ms = state.playhead_ms,
    .frame_cursor = 1u,
    .pm_type = PM_NORMAL,
    .pm_flags = PMF_JUMPED,
    .origin = Vec3(1.f, 2.f, 3.f),
    .velocity = Vec3(320.f, -40.f, 270.f),
    .input_flags = RACE_INPUT_FORMAT_V1 |
                   RACE_INPUT_FORWARD | RACE_INPUT_LEFT |
                   RACE_INPUT_JUMP,
    .strafe_helper = {
      .active = true,
      .forward = { { 1.f, 0.f, 0.f } },
      .velocity = { { 320.f, -40.f, 270.f } },
      .wishdir = { { 0.f, -1.f, 0.f } },
      .wishspeed = 320.f,
      .accel = 1.f,
      .frametime = 0.025f,
      .view_yaw = 90.f
    }
  };
  const size_t telemetry_length = Race_ReplayTelemetry_Encode(
    &telemetry, telemetry_payload, sizeof(telemetry_payload));
  ck_assert_uint_eq(telemetry_length, 104u);

  race_replay_telemetry_message_t decoded;
  ck_assert(Race_ReplayTelemetry_Decode(
    telemetry_payload, telemetry_length, &decoded));
  ck_assert_uint_eq(decoded.generation, telemetry.generation);
  ck_assert_uint_eq(decoded.sequence, telemetry.sequence);
  ck_assert_uint_eq(decoded.playhead_ms, telemetry.playhead_ms);
  ck_assert_uint_eq(decoded.frame_cursor, telemetry.frame_cursor);
  ck_assert_int_eq(decoded.pm_type, telemetry.pm_type);
  ck_assert_uint_eq(decoded.pm_flags, telemetry.pm_flags);
  ck_assert_int_eq(decoded.input_flags, telemetry.input_flags);
  ck_assert_float_eq(decoded.velocity.x, telemetry.velocity.x);
  ck_assert(decoded.strafe_helper.active);
  ck_assert_float_eq(decoded.strafe_helper.wishspeed,
                     telemetry.strafe_helper.wishspeed);
  ck_assert_float_eq(decoded.strafe_helper.view_yaw,
                     telemetry.strafe_helper.view_yaw);

  ck_assert_int_eq(Race_ReplayClientCache_ApplyTelemetry(
                     &cache, telemetry_payload, telemetry_length),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  ck_assert(cache.telemetry_valid);
  ck_assert_uint_eq(cache.telemetry_sequence, state.sequence);
  ck_assert_int_eq(cache.telemetry.input_flags, telemetry.input_flags);

  state.sequence++;
  const size_t next_state_length = Race_ReplayState_Encode(
    &state, state_payload, sizeof(state_payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyState(
                     &cache, state_payload, next_state_length, 1025u),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  ck_assert(!cache.telemetry_valid);
  ck_assert_int_eq(Race_ReplayClientCache_ApplyTelemetry(
                     &cache, telemetry_payload, telemetry_length),
                   RACE_REPLAY_TRANSPORT_STALE);

  telemetry.sequence = state.sequence;
  telemetry.playhead_ms++;
  size_t stale_length = Race_ReplayTelemetry_Encode(
    &telemetry, telemetry_payload, sizeof(telemetry_payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyTelemetry(
                     &cache, telemetry_payload, stale_length),
                   RACE_REPLAY_TRANSPORT_STALE);

  telemetry.playhead_ms = state.playhead_ms;
  telemetry.input_flags = RACE_INPUT_FORWARD;
  ck_assert_uint_eq(Race_ReplayTelemetry_Encode(
                      &telemetry, telemetry_payload,
                      sizeof(telemetry_payload)),
                    0u);
  telemetry.input_flags = 0;
  ck_assert_uint_eq(Race_ReplayTelemetry_Encode(
                      &telemetry, telemetry_payload,
                      sizeof(telemetry_payload)),
                    104u);
  telemetry.input_flags = RACE_INPUT_FORMAT_V1;
  const size_t valid_length = Race_ReplayTelemetry_Encode(
    &telemetry, telemetry_payload, sizeof(telemetry_payload));
  telemetry_payload[49] = 1u;
  ck_assert(!Race_ReplayTelemetry_Decode(
    telemetry_payload, valid_length, &decoded));
} END_TEST

START_TEST(_Race_ReplayProjectileTransportAndLifecycle) {
  uint8_t payload[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD];
  race_replay_client_cache_t cache;
  Race_ReplayClientCache_Clear(&cache);

  race_replay_projectile_message_t message = {
    .op = RACE_REPLAY_PROJECTILE_MESSAGE_RESET,
    .generation = UINT32_MAX,
    .sequence = UINT32_MAX,
    .playhead_ms = 0u
  };
  size_t length = Race_ReplayProjectiles_Encode(
    &message, payload, sizeof(payload));
  ck_assert_uint_eq(length, 16u);
  ck_assert_int_eq(Race_ReplayClientCache_ApplyProjectiles(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_APPLIED);

  message = (race_replay_projectile_message_t) {
    .op = RACE_REPLAY_PROJECTILE_MESSAGE_EVENTS,
    .generation = 1u,
    .sequence = 1u,
    .playhead_ms = 0u,
    .event_count = 1u,
    .events = {
      Race_TestProjectileEvent(
        0u, 7u, RACE_REPLAY_PROJECTILE_ROCKET,
        RACE_REPLAY_PROJECTILE_SPAWN)
    }
  };
  length = Race_ReplayProjectiles_Encode(&message, payload, sizeof(payload));
  ck_assert_uint_eq(length, 60u);
  race_replay_projectile_message_t decoded;
  ck_assert(Race_ReplayProjectiles_Decode(payload, length, &decoded));
  ck_assert_uint_eq(decoded.events[0].id, 7u);
  ck_assert_int_eq(Race_ReplayClientCache_ApplyProjectiles(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  ck_assert_uint_eq(cache.generation, 1u);
  ck_assert_uint_eq(cache.projectile_count, 1u);
  ck_assert_int_eq(Race_ReplayClientCache_ApplyProjectiles(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_STALE);

  message = (race_replay_projectile_message_t) {
    .op = RACE_REPLAY_PROJECTILE_MESSAGE_RESET,
    .generation = 1u,
    .sequence = 2u,
    .playhead_ms = 25u
  };
  length = Race_ReplayProjectiles_Encode(&message, payload, sizeof(payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyProjectiles(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  ck_assert_uint_eq(cache.projectile_count, 0u);

  message = (race_replay_projectile_message_t) {
    .op = RACE_REPLAY_PROJECTILE_MESSAGE_SNAPSHOT,
    .generation = 1u,
    .sequence = 3u,
    .playhead_ms = 25u,
    .event_count = 2u,
    .events = {
      Race_TestProjectileEvent(
        0u, 7u, RACE_REPLAY_PROJECTILE_ROCKET,
        RACE_REPLAY_PROJECTILE_SPAWN),
      Race_TestProjectileEvent(
        25u, 8u, RACE_REPLAY_PROJECTILE_HYPERBLASTER,
        RACE_REPLAY_PROJECTILE_SPAWN)
    }
  };
  length = Race_ReplayProjectiles_Encode(&message, payload, sizeof(payload));
  ck_assert_uint_eq(length, 104u);
  ck_assert_int_eq(Race_ReplayClientCache_ApplyProjectiles(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  ck_assert_uint_eq(cache.projectile_count, 2u);

  message.op = RACE_REPLAY_PROJECTILE_MESSAGE_EVENTS;
  message.sequence = 4u;
  message.event_count = 1u;
  message.events[0] = Race_TestProjectileEvent(
    25u, 7u, RACE_REPLAY_PROJECTILE_ROCKET,
    RACE_REPLAY_PROJECTILE_IMPACT);
  length = Race_ReplayProjectiles_Encode(&message, payload, sizeof(payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyProjectiles(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  ck_assert_uint_eq(cache.projectile_count, 1u);
  ck_assert_uint_eq(cache.projectiles[0].id, 8u);

  message.sequence = 5u;
  message.events[0].id = 99u;
  length = Race_ReplayProjectiles_Encode(&message, payload, sizeof(payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyProjectiles(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_MALFORMED);
  ck_assert_uint_eq(cache.projectile_count, 1u);

  message.event_count = RACE_REPLAY_PROJECTILE_MAX_MESSAGE_EVENTS;
  message.playhead_ms = 50u;
  for (size_t i = 0u; i < message.event_count; i++) {
    message.events[i] = Race_TestProjectileEvent(
      50u, (uint16_t) (100u + i), RACE_REPLAY_PROJECTILE_ROCKET,
      RACE_REPLAY_PROJECTILE_SPAWN);
  }
  ck_assert_uint_eq(Race_ReplayProjectiles_Encode(
                      &message, payload, sizeof(payload)),
                    236u);
  message.event_count++;
  ck_assert_uint_eq(Race_ReplayProjectiles_Encode(
                      &message, payload, sizeof(payload)),
                    0u);
  message.event_count = 1u;
  message.events[0].origin.x = NAN;
  ck_assert_uint_eq(Race_ReplayProjectiles_Encode(
                      &message, payload, sizeof(payload)),
                    0u);
} END_TEST

START_TEST(_Race_RacelineTransport) {
  uint8_t payload[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD];
  uint8_t oversized[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD + 1u] = { 0 };
  race_replay_client_cache_t cache;
  Race_ReplayClientCache_Clear(&cache);
  race_raceline_message_t message = {
    .op = RACE_RACELINE_MESSAGE_BEGIN,
    .source = RACE_REPLAY_SOURCE_WORLD_RECORD,
    .rank = 1u,
    .generation = 1u,
    .sequence = 1u,
    .replay_id = UINT64_C(0x123456789abcdef0),
    .total_points = RACE_REPLAY_RACELINE_CHUNK_MAX_POINTS,
    .duration_ms = 300u
  };
  size_t length = Race_Raceline_Encode(&message, payload, sizeof(payload));
  ck_assert_uint_eq(length, 32u);
  ck_assert_int_eq(Race_ReplayClientCache_ApplyRaceline(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_APPLIED);

  message.op = RACE_RACELINE_MESSAGE_CHUNK;
  message.sequence = 2u;
  message.point_count = RACE_REPLAY_RACELINE_CHUNK_MAX_POINTS;
  for (size_t i = 0u; i < message.point_count; i++) {
    message.points[i] = (race_raceline_point_t) {
      .time_ms = (uint32_t) i * 25u,
      .origin = Vec3((float) i, (float) i * 2.f, 4.f)
    };
  }
  length = Race_Raceline_Encode(&message, payload, sizeof(payload));
  ck_assert_uint_eq(length, 240u);
  race_raceline_message_t decoded;
  ck_assert(Race_Raceline_Decode(payload, length, &decoded));
  ck_assert(!Race_Raceline_Decode(oversized, sizeof(oversized), &decoded));
  ck_assert_uint_eq(decoded.point_count,
                    RACE_REPLAY_RACELINE_CHUNK_MAX_POINTS);
  ck_assert_int_eq(Race_ReplayClientCache_ApplyRaceline(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  ck_assert_uint_eq(cache.raceline_received_points,
                    RACE_REPLAY_RACELINE_CHUNK_MAX_POINTS);
  ck_assert(!cache.raceline_complete);

  message.op = RACE_RACELINE_MESSAGE_END;
  message.sequence = 3u;
  message.first_point = RACE_REPLAY_RACELINE_CHUNK_MAX_POINTS;
  message.point_count = 0u;
  length = Race_Raceline_Encode(&message, payload, sizeof(payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyRaceline(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  ck_assert(cache.raceline_complete);
  ck_assert(!cache.raceline_receiving);
  ck_assert_int_eq(Race_ReplayClientCache_ApplyRaceline(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_MALFORMED);
  ck_assert(!cache.raceline_complete);

  message = (race_raceline_message_t) {
    .op = RACE_RACELINE_MESSAGE_BEGIN,
    .source = RACE_REPLAY_SOURCE_PERSONAL_BEST,
    .generation = 2u,
    .sequence = 1u,
    .replay_id = UINT64_C(0xaaaaaaaaaaaaaaaa),
    .total_points = 3u,
    .duration_ms = 50u
  };
  length = Race_Raceline_Encode(&message, payload, sizeof(payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyRaceline(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  message.op = RACE_RACELINE_MESSAGE_CHUNK;
  message.sequence = 2u;
  message.first_point = 1u;
  message.point_count = 1u;
  message.points[0] = (race_raceline_point_t) {
    .time_ms = 25u,
    .origin = Vec3(1.f, 2.f, 3.f)
  };
  length = Race_Raceline_Encode(&message, payload, sizeof(payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyRaceline(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_MALFORMED);
  ck_assert_uint_eq(cache.raceline_received_points, 0u);

  message = (race_raceline_message_t) {
    .op = RACE_RACELINE_MESSAGE_BEGIN,
    .source = RACE_REPLAY_SOURCE_WORLD_RECORD,
    .rank = 1u,
    .generation = 1u,
    .sequence = 99u,
    .replay_id = UINT64_C(0xbbbbbbbbbbbbbbbb),
    .total_points = 2u,
    .duration_ms = 25u
  };
  length = Race_Raceline_Encode(&message, payload, sizeof(payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyRaceline(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_STALE);

  message = (race_raceline_message_t) {
    .op = RACE_RACELINE_MESSAGE_CLEAR,
    .generation = 2u,
    .sequence = 3u
  };
  length = Race_Raceline_Encode(&message, payload, sizeof(payload));
  ck_assert_int_eq(Race_ReplayClientCache_ApplyRaceline(
                     &cache, payload, length),
                   RACE_REPLAY_TRANSPORT_APPLIED);
  ck_assert(!cache.raceline_receiving);
  ck_assert(!cache.raceline_complete);
  ck_assert_uint_eq(cache.raceline_total_points, 0u);

  ck_assert(!Race_Raceline_Decode(payload, length - 1u, &decoded));
  payload[25] = 1u;
  ck_assert(!Race_Raceline_Decode(payload, length, &decoded));
  payload[25] = 0u;
  message = (race_raceline_message_t) {
    .op = RACE_RACELINE_MESSAGE_CHUNK,
    .source = RACE_REPLAY_SOURCE_WORLD_RECORD,
    .generation = 3u,
    .sequence = 1u,
    .replay_id = 1u,
    .total_points = 2u,
    .point_count = 0u,
    .duration_ms = 25u
  };
  ck_assert_uint_eq(Race_Raceline_Encode(
                      &message, payload, sizeof(payload)),
                    0u);
} END_TEST

START_TEST(_Race_RacelineExtractionAndWindow) {
  const size_t sample_count = RACE_REPLAY_RACELINE_MAX_POINTS + 1u;
  race_replay_sample_t *samples = calloc(sample_count, sizeof(*samples));
  race_raceline_point_t *points = calloc(
    RACE_REPLAY_RACELINE_MAX_POINTS, sizeof(*points));
  vec3_t *line = calloc(
    RACE_REPLAY_RACELINE_MAX_POINTS + 2u, sizeof(*line));
  ck_assert_ptr_nonnull(samples);
  ck_assert_ptr_nonnull(points);
  ck_assert_ptr_nonnull(line);

  race_replay_t replay;
  int32_t player_uid;
  ck_assert(Race_Replay_ProfilePlayerUid(RACE_TEST_UID_A, &player_uid));
  ck_assert(Race_Replay_Init(&replay, samples, sample_count, NULL, 0u,
                             "edge", RACE_TEST_UID_A, "Runner", player_uid,
                             0u));
  replay.elapsed_time = (uint32_t) (sample_count - 1u) *
                        RACE_REPLAY_TICK_MSEC;
  replay.sample_count = sample_count;
  for (size_t i = 0u; i < sample_count; i++) {
    replay.samples[i] = Race_TestReplaySample(
      (uint32_t) i * RACE_REPLAY_TICK_MSEC, (float) i);
  }
  ck_assert(Race_Replay_Valid(&replay));
  const size_t point_count = Race_ReplayRaceline_PointCount(
    replay.sample_count);
  ck_assert_uint_eq(point_count, RACE_REPLAY_RACELINE_MAX_POINTS);
  for (size_t i = 0u; i < point_count; i++) {
    ck_assert(Race_ReplayRaceline_Point(
      &replay, point_count, i, points + i));
  }
  ck_assert_uint_eq(points[0].time_ms, 0u);
  ck_assert_uint_eq(points[point_count - 1u].time_ms,
                    replay.elapsed_time);
  ck_assert(fabsf(points[0].origin.x) < 0.001f);
  ck_assert(fabsf(points[point_count - 1u].origin.x -
                  (float) (sample_count - 1u)) < 0.001f);
  ck_assert_uint_eq(Race_ReplayRaceline_BuildWindow(
                      points, point_count, 0u, 3000u, true,
                      line, RACE_REPLAY_RACELINE_MAX_POINTS + 2u),
                    point_count);
  ck_assert_uint_eq(Race_ReplayRaceline_BuildWindow(
                      points, point_count, 0u, 3000u, false,
                      line, RACE_REPLAY_RACELINE_MAX_POINTS + 2u),
                    1u);
  ck_assert(fabsf(line[0].x) < 0.001f);
  const size_t trail_count = Race_ReplayRaceline_BuildWindow(
    points, point_count, 5000u, 3000u, false,
    line, RACE_REPLAY_RACELINE_MAX_POINTS + 2u);
  ck_assert_uint_gt(trail_count, 2u);
  ck_assert(fabsf(line[0].x - 80.f) < 0.01f);
  ck_assert(fabsf(line[trail_count - 1u].x - 200.f) < 0.01f);
  points[2].time_ms = points[1].time_ms;
  ck_assert_uint_eq(Race_ReplayRaceline_BuildWindow(
                      points, point_count, 5000u, 3000u, false,
                      line, RACE_REPLAY_RACELINE_MAX_POINTS + 2u),
                    0u);

  free(line);
  free(points);
  free(samples);
} END_TEST

START_TEST(_Race_PublicationOrderingAndFailures) {
  race_test_publication_t test = {
    .replay_ok = true,
    .map_ok = true,
    .remove_ok = true,
    .newly_created = true
  };
  ck_assert_int_eq(Race_TestPublicationCommit(&test), RACE_PUBLICATION_OK);
  ck_assert_int_eq(test.order_length, 2u);
  ck_assert_int_eq(memcmp(test.order, "RM", 2u), 0);

  test = (race_test_publication_t) { .map_ok = true, .remove_ok = true };
  ck_assert_int_eq(Race_TestPublicationCommit(&test),
                   RACE_PUBLICATION_REPLAY_FAILED);
  ck_assert_int_eq(test.order_length, 1u);
  ck_assert_int_eq(test.order[0], 'R');

  test = (race_test_publication_t) {
    .replay_ok = true, .remove_ok = true, .newly_created = false
  };
  ck_assert_int_eq(Race_TestPublicationCommit(&test),
                   RACE_PUBLICATION_MAP_STATE_FAILED);
  ck_assert_int_eq(test.order_length, 2u);
  ck_assert_int_eq(memcmp(test.order, "RM", 2u), 0);

  test = (race_test_publication_t) {
    .replay_ok = true, .remove_ok = true, .newly_created = true
  };
  ck_assert_int_eq(Race_TestPublicationCommit(&test),
                   RACE_PUBLICATION_MAP_STATE_FAILED);
  ck_assert_int_eq(test.order_length, 3u);
  ck_assert_int_eq(memcmp(test.order, "RMD", 3u), 0);

  test = (race_test_publication_t) {
    .replay_ok = true, .newly_created = true
  };
  ck_assert_int_eq(Race_TestPublicationCommit(&test),
                   RACE_PUBLICATION_ORPHAN_RETAINED);
  ck_assert_int_eq(test.order_length, 3u);
} END_TEST

START_TEST(_Race_MapStatePersistenceAndRecovery) {
  race_leaderboard_record_t records[4] = { 0 };
  race_map_state_t state = Race_TestMapState(records, RACE_TEST_LENGTHOF(records), 1u);
  records[0] = Race_TestRecord(RACE_TEST_UID_A, "Runner", 1000u, NULL, 0);
  state.record_count = 1u;

  race_map_state_parse_result_t parse_result;
  ck_assert_int_eq(Race_MapStateStore_Commit(race_test_map_committed,
                                             race_test_map_candidate,
                                             &state, &parse_result),
                   RACE_MAP_STATE_STORE_OK);

  race_leaderboard_record_t loaded_records[4] = { 0 };
  race_map_state_t loaded;
  ck_assert_int_eq(Race_MapStateStore_Load(race_test_map_committed, "EDGE",
                                            RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY,
                                            loaded_records,
                                           RACE_TEST_LENGTHOF(loaded_records),
                                           &loaded, &parse_result),
                   RACE_MAP_STATE_STORE_OK);
  ck_assert(Race_MapState_Equals(&state, &loaded));

  race_leaderboard_record_t next_records[4] = { 0 };
  race_map_state_t next = {
    .records = next_records,
    .record_capacity = RACE_TEST_LENGTHOF(next_records)
  };
  race_leaderboard_record_t faster = Race_TestRecord(RACE_TEST_UID_A, "Renamed",
                                                     900u, NULL, 0);
  ck_assert(Race_MapState_ApplyCandidate(&state, &faster, &next, NULL));
  ck_assert_uint_eq(next.generation, 2u);

  char unavailable[RACE_TEST_PATH_SIZE];
  snprintf(unavailable, sizeof(unavailable), "%s/missing/candidate.state",
           race_test_directory);
  ck_assert_int_eq(Race_MapStateStore_Commit(race_test_map_committed,
                                             unavailable,
                                             &next, &parse_result),
                   RACE_MAP_STATE_STORE_IO_ERROR);
  ck_assert_int_eq(Race_MapStateStore_Load(race_test_map_committed, "edge",
                                            RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY,
                                            loaded_records,
                                           RACE_TEST_LENGTHOF(loaded_records),
                                           &loaded, &parse_result),
                   RACE_MAP_STATE_STORE_OK);
  ck_assert(Race_MapState_Equals(&state, &loaded));

  char serialized[8192];
  size_t serialized_length;
  ck_assert(Race_MapState_Serialize(&next, serialized, sizeof(serialized),
                                    &serialized_length));
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_map_candidate,
                                                   serialized, serialized_length),
                   RACE_PERSISTENCE_OK);

  ck_assert_int_eq(Race_MapStateStore_Load(race_test_map_committed, "edge",
                                            RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY,
                                            loaded_records,
                                           RACE_TEST_LENGTHOF(loaded_records),
                                           &loaded, &parse_result),
                   RACE_MAP_STATE_STORE_OK);
  ck_assert(Race_MapState_Equals(&state, &loaded));

  ck_assert_int_eq(remove(race_test_map_candidate), 0);
  ck_assert_int_eq(Race_Persistence_Promote(race_test_map_candidate,
                                            race_test_map_committed),
                   RACE_PERSISTENCE_NOT_FOUND);
  ck_assert_int_eq(Race_MapStateStore_Load(race_test_map_committed, "edge",
                                            RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY,
                                            loaded_records,
                                           RACE_TEST_LENGTHOF(loaded_records),
                                           &loaded, &parse_result),
                   RACE_MAP_STATE_STORE_OK);
  ck_assert(Race_MapState_Equals(&state, &loaded));

  static const char corrupt[] = "not a current map state";
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_map_committed,
                                                   corrupt, sizeof(corrupt) - 1u),
                   RACE_PERSISTENCE_OK);
  ck_assert_int_eq(Race_MapStateStore_Load(race_test_map_committed, "edge",
                                            RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY,
                                            loaded_records,
                                           RACE_TEST_LENGTHOF(loaded_records),
                                           &loaded, &parse_result),
                   RACE_MAP_STATE_STORE_CORRUPT);
  ck_assert(Race_MapState_Equals(&state, &loaded));

  char preserved[64];
  Race_TestPersistenceRead(race_test_map_committed, preserved, sizeof(preserved),
                           corrupt);
} END_TEST

START_TEST(_Race_MapStateMissingAndIdentityMismatch) {
  race_leaderboard_record_t records[4] = { 0 };
  race_map_state_t state;
  race_map_state_parse_result_t parse_result;

  ck_assert_int_eq(Race_MapStateStore_Load(race_test_map_committed, "edge",
                                            RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY,
                                            records, RACE_TEST_LENGTHOF(records),
                                           &state, &parse_result),
                   RACE_MAP_STATE_STORE_MISSING);
  ck_assert_uint_eq(state.generation, 0u);
  ck_assert_uint_eq(state.record_count, 0u);
  ck_assert_str_eq(state.map, "edge");

  state.generation = 1u;
  ck_assert_int_eq(Race_MapStateStore_Commit(race_test_map_committed,
                                             race_test_map_candidate,
                                             &state, &parse_result),
                   RACE_MAP_STATE_STORE_OK);

  race_map_state_t unchanged = state;
  ck_assert_int_eq(Race_MapStateStore_Load(
                                            race_test_map_committed, "edge",
                                            RACE_PHYSICS_PRESET_Q2_V1_KEY,
                                            records, RACE_TEST_LENGTHOF(records),
                                            &state, &parse_result),
                   RACE_MAP_STATE_STORE_IDENTITY_MISMATCH);
  ck_assert(Race_MapState_Equals(&state, &unchanged));
  ck_assert_int_eq(Race_MapStateStore_Load(race_test_map_committed, "fractures",
                                            RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY,
                                            records, RACE_TEST_LENGTHOF(records),
                                           &state, &parse_result),
                   RACE_MAP_STATE_STORE_IDENTITY_MISMATCH);
  ck_assert(Race_MapState_Equals(&state, &unchanged));
} END_TEST

START_TEST(_Race_ReplayPersistencePublicationAndRecovery) {
  race_replay_sample_t samples[4];
  race_replay_t replay = Race_TestReplay(samples, RACE_TEST_LENGTHOF(samples));
  race_replay_parse_result_t parse_result = RACE_REPLAY_PARSE_OK;
  uint64_t replay_id;
  bool newly_created;
  ck_assert_int_eq(Race_ReplayStore_Commit(
                     race_test_replay_directory, &replay, &replay_id,
                     &newly_created, &parse_result),
                   RACE_REPLAY_STORE_OK);
  ck_assert(newly_created);
  ck_assert_uint_ne(replay_id, 0u);
  replay.replay_id = replay_id;

  snprintf(race_test_replay_committed, sizeof(race_test_replay_committed),
           "%s/replay-%016llx.ghost", race_test_replay_directory,
           (unsigned long long) replay_id);
  snprintf(race_test_replay_candidate, sizeof(race_test_replay_candidate),
           "%s/replay-%016llx.candidate", race_test_replay_directory,
           (unsigned long long) replay_id);

  race_replay_sample_t loaded_samples[4];
  race_replay_t loaded;
  ck_assert_int_eq(Race_ReplayStore_Load(
                     race_test_replay_committed, "EDGE", replay_id,
                     loaded_samples, RACE_TEST_LENGTHOF(loaded_samples),
                     NULL, 0u,
                     &loaded, &parse_result),
                   RACE_REPLAY_STORE_OK);
  ck_assert(Race_Replay_Equals(&replay, &loaded));
  ck_assert_int_eq(Race_ReplayStore_Load(
                     race_test_replay_committed, "fractures", replay_id,
                     loaded_samples, RACE_TEST_LENGTHOF(loaded_samples),
                     NULL, 0u,
                     &loaded, &parse_result),
                   RACE_REPLAY_STORE_CORRUPT);
  ck_assert_int_eq(parse_result, RACE_REPLAY_PARSE_IDENTITY_MISMATCH);
  ck_assert_int_eq(Race_ReplayStore_Load(
                     race_test_replay_committed, "edge", replay_id + 1u,
                     loaded_samples, RACE_TEST_LENGTHOF(loaded_samples),
                     NULL, 0u,
                     &loaded, &parse_result),
                   RACE_REPLAY_STORE_CORRUPT);
  ck_assert_int_eq(parse_result, RACE_REPLAY_PARSE_IDENTITY_MISMATCH);

  race_leaderboard_record_t state_records[4] = { 0 };
  race_leaderboard_record_t next_records[4] = { 0 };
  race_map_state_t state = Race_TestMapState(
    state_records, RACE_TEST_LENGTHOF(state_records), 0u);
  race_map_state_t next = {
    .records = next_records,
    .record_capacity = RACE_TEST_LENGTHOF(next_records)
  };
  const uint32_t split = 25u;
  race_leaderboard_record_t record = Race_TestRecord(
    RACE_TEST_UID_A, "Runner", 50u, &split, 1u);
  ck_assert(Race_Leaderboard_RecordAttachReplay(&record, replay_id));
  ck_assert(Race_MapState_ApplyCandidate(&state, &record, &next, NULL));
  race_map_state_parse_result_t map_parse = RACE_MAP_STATE_PARSE_OK;
  ck_assert_int_eq(Race_MapStateStore_Commit(
                     race_test_map_committed, race_test_map_candidate,
                     &next, &map_parse),
                   RACE_MAP_STATE_STORE_OK);
  race_leaderboard_record_t published_records[4];
  race_map_state_t published;
  ck_assert_int_eq(Race_MapStateStore_Load(
                     race_test_map_committed, "edge",
                     RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY, published_records,
                     RACE_TEST_LENGTHOF(published_records), &published,
                     &map_parse),
                   RACE_MAP_STATE_STORE_OK);
  ck_assert(Race_MapState_Equals(&next, &published));
  ck_assert(Race_MapState_ReplayBacked(&published));

  newly_created = true;
  uint64_t reused_id = 0;
  ck_assert_int_eq(Race_ReplayStore_Commit(
                     race_test_replay_directory, &replay, &reused_id,
                     &newly_created, &parse_result),
                   RACE_REPLAY_STORE_OK);
  ck_assert(!newly_created);
  ck_assert_uint_eq(reused_id, replay_id);

  ck_assert_int_eq(Race_ReplayStore_Remove(race_test_replay_committed),
                   RACE_REPLAY_STORE_OK);
  ck_assert_int_eq(Race_ReplayStore_Load(
                     race_test_replay_committed, "edge", replay_id,
                     loaded_samples, RACE_TEST_LENGTHOF(loaded_samples),
                     NULL, 0u,
                     &loaded, &parse_result),
                   RACE_REPLAY_STORE_NOT_FOUND);

  replay.replay_id = 0;
  ck_assert_int_eq(Race_ReplayStore_Commit(
                     race_test_replay_directory, &replay, &replay_id,
                     &newly_created, &parse_result),
                   RACE_REPLAY_STORE_OK);
  static const char corrupt[] = "corrupt replay";
  ck_assert_int_eq(Race_Persistence_WriteCandidate(
                     race_test_replay_committed, corrupt,
                     sizeof(corrupt) - 1u),
                   RACE_PERSISTENCE_OK);
  ck_assert_int_eq(Race_ReplayStore_Load(
                     race_test_replay_committed, "edge", replay_id,
                     loaded_samples, RACE_TEST_LENGTHOF(loaded_samples),
                     NULL, 0u,
                     &loaded, &parse_result),
                   RACE_REPLAY_STORE_CORRUPT);

  char unavailable[RACE_TEST_PATH_SIZE];
  snprintf(unavailable, sizeof(unavailable), "%s/missing/replays",
           race_test_directory);
  replay.replay_id = 0;
  ck_assert_int_eq(Race_ReplayStore_Commit(
                     unavailable, &replay, &replay_id,
                     &newly_created, &parse_result),
                   RACE_REPLAY_STORE_IO_ERROR);
} END_TEST

START_TEST(_Race_SettingsCatalogAndTypes) {
  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  ck_assert_uint_eq(count, RACE_SETTING_TOTAL);
  ck_assert(Race_Settings_ValidateCatalog(catalog, count, NULL, 0));
  ck_assert(Race_Settings_CatalogRankCompatible(catalog, count,
                                                 RACE_PHYSICS_PRESET_Q2_V1_KEY,
                                                 NULL, 0));

  ck_assert_str_eq(catalog[RACE_SETTING_FINISH_CUE_ENABLED].key,
                   "finish_cue_enabled");
  ck_assert_str_eq(catalog[RACE_SETTING_FINISH_CUE_GAIN].key,
                   "finish_cue_gain");
  ck_assert_str_eq(catalog[RACE_SETTING_CHECKPOINT_FEEDBACK].key,
                   "checkpoint_feedback");
  ck_assert_str_eq(catalog[RACE_SETTING_WEAPONS].key, "weapons");
  ck_assert(catalog[RACE_SETTING_WEAPONS].default_value.boolean);
  ck_assert_int_eq(catalog[RACE_SETTING_WEAPONS].scopes,
                   RACE_SETTING_SCOPE_GLOBAL | RACE_SETTING_SCOPE_MAP);
  ck_assert(!catalog[RACE_SETTING_WEAPONS].runtime_mutable);
  ck_assert(catalog[RACE_SETTING_WEAPONS].next_map);
  ck_assert_ptr_eq(Race_Settings_DescriptorForKey("finish_cue_gain"),
                   catalog + RACE_SETTING_FINISH_CUE_GAIN);
  ck_assert_ptr_null(Race_Settings_DescriptorForKey("timelimit"));

  race_setting_value_t value;
  const race_setting_descriptor_t *boolean =
    catalog + RACE_SETTING_FINISH_CUE_ENABLED;
  ck_assert(Race_Settings_ParseValue(boolean, "0", &value, NULL, 0));
  ck_assert(!value.boolean);
  ck_assert(Race_Settings_ParseValue(boolean, "1", &value, NULL, 0));
  ck_assert(value.boolean);
  ck_assert(!Race_Settings_ParseValue(boolean, "true", &value, NULL, 0));

  const race_setting_descriptor_t *integer =
    catalog + RACE_SETTING_FINISH_CUE_GAIN;
  ck_assert(Race_Settings_ParseValue(integer, "1", &value, NULL, 0));
  ck_assert_int_eq(value.integer, 1);
  ck_assert(Race_Settings_ParseValue(integer, "100", &value, NULL, 0));
  ck_assert_int_eq(value.integer, 100);
  ck_assert(!Race_Settings_ParseValue(integer, "0", &value, NULL, 0));
  ck_assert(!Race_Settings_ParseValue(integer, "101", &value, NULL, 0));
  ck_assert(!Race_Settings_ParseValue(integer, "+50", &value, NULL, 0));
  ck_assert(!Race_Settings_ParseValue(integer, "050", &value, NULL, 0));

  const race_setting_descriptor_t *enumeration =
    catalog + RACE_SETTING_CHECKPOINT_FEEDBACK;
  ck_assert(Race_Settings_ParseValue(enumeration, "time", &value, NULL, 0));
  ck_assert_str_eq(value.enumeration, "time");
  ck_assert(Race_Settings_ParseValue(enumeration, "silent", &value, NULL, 0));
  ck_assert_str_eq(value.enumeration, "silent");
  ck_assert(!Race_Settings_ParseValue(enumeration, "verbose", &value, NULL, 0));

  for (size_t i = 0; i < count; i++) {
    ck_assert_int_ne(catalog[i].type, (race_setting_type_t) 3);
    ck_assert_int_eq(catalog[i].ranking_impact,
                     RACE_SETTING_RANKING_COMPATIBLE);
    ck_assert(!catalog[i].affects_prediction);
  }

  race_setting_descriptor_t invalid[RACE_SETTING_TOTAL];
  memcpy(invalid, catalog, sizeof(invalid));
  invalid[1].key = invalid[0].key;
  ck_assert(!Race_Settings_ValidateCatalog(invalid, count, NULL, 0));

  memcpy(invalid, catalog, sizeof(invalid));
  invalid[1].id = invalid[0].id;
  ck_assert(!Race_Settings_ValidateCatalog(invalid, count, NULL, 0));

  memcpy(invalid, catalog, sizeof(invalid));
  invalid[0].ranking_impact = RACE_SETTING_RANKING_INVALIDATES_QUETOO_COMMON;
  ck_assert(!Race_Settings_CatalogRankCompatible(invalid, count,
                                                  RACE_PHYSICS_PRESET_Q2_V1_KEY,
                                                  NULL, 0));

  memcpy(invalid, catalog, sizeof(invalid));
  invalid[0].affects_prediction = true;
  ck_assert(!Race_Settings_CatalogRankCompatible(invalid, count,
                                                  RACE_PHYSICS_PRESET_Q2_V1_KEY,
                                                  NULL, 0));
  ck_assert(!Race_Settings_CatalogRankCompatible(catalog, count,
                                                  "future/ruleset",
                                                  NULL, 0));
} END_TEST

START_TEST(_Race_SettingsResolutionAndRevision) {
  race_settings_state_t state;
  ck_assert(Race_Settings_StateInit(&state));
  ck_assert_uint_eq(state.revision, 1u);
  ck_assert(state.entries[RACE_SETTING_FINISH_CUE_ENABLED].effective.boolean);
  ck_assert_int_eq(state.entries[RACE_SETTING_FINISH_CUE_ENABLED].source,
                   RACE_SETTING_SOURCE_DEFAULT);
  ck_assert(state.entries[RACE_SETTING_WEAPONS].effective.boolean);
  ck_assert_int_eq(state.entries[RACE_SETTING_WEAPONS].source,
                   RACE_SETTING_SOURCE_DEFAULT);

  race_settings_state_t candidate;
  race_settings_change_t change;
  race_settings_state_t weapons_state;
  ck_assert(Race_Settings_StateSet(&state, RACE_SETTING_SOURCE_MAP,
                                   "weapons", "0",
                                   &weapons_state, &change, NULL, 0));
  ck_assert(!weapons_state.entries[RACE_SETTING_WEAPONS].effective.boolean);
  ck_assert(change.effective_changed);
  ck_assert(!Race_Settings_StateSet(&weapons_state, RACE_SETTING_SOURCE_RUNTIME,
                                    "weapons", "1",
                                    &candidate, &change, NULL, 0));

  ck_assert(Race_Settings_StateSet(&state, RACE_SETTING_SOURCE_GLOBAL,
                                   "finish_cue_enabled", "0",
                                   &candidate, &change, NULL, 0));
  ck_assert(change.source_changed);
  ck_assert(change.effective_changed);
  ck_assert_uint_eq(candidate.revision, 2u);
  state = candidate;

  ck_assert(Race_Settings_StateSet(&state, RACE_SETTING_SOURCE_MAP,
                                   "finish_cue_enabled", "1",
                                   &candidate, &change, NULL, 0));
  ck_assert(change.effective_changed);
  ck_assert_uint_eq(candidate.revision, 3u);
  ck_assert_int_eq(candidate.entries[RACE_SETTING_FINISH_CUE_ENABLED].source,
                   RACE_SETTING_SOURCE_MAP);
  state = candidate;

  ck_assert(Race_Settings_StateSet(&state, RACE_SETTING_SOURCE_RUNTIME,
                                   "finish_cue_enabled", "0",
                                   &candidate, &change, NULL, 0));
  ck_assert(change.effective_changed);
  ck_assert_uint_eq(candidate.revision, 4u);
  ck_assert_int_eq(candidate.entries[RACE_SETTING_FINISH_CUE_ENABLED].source,
                   RACE_SETTING_SOURCE_RUNTIME);
  state = candidate;

  ck_assert(Race_Settings_StateSet(&state, RACE_SETTING_SOURCE_GLOBAL,
                                   "finish_cue_enabled", "1",
                                   &candidate, &change, NULL, 0));
  ck_assert(change.source_changed);
  ck_assert(!change.effective_changed);
  ck_assert_uint_eq(candidate.revision, 4u);
  state = candidate;

  ck_assert(Race_Settings_StateSet(&state, RACE_SETTING_SOURCE_GLOBAL,
                                   "finish_cue_enabled", "1",
                                   &candidate, &change, NULL, 0));
  ck_assert(!change.source_changed);
  ck_assert(!change.effective_changed);
  ck_assert_uint_eq(candidate.revision, 4u);

  ck_assert(!Race_Settings_StateSet(&state, RACE_SETTING_SOURCE_GLOBAL,
                                    "finish_cue_gain", "101",
                                    &candidate, &change, NULL, 0));
  ck_assert_uint_eq(state.revision, 4u);

  ck_assert(Race_Settings_StateUnset(&state, RACE_SETTING_SOURCE_RUNTIME,
                                     "finish_cue_enabled",
                                     &candidate, &change, NULL, 0));
  ck_assert(change.effective_changed);
  ck_assert_uint_eq(candidate.revision, 5u);
  ck_assert_int_eq(candidate.entries[RACE_SETTING_FINISH_CUE_ENABLED].source,
                   RACE_SETTING_SOURCE_MAP);
  state = candidate;

  candidate = state;
  for (size_t i = 0; i < RACE_SETTING_TOTAL; i++) {
    memset(&candidate.sources[i][RACE_SETTING_SOURCE_MAP], 0,
           sizeof(candidate.sources[i][RACE_SETTING_SOURCE_MAP]));
    memset(&candidate.sources[i][RACE_SETTING_SOURCE_RUNTIME], 0,
           sizeof(candidate.sources[i][RACE_SETTING_SOURCE_RUNTIME]));
  }
  ck_assert(Race_Settings_StateResolve(&state, &candidate, &change, NULL, 0));
  ck_assert(change.source_changed);
  ck_assert(!change.effective_changed);
  ck_assert_uint_eq(candidate.revision, 5u);
  ck_assert_int_eq(candidate.entries[RACE_SETTING_FINISH_CUE_ENABLED].source,
                   RACE_SETTING_SOURCE_GLOBAL);

  state = candidate;
  state.revision = UINT64_MAX;
  ck_assert(Race_Settings_StateValid(&state));
  ck_assert(!Race_Settings_StateSet(&state, RACE_SETTING_SOURCE_RUNTIME,
                                    "checkpoint_feedback", "silent",
                                    &candidate, &change, NULL, 0));
  ck_assert(state.entries[RACE_SETTING_CHECKPOINT_FEEDBACK].effective.enumeration[0]);
} END_TEST

START_TEST(_Race_SettingsFormatAndLegacyPolicy) {
  race_settings_state_t state;
  race_settings_state_t candidate;
  race_settings_change_t change;
  ck_assert(Race_Settings_StateInit(&state));
  ck_assert(Race_Settings_StateSet(&state, RACE_SETTING_SOURCE_GLOBAL,
                                   "finish_cue_enabled", "0",
                                   &candidate, &change, NULL, 0));
  state = candidate;
  ck_assert(Race_Settings_StateSet(&state, RACE_SETTING_SOURCE_GLOBAL,
                                   "finish_cue_gain", "50",
                                   &candidate, &change, NULL, 0));
  state = candidate;

  race_settings_document_t document;
  ck_assert(Race_Settings_DocumentFromState(&document,
                                            RACE_SETTING_SCOPE_GLOBAL,
                                            NULL, 7u, &state));

  char serialized[4096];
  size_t length;
  ck_assert(Race_Settings_Serialize(&document, serialized,
                                    sizeof(serialized), &length));
  ck_assert_uint_eq(length, strlen(serialized));

  race_settings_document_t parsed;
  ck_assert_int_eq(Race_Settings_Parse(serialized, length,
                                       RACE_SETTING_SCOPE_GLOBAL, NULL,
                                       &parsed),
                   RACE_SETTINGS_PARSE_OK);
  ck_assert(Race_Settings_DocumentEquals(&document, &parsed));

  char repeated[4096];
  size_t repeated_length;
  ck_assert(Race_Settings_Serialize(&parsed, repeated,
                                    sizeof(repeated), &repeated_length));
  ck_assert_uint_eq(repeated_length, length);
  ck_assert_int_eq(memcmp(serialized, repeated, length), 0);

  char committed[MAX_OS_PATH];
  char persistence_candidate[MAX_OS_PATH];
  ck_assert(Race_Settings_Paths(RACE_SETTING_SCOPE_GLOBAL, NULL,
                               committed, sizeof(committed),
                               persistence_candidate,
                               sizeof(persistence_candidate)));
  ck_assert_str_eq(committed, "settings/global.settings");
  ck_assert_str_eq(persistence_candidate, "settings/global.candidate");
  ck_assert(Race_Settings_Paths(RACE_SETTING_SCOPE_MAP, "maps/EDGE.bsp",
                               committed, sizeof(committed),
                               persistence_candidate,
                               sizeof(persistence_candidate)));
  ck_assert_str_eq(committed,
                   "settings/maps/65646765.settings");

  char malformed[4096];
  memcpy(malformed, serialized, length + 1u);
  malformed[strlen(RACE_SETTINGS_MAGIC) - 1u] = '2';
  ck_assert_int_eq(Race_Settings_Parse(malformed, length,
                                       RACE_SETTING_SCOPE_GLOBAL, NULL,
                                       &parsed),
                   RACE_SETTINGS_PARSE_UNKNOWN_VERSION);

  memcpy(malformed, serialized, length + 1u);
  size_t malformed_length = Race_TestSettingsReplace(
    malformed, length, sizeof(malformed),
    "finish_cue_enabled", "legacy_timelimit__");
  Race_TestSettingsUpdateChecksum(malformed, malformed_length);
  ck_assert_int_eq(Race_Settings_Parse(malformed, malformed_length,
                                       RACE_SETTING_SCOPE_GLOBAL, NULL,
                                       &parsed),
                   RACE_SETTINGS_PARSE_UNKNOWN_KEY);

  memcpy(malformed, serialized, length + 1u);
  malformed_length = Race_TestSettingsReplace(
    malformed, length, sizeof(malformed),
    "finish_cue_gain", "finish_cue_enabled");
  Race_TestSettingsUpdateChecksum(malformed, malformed_length);
  ck_assert_int_eq(Race_Settings_Parse(malformed, malformed_length,
                                       RACE_SETTING_SCOPE_GLOBAL, NULL,
                                       &parsed),
                   RACE_SETTINGS_PARSE_DUPLICATE_KEY);

  memcpy(malformed, serialized, length + 1u);
  malformed_length = Race_TestSettingsReplace(
    malformed, length, sizeof(malformed),
    "finish_cue_enabled|bool|0", "finish_cue_enabled|enum|time");
  Race_TestSettingsUpdateChecksum(malformed, malformed_length);
  ck_assert_int_eq(Race_Settings_Parse(malformed, malformed_length,
                                       RACE_SETTING_SCOPE_GLOBAL, NULL,
                                       &parsed),
                   RACE_SETTINGS_PARSE_WRONG_TYPE);

  memcpy(malformed, serialized, length + 1u);
  malformed_length = Race_TestSettingsReplace(
    malformed, length, sizeof(malformed),
    "finish_cue_gain|int|50", "finish_cue_gain|int|0");
  Race_TestSettingsUpdateChecksum(malformed, malformed_length);
  ck_assert_int_eq(Race_Settings_Parse(malformed, malformed_length,
                                       RACE_SETTING_SCOPE_GLOBAL, NULL,
                                       &parsed),
                   RACE_SETTINGS_PARSE_INVALID_VALUE);

  memcpy(malformed, serialized, length + 1u);
  char *unchecked_value = strstr(malformed, "finish_cue_gain|int|50");
  ck_assert_ptr_nonnull(unchecked_value);
  unchecked_value[strlen("finish_cue_gain|int|5")] = '1';
  ck_assert_int_eq(Race_Settings_Parse(malformed, length,
                                       RACE_SETTING_SCOPE_GLOBAL, NULL,
                                       &parsed),
                   RACE_SETTINGS_PARSE_CHECKSUM);
  ck_assert_int_eq(Race_Settings_Parse(serialized, length - 1u,
                                       RACE_SETTING_SCOPE_GLOBAL, NULL,
                                       &parsed),
                   RACE_SETTINGS_PARSE_MALFORMED);
  ck_assert_int_eq(Race_Settings_Parse(serialized,
                                       RACE_SETTINGS_MAX_FILE_BYTES + 1u,
                                       RACE_SETTING_SCOPE_GLOBAL, NULL,
                                       &parsed),
                   RACE_SETTINGS_PARSE_TOO_LARGE);

  const char legacy[] = "// Race admin settings\nset timelimit \"20\"\n";
  ck_assert_int_eq(Race_Settings_Parse(legacy, sizeof(legacy) - 1u,
                                       RACE_SETTING_SCOPE_GLOBAL, NULL,
                                       &parsed),
                   RACE_SETTINGS_PARSE_LEGACY_UNSUPPORTED);
} END_TEST

START_TEST(_Race_SettingsPersistenceAndRecovery) {
  race_settings_document_t loaded;
  race_settings_parse_result_t parse_result;
  ck_assert_int_eq(Race_SettingsStore_Load(race_test_committed,
                                           RACE_SETTING_SCOPE_GLOBAL, NULL,
                                           &loaded, &parse_result),
                   RACE_SETTINGS_STORE_MISSING);
  ck_assert_uint_eq(loaded.generation, 0u);

  race_settings_state_t state;
  race_settings_state_t candidate;
  race_settings_change_t change;
  ck_assert(Race_Settings_StateInit(&state));
  ck_assert(Race_Settings_StateSet(&state, RACE_SETTING_SOURCE_GLOBAL,
                                   "checkpoint_feedback", "silent",
                                   &candidate, &change, NULL, 0));
  state = candidate;

  race_settings_document_t committed;
  ck_assert(Race_Settings_DocumentFromState(&committed,
                                            RACE_SETTING_SCOPE_GLOBAL,
                                            NULL, 1u, &state));
  ck_assert_int_eq(Race_SettingsStore_Commit(race_test_committed,
                                             race_test_candidate,
                                             &committed, &parse_result),
                   RACE_SETTINGS_STORE_OK);
  ck_assert_int_eq(Race_SettingsStore_Load(race_test_committed,
                                           RACE_SETTING_SCOPE_GLOBAL, NULL,
                                           &loaded, &parse_result),
                   RACE_SETTINGS_STORE_OK);
  ck_assert(Race_Settings_DocumentEquals(&committed, &loaded));

  race_settings_document_t stale = committed;
  stale.generation = 2u;
  char serialized[4096];
  size_t serialized_length;
  ck_assert(Race_Settings_Serialize(&stale, serialized,
                                    sizeof(serialized), &serialized_length));
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_candidate,
                                                   serialized,
                                                   serialized_length),
                   RACE_PERSISTENCE_OK);
  ck_assert_int_eq(Race_SettingsStore_Load(race_test_committed,
                                           RACE_SETTING_SCOPE_GLOBAL, NULL,
                                           &loaded, &parse_result),
                   RACE_SETTINGS_STORE_OK);
  ck_assert(Race_Settings_DocumentEquals(&committed, &loaded));

  char unavailable[RACE_TEST_PATH_SIZE];
  snprintf(unavailable, sizeof(unavailable),
           "%s/missing/candidate.settings", race_test_directory);
  ck_assert_int_eq(Race_SettingsStore_Commit(race_test_committed,
                                             unavailable,
                                             &stale, &parse_result),
                   RACE_SETTINGS_STORE_IO_ERROR);
  ck_assert_int_eq(Race_SettingsStore_Load(race_test_committed,
                                           RACE_SETTING_SCOPE_GLOBAL, NULL,
                                           &loaded, &parse_result),
                   RACE_SETTINGS_STORE_OK);
  ck_assert(Race_Settings_DocumentEquals(&committed, &loaded));

  Race_TestPersistenceRemove(race_test_candidate);
  ck_assert_int_eq(Race_SettingsStore_Commit(race_test_directory,
                                             race_test_candidate,
                                             &stale, &parse_result),
                   RACE_SETTINGS_STORE_IO_ERROR);
  ck_assert_int_eq(Race_SettingsStore_Load(race_test_committed,
                                           RACE_SETTING_SCOPE_GLOBAL, NULL,
                                           &loaded, &parse_result),
                   RACE_SETTINGS_STORE_OK);
  ck_assert(Race_Settings_DocumentEquals(&committed, &loaded));

  const char corrupt[] = "QUETOO_RACE_SETTINGS_V1\nscope=global\n";
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_committed,
                                                   corrupt,
                                                   sizeof(corrupt) - 1u),
                   RACE_PERSISTENCE_OK);
  race_settings_document_t unchanged = loaded;
  ck_assert_int_eq(Race_SettingsStore_Load(race_test_committed,
                                           RACE_SETTING_SCOPE_GLOBAL, NULL,
                                           &loaded, &parse_result),
                   RACE_SETTINGS_STORE_CORRUPT);
  ck_assert(Race_Settings_DocumentEquals(&unchanged, &loaded));
  char preserved[128];
  Race_TestPersistenceRead(race_test_committed, preserved,
                           sizeof(preserved), corrupt);
} END_TEST

static uint32_t Race_TestAdminCrc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; i++) {
    crc ^= bytes[i];
    for (size_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                          (uint32_t) -(int32_t) (crc & 1u));
    }
  }
  return ~crc;
}

static void Race_TestAdminResign(char *serialized) {
  char *crc = strstr(serialized, "\ncrc=");
  ck_assert_ptr_nonnull(crc);
  crc++;
  const uint32_t value = Race_TestAdminCrc32(serialized,
                                             (size_t) (crc - serialized));
  ck_assert_int_eq(snprintf(crc, 14u, "crc=%08x\n", value), 13);
}

static void Race_TestReplaceSameLength(char *text, const char *from,
                                       const char *to) {
  ck_assert_uint_eq(strlen(from), strlen(to));
  char *match = strstr(text, from);
  ck_assert_ptr_nonnull(match);
  memcpy(match, to, strlen(to));
}

START_TEST(_Race_AdminAccountModel) {
  race_admin_document_t document;
  ck_assert(Race_Admin_DocumentInit(&document));
  ck_assert(Race_Admin_DocumentValid(&document, false));

  race_admin_document_t candidate;
  ck_assert(Race_Admin_AddAccount(&document, "OWNER-01", "Alice",
                                  RACE_ADMIN_ROLE_OWNER, &candidate));
  document = candidate;
  ck_assert_uint_eq(document.generation, 1u);
  ck_assert_uint_eq(document.count, 1u);
  ck_assert_str_eq(document.accounts[0].id, "owner-01");
  ck_assert_str_eq(document.accounts[0].handle, "alice");
  ck_assert(document.accounts[0].enabled);
  ck_assert_int_eq(document.accounts[0].role, RACE_ADMIN_ROLE_OWNER);

  ck_assert(!Race_Admin_AddAccount(&document, "OWNER-01", "other",
                                   RACE_ADMIN_ROLE_MODERATOR, &candidate));
  ck_assert(!Race_Admin_AddAccount(&document, "other", "ALICE",
                                   RACE_ADMIN_ROLE_MODERATOR, &candidate));
  ck_assert(!Race_Admin_AddAccount(&document, "bad id", "valid",
                                   RACE_ADMIN_ROLE_MODERATOR, &candidate));
  ck_assert(!Race_Admin_AddAccount(&document, "valid", "bad handle",
                                   RACE_ADMIN_ROLE_MODERATOR, &candidate));
  char oversized[RACE_ADMIN_HANDLE_SIZE + 1u];
  memset(oversized, 'a', sizeof(oversized) - 1u);
  oversized[sizeof(oversized) - 1u] = '\0';
  ck_assert(!Race_Admin_AddAccount(&document, "valid", oversized,
                                   RACE_ADMIN_ROLE_MODERATOR, &candidate));

  ck_assert_uint_eq(Race_Admin_RoleCapabilities(RACE_ADMIN_ROLE_MODERATOR),
                    RACE_ADMIN_CAP_PLAYER_KICK |
                    RACE_ADMIN_CAP_PLAYER_BAN |
                    RACE_ADMIN_CAP_VOTE_ADMIN);
  ck_assert(Race_Admin_RoleCapabilities(RACE_ADMIN_ROLE_OPERATOR) &
            RACE_ADMIN_CAP_SETTINGS_MUTATE);
  ck_assert(Race_Admin_RoleCapabilities(RACE_ADMIN_ROLE_OWNER) &
            RACE_ADMIN_CAP_ACCOUNT_MANAGE);
  ck_assert_uint_eq(Race_Admin_RoleCapabilities(RACE_ADMIN_ROLE_NONE), 0u);

  ck_assert(Race_Admin_SetAccountRole(&document, "owner-01",
                                      RACE_ADMIN_ROLE_OPERATOR, &candidate));
  document = candidate;
  ck_assert_uint_eq(document.accounts[0].revision, 2u);
  ck_assert(Race_Admin_SetAccountEnabled(&document, "owner-01", false,
                                         &candidate));
  document = candidate;
  ck_assert(!document.accounts[0].enabled);
  ck_assert_uint_eq(document.accounts[0].revision, 3u);
  ck_assert(Race_Admin_SetAccountHandle(&document, "owner-01", "renamed",
                                        &candidate));
  document = candidate;
  ck_assert_ptr_nonnull(Race_Admin_AccountByHandle(&document, "RENAMED"));

  race_admin_document_t maximum;
  ck_assert(Race_Admin_DocumentInit(&maximum));
  for (size_t i = 0; i < RACE_ADMIN_MAX_ACCOUNTS; i++) {
    char id[16], handle[16];
    snprintf(id, sizeof(id), "id%02zu", i);
    snprintf(handle, sizeof(handle), "handle%02zu", i);
    ck_assert(Race_Admin_AddAccount(&maximum, id, handle,
                                    RACE_ADMIN_ROLE_MODERATOR, &candidate));
    maximum = candidate;
  }
  ck_assert_uint_eq(maximum.count, RACE_ADMIN_MAX_ACCOUNTS);
  ck_assert(!Race_Admin_AddAccount(&maximum, "overflow", "overflow",
                                   RACE_ADMIN_ROLE_OWNER, &candidate));
} END_TEST

START_TEST(_Race_AdminSessionAndAuthorization) {
  race_admin_document_t document, candidate;
  ck_assert(Race_Admin_DocumentInit(&document));
  ck_assert(Race_Admin_AddAccount(&document, "moderator-id", "moderator",
                                  RACE_ADMIN_ROLE_MODERATOR, &candidate));
  document = candidate;

  race_admin_session_t session = { 0 };
  ck_assert(!Race_Admin_SessionAuthenticated(&session, &document));
  ck_assert(!Race_Admin_SessionHasCapability(
    &session, &document, RACE_ADMIN_CAP_PLAYER_KICK));
  ck_assert(Race_Admin_SessionGrant(&session, &document, "moderator-id"));
  ck_assert(Race_Admin_SessionAuthenticated(&session, &document));
  ck_assert(Race_Admin_SessionHasCapability(
    &session, &document, RACE_ADMIN_CAP_PLAYER_KICK));
  ck_assert(!Race_Admin_SessionHasCapability(
    &session, &document, RACE_ADMIN_CAP_SETTINGS_MUTATE));
  ck_assert(!Race_Admin_SessionHasCapability(
    &session, &document, (race_admin_capability_t) (1u << 31)));

  race_admin_session_t map_change_session = session;
  Race_Admin_SessionClear(&map_change_session);
  ck_assert(!Race_Admin_SessionAuthenticated(&map_change_session, &document));

  ck_assert(Race_Admin_SetAccountEnabled(&document, "moderator-id", false,
                                         &candidate));
  document = candidate;
  ck_assert(!Race_Admin_SessionAuthenticated(&session, &document));
  ck_assert(!Race_Admin_SessionGrant(&session, &document, "moderator-id"));

  ck_assert(Race_Admin_SetAccountEnabled(&document, "moderator-id", true,
                                         &candidate));
  document = candidate;
  ck_assert(Race_Admin_SessionGrant(&session, &document, "moderator-id"));
  ck_assert(Race_Admin_SetAccountRole(&document, "moderator-id",
                                      RACE_ADMIN_ROLE_OPERATOR, &candidate));
  document = candidate;
  ck_assert(!Race_Admin_SessionAuthenticated(&session, &document));
  ck_assert(Race_Admin_SessionGrant(&session, &document, "moderator-id"));
  ck_assert(Race_Admin_SessionHasCapability(
    &session, &document, RACE_ADMIN_CAP_SETTINGS_MUTATE));

  Race_Admin_SessionClear(&session);
  ck_assert(!Race_Admin_SessionAuthenticated(&session, &document));

  ck_assert(Race_Admin_SessionGrant(&session, &document, "moderator-id"));
  memset(&session, 0, sizeof(session));
  ck_assert(!Race_Admin_SessionAuthenticated(&session, &document));

  ck_assert(Race_Admin_SessionGrant(&session, &document, "moderator-id"));
  ck_assert(Race_Admin_RemoveAccount(&document, "moderator-id", &candidate));
  document = candidate;
  ck_assert(!Race_Admin_SessionAuthenticated(&session, &document));
} END_TEST

START_TEST(_Race_AdminActionAuthorization) {
  race_admin_document_t document, candidate;
  ck_assert(Race_Admin_DocumentInit(&document));
  ck_assert(Race_Admin_AddAccount(&document, "moderator-id", "moderator",
                                  RACE_ADMIN_ROLE_MODERATOR, &candidate));
  document = candidate;
  ck_assert(Race_Admin_AddAccount(&document, "operator-id", "operator",
                                  RACE_ADMIN_ROLE_OPERATOR, &candidate));
  document = candidate;

  ck_assert_int_eq(Race_Admin_ActionCapability(
                     RACE_ADMIN_ACTION_SETTINGS_MUTATE),
                   RACE_ADMIN_CAP_SETTINGS_MUTATE);
  ck_assert_int_eq(Race_Admin_ActionCapability(RACE_ADMIN_ACTION_MAP_CHANGE),
                   RACE_ADMIN_CAP_MAP_CHANGE);
  ck_assert_int_eq(Race_Admin_ActionCapability(RACE_ADMIN_ACTION_PLAYER_KICK),
                   RACE_ADMIN_CAP_PLAYER_KICK);
  ck_assert_int_eq(Race_Admin_ActionCapability(RACE_ADMIN_ACTION_VOTE_CANCEL),
                   RACE_ADMIN_CAP_VOTE_ADMIN);
  ck_assert_int_eq(Race_Admin_ActionCapability(RACE_ADMIN_ACTION_TOTAL), 0);

  race_admin_session_t session = { 0 };
  for (race_admin_action_t action = RACE_ADMIN_ACTION_SETTINGS_MUTATE;
       action < RACE_ADMIN_ACTION_TOTAL; action++) {
    ck_assert(!Race_Admin_SessionCanPerform(&session, &document, action));
  }

  ck_assert(Race_Admin_SessionGrant(&session, &document, "moderator-id"));
  ck_assert(!Race_Admin_SessionCanPerform(
    &session, &document, RACE_ADMIN_ACTION_SETTINGS_MUTATE));
  ck_assert(!Race_Admin_SessionCanPerform(
    &session, &document, RACE_ADMIN_ACTION_MAP_CHANGE));
  ck_assert(Race_Admin_SessionCanPerform(
    &session, &document, RACE_ADMIN_ACTION_PLAYER_KICK));
  ck_assert(Race_Admin_SessionCanPerform(
    &session, &document, RACE_ADMIN_ACTION_VOTE_CANCEL));

  race_admin_session_t wrong_vote_capability = session;
  wrong_vote_capability.capabilities &= ~RACE_ADMIN_CAP_VOTE_ADMIN;
  ck_assert(!Race_Admin_SessionCanPerform(
    &wrong_vote_capability, &document, RACE_ADMIN_ACTION_VOTE_CANCEL));

  ck_assert(Race_Admin_SessionGrant(&session, &document, "operator-id"));
  for (race_admin_action_t action = RACE_ADMIN_ACTION_SETTINGS_MUTATE;
       action < RACE_ADMIN_ACTION_TOTAL; action++) {
    ck_assert(Race_Admin_SessionCanPerform(&session, &document, action));
  }

  // The current map-change reconnect handshake clears connection-local state.
  race_admin_session_t map_session = session;
  Race_Admin_SessionClear(&map_session);
  ck_assert(!Race_Admin_SessionCanPerform(
    &map_session, &document, RACE_ADMIN_ACTION_MAP_CHANGE));

  // Profile identifiers and display names are not administrator account IDs.
  race_admin_session_t spoofed = {
    .authenticated = true,
    .account_revision = 1u,
    .capabilities = RACE_ADMIN_CAP_ALL
  };
  memcpy(spoofed.account_id, RACE_TEST_UID_A, RACE_ADMIN_ACCOUNT_ID_MAX);
  spoofed.account_id[RACE_ADMIN_ACCOUNT_ID_MAX] = '\0';
  ck_assert(!Race_Admin_SessionCanPerform(
    &spoofed, &document, RACE_ADMIN_ACTION_PLAYER_KICK));
  snprintf(spoofed.account_id, sizeof(spoofed.account_id), "%s",
           "duplicate-display-name");
  ck_assert(!Race_Admin_SessionCanPerform(
    &spoofed, &document, RACE_ADMIN_ACTION_PLAYER_KICK));

  ck_assert(Race_Admin_SetAccountEnabled(&document, "operator-id", false,
                                         &candidate));
  document = candidate;
  ck_assert(!Race_Admin_SessionCanPerform(
    &session, &document, RACE_ADMIN_ACTION_SETTINGS_MUTATE));

  ck_assert(Race_Admin_SetAccountEnabled(&document, "operator-id", true,
                                         &candidate));
  document = candidate;
  ck_assert(Race_Admin_SessionGrant(&session, &document, "operator-id"));
  ck_assert(Race_Admin_SetAccountRole(&document, "operator-id",
                                      RACE_ADMIN_ROLE_MODERATOR, &candidate));
  document = candidate;
  ck_assert(!Race_Admin_SessionCanPerform(
    &session, &document, RACE_ADMIN_ACTION_MAP_CHANGE));
  ck_assert(Race_Admin_SessionGrant(&session, &document, "operator-id"));
  ck_assert(!Race_Admin_SessionCanPerform(
    &session, &document, RACE_ADMIN_ACTION_MAP_CHANGE));
  ck_assert(Race_Admin_SessionCanPerform(
    &session, &document, RACE_ADMIN_ACTION_PLAYER_KICK));

  ck_assert(Race_Admin_RemoveAccount(&document, "operator-id", &candidate));
  document = candidate;
  ck_assert(!Race_Admin_SessionCanPerform(
    &session, &document, RACE_ADMIN_ACTION_PLAYER_KICK));

  ck_assert(Race_Admin_SessionGrant(&session, &document, "moderator-id"));
  Race_Admin_SessionClear(&session);
  ck_assert(!Race_Admin_SessionCanPerform(
    &session, &document, RACE_ADMIN_ACTION_PLAYER_KICK));
  memset(&session, 0, sizeof(session));
  ck_assert(!Race_Admin_SessionCanPerform(
    &session, &document, RACE_ADMIN_ACTION_PLAYER_KICK));
} END_TEST

START_TEST(_Race_AdminActionInputsAndSettings) {
  int32_t slot = -1;
  ck_assert(Race_Admin_ParseClientSlot("0", 8, &slot));
  ck_assert_int_eq(slot, 0);
  ck_assert(Race_Admin_ParseClientSlot("7", 8, &slot));
  ck_assert_int_eq(slot, 7);
  ck_assert(!Race_Admin_ParseClientSlot("8", 8, &slot));
  ck_assert(!Race_Admin_ParseClientSlot("-1", 8, &slot));
  ck_assert(!Race_Admin_ParseClientSlot("+1", 8, &slot));
  ck_assert(!Race_Admin_ParseClientSlot("1x", 8, &slot));
  ck_assert(!Race_Admin_ParseClientSlot("2147483648", 8, &slot));
  ck_assert(!Race_Admin_ParseClientSlot("1", 0, &slot));

  ck_assert_int_eq(Race_Admin_ValidateKickTarget(0, 1, 8, true),
                   RACE_ADMIN_KICK_TARGET_OK);
  ck_assert_int_eq(Race_Admin_ValidateKickTarget(0, 0, 8, true),
                   RACE_ADMIN_KICK_TARGET_SELF);
  ck_assert_int_eq(Race_Admin_ValidateKickTarget(0, 1, 8, false),
                   RACE_ADMIN_KICK_TARGET_UNAVAILABLE);
  ck_assert_int_eq(Race_Admin_ValidateKickTarget(0, 8, 8, true),
                   RACE_ADMIN_KICK_TARGET_INVALID);

  char canonical[RACE_MAP_IDENTITY_SIZE];
  ck_assert(Race_MapState_CanonicalizeMap("Race-Test", canonical));
  ck_assert_str_eq(canonical, "race-test");
  const char *unsafe_maps[] = {
    "../race-test", "race-test;quit", "race-test\nquit",
    "C:/race-test", "/race-test",
    "maps/../../race-test.bsp"
  };
  for (size_t i = 0; i < lengthof(unsafe_maps); i++) {
    ck_assert(!Race_MapState_CanonicalizeMap(unsafe_maps[i], canonical));
  }
  char oversized_map[RACE_MAP_IDENTITY_SIZE + 1u];
  memset(oversized_map, 'a', sizeof(oversized_map) - 1u);
  oversized_map[sizeof(oversized_map) - 1u] = '\0';
  ck_assert(!Race_MapState_CanonicalizeMap(oversized_map, canonical));

  race_admin_document_t document, admin_candidate;
  ck_assert(Race_Admin_DocumentInit(&document));
  ck_assert(Race_Admin_AddAccount(&document, "moderator-id", "moderator",
                                  RACE_ADMIN_ROLE_MODERATOR,
                                  &admin_candidate));
  document = admin_candidate;
  ck_assert(Race_Admin_AddAccount(&document, "operator-id", "operator",
                                  RACE_ADMIN_ROLE_OPERATOR,
                                  &admin_candidate));
  document = admin_candidate;

  race_settings_state_t state, settings_candidate;
  race_settings_change_t change;
  ck_assert(Race_Settings_StateInit(&state));
  race_admin_session_t session = { 0 };
  ck_assert(Race_Admin_SessionGrant(&session, &document, "moderator-id"));
  bool mutated = false;
  if (Race_Admin_SessionCanPerform(
        &session, &document, RACE_ADMIN_ACTION_SETTINGS_MUTATE)) {
    mutated = Race_Settings_StateSet(
      &state, RACE_SETTING_SOURCE_RUNTIME, "finish_cue_enabled", "0",
      &settings_candidate, &change, NULL, 0);
  }
  ck_assert(!mutated);
  ck_assert_uint_eq(state.revision, 1u);

  ck_assert(Race_Admin_SessionGrant(&session, &document, "operator-id"));
  ck_assert(Race_Admin_SessionCanPerform(
    &session, &document, RACE_ADMIN_ACTION_SETTINGS_MUTATE));
  ck_assert(Race_Settings_StateSet(
    &state, RACE_SETTING_SOURCE_RUNTIME, "finish_cue_enabled", "0",
    &settings_candidate, &change, NULL, 0));
  ck_assert(change.effective_changed);
  state = settings_candidate;
  ck_assert_uint_eq(state.revision, 2u);
  ck_assert(!Race_Settings_StateSet(
    &state, RACE_SETTING_SOURCE_RUNTIME, "finish_cue_gain", "101",
    &settings_candidate, &change, NULL, 0));
  ck_assert_uint_eq(state.revision, 2u);
  ck_assert(Race_Settings_StateSet(
    &state, RACE_SETTING_SOURCE_RUNTIME, "finish_cue_enabled", "0",
    &settings_candidate, &change, NULL, 0));
  ck_assert(!change.source_changed);
  ck_assert(!change.effective_changed);
  ck_assert_uint_eq(settings_candidate.revision, 2u);
} END_TEST

START_TEST(_Race_PhysicsCatalogAndRankingPolicy) {
  ck_assert_int_eq(RACE_PHYSICS_FAMILY_QUETOO, 1);
  ck_assert_int_eq(RACE_PHYSICS_FAMILY_Q2, 2);
  ck_assert_int_eq(RACE_PHYSICS_PRESET_QUETOO_COMMON_V1, 1);
  ck_assert_int_eq(RACE_PHYSICS_PRESET_Q2, 2);
  ck_assert_int_eq(RACE_PHYSICS_PRESET_QUETOO_FIX_V1, 3);
  ck_assert_int_eq(RACE_PHYSICS_Q2_SNAP_OFF, 0);
  ck_assert_int_eq(RACE_PHYSICS_Q2_SNAP_NEAREST, 1);
  ck_assert_int_eq(RACE_PHYSICS_Q2_SNAP_TRUNCATE, 2);
  ck_assert_str_eq(Race_Physics_Q2SnapModeKey(RACE_PHYSICS_Q2_SNAP_OFF),
                   "off");
  ck_assert_str_eq(
    Race_Physics_Q2SnapModeKey(RACE_PHYSICS_Q2_SNAP_NEAREST), "nearest");
  ck_assert_str_eq(
    Race_Physics_Q2SnapModeKey(RACE_PHYSICS_Q2_SNAP_TRUNCATE), "truncate");
  ck_assert_ptr_null(Race_Physics_Q2SnapModeKey(
    (race_physics_q2_snap_mode_t) 3));

  size_t family_count;
  const race_physics_family_descriptor_t *families =
    Race_Physics_Families(&family_count);
  ck_assert_ptr_nonnull(families);
  ck_assert_uint_eq(family_count, 2u);
  ck_assert_int_eq(families[0].id, RACE_PHYSICS_FAMILY_QUETOO);
  ck_assert_str_eq(families[0].key, RACE_PHYSICS_FAMILY_QUETOO_KEY);
  ck_assert_str_eq(families[0].name, "Current Quetoo");
  ck_assert(families[0].available);
  ck_assert_int_eq(families[1].id, RACE_PHYSICS_FAMILY_Q2);
  ck_assert_str_eq(families[1].key, RACE_PHYSICS_FAMILY_Q2_KEY);
  ck_assert_str_eq(families[1].name, "Q2");
  ck_assert(families[1].available);
  ck_assert_ptr_null(Race_Physics_Family(RACE_PHYSICS_FAMILY_INVALID));
  ck_assert_ptr_null(Race_Physics_FamilyForKey(NULL));
  ck_assert_ptr_null(Race_Physics_FamilyForKey("unknown"));

  size_t preset_count;
  const race_physics_preset_descriptor_t *presets =
    Race_Physics_Presets(&preset_count);
  ck_assert_ptr_nonnull(presets);
  ck_assert_uint_eq(preset_count, 3u);
  ck_assert_int_eq(presets[0].id, RACE_PHYSICS_PRESET_QUETOO_COMMON_V1);
  ck_assert_int_eq(presets[0].family, RACE_PHYSICS_FAMILY_QUETOO);
  ck_assert_str_eq(presets[0].key,
                   RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY);
  ck_assert_str_eq(presets[0].name, "Current common Quetoo");
  ck_assert_str_eq(presets[0].ruleset,
                   RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY);
  ck_assert(presets[0].available);
  ck_assert(presets[0].rankable);

  ck_assert_int_eq(presets[1].id, RACE_PHYSICS_PRESET_Q2);
  ck_assert_int_eq(presets[1].family, RACE_PHYSICS_FAMILY_Q2);
  ck_assert_str_eq(presets[1].key, RACE_PHYSICS_PRESET_Q2_V1_KEY);
  ck_assert_str_eq(presets[1].name, "Quake II");
  ck_assert_str_eq(presets[1].short_name, "Q2");
  ck_assert_str_eq(presets[1].ruleset, RACE_PHYSICS_PRESET_Q2_V1_KEY);
  ck_assert(presets[1].available);
  ck_assert(presets[1].rankable);
  ck_assert_int_eq(presets[2].id, RACE_PHYSICS_PRESET_QUETOO_FIX_V1);
  ck_assert_int_eq(presets[2].family, RACE_PHYSICS_FAMILY_Q2);
  ck_assert(presets[2].available);
  ck_assert(presets[2].rankable);
  ck_assert_ptr_null(Race_Physics_Preset(RACE_PHYSICS_PRESET_INVALID));
  ck_assert_ptr_null(Race_Physics_PresetForKey(NULL));
  ck_assert_ptr_null(Race_Physics_PresetForKey("unknown"));

  const race_physics_config_t *current = Race_Physics_Default();
  ck_assert_uint_eq(current->version, RACE_PHYSICS_CONFIG_VERSION);
  ck_assert_int_eq(current->family, RACE_PHYSICS_FAMILY_Q2);
  ck_assert_int_eq(current->preset, RACE_PHYSICS_PRESET_Q2);
  ck_assert_int_eq(current->q2_snap_mode, RACE_PHYSICS_Q2_SNAP_NEAREST);
  ck_assert(Race_Physics_ConfigValid(current));
  ck_assert(Race_Physics_ConfigAvailable(current));
  ck_assert(Race_Physics_ConfigRankable(current));
  ck_assert_str_eq(Race_Physics_ConfigRuleset(current),
                   RACE_PHYSICS_PRESET_Q2_V1_KEY);

  const race_physics_config_t q2 = {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = RACE_PHYSICS_FAMILY_Q2,
    .preset = RACE_PHYSICS_PRESET_Q2,
    .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_NEAREST
  };
  const race_physics_config_t quetoo_fix = {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = RACE_PHYSICS_FAMILY_Q2,
    .preset = RACE_PHYSICS_PRESET_QUETOO_FIX_V1,
    .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_NEAREST
  };
  ck_assert(Race_Physics_ConfigValid(&q2));
  ck_assert(Race_Physics_ConfigAvailable(&q2));
  ck_assert(Race_Physics_ConfigRankable(&q2));
  ck_assert_str_eq(Race_Physics_ConfigRuleset(&q2),
                   RACE_PHYSICS_PRESET_Q2_V1_KEY);
  ck_assert(Race_Physics_ConfigValid(&quetoo_fix));
  ck_assert(Race_Physics_ConfigAvailable(&quetoo_fix));
  ck_assert(Race_Physics_ConfigRankable(&quetoo_fix));
  ck_assert_str_eq(Race_Physics_ConfigRuleset(&quetoo_fix),
                   RACE_PHYSICS_PRESET_QUETOO_FIX_V1_KEY);

  race_physics_config_t selected;
  ck_assert(Race_Physics_ConfigForPresetKey(
    RACE_PHYSICS_PRESET_Q2_V1_KEY, &selected));
  ck_assert(Race_Physics_ConfigEquals(&selected, &q2));
  ck_assert(Race_Physics_ConfigForSelector(
    RACE_PHYSICS_SELECTOR_Q2_KEY, &selected));
  ck_assert(Race_Physics_ConfigEquals(&selected, &q2));
  ck_assert(Race_Physics_ConfigForSelector(
    RACE_PHYSICS_SELECTOR_QUAKE2_KEY, &selected));
  ck_assert(Race_Physics_ConfigEquals(&selected, &q2));
  ck_assert(Race_Physics_ConfigForSelector(
    RACE_PHYSICS_PRESET_Q2_V1_KEY, &selected));
  ck_assert(Race_Physics_ConfigEquals(&selected, &q2));
  ck_assert(!Race_Physics_ConfigForSelector("q2pro", &selected));
  ck_assert(!Race_Physics_ConfigForSelector("q2pro-v1", &selected));
  ck_assert(Race_Physics_ConfigForPresetKey(
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1_KEY, &selected));
  ck_assert(Race_Physics_ConfigEquals(&selected, &quetoo_fix));
  ck_assert(Race_Physics_ConfigForPresetKey(
    RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY, &selected));
  ck_assert_int_eq(selected.family, RACE_PHYSICS_FAMILY_QUETOO);
  ck_assert_int_eq(selected.preset,
                   RACE_PHYSICS_PRESET_QUETOO_COMMON_V1);
  ck_assert_int_eq(selected.q2_snap_mode, RACE_PHYSICS_Q2_SNAP_OFF);
  ck_assert(Race_Physics_ConfigRankable(&selected));
  ck_assert(!Race_Physics_ConfigForPresetKey(NULL, &selected));
  ck_assert(!Race_Physics_ConfigForPresetKey("unknown", &selected));
  ck_assert(!Race_Physics_ConfigForSelector(NULL, &selected));
  ck_assert(!Race_Physics_ConfigForSelector("unknown", &selected));
  ck_assert(!Race_Physics_ConfigForPresetKey(
    RACE_PHYSICS_PRESET_Q2_V1_KEY, NULL));

  const race_physics_config_t invalid = {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = RACE_PHYSICS_FAMILY_INVALID,
    .preset = RACE_PHYSICS_PRESET_INVALID,
    .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_OFF
  };
  ck_assert(!Race_Physics_ConfigValid(&invalid));
  ck_assert(!Race_Physics_ConfigAvailable(&invalid));
  ck_assert(!Race_Physics_ConfigRankable(&invalid));
  ck_assert_ptr_null(Race_Physics_ConfigRuleset(&invalid));
} END_TEST

START_TEST(_Race_PhysicsConfigCodec) {
  char wire[RACE_PHYSICS_CONFIG_STRING_SIZE];
  ck_assert(Race_Physics_Encode(Race_Physics_Default(), wire));
  ck_assert_str_eq(wire, "v2\\q2\\q2-v1\\nearest");

  const race_physics_config_t q2 = {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = RACE_PHYSICS_FAMILY_Q2,
    .preset = RACE_PHYSICS_PRESET_Q2,
    .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_NEAREST
  };
  const race_physics_config_t q2fix = {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = RACE_PHYSICS_FAMILY_Q2,
    .preset = RACE_PHYSICS_PRESET_QUETOO_FIX_V1,
    .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_NEAREST
  };
  const race_physics_config_t common = {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = RACE_PHYSICS_FAMILY_QUETOO,
    .preset = RACE_PHYSICS_PRESET_QUETOO_COMMON_V1,
    .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_OFF
  };
  ck_assert(Race_Physics_Encode(&q2, wire));

  race_physics_config_t decoded;
  ck_assert_int_eq(Race_Physics_Decode(wire, &decoded),
                   RACE_PHYSICS_PARSE_OK);
  ck_assert(Race_Physics_ConfigEquals(&decoded, Race_Physics_Default()));

  ck_assert_int_eq(Race_Physics_Decode(NULL, &decoded),
                   RACE_PHYSICS_PARSE_INVALID_ARGUMENT);
  ck_assert_int_eq(Race_Physics_Decode(wire, NULL),
                   RACE_PHYSICS_PARSE_INVALID_ARGUMENT);
  ck_assert_int_eq(Race_Physics_Decode("", &decoded),
                   RACE_PHYSICS_PARSE_MISSING);
  ck_assert_int_eq(Race_Physics_Decode("v1\\quetoo", &decoded),
                   RACE_PHYSICS_PARSE_MALFORMED);
  ck_assert_int_eq(Race_Physics_Decode(
                     "v1\\quetoo\\quetoo-common-v1\\extra", &decoded),
                   RACE_PHYSICS_PARSE_MALFORMED);
  ck_assert_int_eq(Race_Physics_Decode(
                     "v1\\quetoo\\quetoo-common-v1\\", &decoded),
                   RACE_PHYSICS_PARSE_MALFORMED);
  ck_assert_int_eq(Race_Physics_Decode(
                     "v2\\quetoo\\quetoo-common-v1", &decoded),
                   RACE_PHYSICS_PARSE_MALFORMED);
  ck_assert_int_eq(Race_Physics_Decode(
                     "v3\\quetoo\\quetoo-common-v1\\off", &decoded),
                   RACE_PHYSICS_PARSE_UNKNOWN_VERSION);
  ck_assert_int_eq(Race_Physics_Decode(
                     "v1\\unknown\\quetoo-common-v1", &decoded),
                   RACE_PHYSICS_PARSE_UNKNOWN_FAMILY);
  ck_assert_int_eq(Race_Physics_Decode(
                     "v1\\quetoo\\unknown", &decoded),
                   RACE_PHYSICS_PARSE_UNKNOWN_PRESET);
  ck_assert_int_eq(Race_Physics_Decode(
                     "v1\\quetoo\\q2-v1", &decoded),
                   RACE_PHYSICS_PARSE_FAMILY_MISMATCH);
  ck_assert_int_eq(Race_Physics_Decode(
                     "v1\\q2\\q2-v1", &decoded),
                   RACE_PHYSICS_PARSE_OK);
  ck_assert(Race_Physics_ConfigEquals(&decoded, &q2));
  ck_assert_int_eq(Race_Physics_Decode(
                     "v1\\q2\\q2pro-v1", &decoded),
                   RACE_PHYSICS_PARSE_UNKNOWN_PRESET);
  ck_assert_int_eq(Race_Physics_Decode(
                     "v2\\q2\\q2pro-v1\\nearest", &decoded),
                   RACE_PHYSICS_PARSE_UNKNOWN_PRESET);
  ck_assert(Race_Physics_Encode(&q2fix, wire));
  ck_assert_str_eq(wire, "v2\\q2\\quetoo-fix-v1\\nearest");
  ck_assert_int_eq(Race_Physics_Decode(wire, &decoded),
                   RACE_PHYSICS_PARSE_OK);
  ck_assert(Race_Physics_ConfigEquals(&decoded, &q2fix));
  ck_assert(Race_Physics_Encode(&common, wire));
  ck_assert_str_eq(wire, "v2\\quetoo\\quetoo-common-v1\\off");
  ck_assert_int_eq(Race_Physics_Decode(wire, &decoded),
                   RACE_PHYSICS_PARSE_OK);
  ck_assert(Race_Physics_ConfigEquals(&decoded, &common));

  race_physics_config_t alternate = q2;
  alternate.q2_snap_mode = RACE_PHYSICS_Q2_SNAP_TRUNCATE;
  ck_assert(Race_Physics_Encode(&alternate, wire));
  ck_assert_str_eq(wire, "v2\\q2\\q2-v1\\truncate");
  ck_assert_int_eq(Race_Physics_Decode(wire, &decoded),
                   RACE_PHYSICS_PARSE_OK);
  ck_assert(Race_Physics_ConfigEquals(&decoded, &alternate));
  ck_assert_str_eq(Race_Physics_ConfigRuleset(&decoded),
                    "q2-v1-snap-truncate");

  alternate.q2_snap_mode = RACE_PHYSICS_Q2_SNAP_OFF;
  ck_assert(Race_Physics_Encode(&alternate, wire));
  ck_assert_str_eq(wire, "v2\\q2\\q2-v1\\off");
  ck_assert_str_eq(Race_Physics_ConfigRuleset(&alternate),
                    "q2-v1-snap-off");

  ck_assert_int_eq(Race_Physics_Decode(
                     "v2\\q2\\q2-v1\\unknown", &decoded),
                   RACE_PHYSICS_PARSE_UNKNOWN_SNAP_MODE);
  ck_assert_int_eq(Race_Physics_Decode(
                     "v2\\quetoo\\quetoo-common-v1\\nearest", &decoded),
                   RACE_PHYSICS_PARSE_SNAP_MODE_MISMATCH);

  ck_assert_int_eq(Race_Physics_Decode(
                     "v1\\quetoo\\quetoo-common-v1", &decoded),
                   RACE_PHYSICS_PARSE_OK);
  ck_assert(Race_Physics_ConfigEquals(&decoded, &common));

  char oversized[RACE_PHYSICS_CONFIG_STRING_SIZE + 1u];
  memset(oversized, 'x', RACE_PHYSICS_CONFIG_STRING_SIZE);
  oversized[RACE_PHYSICS_CONFIG_STRING_SIZE] = '\0';
  ck_assert_int_eq(Race_Physics_Decode(oversized, &decoded),
                   RACE_PHYSICS_PARSE_TOO_LARGE);
} END_TEST

START_TEST(_Race_PhysicsIdentityParameterAgreement) {
  race_physics_config_t q2, q2fix, common;
  pm_params_t q2_params, q2fix_params, arbitrary = { 0 };
  ck_assert(Race_Physics_ConfigForPresetKey(
    RACE_PHYSICS_PRESET_Q2_V1_KEY, &q2));
  ck_assert(Race_Physics_ConfigForPresetKey(
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1_KEY, &q2fix));
  ck_assert(Race_Physics_ConfigForPresetKey(
    RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY, &common));
  ck_assert(Race_Physics_FixedParamsForPreset(q2.preset, &q2_params));
  ck_assert(Race_Physics_FixedParamsForPreset(q2fix.preset, &q2fix_params));

  ck_assert_msg(Race_Physics_ParamsHash(&q2_params) ==
                  UINT64_C(0xa0238a6a2bf3be4f),
                "q2-v1 exact parameter bits changed");
  ck_assert(Race_Physics_ConfigParamsAgree(&q2, &q2_params));
  ck_assert(!Race_Physics_ConfigParamsAgree(&q2, &q2fix_params));
  ck_assert(Race_Physics_ConfigParamsAgree(&q2fix, &q2fix_params));
  ck_assert(!Race_Physics_ConfigParamsAgree(&q2fix, &q2_params));
  ck_assert(Race_Physics_ConfigParamsAgree(&common, &arbitrary));
  ck_assert(!Race_Physics_ConfigParamsAgree(NULL, &arbitrary));
  ck_assert(!Race_Physics_ConfigParamsAgree(&q2, NULL));
  ck_assert_str_eq(
    Race_Physics_ParseResultName(RACE_PHYSICS_PARSE_PARAMETER_MISMATCH),
    "parameter-mismatch");

  ck_assert(!Race_Physics_PredictionReady(
    RACE_PHYSICS_PARSE_MISSING, false, &q2, NULL));
  ck_assert(!Race_Physics_PredictionReady(
    RACE_PHYSICS_PARSE_OK, false, &q2, &q2_params));
  ck_assert(!Race_Physics_PredictionReady(
    RACE_PHYSICS_PARSE_OK, true, &q2, &q2fix_params));
  ck_assert(Race_Physics_PredictionReady(
    RACE_PHYSICS_PARSE_OK, true, &q2, &q2_params));
} END_TEST

START_TEST(_Race_PhysicsAuthoritativeAndPredictionLifecycle) {
  Race_Physics_SetProvider(NULL);
  Race_Physics_Reset();
  ck_assert(Race_Physics_ConfigEquals(Race_Physics_Current(),
                                     Race_Physics_Default()));

  race_test_physics_provided = *Race_Physics_Default();
  race_test_physics_provider_result = RACE_PHYSICS_PARSE_OK;
  race_test_physics_provider_calls = 0u;
  Race_Physics_SetProvider(Race_TestPhysicsProvider);
  ck_assert(Race_Physics_ConfigEquals(Race_Physics_Current(),
                                     Race_Physics_Default()));
  ck_assert_uint_eq(race_test_physics_provider_calls, 1u);

  Race_Physics_Reset();
  ck_assert(Race_Physics_ConfigEquals(Race_Physics_Current(),
                                     Race_Physics_Default()));
  ck_assert_uint_eq(race_test_physics_provider_calls, 2u);

  race_test_physics_provided = (race_physics_config_t) {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = RACE_PHYSICS_FAMILY_Q2,
    .preset = RACE_PHYSICS_PRESET_QUETOO_FIX_V1,
    .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_NEAREST
  };
  race_test_physics_provider_result = RACE_PHYSICS_PARSE_PARAMETER_MISMATCH;
  ck_assert(Race_Physics_ConfigEquals(Race_Physics_Current(),
                                     Race_Physics_Default()));
  ck_assert_uint_eq(race_test_physics_provider_calls, 3u);
  ck_assert(Race_Physics_SetActive(&race_test_physics_provided));
  ck_assert(Race_Physics_ConfigEquals(Race_Physics_Current(),
                                     &race_test_physics_provided));

  Race_Physics_SetProvider(NULL);
  ck_assert(Race_Physics_ConfigEquals(Race_Physics_Current(),
                                     Race_Physics_Default()));
} END_TEST

START_TEST(_Race_AdminIdentityIsolation) {
  race_admin_document_t document, candidate;
  ck_assert(Race_Admin_DocumentInit(&document));
  ck_assert(Race_Admin_AddAccount(&document, "separate-admin", "admin-handle",
                                  RACE_ADMIN_ROLE_OWNER, &candidate));
  document = candidate;

  const char *spoofed_profile_guid = RACE_TEST_UID_A;
  const char *colliding_display_name = "admin-handle";
  ck_assert_ptr_nonnull(spoofed_profile_guid);
  ck_assert_ptr_nonnull(colliding_display_name);

  race_admin_session_t spoofed = { 0 };
  memcpy(spoofed.account_id, "separate-admin", sizeof("separate-admin"));
  spoofed.capabilities = RACE_ADMIN_CAP_ALL;
  ck_assert(!Race_Admin_SessionAuthenticated(&spoofed, &document));
  ck_assert(!Race_Admin_SessionHasCapability(
    &spoofed, &document, RACE_ADMIN_CAP_ACCOUNT_MANAGE));

  spoofed.authenticated = true;
  ck_assert(!Race_Admin_SessionAuthenticated(&spoofed, &document));
  ck_assert(!Race_Admin_SessionHasCapability(
    &spoofed, &document, RACE_ADMIN_CAP_ACCOUNT_MANAGE));
} END_TEST

START_TEST(_Race_AdminFormatAndCredentialPolicy) {
  race_admin_document_t document, candidate;
  ck_assert(Race_Admin_DocumentInit(&document));
  ck_assert(Race_Admin_AddAccount(&document, "a1", "h1",
                                  RACE_ADMIN_ROLE_OWNER, &candidate));
  document = candidate;
  ck_assert(Race_Admin_AddAccount(&document, "b1", "h2",
                                  RACE_ADMIN_ROLE_OPERATOR, &candidate));
  document = candidate;

  char serialized[RACE_ADMIN_SERIALIZED_MAX];
  size_t serialized_length;
  ck_assert(Race_Admin_Serialize(&document, serialized, sizeof(serialized),
                                 &serialized_length));
  serialized[serialized_length] = '\0';
  ck_assert_ptr_nonnull(strstr(serialized, "credential=disabled\n"));
  ck_assert_ptr_null(strstr(serialized, "password"));
  ck_assert_ptr_null(strstr(serialized, "supersecret"));

  race_admin_document_t parsed;
  ck_assert_int_eq(Race_Admin_Parse(serialized, serialized_length, &parsed),
                   RACE_ADMIN_PARSE_OK);
  ck_assert(Race_Admin_DocumentEquals(&document, &parsed));

  char malformed[RACE_ADMIN_SERIALIZED_MAX];
  memcpy(malformed, serialized, serialized_length + 1u);
  Race_TestReplaceSameLength(malformed, "credential=disabled",
                             "credential=argon2xx");
  Race_TestAdminResign(malformed);
  ck_assert_int_eq(Race_Admin_Parse(malformed, strlen(malformed), &parsed),
                   RACE_ADMIN_PARSE_MALFORMED);

  memcpy(malformed, serialized, serialized_length + 1u);
  Race_TestReplaceSameLength(malformed, "account=b1|", "account=a1|");
  Race_TestAdminResign(malformed);
  ck_assert_int_eq(Race_Admin_Parse(malformed, strlen(malformed), &parsed),
                   RACE_ADMIN_PARSE_MALFORMED);

  memcpy(malformed, serialized, serialized_length + 1u);
  Race_TestReplaceSameLength(malformed, "|operator|", "|bad-role|");
  Race_TestAdminResign(malformed);
  ck_assert_int_eq(Race_Admin_Parse(malformed, strlen(malformed), &parsed),
                   RACE_ADMIN_PARSE_MALFORMED);

  const char unknown[] = "QUETOO_RACE_ADMINS_V2\n";
  ck_assert_int_eq(Race_Admin_Parse(unknown, sizeof(unknown) - 1u, &parsed),
                   RACE_ADMIN_PARSE_UNKNOWN_VERSION);
  const char legacy[] = "# Race admin accounts: name password level\nroot secret 5\n";
  ck_assert_int_eq(Race_Admin_Parse(legacy, sizeof(legacy) - 1u, &parsed),
                   RACE_ADMIN_PARSE_LEGACY);
  ck_assert_int_eq(Race_Admin_Parse(serialized, serialized_length - 1u, &parsed),
                   RACE_ADMIN_PARSE_MALFORMED);

  char oversized[RACE_ADMIN_SERIALIZED_MAX + 1u];
  memset(oversized, 'x', sizeof(oversized));
  ck_assert_int_eq(Race_Admin_Parse(oversized, sizeof(oversized), &parsed),
                   RACE_ADMIN_PARSE_TOO_LARGE);

  memcpy(malformed, serialized, serialized_length + 1u);
  char *generation = strstr(malformed, "generation=");
  ck_assert_ptr_nonnull(generation);
  generation[sizeof("generation=") - 1u] ^= 1;
  ck_assert_int_eq(Race_Admin_Parse(malformed, serialized_length, &parsed),
                   RACE_ADMIN_PARSE_CHECKSUM);
} END_TEST

START_TEST(_Race_AdminPersistenceAndRecovery) {
  race_admin_document_t document, candidate;
  ck_assert(Race_Admin_DocumentInit(&document));
  ck_assert(Race_Admin_AddAccount(&document, "owner", "owner",
                                  RACE_ADMIN_ROLE_OWNER, &candidate));
  document = candidate;

  race_admin_parse_result_t parse_result = RACE_ADMIN_PARSE_OK;
  ck_assert_int_eq(Race_AdminStore_Commit(race_test_admin_committed,
                                          race_test_admin_candidate,
                                          &document, &parse_result),
                   RACE_ADMIN_STORE_OK);

  race_admin_document_t loaded;
  ck_assert(Race_Admin_DocumentInit(&loaded));
  ck_assert_int_eq(Race_AdminStore_Load(race_test_admin_committed, &loaded,
                                        &parse_result),
                   RACE_ADMIN_STORE_OK);
  ck_assert(Race_Admin_DocumentEquals(&document, &loaded));

  ck_assert(Race_Admin_AddAccount(&document, "operator", "operator",
                                  RACE_ADMIN_ROLE_OPERATOR, &candidate));
  char stale[RACE_ADMIN_SERIALIZED_MAX];
  size_t stale_length;
  ck_assert(Race_Admin_Serialize(&candidate, stale, sizeof(stale),
                                 &stale_length));
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_admin_candidate,
                                                   stale, stale_length),
                   RACE_PERSISTENCE_OK);
  ck_assert_int_eq(Race_AdminStore_Load(race_test_admin_committed, &loaded,
                                        &parse_result),
                   RACE_ADMIN_STORE_OK);
  ck_assert(Race_Admin_DocumentEquals(&document, &loaded));

  char unavailable[RACE_TEST_PATH_SIZE];
  snprintf(unavailable, sizeof(unavailable), "%s/missing/admins.candidate",
           race_test_directory);
  ck_assert_int_eq(Race_AdminStore_Commit(race_test_admin_committed,
                                          unavailable, &candidate,
                                          &parse_result),
                   RACE_ADMIN_STORE_IO_ERROR);
  ck_assert_int_eq(Race_AdminStore_Load(race_test_admin_committed, &loaded,
                                        &parse_result),
                   RACE_ADMIN_STORE_OK);
  ck_assert(Race_Admin_DocumentEquals(&document, &loaded));

  const char corrupt[] = RACE_ADMIN_MAGIC "\ngeneration=1\n";
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_admin_committed,
                                                   corrupt,
                                                   sizeof(corrupt) - 1u),
                   RACE_PERSISTENCE_OK);
  const race_admin_document_t unchanged = loaded;
  ck_assert_int_eq(Race_AdminStore_Load(race_test_admin_committed, &loaded,
                                        &parse_result),
                   RACE_ADMIN_STORE_CORRUPT);
  ck_assert(Race_Admin_DocumentEquals(&unchanged, &loaded));
  char preserved[128];
  Race_TestPersistenceRead(race_test_admin_committed, preserved,
                           sizeof(preserved), corrupt);

  char oversized[RACE_ADMIN_SERIALIZED_MAX + 1u];
  memset(oversized, 'x', sizeof(oversized));
  ck_assert_int_eq(Race_Persistence_WriteCandidate(race_test_admin_committed,
                                                   oversized,
                                                   sizeof(oversized)),
                   RACE_PERSISTENCE_OK);
  ck_assert_int_eq(Race_AdminStore_Load(race_test_admin_committed, &loaded,
                                        &parse_result),
                   RACE_ADMIN_STORE_CORRUPT);
  ck_assert_int_eq(parse_result, RACE_ADMIN_PARSE_TOO_LARGE);
  ck_assert(Race_Admin_DocumentEquals(&unchanged, &loaded));
} END_TEST

int32_t main(int32_t argc, char **argv) {
  (void) argc;
  (void) argv;

  Suite *suite = suite_create("check_race");
  TCase *tcase = tcase_create("race");

  tcase_add_test(tcase, _Race_CourseValidation);
  tcase_add_test(tcase, _Race_MaximumCourse);
  tcase_add_test(tcase, _Race_OptionalCatalogValidation);
  tcase_add_test(tcase, _Race_Progression);
  tcase_add_test(tcase, _Race_SplitAndStageProgression);
  tcase_add_test(tcase, _Race_BarrierPolicies);
  tcase_add_test(tcase, _Race_StartPolicies);
  tcase_add_test(tcase, _Race_InvalidStartPreservesAttempt);
  tcase_add_test(tcase, _Race_RunMetadata);
  tcase_add_test(tcase, _Race_FinishReportWireV1);
  tcase_add_test(tcase, _Race_ResetAndRestart);
  tcase_add_test(tcase, _Race_ZeroCheckpointCourse);
  tcase_add_test(tcase, _Race_ClientIndependence);
  tcase_add_test(tcase, _Race_TriggerDebounce);
  tcase_add_test(tcase, _Race_Modes);
  tcase_add_test(tcase, _Race_DamagePolicy);
  tcase_add_test(tcase, _Race_ModeClientIndependence);
  tcase_add_test(tcase, _Race_AutoStart);
  tcase_add_test(tcase, _Race_Elapsed);
  tcase_add_test(tcase, _Race_WireElapsed);
  tcase_add_test(tcase, _Race_StoredSpawn);
  tcase_add_test(tcase, _Race_StoredSpawnClientIndependence);
  tcase_add_test(tcase, _Race_MovementCollisionPolicy);
  tcase_add_test(tcase, _Race_TrainingInputContract);
  tcase_add_test(tcase, _Race_InputViewerLegacyContract);
  tcase_add_test(tcase, _Race_StrafeHelperLegacyMath);
  tcase_add_test(tcase, _Race_PresentationLabels);
  tcase_add_test(tcase, _Race_PresentationFormatting);
  tcase_add_test(tcase, _Race_PresentationMarkers);
  tcase_add_test(tcase, _Race_PresentationSpeed);
  tcase_add_test(tcase, _Race_HudLayoutVisibilityAndClimb);
  tcase_add_test(tcase, _Race_VoteEligibilityMathAndCooldown);
  tcase_add_test(tcase, _Race_VoteCreationAndBallots);
  tcase_add_test(tcase, _Race_VoteOutcomesAndConnections);
  tcase_add_test(tcase, _Race_VoteExecutionOnce);
  tcase_add_test(tcase, _Race_VotePhysicsTarget);
  tcase_add_test(tcase, _Race_VoteMenuBallotsAndSpectators);
  tcase_add_test(tcase, _Race_VoteMenuDeadlineAndTieResolution);
  tcase_add_test(tcase, _Race_ProfileIdentity);
  tcase_add_test(tcase, _Race_ProfileIdentityPolicy);
  tcase_add_test(tcase, _Race_ProfilePathSafety);
  tcase_add_test(tcase, _Race_ProfileSerialization);
  tcase_add_test(tcase, _Race_ProfileSerializationBounds);
  tcase_add_test(tcase, _Race_ProfileMalformedInput);
  tcase_add_test(tcase, _Race_LeaderboardPbWrAndIdentity);
  tcase_add_test(tcase, _Race_LeaderboardTopBoundAndUpdate);
  tcase_add_test(tcase, _Race_MapIdentityAndPaths);
  tcase_add_test(tcase, _Race_MapStateGeneration);
  tcase_add_test(tcase, _Race_MapStateReplayBackedTransition);
  tcase_add_test(tcase, _Race_MapStateV2Compatibility);
  tcase_add_test(tcase, _Race_MapStateV3Compatibility);
  tcase_add_test(tcase, _Race_LeaderboardWireV1);
  tcase_add_test(tcase, _Race_MapStateRoundTripAndBounds);
  tcase_add_test(tcase, _Race_MapStateMalformedInput);
  tcase_add_test(tcase, _Race_MapStateMaximumRecords);
  tcase_add_test(tcase, _Race_ReplayFormatRoundTripAndIdentity);
  tcase_add_test(tcase, _Race_ReplayFormatRejectsCorruptionAndBounds);
  tcase_add_test(tcase, _Race_ReplayProjectileFormatAndLifecycle);
  tcase_add_test(tcase, _Race_ReplayRecordingCadenceAndLimits);
  tcase_add_test(tcase, _Race_ReplayPlaybackClock);
  tcase_add_test(tcase, _Race_ReplaySelection);
  tcase_add_test(tcase, _Race_ReplayPlaybackSamplingAndInterpolation);
  tcase_add_test(tcase, _Race_ReplayViewerState);
  tcase_add_test(tcase, _Race_ReplayStateTransportAndLifecycle);
  tcase_add_test(tcase, _Race_ReplayTelemetryTransportAndLifecycle);
  tcase_add_test(tcase, _Race_ReplayProjectileTransportAndLifecycle);
  tcase_add_test(tcase, _Race_RacelineTransport);
  tcase_add_test(tcase, _Race_RacelineExtractionAndWindow);
  tcase_add_test(tcase, _Race_PublicationOrderingAndFailures);
  tcase_add_test(tcase, _Race_SettingsCatalogAndTypes);
  tcase_add_test(tcase, _Race_SettingsResolutionAndRevision);
  tcase_add_test(tcase, _Race_SettingsFormatAndLegacyPolicy);
  tcase_add_test(tcase, _Race_PhysicsCatalogAndRankingPolicy);
  tcase_add_test(tcase, _Race_PhysicsConfigCodec);
  tcase_add_test(tcase, _Race_PhysicsIdentityParameterAgreement);
  tcase_add_test(tcase, _Race_PhysicsAuthoritativeAndPredictionLifecycle);
  tcase_add_test(tcase, _Race_AdminAccountModel);
  tcase_add_test(tcase, _Race_AdminSessionAndAuthorization);
  tcase_add_test(tcase, _Race_AdminActionAuthorization);
  tcase_add_test(tcase, _Race_AdminActionInputsAndSettings);
  tcase_add_test(tcase, _Race_AdminIdentityIsolation);
  tcase_add_test(tcase, _Race_AdminFormatAndCredentialPolicy);
  Race_MapBrowser_AddTests(tcase);
  suite_add_tcase(suite, tcase);

  TCase *persistence = tcase_create("persistence");
  tcase_add_checked_fixture(persistence,
                            Race_TestPersistenceSetup,
                            Race_TestPersistenceTeardown);
  tcase_add_test(persistence, _Race_PersistenceCandidatePromotion);
  tcase_add_test(persistence, _Race_PersistenceRealPathBounds);
  tcase_add_test(persistence, _Race_PersistenceFailures);
  tcase_add_test(persistence, _Race_PersistenceBoundsAndCorruption);
  tcase_add_test(persistence, _Race_PersistenceReconnectAndRename);
  tcase_add_test(persistence, _Race_MapStatePersistenceAndRecovery);
  tcase_add_test(persistence, _Race_MapStateMissingAndIdentityMismatch);
  tcase_add_test(persistence, _Race_ReplayPersistencePublicationAndRecovery);
  tcase_add_test(persistence, _Race_SettingsPersistenceAndRecovery);
  tcase_add_test(persistence, _Race_AdminPersistenceAndRecovery);
  suite_add_tcase(suite, persistence);

  SRunner *runner = srunner_create(suite);
  srunner_run_all(runner, CK_VERBOSE);
  const int32_t failed = srunner_ntests_failed(runner);
  srunner_free(runner);

  return failed;
}
