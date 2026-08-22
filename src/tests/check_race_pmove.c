/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <check.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/common/bg_pmove.h"
#include "race_physics.h"
#include "race_training.h"

box3_t Pm_PlayerBounds(bool ducked);
void Pm_Move_CommonReference(pm_move_t *pm_move);
box3_t Pm_PlayerBounds_CommonReference(bool ducked);
extern const box3_t PM_BOUNDS_CommonReference;
extern const box3_t PM_CROUCHED_BOUNDS_CommonReference;
void Pm_SetQ2AirWishspeedCapForTest(float cap);
void Pm_SetQ2SnapEnabledForTest(bool enabled);
void Pm_SetQ2SnapModeForTest(race_physics_q2_snap_mode_t mode);
uint16_t Pm_Q2TimeForTest(uint16_t msec);
float Pm_Q2SnapFloatForTest(float value);
vec3_t Pm_Q2ClipVelocityForTest(vec3_t in, vec3_t normal);
void Pm_Q2StepSlideMoveForTest(pm_move_t *move);
void Pm_Q2CheckGroundForTest(pm_move_t *move, vec3_t previous_velocity);
void Pm_Q2CheckGroundForTest_CgameReference(pm_move_t *move,
                                             vec3_t previous_velocity);
void Pm_Q2InitialSnapPositionForTest(pm_move_t *move,
                                     vec3_t previous_origin);
void Pm_Q2SnapPositionForTest(pm_move_t *move, vec3_t previous_origin);
vec3_t Pm_Q2LadderWishForTest(pm_move_t *move, vec3_t angles);
void Pm_Q2LadderMoveForTest(pm_move_t *move, vec3_t angles);
void Pm_Q2CheckWaterForTest(pm_move_t *move);
vec3_t Pm_Q2AddCurrentsForTest(pm_move_t *move, vec3_t angles,
                               vec3_t wish_velocity);
void Pm_Q2FrictionForTest(pm_move_t *move, vec3_t angles);
void Pm_Q2WaterMoveForTest(pm_move_t *move, vec3_t angles);
bool Pm_Q2CheckWaterJumpForTest(pm_move_t *move, vec3_t angles);
bool Pm_Q2CheckJumpForTest(pm_move_t *move, vec3_t angles);
void Pm_Move_CgameReference(pm_move_t *pm_move);
void Pm_SetQ2AirWishspeedCapForTest_CgameReference(float cap);
void Pm_SetQ2SnapEnabledForTest_CgameReference(bool enabled);
void Pm_SetQ2SnapModeForTest_CgameReference(
  race_physics_q2_snap_mode_t mode);
float Pm_Q2SnapFloatForTest_CgameReference(float value);
vec3_t Pm_Q2LadderWishForTest_CgameReference(pm_move_t *move,
                                             vec3_t angles);
void Pm_Q2LadderMoveForTest_CgameReference(pm_move_t *move,
                                           vec3_t angles);
void Pm_Q2CheckWaterForTest_CgameReference(pm_move_t *move);
vec3_t Pm_Q2AddCurrentsForTest_CgameReference(pm_move_t *move,
                                              vec3_t angles,
                                              vec3_t wish_velocity);
void Pm_Q2FrictionForTest_CgameReference(pm_move_t *move, vec3_t angles);
void Pm_Q2WaterMoveForTest_CgameReference(pm_move_t *move, vec3_t angles);
bool Pm_Q2CheckWaterJumpForTest_CgameReference(pm_move_t *move,
                                               vec3_t angles);
bool Pm_Q2CheckJumpForTest_CgameReference(pm_move_t *move, vec3_t angles);
void Race_CgameBounds_AddTests(TCase *tcase);
void Race_PhysicsService_AddTests(TCase *tcase);

static size_t race_pmove_physics_provider_calls;
static bool race_pmove_feet_current;
static bool race_pmove_ladder_liquid;
static float race_pmove_ladder_liquid_surface;
static int32_t race_pmove_current_contents = CONTENTS_CURRENT_90;
static float race_pmove_tunnel_height;
static race_strafe_sample_t race_pmove_training_sample;
static size_t race_pmove_training_calls;

static void Race_PmoveTrainingObserver(
    const race_strafe_sample_t *sample, void *context) {
  ck_assert_ptr_eq(context, &race_pmove_training_sample);
  ck_assert_ptr_nonnull(sample);
  race_pmove_training_sample = *sample;
  race_pmove_training_calls++;
}

static const race_physics_config_t race_pmove_q2_config = {
  .version = RACE_PHYSICS_CONFIG_VERSION,
  .family = RACE_PHYSICS_FAMILY_Q2,
  .preset = RACE_PHYSICS_PRESET_Q2,
  .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_NEAREST
};

static const race_physics_config_t race_pmove_q2_fix_config = {
  .version = RACE_PHYSICS_CONFIG_VERSION,
  .family = RACE_PHYSICS_FAMILY_Q2,
  .preset = RACE_PHYSICS_PRESET_QUETOO_FIX_V1,
  .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_NEAREST
};

static const race_physics_config_t race_pmove_common_config = {
  .version = RACE_PHYSICS_CONFIG_VERSION,
  .family = RACE_PHYSICS_FAMILY_QUETOO,
  .preset = RACE_PHYSICS_PRESET_QUETOO_COMMON_V1,
  .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_OFF
};

static race_physics_parse_result_t Race_PmovePhysicsProvider(
  race_physics_config_t *config) {
  race_pmove_physics_provider_calls++;
  *config = race_pmove_common_config;
  return RACE_PHYSICS_PARSE_OK;
}

typedef enum {
  RACE_PMOVE_WORLD_EMPTY,
  RACE_PMOVE_WORLD_FLAT,
  RACE_PMOVE_WORLD_WALL,
  RACE_PMOVE_WORLD_CORNER,
  RACE_PMOVE_WORLD_CEILING,
  RACE_PMOVE_WORLD_ALL_SOLID_JITTER,
  RACE_PMOVE_WORLD_RAMP,
  RACE_PMOVE_WORLD_STEEP,
  RACE_PMOVE_WORLD_STEP_LOW,
  RACE_PMOVE_WORLD_STEP_16,
  RACE_PMOVE_WORLD_LEDGE,
  RACE_PMOVE_WORLD_WATER,
  RACE_PMOVE_WORLD_LADDER,
  RACE_PMOVE_WORLD_LADDER_NEGATIVE,
  RACE_PMOVE_WORLD_TUNNEL
} race_pmove_world_t;

typedef enum {
  RACE_PMOVE_ENTITY_NONE,
  RACE_PMOVE_ENTITY_FLOOR,
  RACE_PMOVE_ENTITY_WALL,
  RACE_PMOVE_ENTITY_WALL_Y,
  RACE_PMOVE_ENTITY_CEILING,
  RACE_PMOVE_ENTITY_RAMP,
  RACE_PMOVE_ENTITY_STEEP,
  RACE_PMOVE_ENTITY_STEP,
  RACE_PMOVE_ENTITY_LEDGE,
  RACE_PMOVE_ENTITY_LADDER
} race_pmove_entity_t;

typedef enum {
  RACE_PMOVE_EMPTY_IDLE,
  RACE_PMOVE_EMPTY_FORWARD_16,
  RACE_PMOVE_EMPTY_FORWARD_50,
  RACE_PMOVE_EMPTY_BACK_SIDE,
  RACE_PMOVE_EMPTY_DIAGONAL,
  RACE_PMOVE_GROUND_ACCELERATION,
  RACE_PMOVE_GROUND_FRICTION,
  RACE_PMOVE_GROUND_STOP,
  RACE_PMOVE_JUMP,
  RACE_PMOVE_HELD_JUMP,
  RACE_PMOVE_LANDING,
  RACE_PMOVE_AIR_FORWARD,
  RACE_PMOVE_AIR_STRAFE,
  RACE_PMOVE_AIR_DIAGONAL,
  RACE_PMOVE_WALL_CLIP,
  RACE_PMOVE_CORNER_CLIP,
  RACE_PMOVE_CEILING_CLIP,
  RACE_PMOVE_ALL_SOLID_RECOVERY,
  RACE_PMOVE_STEP_UP_LOW,
  RACE_PMOVE_STEP_16,
  RACE_PMOVE_LEDGE_DEPARTURE,
  RACE_PMOVE_RAMP_CLIMB,
  RACE_PMOVE_STEEP_GROUND,
  RACE_PMOVE_GROUND_SNAP,
  RACE_PMOVE_CROUCH,
  RACE_PMOVE_WATER,
  RACE_PMOVE_LADDER,
  RACE_PMOVE_SPECTATOR,
  RACE_PMOVE_FREEZE,
  RACE_PMOVE_FIXTURE_TOTAL
} race_pmove_fixture_id_t;

typedef struct {
  pm_type_t type;
  vec3_t origin;
  vec3_t velocity;
  uint16_t flags;
  uint16_t time;
  vec3_t view_offset;
  float step_offset;
  vec3_t view_angles;
  vec3_t angles;
  box3_t bounds;
  race_pmove_entity_t ground_entity;
  float ground_fraction;
  vec3_t ground_end;
  vec3_t ground_normal;
  int32_t ground_contents;
  int32_t ground_surface;
  int32_t water_type;
  pm_water_level_t water_level;
  float step;
  int32_t num_touched;
  race_pmove_entity_t touched[8];
} race_pmove_expected_t;

static const char *race_pmove_fixture_names[RACE_PMOVE_FIXTURE_TOTAL] = {
  [RACE_PMOVE_EMPTY_IDLE] = "empty idle at negative origin",
  [RACE_PMOVE_EMPTY_FORWARD_16] = "empty forward 16 ms",
  [RACE_PMOVE_EMPTY_FORWARD_50] = "empty forward 50 ms",
  [RACE_PMOVE_EMPTY_BACK_SIDE] = "empty backward and side movement",
  [RACE_PMOVE_EMPTY_DIAGONAL] = "empty diagonal movement",
  [RACE_PMOVE_GROUND_ACCELERATION] = "ground acceleration",
  [RACE_PMOVE_GROUND_FRICTION] = "ground friction",
  [RACE_PMOVE_GROUND_STOP] = "ground stopping",
  [RACE_PMOVE_JUMP] = "jump",
  [RACE_PMOVE_HELD_JUMP] = "held jump",
  [RACE_PMOVE_LANDING] = "landing",
  [RACE_PMOVE_AIR_FORWARD] = "forward air movement",
  [RACE_PMOVE_AIR_STRAFE] = "air strafing",
  [RACE_PMOVE_AIR_DIAGONAL] = "diagonal air movement",
  [RACE_PMOVE_WALL_CLIP] = "wall clipping",
  [RACE_PMOVE_CORNER_CLIP] = "corner clipping",
  [RACE_PMOVE_CEILING_CLIP] = "ceiling clipping",
  [RACE_PMOVE_ALL_SOLID_RECOVERY] = "all-solid jitter recovery",
  [RACE_PMOVE_STEP_UP_LOW] = "successful 12-unit step up",
  [RACE_PMOVE_STEP_16] = "16-unit step boundary",
  [RACE_PMOVE_LEDGE_DEPARTURE] = "step down and ledge departure",
  [RACE_PMOVE_RAMP_CLIMB] = "ramp climbing",
  [RACE_PMOVE_STEEP_GROUND] = "steep non-walkable plane",
  [RACE_PMOVE_GROUND_SNAP] = "ground snapping",
  [RACE_PMOVE_CROUCH] = "crouch",
  [RACE_PMOVE_WATER] = "water movement",
  [RACE_PMOVE_LADDER] = "ladder movement",
  [RACE_PMOVE_SPECTATOR] = "spectator movement",
  [RACE_PMOVE_FREEZE] = "freeze movement"
};

/*
 * Captured from common bg_pmove.c at SHA-256
 * 364647DE55B1677056607C79662559D233ED800979B9E96EFDC2102A2FDC10A9.
 */
static const race_pmove_expected_t race_pmove_expected[RACE_PMOVE_FIXTURE_TOTAL] = {
  [RACE_PMOVE_EMPTY_IDLE] = { /* empty idle at negative origin */
    .type = PM_NORMAL,
    .origin = { { -0x1p+7f, -0x1p+6f, 0x1.7f2e48p+6f } },
    .velocity = { { 0x0p+0f, 0x0p+0f, -0x1.99999ap+3f } },
    .flags = 0x0000, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 0, .touched = {}
  },
  [RACE_PMOVE_EMPTY_FORWARD_16] = { /* empty forward 16 ms */
    .type = PM_NORMAL,
    .origin = { { -0x1.7f62b6p+6f, -0x1p+5f, 0x1.ff2e48p+6f } },
    .velocity = { { 0x1.333334p+3f, 0x0p+0f, -0x1.99999ap+3f } },
    .flags = 0x0000, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 0, .touched = {}
  },
  [RACE_PMOVE_EMPTY_FORWARD_50] = { /* empty forward 50 ms */
    .type = PM_NORMAL,
    .origin = { { -0x1.7ap+6f, -0x1p+5f, 0x1.f8p+6f } },
    .velocity = { { 0x1.ep+4f, 0x0p+0f, -0x1.4p+5f } },
    .flags = 0x0000, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 0, .touched = {}
  },
  [RACE_PMOVE_EMPTY_BACK_SIDE] = { /* empty backward and side movement */
    .type = PM_NORMAL,
    .origin = { { -0x1.02897ep+6f, -0x1.00d88p+7f, 0x1.3e41f2p+7f } },
    .velocity = { { -0x1.33869cp+4f, -0x1.9a08cep+3f, -0x1.a66666p+4f } },
    .flags = 0x0000, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 0, .touched = {}
  },
  [RACE_PMOVE_EMPTY_DIAGONAL] = { /* empty diagonal movement */
    .type = PM_NORMAL,
    .origin = { { -0x1.7619bap+7f, -0x1.93cc8ap+6f, 0x1.7p+7f } },
    .velocity = { { 0x1.8bfad4p+5f, -0x1.8bfad4p+5f, -0x1.4p+6f } },
    .flags = 0x0000, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 0, .touched = {}
  },
  [RACE_PMOVE_GROUND_ACCELERATION] = { /* ground acceleration */
    .type = PM_NORMAL,
    .origin = { { 0x1.ep+4f, 0x0p+0f, 0x1.8p+4f } },
    .velocity = { { 0x1.2cp+8f, 0x0p+0f, 0x0p+0f } },
    .flags = 0x0008, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_FLOOR, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x1.ep+4f, 0x0p+0f, 0x1.8p+4f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x1p+0f } },
    .ground_contents = CONTENTS_SOLID, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_FLOOR }
  },
  [RACE_PMOVE_GROUND_FRICTION] = { /* ground friction */
    .type = PM_NORMAL,
    .origin = { { 0x1.8p+3f, 0x0p+0f, 0x1.8p+4f } },
    .velocity = { { 0x1.ep+6f, 0x0p+0f, 0x0p+0f } },
    .flags = 0x0008, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_FLOOR, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x1.8p+3f, 0x0p+0f, 0x1.8p+4f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x1p+0f } },
    .ground_contents = CONTENTS_SOLID, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_FLOOR }
  },
  [RACE_PMOVE_GROUND_STOP] = { /* ground stopping */
    .type = PM_NORMAL,
    .origin = { { 0x0p+0f, 0x0p+0f, 0x1.8p+4f } },
    .velocity = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .flags = 0x0008, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_FLOOR, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x1.8p+4f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x1p+0f } },
    .ground_contents = CONTENTS_SOLID, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_FLOOR }
  },
  [RACE_PMOVE_JUMP] = { /* jump */
    .type = PM_NORMAL,
    .origin = { { 0x1.8p+2f, 0x0p+0f, 0x1.554cccp+5f } },
    .velocity = { { 0x1.ep+5f, 0x0p+0f, 0x1.754p+7f } },
    .flags = 0x2006, .time = 100,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_FLOOR }
  },
  [RACE_PMOVE_HELD_JUMP] = { /* held jump */
    .type = PM_NORMAL,
    .origin = { { 0x1.ep+4f, 0x0p+0f, 0x1.8p+4f } },
    .velocity = { { 0x1.2cp+8f, 0x0p+0f, 0x0p+0f } },
    .flags = 0x000c, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_FLOOR, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x1.ep+4f, 0x0p+0f, 0x1.8p+4f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x1p+0f } },
    .ground_contents = CONTENTS_SOLID, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_FLOOR }
  },
  [RACE_PMOVE_LANDING] = { /* landing */
    .type = PM_NORMAL,
    .origin = { { 0x0p+0f, 0x0p+0f, 0x1.8p+4f } },
    .velocity = { { 0x0p+0f, 0x0p+0f, 0x1.499ap+2f } },
    .flags = 0x0208, .time = 1,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_FLOOR, .ground_fraction = 0x1.425bp-3f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x1.8p+4f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x1p+0f } },
    .ground_contents = CONTENTS_SOLID, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_FLOOR }
  },
  [RACE_PMOVE_AIR_FORWARD] = { /* forward air movement */
    .type = PM_NORMAL,
    .origin = { { -0x1.68p+4f, -0x1.8p+5f, 0x1.f8p+6f } },
    .velocity = { { 0x1.ep+4f, 0x0p+0f, -0x1.4p+5f } },
    .flags = 0x0000, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 0, .touched = {}
  },
  [RACE_PMOVE_AIR_STRAFE] = { /* air strafing */
    .type = PM_NORMAL,
    .origin = { { -0x1.308p+4f, -0x1.8cp+5f, 0x1.f8p+6f } },
    .velocity = { { 0x1.8d8p+6f, -0x1.ep+4f, -0x1.4p+5f } },
    .flags = 0x0000, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 0, .touched = {}
  },
  [RACE_PMOVE_AIR_DIAGONAL] = { /* diagonal air movement */
    .type = PM_NORMAL,
    .origin = { { 0x1.fcp+3f, -0x1.999a3p-6f, 0x1.ep+6f } },
    .velocity = { { 0x1.3d8p+7f, -0x1.00005ep-2f, -0x1.4p+6f } },
    .flags = 0x0000, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 0, .touched = {}
  },
  [RACE_PMOVE_WALL_CLIP] = { /* wall clipping */
    .type = PM_NORMAL,
    .origin = { { 0x1.7e4cccp+5f, 0x0p+0f, 0x1.8p+4f } },
    .velocity = { { -0x1.ep+1f, 0x0p+0f, 0x0p+0f } },
    .flags = 0x0008, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_FLOOR, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x1.7e4cccp+5f, 0x0p+0f, 0x1.8p+4f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x1p+0f } },
    .ground_contents = CONTENTS_SOLID, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 2, .touched = { RACE_PMOVE_ENTITY_FLOOR, RACE_PMOVE_ENTITY_WALL }
  },
  [RACE_PMOVE_CORNER_CLIP] = { /* corner clipping */
    .type = PM_NORMAL,
    .origin = { { 0x1.7e383cp+5f, 0x1.6992d8p+5f, 0x1.8p+4f } },
    .velocity = { { -0x1.f9b5p+1f, 0x1.9fbc64p+6f, 0x0p+0f } },
    .flags = 0x0008, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_FLOOR, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x1.7e383cp+5f, 0x1.6992d8p+5f, 0x1.8p+4f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x1p+0f } },
    .ground_contents = CONTENTS_SOLID, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 2, .touched = { RACE_PMOVE_ENTITY_FLOOR, RACE_PMOVE_ENTITY_WALL }
  },
  [RACE_PMOVE_CEILING_CLIP] = { /* ceiling clipping */
    .type = PM_NORMAL,
    .origin = { { 0x0p+0f, 0x0p+0f, 0x1.bb1146p+4f } },
    .velocity = { { 0x0p+0f, 0x0p+0f, -0x1.2a9ap+1f } },
    .flags = 0x2006, .time = 100,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 2, .touched = { RACE_PMOVE_ENTITY_FLOOR, RACE_PMOVE_ENTITY_CEILING }
  },
  [RACE_PMOVE_ALL_SOLID_RECOVERY] = { /* all-solid jitter recovery */
    .type = PM_NORMAL,
    .origin = { { -0x1.fcb924p+2f, -0x1p+3f, 0x1.7cb924p+4f } },
    .velocity = { { 0x1.99999ap+1f, 0x0p+0f, -0x1.99999ap+3f } },
    .flags = 0x0000, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 0, .touched = {}
  },
  [RACE_PMOVE_STEP_UP_LOW] = { /* successful 12-unit step up */
    .type = PM_NORMAL,
    .origin = { { 0x1.ep+4f, 0x0p+0f, 0x1.2p+5f } },
    .velocity = { { 0x1.2cp+8f, 0x0p+0f, 0x0p+0f } },
    .flags = 0x0008, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_STEP, .ground_fraction = -0x0p+0f,
    .ground_end = { { 0x1.ep+4f, 0x0p+0f, 0x1.2p+5f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x1p+0f } },
    .ground_contents = CONTENTS_SOLID, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x1.8p+3f,
    .num_touched = 2, .touched = { RACE_PMOVE_ENTITY_FLOOR, RACE_PMOVE_ENTITY_STEP }
  },
  [RACE_PMOVE_STEP_16] = { /* 16-unit step boundary */
    .type = PM_NORMAL,
    .origin = { { 0x1.f66666p+3f, 0x0p+0f, 0x1.8p+4f } },
    .velocity = { { -0x1.ep+1f, 0x0p+0f, 0x0p+0f } },
    .flags = 0x0008, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_FLOOR, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x1.f66666p+3f, 0x0p+0f, 0x1.8p+4f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x1p+0f } },
    .ground_contents = CONTENTS_SOLID, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 2, .touched = { RACE_PMOVE_ENTITY_FLOOR, RACE_PMOVE_ENTITY_STEP }
  },
  [RACE_PMOVE_LEDGE_DEPARTURE] = { /* step down and ledge departure */
    .type = PM_NORMAL,
    .origin = { { 0x1.6p+4f, -0x1p+5f, 0x1.8p+4f } },
    .velocity = { { 0x1.2cp+8f, 0x0p+0f, 0x0p+0f } },
    .flags = 0x0000, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_LEDGE }
  },
  [RACE_PMOVE_RAMP_CLIMB] = { /* ramp climbing */
    .type = PM_NORMAL,
    .origin = { { 0x1.7ecccep+4f, 0x0p+0f, 0x1.5fb334p+5f } },
    .velocity = { { 0x1.de8p+7f, 0x0p+0f, 0x1.e60002p+6f } },
    .flags = 0x0008, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_RAMP, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x1.7ecccep+4f, 0x0p+0f, 0x1.5fb334p+5f } },
    .ground_normal = { { -0x1.c9f25cp-2f, 0x0p+0f, 0x1.c9f25cp-1f } },
    .ground_contents = CONTENTS_SOLID, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x1.7eccdp+3f,
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_RAMP }
  },
  [RACE_PMOVE_STEEP_GROUND] = { /* steep non-walkable plane */
    .type = PM_NORMAL,
    .origin = { { 0x1.32b60cp-2f, 0x0p+0f, 0x1.c87e9ep+5f } },
    .velocity = { { 0x1.0c333p+3f, 0x0p+0f, 0x1.47f334p+4f } },
    .flags = 0x0000, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEEP }
  },
  [RACE_PMOVE_GROUND_SNAP] = { /* ground snapping */
    .type = PM_NORMAL,
    .origin = { { 0x0p+0f, 0x0p+0f, 0x1.8p+4f } },
    .velocity = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .flags = 0x0008, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_FLOOR, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x1.8p+4f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x1p+0f } },
    .ground_contents = CONTENTS_SOLID, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_FLOOR }
  },
  [RACE_PMOVE_CROUCH] = { /* crouch */
    .type = PM_NORMAL,
    .origin = { { 0x0p+0f, 0x0p+0f, 0x1.8p+4f } },
    .velocity = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .flags = 0x0009, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.4p+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.8p+2f } } },
    .ground_entity = RACE_PMOVE_ENTITY_FLOOR, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x1.8p+4f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x1p+0f } },
    .ground_contents = CONTENTS_SOLID, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_FLOOR }
  },
  [RACE_PMOVE_WATER] = { /* water movement */
    .type = PM_NORMAL,
    .origin = { { 0x1.3f019p+2f, 0x0p+0f, 0x1.540216p+0f } },
    .velocity = { { 0x1.8ec1f4p+5f, 0x0p+0f, 0x1.a9029ap+3f } },
    .flags = 0x0020, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = CONTENTS_WATER, .water_level = WATER_UNDER, .step = 0x0p+0f,
    .num_touched = 0, .touched = {}
  },
  [RACE_PMOVE_LADDER] = { /* ladder movement */
    .type = PM_NORMAL,
    .origin = { { 0x1.fb62d2p+3f, 0x0p+0f, 0x1.a23c4ap+5f } },
    .velocity = { { -0x1.8ce98p-3f, 0x0p+0f, 0x1.f0239p+6f } },
    .flags = 0x0014, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_LADDER }
  },
  [RACE_PMOVE_SPECTATOR] = { /* spectator movement */
    .type = PM_SPECTATOR,
    .origin = { { 0x1.3c1dbcp+4f, 0x0p+0f, 0x1.39c92cp+1f } },
    .velocity = { { 0x1.8b252ap+7f, 0x0p+0f, 0x1.883b76p+4f } },
    .flags = 0x0000, .time = 0,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .angles = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 0, .touched = {}
  },
  [RACE_PMOVE_FREEZE] = { /* freeze movement */
    .type = PM_FREEZE,
    .origin = { { 0x1p+0f, 0x1p+1f, 0x1.8p+1f } },
    .velocity = { { 0x1p+2f, 0x1.4p+2f, 0x1.8p+2f } },
    .flags = 0x0400, .time = 16,
    .view_offset = { { 0x0p+0f, 0x0p+0f, 0x1.ep+4f } },
    .step_offset = 0x0p+0f,
    .view_angles = { { 0x1.4p+3f, 0x1.4p+4f, 0x0p+0f } },
    .angles = { { 0x1.4p+3f, 0x1.4p+4f, 0x0p+0f } },
    .bounds = { .mins = { { -0x1p+4f, -0x1p+4f, -0x1.8p+4f } }, .maxs = { { 0x1p+4f, 0x1p+4f, 0x1.2p+5f } } },
    .ground_entity = RACE_PMOVE_ENTITY_NONE, .ground_fraction = 0x0p+0f,
    .ground_end = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_normal = { { 0x0p+0f, 0x0p+0f, 0x0p+0f } },
    .ground_contents = 0, .ground_surface = 0,
    .water_type = 0, .water_level = WATER_NONE, .step = 0x0p+0f,
    .num_touched = 0, .touched = {}
  },
};

static race_pmove_world_t race_pmove_world;
static const uint8_t race_pmove_floor_entity;
static const uint8_t race_pmove_wall_entity;
static const uint8_t race_pmove_wall_y_entity;
static const uint8_t race_pmove_ceiling_entity;
static const uint8_t race_pmove_ramp_entity;
static const uint8_t race_pmove_steep_entity;
static const uint8_t race_pmove_step_entity;
static const uint8_t race_pmove_ledge_entity;
static const uint8_t race_pmove_ladder_entity;

static const void *Race_PmoveEntityPointer(race_pmove_entity_t entity) {
  switch (entity) {
    case RACE_PMOVE_ENTITY_FLOOR:
      return &race_pmove_floor_entity;
    case RACE_PMOVE_ENTITY_WALL:
      return &race_pmove_wall_entity;
    case RACE_PMOVE_ENTITY_WALL_Y:
      return &race_pmove_wall_y_entity;
    case RACE_PMOVE_ENTITY_CEILING:
      return &race_pmove_ceiling_entity;
    case RACE_PMOVE_ENTITY_RAMP:
      return &race_pmove_ramp_entity;
    case RACE_PMOVE_ENTITY_STEEP:
      return &race_pmove_steep_entity;
    case RACE_PMOVE_ENTITY_STEP:
      return &race_pmove_step_entity;
    case RACE_PMOVE_ENTITY_LEDGE:
      return &race_pmove_ledge_entity;
    case RACE_PMOVE_ENTITY_LADDER:
      return &race_pmove_ladder_entity;
    default:
      return NULL;
  }
}

static race_pmove_entity_t Race_PmoveEntityId(const void *entity) {
  for (race_pmove_entity_t id = RACE_PMOVE_ENTITY_FLOOR;
       id <= RACE_PMOVE_ENTITY_LADDER; id++) {
    if (entity == Race_PmoveEntityPointer(id)) {
      return id;
    }
  }

  return RACE_PMOVE_ENTITY_NONE;
}

static float Race_PmovePlaneSupportMin(const box3_t bounds,
                                       const vec3_t normal) {
  return (normal.x >= 0.f ? bounds.mins.x : bounds.maxs.x) * normal.x +
         (normal.y >= 0.f ? bounds.mins.y : bounds.maxs.y) * normal.y +
         (normal.z >= 0.f ? bounds.mins.z : bounds.maxs.z) * normal.z;
}

static void Race_PmoveTraceCandidate(cm_trace_t *trace,
                                     const cm_trace_t candidate) {
  if (candidate.start_solid || candidate.fraction < trace->fraction) {
    *trace = candidate;
  }
}

static void Race_PmoveTracePlane(cm_trace_t *trace,
                                 const vec3_t start,
                                 const vec3_t end,
                                 const box3_t bounds,
                                 const vec3_t normal,
                                 float dist,
                                 race_pmove_entity_t entity,
                                 int32_t contents,
                                 int32_t surface) {
  const float support = Race_PmovePlaneSupportMin(bounds, normal);
  const float start_distance = Vec3_Dot(start, normal) + support - dist;
  const float end_distance = Vec3_Dot(end, normal) + support - dist;

  cm_trace_t candidate = {
    .fraction = 1.f,
    .end = end,
    .plane = {
      .normal = normal,
      .dist = dist
    },
    .contents = contents,
    .surface = surface,
    .ent = (void *) Race_PmoveEntityPointer(entity)
  };

  if (start_distance < 0.f) {
    candidate.start_solid = true;
    candidate.all_solid = end_distance < 0.f;
    candidate.fraction = 0.f;
    candidate.end = start;
    Race_PmoveTraceCandidate(trace, candidate);
    return;
  }

  if (end_distance < 0.f) {
    candidate.fraction = start_distance / (start_distance - end_distance);
    candidate.end = Vec3_Fmaf(start, candidate.fraction,
                              Vec3_Subtract(end, start));
    Race_PmoveTraceCandidate(trace, candidate);
  }
}

static bool Race_PmovePointInside(const vec3_t point, const box3_t bounds) {
  return point.x > bounds.mins.x && point.x < bounds.maxs.x &&
         point.y > bounds.mins.y && point.y < bounds.maxs.y &&
         point.z > bounds.mins.z && point.z < bounds.maxs.z;
}

static void Race_PmoveTraceBox(cm_trace_t *trace,
                               const vec3_t start,
                               const vec3_t end,
                               const box3_t bounds,
                               const box3_t obstacle,
                               race_pmove_entity_t entity,
                               int32_t contents,
                               int32_t surface) {
  const box3_t expanded = Box3(Vec3_Subtract(obstacle.mins, bounds.maxs),
                               Vec3_Subtract(obstacle.maxs, bounds.mins));
  const bool starts_inside = Race_PmovePointInside(start, expanded);

  if (starts_inside) {
    const cm_trace_t candidate = {
      .all_solid = Race_PmovePointInside(end, expanded),
      .start_solid = true,
      .fraction = 0.f,
      .end = start,
      .contents = contents,
      .surface = surface,
      .ent = (void *) Race_PmoveEntityPointer(entity)
    };
    Race_PmoveTraceCandidate(trace, candidate);
    return;
  }

  const float starts[3] = { start.x, start.y, start.z };
  const float ends[3] = { end.x, end.y, end.z };
  const float mins[3] = { expanded.mins.x, expanded.mins.y,
                          expanded.mins.z };
  const float maxs[3] = { expanded.maxs.x, expanded.maxs.y,
                          expanded.maxs.z };
  float enter = 0.f;
  float leave = 1.f;
  int32_t enter_axis = -1;
  float enter_sign = 0.f;

  for (int32_t axis = 0; axis < 3; axis++) {
    const float delta = ends[axis] - starts[axis];
    if (delta == 0.f) {
      if (starts[axis] < mins[axis] || starts[axis] > maxs[axis]) {
        return;
      }
      continue;
    }

    float near = (mins[axis] - starts[axis]) / delta;
    float far = (maxs[axis] - starts[axis]) / delta;
    float sign = -1.f;
    if (near > far) {
      const float swap = near;
      near = far;
      far = swap;
      sign = 1.f;
    }

    if (near >= enter) {
      enter = near;
      enter_axis = axis;
      enter_sign = sign;
    }
    leave = Minf(leave, far);
    if (enter > leave) {
      return;
    }
  }

  if (enter_axis == -1 || enter < 0.f || enter > 1.f) {
    return;
  }

  vec3_t normal = Vec3_Zero();
  if (enter_axis == 0) {
    normal.x = enter_sign;
  } else if (enter_axis == 1) {
    normal.y = enter_sign;
  } else {
    normal.z = enter_sign;
  }

  const vec3_t delta = Vec3_Subtract(end, start);
  if (enter == 0.f && Vec3_Dot(delta, normal) >= 0.f) {
    return;
  }

  const cm_trace_t candidate = {
    .fraction = enter,
    .end = Vec3_Fmaf(start, enter, delta),
    .plane = {
      .normal = normal
    },
    .contents = contents,
    .surface = surface,
    .ent = (void *) Race_PmoveEntityPointer(entity)
  };
  Race_PmoveTraceCandidate(trace, candidate);
}

static vec3_t Race_PmoveRampNormal(void) {
  return Vec3_Normalize(Vec3(-.5f, 0.f, 1.f));
}

static vec3_t Race_PmoveSteepNormal(void) {
  return Vec3_Normalize(Vec3(-1.f, 0.f, .5f));
}

static cm_trace_t Race_PmoveTrace(const vec3_t start,
                                  const vec3_t end,
                                  const box3_t bounds) {
  cm_trace_t trace = {
    .fraction = 1.f,
    .end = end
  };

  switch (race_pmove_world) {
    case RACE_PMOVE_WORLD_FLAT:
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3_Up(), 0.f,
                           RACE_PMOVE_ENTITY_FLOOR, CONTENTS_SOLID, 0);
      break;

    case RACE_PMOVE_WORLD_WALL:
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3_Up(), 0.f,
                           RACE_PMOVE_ENTITY_FLOOR, CONTENTS_SOLID, 0);
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3(-1.f, 0.f, 0.f),
                           -64.f, RACE_PMOVE_ENTITY_WALL, CONTENTS_SOLID, 0);
      break;

    case RACE_PMOVE_WORLD_CORNER:
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3_Up(), 0.f,
                           RACE_PMOVE_ENTITY_FLOOR, CONTENTS_SOLID, 0);
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3(-1.f, 0.f, 0.f),
                           -64.f, RACE_PMOVE_ENTITY_WALL, CONTENTS_SOLID, 0);
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3(0.f, -1.f, 0.f),
                           -64.f, RACE_PMOVE_ENTITY_WALL_Y,
                           CONTENTS_SOLID, 0);
      break;

    case RACE_PMOVE_WORLD_CEILING:
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3_Up(), 0.f,
                           RACE_PMOVE_ENTITY_FLOOR, CONTENTS_SOLID, 0);
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3_Down(), -64.f,
                           RACE_PMOVE_ENTITY_CEILING, CONTENTS_SOLID, 0);
      break;

    case RACE_PMOVE_WORLD_ALL_SOLID_JITTER:
      if (start.z <= 24.f) {
        trace = (cm_trace_t) {
          .all_solid = true,
          .start_solid = true,
          .end = start,
          .contents = CONTENTS_SOLID,
          .ent = (void *) Race_PmoveEntityPointer(RACE_PMOVE_ENTITY_WALL)
        };
      }
      break;

    case RACE_PMOVE_WORLD_RAMP:
      Race_PmoveTracePlane(&trace, start, end, bounds,
                           Race_PmoveRampNormal(), 0.f,
                           RACE_PMOVE_ENTITY_RAMP, CONTENTS_SOLID, 0);
      break;

    case RACE_PMOVE_WORLD_STEEP:
      Race_PmoveTracePlane(&trace, start, end, bounds,
                           Race_PmoveSteepNormal(), 0.f,
                           RACE_PMOVE_ENTITY_STEEP, CONTENTS_SOLID, 0);
      break;

    case RACE_PMOVE_WORLD_STEP_LOW:
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3_Up(), 0.f,
                           RACE_PMOVE_ENTITY_FLOOR, CONTENTS_SOLID, 0);
      Race_PmoveTraceBox(&trace, start, end, bounds,
                         Box3(Vec3(32.f, -64.f, 0.f),
                              Vec3(96.f, 64.f, 12.f)),
                         RACE_PMOVE_ENTITY_STEP, CONTENTS_SOLID, 0);
      break;

    case RACE_PMOVE_WORLD_STEP_16:
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3_Up(), 0.f,
                           RACE_PMOVE_ENTITY_FLOOR, CONTENTS_SOLID, 0);
      Race_PmoveTraceBox(&trace, start, end, bounds,
                         Box3(Vec3(32.f, -64.f, 0.f),
                              Vec3(96.f, 64.f, PM_STEP_HEIGHT)),
                         RACE_PMOVE_ENTITY_STEP, CONTENTS_SOLID, 0);
      break;

    case RACE_PMOVE_WORLD_LEDGE:
      if (start.x <= 16.f || end.x <= 16.f) {
        Race_PmoveTracePlane(&trace, start, end, bounds, Vec3_Up(), 0.f,
                             RACE_PMOVE_ENTITY_LEDGE, CONTENTS_SOLID, 0);
      }
      break;

    case RACE_PMOVE_WORLD_LADDER:
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3(-1.f, 0.f, 0.f),
                           -32.f, RACE_PMOVE_ENTITY_LADDER,
                           CONTENTS_SOLID | CONTENTS_LADDER, 0);
      break;

    case RACE_PMOVE_WORLD_LADDER_NEGATIVE:
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3(1.f, 0.f, 0.f),
                           -32.f, RACE_PMOVE_ENTITY_LADDER,
                           CONTENTS_SOLID | CONTENTS_LADDER, 0);
      break;

    case RACE_PMOVE_WORLD_TUNNEL:
      Race_PmoveTracePlane(&trace, start, end, bounds, Vec3_Up(), 0.f,
                           RACE_PMOVE_ENTITY_FLOOR, CONTENTS_SOLID, 0);
      Race_PmoveTraceBox(&trace, start, end, bounds,
                         Box3(Vec3(0.f, -64.f, race_pmove_tunnel_height),
                              Vec3(128.f, 64.f, 128.f)),
                         RACE_PMOVE_ENTITY_CEILING, CONTENTS_SOLID, 0);
      break;

    case RACE_PMOVE_WORLD_EMPTY:
    case RACE_PMOVE_WORLD_WATER:
      break;
  }

  return trace;
}

typedef struct {
  vec3_t start;
  vec3_t end;
  box3_t bounds;
  cm_trace_t result;
} race_pmove_ladder_trace_t;

static race_pmove_ladder_trace_t race_pmove_ladder_traces[64];
static size_t race_pmove_ladder_num_traces;
static race_pmove_ladder_trace_t race_pmove_ladder_cgame_traces[64];
static size_t race_pmove_ladder_cgame_num_traces;

static cm_trace_t Race_PmoveRecordLadderTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds,
    race_pmove_ladder_trace_t *traces, size_t *num_traces) {
  const cm_trace_t result = Race_PmoveTrace(start, end, bounds);
  ck_assert_msg(*num_traces < lengthof(race_pmove_ladder_traces),
                "Q2 ladder trace log overflow");
  traces[(*num_traces)++] =
    (race_pmove_ladder_trace_t) { start, end, bounds, result };
  return result;
}

static cm_trace_t Race_PmoveLadderTrace(const vec3_t start,
                                        const vec3_t end,
                                        const box3_t bounds) {
  return Race_PmoveRecordLadderTrace(
    start, end, bounds, race_pmove_ladder_traces,
    &race_pmove_ladder_num_traces);
}

static cm_trace_t Race_PmoveLadderTraceCgame(const vec3_t start,
                                             const vec3_t end,
                                             const box3_t bounds) {
  return Race_PmoveRecordLadderTrace(
    start, end, bounds, race_pmove_ladder_cgame_traces,
    &race_pmove_ladder_cgame_num_traces);
}

static int32_t Race_PmovePointContents(const vec3_t point) {
  if (race_pmove_ladder_liquid &&
      point.z <= race_pmove_ladder_liquid_surface) {
    return CONTENTS_WATER | race_pmove_current_contents;
  }

  if (race_pmove_feet_current && point.z <= 1.f) {
    return CONTENTS_WATER | race_pmove_current_contents;
  }

  if (race_pmove_world == RACE_PMOVE_WORLD_WATER && point.z < 100.f) {
    return CONTENTS_WATER;
  }

  return 0;
}

static int32_t Race_PmoveBoxContents(const box3_t box) {
  return Race_PmovePointContents(Box3_Center(box));
}

static int32_t race_pmove_q2_water_script[16];
static size_t race_pmove_q2_water_script_count;
static vec3_t race_pmove_q2_water_points[64];
static size_t race_pmove_q2_water_point_count;

static int32_t Race_PmoveQ2WaterScriptPointContents(const vec3_t point) {
  ck_assert_msg(race_pmove_q2_water_point_count <
                  lengthof(race_pmove_q2_water_points),
                "Q2 water point log overflow");
  const size_t call = race_pmove_q2_water_point_count++;
  race_pmove_q2_water_points[call] = point;
  return call < race_pmove_q2_water_script_count
    ? race_pmove_q2_water_script[call]
    : 0;
}

static vec3_t race_pmove_q2_water_ledge_lower;
static vec3_t race_pmove_q2_water_ledge_upper;
static int32_t race_pmove_q2_water_ledge_lower_contents;
static int32_t race_pmove_q2_water_ledge_upper_contents;

static int32_t Race_PmoveQ2WaterLedgePointContents(const vec3_t point) {
  ck_assert_msg(race_pmove_q2_water_point_count <
                  lengthof(race_pmove_q2_water_points),
                "Q2 water ledge point log overflow");
  race_pmove_q2_water_points[race_pmove_q2_water_point_count++] = point;

  if (Vec3_Equal(point, race_pmove_q2_water_ledge_lower)) {
    return race_pmove_q2_water_ledge_lower_contents;
  }
  if (Vec3_Equal(point, race_pmove_q2_water_ledge_upper)) {
    return race_pmove_q2_water_ledge_upper_contents;
  }
  return 0;
}

static int32_t Race_PmoveQ2WaterCoursePointContents(const vec3_t point) {
  if (point.x >= 16.f && point.x <= 64.f && point.z < 32.f) {
    return CONTENTS_SOLID;
  }
  if (point.x < 16.f && point.z < 40.f) {
    return CONTENTS_WATER;
  }
  return 0;
}

static int32_t Race_PmoveQ2WaterCourseBoxContents(const box3_t box) {
  return Race_PmoveQ2WaterCoursePointContents(Box3_Center(box));
}

static int32_t race_pmove_q2_pool_contents = CONTENTS_WATER;
static float race_pmove_q2_pool_surface = 40.f;

static int32_t Race_PmoveQ2WaterPoolPointContents(const vec3_t point) {
  return point.z < race_pmove_q2_pool_surface
    ? race_pmove_q2_pool_contents
    : 0;
}

static int32_t Race_PmoveQ2WaterPoolBoxContents(const box3_t box) {
  return Race_PmoveQ2WaterPoolPointContents(Box3_Center(box));
}

static debug_t Race_PmoveDebugMask(void) {
  return 0;
}

static void Race_PmoveDebug(debug_t debug, const char *func,
                            const char *fmt, ...) {
  (void) debug;
  (void) func;
  (void) fmt;
}

/* `Pm_Debug` references this shared helper even with the debug mask disabled. */
char *vtos(const vec3_t v) {
  static char value[64];
  snprintf(value, sizeof(value), "(%.2f %.2f %.2f)", v.x, v.y, v.z);
  return value;
}

static pm_params_t Race_PmoveDefaultParams(void) {
  pm_params_t params;
  memset(&params, 0, sizeof(params));
  params.gravity = 800;
  params.gravity_water = PM_GRAVITY_WATER;
  params.accel_ground = PM_ACCEL_GROUND;
  params.accel_ground_slick = PM_ACCEL_GROUND_SLICK;
  params.accel_air = PM_ACCEL_AIR;
  params.accel_water = PM_ACCEL_WATER;
  params.accel_spectator = PM_ACCEL_SPECTATOR;
  params.accel_ladder = PM_ACCEL_LADDER;
  params.friction_ground = PM_FRICT_GROUND;
  params.friction_ground_slick = PM_FRICT_GROUND_SLICK;
  params.friction_air = PM_FRICT_AIR;
  params.friction_water = PM_FRICT_WATER;
  params.friction_spectator = PM_FRICT_SPECTATOR;
  params.friction_ladder = PM_FRICT_LADDER;
  params.speed_ground = PM_SPEED_RUN;
  params.speed_air = PM_SPEED_AIR;
  params.speed_water = PM_SPEED_WATER;
  params.speed_ladder = PM_SPEED_LADDER;
  params.speed_spectator = PM_SPEED_SPECTATOR;
  params.speed_stop = PM_SPEED_STOP;
  params.speed_jump = PM_SPEED_JUMP;
  params.speed_ducked = PM_SPEED_DUCKED;
  params.speed_duck_stand = PM_SPEED_DUCK_STAND;
  params.speed_water_jump = PM_SPEED_WATER_JUMP;
  return params;
}

static pm_params_t Race_PmoveQ2NamedParams(
    const race_physics_preset_id_t preset) {
  pm_params_t params;
  if (!Race_Physics_FixedParamsForPreset(preset, &params)) {
    abort();
  }
  return params;
}

static void Race_PmoveSetGround(pm_move_t *pm,
                                race_pmove_entity_t entity,
                                const vec3_t normal) {
  pm->s.flags |= PMF_ON_GROUND;
  pm->ground = (cm_trace_t) {
    .fraction = 0.f,
    .end = pm->s.origin,
    .plane = {
      .normal = normal
    },
    .contents = CONTENTS_SOLID,
    .ent = (void *) Race_PmoveEntityPointer(entity)
  };
}

static pm_move_t Race_PmoveSetup(race_pmove_fixture_id_t id) {
  pm_move_t pm;
  memset(&pm, 0, sizeof(pm));

  pm.s.type = PM_NORMAL;
  pm.s.params = Race_PmoveDefaultParams();
  pm.s.view_offset = Vec3(0.f, 0.f, 30.f);
  pm.PointContents = Race_PmovePointContents;
  pm.BoxContents = Race_PmoveBoxContents;
  pm.Trace = Race_PmoveTrace;
  pm.DebugMask = Race_PmoveDebugMask;
  pm.Debug = Race_PmoveDebug;

  switch (id) {
    case RACE_PMOVE_EMPTY_IDLE:
      race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
      pm.s.origin = Vec3(-128.f, -64.f, 96.f);
      pm.cmd.msec = 16;
      break;

    case RACE_PMOVE_EMPTY_FORWARD_16:
      race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
      pm.s.origin = Vec3(-96.f, -32.f, 128.f);
      pm.cmd.msec = 16;
      pm.cmd.forward = 300;
      break;

    case RACE_PMOVE_EMPTY_FORWARD_50:
      race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
      pm.s.origin = Vec3(-96.f, -32.f, 128.f);
      pm.cmd.msec = 50;
      pm.cmd.forward = 300;
      break;

    case RACE_PMOVE_EMPTY_BACK_SIDE:
      race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
      pm.s.origin = Vec3(-64.f, -128.f, 160.f);
      pm.cmd.msec = 33;
      pm.cmd.forward = -300;
      pm.cmd.right = 200;
      break;

    case RACE_PMOVE_EMPTY_DIAGONAL:
      race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
      pm.s.origin = Vec3(-192.f, -96.f, 192.f);
      pm.cmd.msec = 100;
      pm.cmd.forward = 300;
      pm.cmd.right = 300;
      break;

    case RACE_PMOVE_GROUND_ACCELERATION:
      race_pmove_world = RACE_PMOVE_WORLD_FLAT;
      pm.s.origin = Vec3(0.f, 0.f, 24.f);
      pm.cmd.msec = 100;
      pm.cmd.forward = 300;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
      break;

    case RACE_PMOVE_GROUND_FRICTION:
      race_pmove_world = RACE_PMOVE_WORLD_FLAT;
      pm.s.origin = Vec3(0.f, 0.f, 24.f);
      pm.s.velocity = Vec3(300.f, 0.f, 0.f);
      pm.cmd.msec = 100;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
      break;

    case RACE_PMOVE_GROUND_STOP:
      race_pmove_world = RACE_PMOVE_WORLD_FLAT;
      pm.s.origin = Vec3(0.f, 0.f, 24.f);
      pm.s.velocity = Vec3(30.f, 0.f, 0.f);
      pm.cmd.msec = 100;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
      break;

    case RACE_PMOVE_AIR_FORWARD:
      race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
      pm.s.origin = Vec3(-24.f, -48.f, 128.f);
      pm.cmd.msec = 50;
      pm.cmd.forward = 300;
      break;

    case RACE_PMOVE_AIR_STRAFE:
      race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
      pm.s.origin = Vec3(-24.f, -48.f, 128.f);
      pm.s.velocity = Vec3(100.f, 0.f, 0.f);
      pm.cmd.msec = 50;
      pm.cmd.right = 300;
      break;

    case RACE_PMOVE_AIR_DIAGONAL:
      race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
      pm.s.origin = Vec3(0.f, 0.f, 128.f);
      pm.s.velocity = Vec3(100.f, 20.f, 0.f);
      pm.cmd.msec = 100;
      pm.cmd.forward = 300;
      pm.cmd.right = 100;
      break;

    case RACE_PMOVE_JUMP:
      race_pmove_world = RACE_PMOVE_WORLD_FLAT;
      pm.s.origin = Vec3(0.f, 0.f, 24.f);
      pm.cmd.msec = 100;
      pm.cmd.forward = 300;
      pm.cmd.up = 300;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
      break;

    case RACE_PMOVE_HELD_JUMP:
      race_pmove_world = RACE_PMOVE_WORLD_FLAT;
      pm.s.origin = Vec3(0.f, 0.f, 24.f);
      pm.s.flags = PMF_JUMP_HELD;
      pm.cmd.msec = 100;
      pm.cmd.forward = 300;
      pm.cmd.up = 300;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
      break;

    case RACE_PMOVE_LANDING:
      race_pmove_world = RACE_PMOVE_WORLD_FLAT;
      pm.s.origin = Vec3(0.f, 0.f, 28.f);
      pm.s.velocity = Vec3(0.f, 0.f, -400.f);
      pm.cmd.msec = 16;
      break;

    case RACE_PMOVE_WALL_CLIP:
      race_pmove_world = RACE_PMOVE_WORLD_WALL;
      pm.s.origin = Vec3(40.f, 0.f, 24.f);
      pm.s.velocity = Vec3(300.f, 0.f, 0.f);
      pm.cmd.msec = 50;
      pm.cmd.forward = 300;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
      break;

    case RACE_PMOVE_CORNER_CLIP:
      race_pmove_world = RACE_PMOVE_WORLD_CORNER;
      pm.s.origin = Vec3(40.f, 40.f, 24.f);
      pm.s.velocity = Vec3(300.f, 300.f, 0.f);
      pm.cmd.msec = 50;
      pm.cmd.forward = 300;
      pm.cmd.right = 300;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
      break;

    case RACE_PMOVE_CEILING_CLIP:
      race_pmove_world = RACE_PMOVE_WORLD_CEILING;
      pm.s.origin = Vec3(0.f, 0.f, 24.f);
      pm.cmd.msec = 100;
      pm.cmd.up = 300;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
      break;

    case RACE_PMOVE_ALL_SOLID_RECOVERY:
      race_pmove_world = RACE_PMOVE_WORLD_ALL_SOLID_JITTER;
      pm.s.origin = Vec3(-8.f, -8.f, 24.f);
      pm.cmd.msec = 16;
      pm.cmd.forward = 100;
      break;

    case RACE_PMOVE_RAMP_CLIMB:
      race_pmove_world = RACE_PMOVE_WORLD_RAMP;
      pm.s.origin = Vec3(0.f, 0.f, 32.f);
      pm.cmd.msec = 100;
      pm.cmd.forward = 300;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_RAMP,
                          Race_PmoveRampNormal());
      break;

    case RACE_PMOVE_STEP_UP_LOW:
      race_pmove_world = RACE_PMOVE_WORLD_STEP_LOW;
      pm.s.origin = Vec3(0.f, 0.f, 24.f);
      pm.cmd.msec = 100;
      pm.cmd.forward = 300;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
      break;

    case RACE_PMOVE_STEP_16:
      race_pmove_world = RACE_PMOVE_WORLD_STEP_16;
      pm.s.origin = Vec3(0.f, 0.f, 24.f);
      pm.cmd.msec = 100;
      pm.cmd.forward = 300;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
      break;

    case RACE_PMOVE_LEDGE_DEPARTURE:
      race_pmove_world = RACE_PMOVE_WORLD_LEDGE;
      pm.s.origin = Vec3(-8.f, -32.f, 24.f);
      pm.cmd.msec = 100;
      pm.cmd.forward = 300;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_LEDGE, Vec3_Up());
      break;

    case RACE_PMOVE_GROUND_SNAP:
      race_pmove_world = RACE_PMOVE_WORLD_FLAT;
      pm.s.origin = Vec3(0.f, 0.f, 24.75f);
      pm.cmd.msec = 16;
      break;

    case RACE_PMOVE_STEEP_GROUND:
      race_pmove_world = RACE_PMOVE_WORLD_STEEP;
      pm.s.origin = Vec3(0.f, 0.f, 56.f);
      pm.s.velocity = Vec3(100.f, 0.f, 0.f);
      pm.cmd.msec = 50;
      pm.cmd.forward = 300;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_STEEP,
                          Race_PmoveSteepNormal());
      break;

    case RACE_PMOVE_CROUCH:
      race_pmove_world = RACE_PMOVE_WORLD_FLAT;
      pm.s.origin = Vec3(0.f, 0.f, 24.f);
      pm.cmd.msec = 50;
      pm.cmd.up = -300;
      Race_PmoveSetGround(&pm, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
      break;

    case RACE_PMOVE_WATER:
      race_pmove_world = RACE_PMOVE_WORLD_WATER;
      pm.s.origin = Vec3_Zero();
      pm.s.velocity = Vec3(30.f, 0.f, 0.f);
      pm.cmd.msec = 100;
      pm.cmd.forward = 300;
      pm.cmd.up = 100;
      break;

    case RACE_PMOVE_LADDER:
      race_pmove_world = RACE_PMOVE_WORLD_LADDER;
      pm.s.origin = Vec3(16.f, 0.f, 40.f);
      pm.cmd.msec = 100;
      pm.cmd.forward = 300;
      pm.cmd.up = 300;
      break;

    case RACE_PMOVE_SPECTATOR:
      race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
      pm.s.type = PM_SPECTATOR;
      pm.s.velocity = Vec3(100.f, 0.f, 0.f);
      pm.cmd.msec = 100;
      pm.cmd.forward = 500;
      pm.cmd.up = 100;
      break;

    case RACE_PMOVE_FREEZE:
      race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
      pm.s.type = PM_FREEZE;
      pm.s.origin = Vec3(1.f, 2.f, 3.f);
      pm.s.velocity = Vec3(4.f, 5.f, 6.f);
      pm.s.flags = PMF_TIME_TELEPORT;
      pm.s.time = 32;
      pm.cmd.msec = 16;
      pm.cmd.angles = Vec3(10.f, 20.f, 0.f);
      break;

    case RACE_PMOVE_FIXTURE_TOTAL:
      abort();
  }

  return pm;
}

typedef void (*race_pmove_func_t)(pm_move_t *pm);

static pm_move_t Race_PmoveRun(race_pmove_fixture_id_t id,
                               race_pmove_func_t move) {
  pm_move_t pm = Race_PmoveSetup(id);
  move(&pm);
  return pm;
}

static void Race_PmoveUseCommonPhysics(void) {
  Race_Physics_SetProvider(NULL);
  ck_assert(Race_Physics_SetActive(&race_pmove_common_config));
  ck_assert(Race_Physics_ConfigEquals(Race_Physics_Current(),
                                     &race_pmove_common_config));
}

static void Race_PmoveUseQ2TestPhysics(void) {
  Race_Physics_SetProvider(NULL);
  ck_assert(Race_Physics_SetActive(&race_pmove_q2_config));
  ck_assert(Race_Physics_ConfigEquals(Race_Physics_Current(),
                                     &race_pmove_q2_config));
  // Phase 8C-G exact continuous-state goldens intentionally predate Q2 state
  // snapping. Phase 8H cases enable snapping explicitly after selecting Q2.
  Pm_SetQ2SnapEnabledForTest(false);
}

static void Race_PmoveUseQ2FixTestPhysics(void) {
  Race_Physics_SetProvider(NULL);
  ck_assert(Race_Physics_SetActive(&race_pmove_q2_fix_config));
  ck_assert(Race_Physics_ConfigEquals(Race_Physics_Current(),
                                     &race_pmove_q2_fix_config));
  Pm_SetQ2SnapEnabledForTest(false);
}

static void Race_PmoveUseQ2SnapTestPhysics(void) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2SnapEnabledForTest(true);
}

static void Race_PmoveUseQ2FixSnapTestPhysics(void) {
  Race_PmoveUseQ2FixTestPhysics();
  Pm_SetQ2SnapEnabledForTest(true);
}

static void Race_PmoveUseQ2NamedTestPhysics(
    const race_physics_preset_id_t preset, const bool snap) {
  switch (preset) {
    case RACE_PHYSICS_PRESET_Q2:
      Race_PmoveUseQ2TestPhysics();
      break;
    case RACE_PHYSICS_PRESET_QUETOO_FIX_V1:
      Race_PmoveUseQ2FixTestPhysics();
      break;
    default:
      abort();
  }

  Pm_SetQ2SnapEnabledForTest(snap);
}

static void Race_PmoveCommand(pm_move_t *pm, uint16_t msec, int16_t up) {
  pm->cmd = (pm_cmd_t) {
    .msec = msec,
    .up = up
  };
  Pm_Move(pm);
}

static pm_move_t Race_PmoveQ2Landing(
    const race_physics_preset_id_t preset, const float velocity,
    const uint16_t msec, const int16_t up, const uint16_t flags) {
  Race_PmoveUseQ2NamedTestPhysics(preset, false);

  pm_move_t pm = Race_PmoveSetup(RACE_PMOVE_LANDING);
  pm.s.params = Race_PmoveQ2NamedParams(preset);
  pm.s.origin = Vec3(-64.f, -32.f, 24.5f);
  pm.s.velocity = Vec3(0.f, 0.f, velocity);
  pm.s.flags = flags;
  pm.s.time = 0;
  memset(&pm.ground, 0, sizeof(pm.ground));
  Race_PmoveCommand(&pm, msec, up);
  return pm;
}

static uint32_t Race_PmoveFloatBits(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static void Race_PmoveAssertFloat(const char *fixture,
                                  const char *field,
                                  float actual,
                                  float expected) {
  ck_assert_msg(Race_PmoveFloatBits(actual) == Race_PmoveFloatBits(expected),
                "%s: %s was %a (0x%08x), expected %a (0x%08x)",
                fixture, field, (double) actual, Race_PmoveFloatBits(actual),
                (double) expected, Race_PmoveFloatBits(expected));
}

static void Race_PmoveAssertVec3(const char *fixture,
                                 const char *field,
                                 const vec3_t actual,
                                 const vec3_t expected) {
  char component[64];
  snprintf(component, sizeof(component), "%s.x", field);
  Race_PmoveAssertFloat(fixture, component, actual.x, expected.x);
  snprintf(component, sizeof(component), "%s.y", field);
  Race_PmoveAssertFloat(fixture, component, actual.y, expected.y);
  snprintf(component, sizeof(component), "%s.z", field);
  Race_PmoveAssertFloat(fixture, component, actual.z, expected.z);
}

static void Race_PmoveAssertBounds(const char *fixture,
                                   box3_t actual,
                                   box3_t expected);

static void Race_PmoveAssertExpected(race_pmove_fixture_id_t id,
                                     const pm_move_t *pm) {
  const char *name = race_pmove_fixture_names[id];
  const race_pmove_expected_t *expected = &race_pmove_expected[id];

  ck_assert_msg(pm->s.type == expected->type,
                "%s: type was %d, expected %d", name,
                pm->s.type, expected->type);
  Race_PmoveAssertVec3(name, "origin", pm->s.origin, expected->origin);
  Race_PmoveAssertVec3(name, "velocity", pm->s.velocity, expected->velocity);
  ck_assert_msg(pm->s.flags == expected->flags,
                "%s: flags were 0x%04x, expected 0x%04x", name,
                pm->s.flags, expected->flags);
  ck_assert_msg(pm->s.time == expected->time,
                "%s: time was %u, expected %u", name,
                pm->s.time, expected->time);
  Race_PmoveAssertVec3(name, "view_offset", pm->s.view_offset,
                       expected->view_offset);
  Race_PmoveAssertFloat(name, "step_offset", pm->s.step_offset,
                        expected->step_offset);
  Race_PmoveAssertVec3(name, "view_angles", pm->s.view_angles,
                       expected->view_angles);
  Race_PmoveAssertVec3(name, "angles", pm->angles, expected->angles);
  Race_PmoveAssertVec3(name, "bounds.mins", pm->bounds.mins,
                       expected->bounds.mins);
  Race_PmoveAssertVec3(name, "bounds.maxs", pm->bounds.maxs,
                       expected->bounds.maxs);
  ck_assert_msg(Race_PmoveEntityId(pm->ground.ent) ==
                  expected->ground_entity,
                "%s: ground entity was %d, expected %d", name,
                Race_PmoveEntityId(pm->ground.ent), expected->ground_entity);
  Race_PmoveAssertFloat(name, "ground.fraction", pm->ground.fraction,
                        expected->ground_fraction);
  Race_PmoveAssertVec3(name, "ground.end", pm->ground.end,
                       expected->ground_end);
  Race_PmoveAssertVec3(name, "ground.normal", pm->ground.plane.normal,
                       expected->ground_normal);
  ck_assert_msg(pm->ground.contents == expected->ground_contents,
                "%s: ground contents were %d, expected %d", name,
                pm->ground.contents, expected->ground_contents);
  ck_assert_msg(pm->ground.surface == expected->ground_surface,
                "%s: ground surface was %d, expected %d", name,
                pm->ground.surface, expected->ground_surface);
  ck_assert_msg(pm->water_type == expected->water_type,
                "%s: water type was %d, expected %d", name,
                pm->water_type, expected->water_type);
  ck_assert_msg(pm->water_level == expected->water_level,
                "%s: water level was %d, expected %d", name,
                pm->water_level, expected->water_level);
  Race_PmoveAssertFloat(name, "step", pm->step, expected->step);
  ck_assert_msg(pm->num_touched == expected->num_touched,
                "%s: touched count was %d, expected %d", name,
                pm->num_touched, expected->num_touched);
  ck_assert_msg(pm->num_touched <= 8,
                "%s: fixture expected-touch capacity exceeded", name);
  for (int32_t i = 0; i < pm->num_touched; i++) {
    ck_assert_msg(Race_PmoveEntityId(pm->touched[i].ent) ==
                    expected->touched[i],
                  "%s: touched[%d] was %d, expected %d", name, i,
                  Race_PmoveEntityId(pm->touched[i].ent),
                  expected->touched[i]);
  }

  const pm_params_t defaults = Race_PmoveDefaultParams();
  ck_assert_msg(memcmp(&pm->s.params, &defaults, sizeof(defaults)) == 0,
                "%s: Pm_Move modified replicated movement parameters", name);
}

START_TEST(_Race_PmoveCommonParity) {
  Race_PmoveUseCommonPhysics();

  ck_assert_int_eq(memcmp(&PM_BOUNDS, &PM_BOUNDS_CommonReference,
                          sizeof(PM_BOUNDS)), 0);
  ck_assert_int_eq(memcmp(&PM_CROUCHED_BOUNDS,
                          &PM_CROUCHED_BOUNDS_CommonReference,
                          sizeof(PM_CROUCHED_BOUNDS)), 0);

  for (race_pmove_fixture_id_t id = RACE_PMOVE_EMPTY_IDLE;
       id < RACE_PMOVE_FIXTURE_TOTAL; id++) {
    const pm_move_t race = Race_PmoveRun(id, Pm_Move);
    const pm_move_t common = Race_PmoveRun(id, Pm_Move_CommonReference);
    ck_assert_msg(memcmp(&race, &common, sizeof(race)) == 0,
                  "%s: Race and common Pm_Move outputs differ",
                  race_pmove_fixture_names[id]);
  }
} END_TEST

START_TEST(_Race_PmoveGoldenBaseline) {
  Race_PmoveUseCommonPhysics();

  for (race_pmove_fixture_id_t id = RACE_PMOVE_EMPTY_IDLE;
       id < RACE_PMOVE_FIXTURE_TOTAL; id++) {
    const pm_move_t pm = Race_PmoveRun(id, Pm_Move);
    Race_PmoveAssertExpected(id, &pm);
  }
} END_TEST

START_TEST(_Race_PmoveReadsPhysicsIdentityOnce) {
  race_pmove_physics_provider_calls = 0u;
  Race_Physics_SetProvider(Race_PmovePhysicsProvider);
  (void) Race_PmoveRun(RACE_PMOVE_GROUND_ACCELERATION, Pm_Move);
  ck_assert_uint_eq(race_pmove_physics_provider_calls, 1u);
  Race_Physics_SetProvider(NULL);
} END_TEST

START_TEST(_Race_PmoveTrainingObserverContract) {
  Race_PmoveUseCommonPhysics();

  pm_move_t expected = Race_PmoveSetup(RACE_PMOVE_AIR_STRAFE);
  Pm_Move(&expected);

  memset(&race_pmove_training_sample, 0,
         sizeof(race_pmove_training_sample));
  race_pmove_training_calls = 0u;
  pm_move_t observed = Race_PmoveSetup(RACE_PMOVE_AIR_STRAFE);
  Pm_RaceTraining_SetObserver(
    Race_PmoveTrainingObserver, &race_pmove_training_sample);
  Pm_Move(&observed);

  ck_assert_int_eq(memcmp(&observed, &expected, sizeof(observed)), 0);
  ck_assert_uint_eq(race_pmove_training_calls, 1u);
  ck_assert(race_pmove_training_sample.active);
  ck_assert_float_eq_tol(race_pmove_training_sample.forward.x, 1.f, 0.001f);
  ck_assert_float_eq_tol(race_pmove_training_sample.velocity.x,
                         99.375f, 0.001f);
  ck_assert_float_gt(race_pmove_training_sample.wishspeed, 0.f);
  ck_assert_float_eq_tol(race_pmove_training_sample.accel,
                         PM_ACCEL_AIR, 0.001f);
  ck_assert_float_eq_tol(race_pmove_training_sample.frametime,
                         0.05f, 0.001f);
  ck_assert_float_eq_tol(race_pmove_training_sample.view_yaw, 0.f, 0.001f);

  pm_move_t unobserved = Race_PmoveSetup(RACE_PMOVE_AIR_STRAFE);
  Pm_Move(&unobserved);
  ck_assert_uint_eq(race_pmove_training_calls, 1u);

  race_pmove_training_calls = 0u;
  pm_move_t frozen = Race_PmoveSetup(RACE_PMOVE_FREEZE);
  Pm_RaceTraining_SetObserver(
    Race_PmoveTrainingObserver, &race_pmove_training_sample);
  Pm_Move(&frozen);
  ck_assert_uint_eq(race_pmove_training_calls, 0u);
  unobserved = Race_PmoveSetup(RACE_PMOVE_AIR_STRAFE);
  Pm_Move(&unobserved);
  ck_assert_uint_eq(race_pmove_training_calls, 0u);
} END_TEST

START_TEST(_Race_PmoveQ2GroundJumpContract) {
  Race_PmoveUseQ2TestPhysics();

  pm_move_t pm = Race_PmoveSetup(RACE_PMOVE_JUMP);
  pm.s.origin = Vec3(-96.f, -48.f, 24.f);
  pm.s.params.gravity = 0.f;
  pm.s.params.speed_jump = 275.f;
  pm.s.flags |= PMF_JUMP_HELD;

  Race_PmoveCommand(&pm, 8, 9);
  ck_assert(pm.s.flags & PMF_JUMP_HELD);
  ck_assert(!(pm.s.flags & PMF_JUMPED));
  ck_assert(pm.s.flags & PMF_ON_GROUND);
  ck_assert_ptr_nonnull(pm.ground.ent);

  Race_PmoveCommand(&pm, 0, 10);
  ck_assert(pm.s.flags & PMF_JUMP_HELD);
  ck_assert(!(pm.s.flags & PMF_JUMPED));
  ck_assert(pm.s.flags & PMF_ON_GROUND);
  ck_assert_ptr_nonnull(pm.ground.ent);

  Race_PmoveCommand(&pm, 0, 0);
  ck_assert(!(pm.s.flags & PMF_JUMP_HELD));
  ck_assert(!(pm.s.flags & PMF_JUMPED));
  ck_assert(pm.s.flags & PMF_ON_GROUND);
  ck_assert_ptr_nonnull(pm.ground.ent);

  Race_PmoveCommand(&pm, 0, 10);
  ck_assert(pm.s.flags & PMF_JUMPED);
  ck_assert(pm.s.flags & PMF_JUMP_HELD);
  ck_assert(!(pm.s.flags & PMF_ON_GROUND));
  ck_assert(!(pm.s.flags & PMF_TIME_TRICK_START));
  ck_assert_uint_eq(pm.s.time, 0u);
  ck_assert_ptr_null(pm.ground.ent);
  Race_PmoveAssertFloat("Q2 fresh jump", "velocity.z",
                        pm.s.velocity.z, 275.f);

  pm = Race_PmoveSetup(RACE_PMOVE_JUMP);
  pm.s.params.gravity = 0.f;
  pm.s.params.speed_jump = 275.f;
  pm.s.velocity.z = 25.f;
  pm.cmd.msec = 0;
  pm.cmd.up = 10;
  Pm_Move(&pm);
  Race_PmoveAssertFloat("Q2 positive-velocity jump", "velocity.z",
                        pm.s.velocity.z, 300.f);

  pm = Race_PmoveSetup(RACE_PMOVE_JUMP);
  pm.s.params.gravity = 0.f;
  pm.s.params.speed_jump = 275.f;
  pm.s.velocity.z = -25.f;
  pm.cmd.msec = 0;
  pm.cmd.up = 10;
  Pm_Move(&pm);
  Race_PmoveAssertFloat("Q2 negative-velocity jump", "velocity.z",
                        pm.s.velocity.z, 275.f);
} END_TEST

START_TEST(_Race_PmoveQ2LandingThresholds) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const float velocities[] = {
    -199.f, -200.f, -200.001f, -399.f, -400.f, -400.001f, -800.f
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const pm_params_t expected = Race_PmoveQ2NamedParams(presets[preset]);

    for (size_t velocity = 0; velocity < lengthof(velocities); velocity++) {
      const pm_move_t pm = Race_PmoveQ2Landing(
        presets[preset], velocities[velocity], 8, 0, 0);
      ck_assert_uint_eq(pm.s.flags, PMF_ON_GROUND);
      ck_assert_uint_eq(pm.s.time, 0u);
      ck_assert(!(pm.s.flags & PMF_TIME_LAND));
      ck_assert(!(pm.s.flags & PMF_JUMPED));
      ck_assert_int_eq(Race_PmoveEntityId(pm.ground.ent),
                       RACE_PMOVE_ENTITY_FLOOR);
      Race_PmoveAssertFloat("Q2 named landing", "ground.fraction",
                            pm.ground.fraction, 0.f);
      Race_PmoveAssertVec3("Q2 named landing", "ground.end",
                           pm.ground.end, Vec3(-64.f, -32.f, 24.f));
      Race_PmoveAssertVec3("Q2 named landing", "ground.normal",
                           pm.ground.plane.normal, Vec3_Up());
      Race_PmoveAssertVec3("Q2 named landing", "origin", pm.s.origin,
                           Vec3(-64.f, -32.f, 24.f));
      Race_PmoveAssertVec3("Q2 named landing", "velocity", pm.s.velocity,
                           Vec3_Zero());
      Race_PmoveAssertFloat("Q2 named landing", "step", pm.step, 0.f);
      ck_assert_int_eq(pm.num_touched, 1);
      ck_assert_int_eq(Race_PmoveEntityId(pm.touched[0].ent),
                       RACE_PMOVE_ENTITY_FLOOR);
      ck_assert(Race_Physics_ParamsEqual(&pm.s.params, &expected));
    }
  }
} END_TEST

START_TEST(_Race_PmoveQ2LandingDurations) {
  static const uint16_t command_msec[] = {
    8u, 16u, 25u, 50u
  };
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const pm_params_t expected = Race_PmoveQ2NamedParams(presets[preset]);

    for (size_t command = 0;
         command < lengthof(command_msec); command++) {
      pm_move_t pm = Race_PmoveQ2Landing(
        presets[preset], -800.f, command_msec[command], 10, PMF_JUMP_HELD);
      ck_assert(pm.s.flags & PMF_ON_GROUND);
      ck_assert(pm.s.flags & PMF_JUMP_HELD);
      ck_assert(!(pm.s.flags & (PMF_JUMPED | PMF_TIME_LAND)));
      ck_assert_uint_eq(pm.s.time, 0u);

      for (size_t hold = 0; hold < 3u; hold++) {
        Race_PmoveCommand(&pm, command_msec[command], 10);
        ck_assert(pm.s.flags & PMF_ON_GROUND);
        ck_assert(pm.s.flags & PMF_JUMP_HELD);
        ck_assert(!(pm.s.flags & (PMF_JUMPED | PMF_TIME_LAND)));
        ck_assert_uint_eq(pm.s.time, 0u);
      }

      Race_PmoveCommand(&pm, command_msec[command], 0);
      ck_assert(pm.s.flags & PMF_ON_GROUND);
      ck_assert(!(pm.s.flags & PMF_JUMP_HELD));

      Race_PmoveCommand(&pm, command_msec[command], 10);
      ck_assert(pm.s.flags & PMF_JUMPED);
      ck_assert(pm.s.flags & PMF_JUMP_HELD);
      ck_assert(!(pm.s.flags & (PMF_ON_GROUND | PMF_TIME_LAND)));
      ck_assert_uint_eq(pm.s.time, 0u);

      Race_PmoveCommand(&pm, command_msec[command], 10);
      ck_assert(!(pm.s.flags & PMF_JUMPED));
      ck_assert(pm.s.flags & PMF_JUMP_HELD);
      ck_assert(Race_Physics_ParamsEqual(&pm.s.params, &expected));
    }
  }
} END_TEST

#define RACE_PMOVE_Q2_SEQUENCE_COMMANDS 8

static void Race_PmoveQ2Sequence(
    const race_physics_preset_id_t preset, const bool snap,
    pm_move_t states[RACE_PMOVE_Q2_SEQUENCE_COMMANDS]) {
  Race_PmoveUseQ2NamedTestPhysics(preset, snap);

  pm_move_t pm = Race_PmoveSetup(RACE_PMOVE_JUMP);
  pm.s.params = Race_PmoveQ2NamedParams(preset);
  pm.s.origin = Vec3(-96.f, -48.f, 24.f);
  pm.cmd.msec = 8;
  pm.cmd.up = 10;
  Pm_Move(&pm);
  states[0] = pm;

  Race_PmoveCommand(&pm, 16, 10);
  states[1] = pm;

  pm.s.origin = Vec3(-96.f, -48.f, 24.5f);
  pm.s.velocity = Vec3(0.f, 0.f, -800.f);
  pm.s.flags &= ~(PMF_ON_GROUND | PMF_JUMPED | PMF_TIME_MASK);
  pm.s.flags |= PMF_JUMP_HELD;
  pm.s.time = 0;
  memset(&pm.ground, 0, sizeof(pm.ground));
  Race_PmoveCommand(&pm, 8, 10);
  states[2] = pm;

  Race_PmoveCommand(&pm, 8, 10);
  states[3] = pm;
  Race_PmoveCommand(&pm, 16, 10);
  states[4] = pm;
  Race_PmoveCommand(&pm, 25, 10);
  states[5] = pm;

  Race_PmoveCommand(&pm, 8, 0);
  states[6] = pm;
  Race_PmoveCommand(&pm, 8, 10);
  states[7] = pm;
}

START_TEST(_Race_PmoveQ2NoAutohopStateMachine) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    pm_move_t states[RACE_PMOVE_Q2_SEQUENCE_COMMANDS];
    Race_PmoveQ2Sequence(presets[preset], false, states);

    ck_assert(states[0].s.flags & PMF_JUMPED);
    ck_assert(states[0].s.flags & PMF_JUMP_HELD);
    ck_assert(!(states[1].s.flags & PMF_JUMPED));

    for (size_t i = 2u; i <= 5u; i++) {
      ck_assert(states[i].s.flags & PMF_ON_GROUND);
      ck_assert(states[i].s.flags & PMF_JUMP_HELD);
      ck_assert(!(states[i].s.flags & (PMF_JUMPED | PMF_TIME_LAND)));
      ck_assert_uint_eq(states[i].s.time, 0u);
      ck_assert_ptr_nonnull(states[i].ground.ent);
    }

    ck_assert(!(states[6].s.flags & PMF_JUMP_HELD));
    ck_assert(states[6].s.flags & PMF_ON_GROUND);
    ck_assert(states[7].s.flags & PMF_JUMPED);
    ck_assert(states[7].s.flags & PMF_JUMP_HELD);
    ck_assert(!(states[7].s.flags & PMF_ON_GROUND));
  }
} END_TEST

START_TEST(_Race_PmoveQ2GameCgameCommandParity) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    pm_move_t game[RACE_PMOVE_Q2_SEQUENCE_COMMANDS];
    pm_move_t cgame[RACE_PMOVE_Q2_SEQUENCE_COMMANDS];
    Race_PmoveQ2Sequence(presets[preset], false, game);
    Race_PmoveQ2Sequence(presets[preset], false, cgame);

    for (size_t i = 0; i < RACE_PMOVE_Q2_SEQUENCE_COMMANDS; i++) {
      ck_assert_msg(memcmp(game + i, cgame + i, sizeof(*game)) == 0,
                    "Q2 preset %d GAME/CGAME differs after command %zu",
                    presets[preset], i + 1u);
    }
  }
} END_TEST

START_TEST(_Race_PmoveQ2AiDirectAndDefaultWaterRegression) {
  Race_PmoveUseQ2TestPhysics();
  pm_move_t ai = Race_PmoveSetup(RACE_PMOVE_JUMP);
  ai.s.params.gravity = 0.f;
  ai.cmd.msec = 8;
  ai.cmd.up = 10;
  Pm_Move(&ai);
  ck_assert(ai.s.flags & PMF_JUMPED);
  ck_assert(ai.s.flags & PMF_JUMP_HELD);
  ck_assert_ptr_null(ai.ground.ent);

  Race_PmoveUseCommonPhysics();
  const pm_move_t default_water = Race_PmoveRun(RACE_PMOVE_WATER, Pm_Move);
  Race_PmoveAssertExpected(RACE_PMOVE_WATER, &default_water);
  Race_PmoveUseQ2TestPhysics();
  pm_move_t q2_water = Race_PmoveRun(RACE_PMOVE_WATER, Pm_Move);
  Race_PmoveAssertBounds("Q2 water standing hull", q2_water.bounds,
                         Pm_PlayerBounds(false));
  Race_PmoveAssertFloat("Q2 water standing view", "view_offset.z",
                        q2_water.s.view_offset.z, 22.f);
  ck_assert_int_eq(q2_water.water_level, WATER_UNDER);
  ck_assert_int_eq(q2_water.water_type, CONTENTS_WATER);
  ck_assert_msg(memcmp(&default_water.s.params, &q2_water.s.params,
                       sizeof(default_water.s.params)) == 0,
                "Q2 water migration changed the supplied parameter snapshot");
} END_TEST

typedef struct {
  const char *name;
  float air_wishspeed_cap;
  vec3_t velocity;
  uint16_t msec;
  int16_t forward;
  int16_t right;
  vec3_t angles;
  float gravity;
  vec3_t expected_origin;
  vec3_t expected_velocity;
} race_pmove_q2_air_case_t;

static const race_pmove_q2_air_case_t race_pmove_q2_air_cases[] = {
  {
    "stationary forward", 0.f, { { 0.f, 0.f, 0.f } }, 16, 300, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.7fb15cp+6f, -0x1.8p+5f, 0x1p+7f } },
    { { 0x1.333334p+2f, 0.f, 0.f } }
  },
  {
    "stationary side", 0.f, { { 0.f, 0.f, 0.f } }, 16, 0, 300,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.8p+6f, -0x1.809d4ap+5f, 0x1p+7f } },
    { { 0.f, -0x1.333334p+2f, 0.f } }
  },
  {
    "stationary diagonal", 0.f, { { 0.f, 0.f, 0.f } }, 16, 300, 300,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.7fc864p+6f, -0x1.806f38p+5f, 0x1p+7f } },
    { { 0x1.b27248p+1f, -0x1.b27248p+1f, 0.f } }
  },
  {
    "pitched diagonal", 0.f, { { 0.f, 0.f, 0.f } }, 16, 300, 300,
    { { 60.f, 0.f, 0.f } }, 0.f,
    { { -0x1.7fca26p+6f, -0x1.80729ep+5f, 0x1p+7f } },
    { { 0x1.a4bc52p+1f, -0x1.bfbccep+1f, 0.f } }
  },
  {
    "same direction", 0.f, { { 100.f, 0.f, 50.f } }, 16, 300, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.794af4p+6f, -0x1.8p+5f, 0x1.01999ap+7f } },
    { { 0x1.a33334p+6f, 0.f, 0x1.9p+5f } }
  },
  {
    "perpendicular", 0.f, { { 0.f, 500.f, 50.f } }, 16, 300, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.7fb15cp+6f, -0x1.4p+5f, 0x1.01999ap+7f } },
    { { 0x1.333334p+2f, 0x1.f4p+8f, 0x1.9p+5f } }
  },
  {
    "partial reverse", 0.f, { { -100.f, 50.f, 50.f } }, 16, 300, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.8617c2p+6f, -0x1.79999ap+5f, 0x1.01999ap+7f } },
    { { -0x1.7cccccp+6f, 0x1.9p+5f, 0x1.9p+5f } }
  },
  {
    "full reverse", 0.f, { { -500.f, 0.f, 50.f } }, 16, 300, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.9fb15cp+6f, -0x1.8p+5f, 0x1.01999ap+7f } },
    { { -0x1.ef3334p+8f, 0.f, 0x1.9p+5f } }
  },
  {
    "desired speed at limit", 0.f, { { 300.f, 0.f, 0.f } }, 16, 300, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.6cccccp+6f, -0x1.8p+5f, 0x1p+7f } },
    { { 0x1.2cp+8f, 0.f, 0.f } }
  },
  {
    "desired speed above limit", 0.f, { { 301.f, 0.f, 0.f } }, 16, 300, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.6cbc6ap+6f, -0x1.8p+5f, 0x1p+7f } },
    { { 0x1.2dp+8f, 0.f, 0.f } }
  },
  {
    "acceleration cap below", 30.f, { { 29.f, 0.f, 0.f } }, 16, 300, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.7e147ap+6f, -0x1.8p+5f, 0x1p+7f } },
    { { 0x1.ep+4f, 0.f, 0.f } }
  },
  {
    "acceleration cap at", 30.f, { { 30.f, 0.f, 0.f } }, 16, 300, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.7e147ap+6f, -0x1.8p+5f, 0x1p+7f } },
    { { 0x1.ep+4f, 0.f, 0.f } }
  },
  {
    "acceleration cap just above", 30.f, { { 30.001f, 0.f, 0.f } },
    16, 300, 0, { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.7e1476p+6f, -0x1.8p+5f, 0x1p+7f } },
    { { 0x1.e00418p+4f, 0.f, 0.f } }
  },
  {
    "acceleration cap high raw input", 30.f, { { 0.f, 0.f, 0.f } },
    16, 3000, 0, { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.7fb15cp+6f, -0x1.8p+5f, 0x1p+7f } },
    { { 0x1.333334p+2f, 0.f, 0.f } }
  },
  {
    "acceleration cap high existing velocity", 30.f,
    { { 0.f, 700.f, 0.f } }, 16, 3000, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.7fb15cp+6f, -0x1.266666p+5f, 0x1p+7f } },
    { { 0x1.333334p+2f, 0x1.5ep+9f, 0.f } }
  },
  {
    "8 ms", 0.f, { { 0.f, 0.f, 0.f } }, 8, 300, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.7fec56p+6f, -0x1.8p+5f, 0x1p+7f } },
    { { 0x1.333334p+1f, 0.f, 0.f } }
  },
  {
    "25 ms", 0.f, { { 0.f, 0.f, 0.f } }, 25, 300, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.7f4p+6f, -0x1.8p+5f, 0x1p+7f } },
    { { 0x1.ep+2f, 0.f, 0.f } }
  },
  {
    "50 ms", 0.f, { { 0.f, 0.f, 0.f } }, 50, 300, 0,
    { { 0.f, 0.f, 0.f } }, 0.f,
    { { -0x1.7dp+6f, -0x1.8p+5f, 0x1p+7f } },
    { { 0x1.ep+3f, 0.f, 0.f } }
  },
  {
    "ascending", 0.f, { { 0.f, 0.f, 270.f } }, 16, 300, 0,
    { { 0.f, 0.f, 0.f } }, 800.f,
    { { -0x1.7fb15cp+6f, -0x1.8p+5f, 0x1.083afcp+7f } },
    { { 0x1.333334p+2f, 0.f, 0x1.013334p+8f } }
  },
  {
    "near apex", 0.f, { { 0.f, 0.f, 1.f } }, 16, 300, 0,
    { { 0.f, 0.f, 0.f } }, 800.f,
    { { -0x1.7fb15cp+6f, -0x1.8p+5f, 0x1.ff3eacp+6f } },
    { { 0x1.333334p+2f, 0.f, -0x1.79999ap+3f } }
  },
  {
    "descending", 0.f, { { 0.f, 0.f, -200.f } }, 16, 300, 0,
    { { 0.f, 0.f, 0.f } }, 800.f,
    { { -0x1.7fb15cp+6f, -0x1.8p+5f, 0x1.f2617cp+6f } },
    { { 0x1.333334p+2f, 0.f, -0x1.a9999ap+7f } }
  },
  {
    "zero input", 0.f, { { 123.f, -45.f, 50.f } }, 16, 0, 0,
    { { 0.f, 0.f, 0.f } }, 800.f,
    { { -0x1.7820c4p+6f, -0x1.85c29p+5f, 0x1.0130bep+7f } },
    { { 0x1.ecp+6f, -0x1.68p+5f, 0x1.29999ap+5f } }
  }
};

static pm_params_t Race_PmoveQ2AirParams(void) {
  pm_params_t params = Race_PmoveDefaultParams();
  params.gravity = 0.f;
  params.gravity_water = 1.f;
  params.accel_air = 1.f;
  params.friction_air = .125f;
  params.speed_ground = 300.f;
  params.speed_air = 123.f;
  params.speed_water = 400.f;
  params.speed_jump = 270.f;
  params.speed_ducked = 100.f;
  return params;
}

static pm_move_t Race_PmoveQ2AirSetup(const vec3_t origin,
                                      const vec3_t velocity) {
  pm_move_t pm = Race_PmoveSetup(RACE_PMOVE_EMPTY_IDLE);
  pm.s.origin = origin;
  pm.s.velocity = velocity;
  pm.s.flags = 0;
  pm.s.time = 0;
  pm.s.params = Race_PmoveQ2AirParams();
  pm.s.view_offset = Vec3(0.f, 0.f, 22.f);
  memset(&pm.ground, 0, sizeof(pm.ground));
  return pm;
}

static void Race_PmoveQ2AirCommand(pm_move_t *pm, uint16_t msec,
                                   int16_t forward, int16_t right,
                                   int16_t up, const vec3_t angles) {
  pm->cmd = (pm_cmd_t) {
    .msec = msec,
    .angles = angles,
    .forward = forward,
    .right = right,
    .up = up
  };
  Pm_Move(pm);
}

START_TEST(_Race_PmoveQ2AirLegacyCases) {
  Race_PmoveUseQ2TestPhysics();
  race_pmove_feet_current = false;

  for (size_t i = 0;
       i < sizeof(race_pmove_q2_air_cases) /
           sizeof(*race_pmove_q2_air_cases); i++) {
    const race_pmove_q2_air_case_t *air = race_pmove_q2_air_cases + i;
    pm_move_t pm = Race_PmoveQ2AirSetup(Vec3(-96.f, -48.f, 128.f),
                                        air->velocity);
    pm.s.params.gravity = air->gravity;
    const pm_params_t params = pm.s.params;

    Pm_SetQ2AirWishspeedCapForTest(air->air_wishspeed_cap);
    Race_PmoveQ2AirCommand(&pm, air->msec, air->forward, air->right, 0,
                           air->angles);
    Pm_SetQ2AirWishspeedCapForTest(0.f);

    Race_PmoveAssertVec3(air->name, "origin", pm.s.origin,
                         air->expected_origin);
    Race_PmoveAssertVec3(air->name, "velocity", pm.s.velocity,
                         air->expected_velocity);
    ck_assert_uint_eq(pm.s.flags, 0u);
    ck_assert_uint_eq(pm.s.time, 0u);
    ck_assert_int_eq(pm.water_level, WATER_NONE);
    ck_assert_ptr_null(pm.ground.ent);
    ck_assert_msg(memcmp(&pm.s.params, &params, sizeof(params)) == 0,
                  "%s: Q2 air move changed replicated parameters", air->name);
  }
} END_TEST

START_TEST(_Race_PmoveQ2AirTransitionAndCurrent) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);

  pm_move_t jump = Race_PmoveQ2AirSetup(Vec3(-96.f, -48.f, 24.f),
                                        Vec3_Zero());
  race_pmove_world = RACE_PMOVE_WORLD_FLAT;
  jump.s.flags = PMF_ON_GROUND;
  const pm_params_t jump_params = jump.s.params;
  Race_PmoveQ2AirCommand(&jump, 16, 300, 0, 10, Vec3_Zero());

  Race_PmoveAssertVec3("jump command air", "origin", jump.s.origin,
                       Vec3(-0x1.7fb15cp+6f, -0x1.8p+5f,
                            0x1.c51eb8p+4f));
  Race_PmoveAssertVec3("jump command air", "velocity", jump.s.velocity,
                       Vec3(0x1.333334p+2f, 0.f, 0x1.0ep+8f));
  ck_assert(jump.s.flags & PMF_JUMPED);
  ck_assert(jump.s.flags & PMF_JUMP_HELD);
  ck_assert(!(jump.s.flags & PMF_ON_GROUND));
  ck_assert(!(jump.s.flags & PMF_TIME_LAND));
  ck_assert_ptr_null(jump.ground.ent);
  ck_assert_msg(memcmp(&jump.s.params, &jump_params,
                       sizeof(jump_params)) == 0,
                "jump-command air path changed replicated parameters");

  race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
  race_pmove_current_contents = CONTENTS_CURRENT_90;
  race_pmove_feet_current = true;
  pm_move_t current = Race_PmoveQ2AirSetup(Vec3(-96.f, -48.f, 24.f),
                                           Vec3_Zero());
  const pm_params_t current_params = current.s.params;
  Race_PmoveQ2AirCommand(&current, 16, 300, 0, 0, Vec3_Zero());
  race_pmove_feet_current = false;

  Race_PmoveAssertVec3("feet current", "origin", current.s.origin,
                       Vec3(-0x1.7fd0dp+6f, -0x1.7f822cp+5f,
                            0x1.8p+4f));
  Race_PmoveAssertVec3("feet current", "velocity", current.s.velocity,
                       Vec3(0x1.70a3d8p+1f, 0x1.eb852p+1f, 0.f));
  ck_assert_int_eq(current.water_level, WATER_FEET);
  ck_assert(current.water_type & CONTENTS_WATER);
  ck_assert(current.water_type & CONTENTS_CURRENT_90);
  ck_assert_msg(memcmp(&current.s.params, &current_params,
                       sizeof(current_params)) == 0,
                "feet-current air path changed replicated parameters");

  race_pmove_current_contents = CONTENTS_CURRENT_UP;
  race_pmove_feet_current = true;
  pm_move_t up_current = Race_PmoveQ2AirSetup(Vec3(-96.f, -48.f, 24.f),
                                              Vec3_Zero());
  const pm_params_t up_current_params = up_current.s.params;
  Race_PmoveQ2AirCommand(&up_current, 16, 0, 0, 0, Vec3_Zero());
  race_pmove_feet_current = false;
  race_pmove_current_contents = CONTENTS_CURRENT_90;

  Race_PmoveAssertVec3("feet current up", "origin", up_current.s.origin,
                       Vec3(-0x1.8p+6f, -0x1.8p+5f, 0x1.813a92p+4f));
  Race_PmoveAssertVec3("feet current up", "velocity", up_current.s.velocity,
                       Vec3(0.f, 0.f, 0x1.333334p+2f));
  ck_assert_msg(memcmp(&up_current.s.params, &up_current_params,
                       sizeof(up_current_params)) == 0,
                "vertical-current air path changed replicated parameters");
} END_TEST

#define RACE_PMOVE_Q2_AIR_STREAM_COMMANDS 9

typedef struct {
  uint16_t msec;
  int16_t forward;
  int16_t right;
  vec3_t angles;
  vec3_t expected_origin;
  vec3_t expected_velocity;
} race_pmove_q2_air_stream_t;

static const race_pmove_q2_air_stream_t race_pmove_q2_air_stream[] = {
  {
    8, 300, 300, { { 15.f, 30.f, 0.f } },
    { { -0x1.fca3d8p+8f, -0x1.01p+8f, 0x1.00b852p+9f } },
    { { 0x1.a4p+8f, -0x1.f4p+6f, 0x1.68p+7f } }
  },
  {
    16, 300, 300, { { 15.f, 35.f, 0.f } },
    { { -0x1.f5eb86p+8f, -0x1.03p+8f, 0x1.0228f6p+9f } },
    { { 0x1.a4p+8f, -0x1.f4p+6f, 0x1.68p+7f } }
  },
  {
    25, 0, 300, { { -30.f, 45.f, 0.f } },
    { { -0x1.eb6b86p+8f, -0x1.062p+8f, 0x1.0468f6p+9f } },
    { { 0x1.a4p+8f, -0x1.f4p+6f, 0x1.68p+7f } }
  },
  {
    50, -300, 300, { { 0.f, 60.f, 0.f } },
    { { -0x1.d639d4p+8f, -0x1.0d1976p+8f, 0x1.08e8f6p+9f } },
    { { 0x1.a7e1dep+8f, -0x1.16fa5p+7f, 0x1.68p+7f } }
  },
  {
    8, -300, 0, { { 45.f, 75.f, 0.f } },
    { { -0x1.d2d6f2p+8f, -0x1.0e3bb8p+8f, 0x1.09a148p+9f } },
    { { 0x1.a74844p+8f, -0x1.1b74ccp+7f, 0x1.68p+7f } }
  },
  {
    16, 300, -300, { { 10.f, 90.f, 0.f } },
    { { -0x1.cc1f18p+8f, -0x1.107258p+8f, 0x1.0b11ecp+9f } },
    { { 0x1.a3e2a4p+8f, -0x1.14ac7cp+7f, 0x1.68p+7f } }
  },
  {
    25, 0, 0, { { 0.f, 105.f, 0.f } },
    { { -0x1.c19fd4p+8f, -0x1.13e7b4p+8f, 0x1.0d51ecp+9f } },
    { { 0x1.a3e2a4p+8f, -0x1.14ac7cp+7f, 0x1.68p+7f } }
  },
  {
    50, 300, 300, { { -45.f, 120.f, 0.f } },
    { { -0x1.ac6c66p+8f, -0x1.1a19d8p+8f, 0x1.11d1ecp+9f } },
    { { 0x1.a804a2p+8f, -0x1.efab78p+6f, 0x1.68p+7f } }
  },
  {
    8, 300, -300, { { 20.f, 135.f, 0.f } },
    { { -0x1.a90cecp+8f, -0x1.1b17a4p+8f, 0x1.128a3ep+9f } },
    { { 0x1.a59e3cp+8f, -0x1.efb3cep+6f, 0x1.68p+7f } }
  }
};

START_TEST(_Race_PmoveQ2AirLongStreamGameCgame) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);
  race_pmove_feet_current = false;

  pm_move_t game = Race_PmoveQ2AirSetup(Vec3(-512.f, -256.f, 512.f),
                                        Vec3(420.f, -125.f, 180.f));
  pm_move_t cgame = Race_PmoveQ2AirSetup(Vec3(-512.f, -256.f, 512.f),
                                         Vec3(420.f, -125.f, 180.f));
  const pm_params_t params = game.s.params;

  for (size_t i = 0; i < RACE_PMOVE_Q2_AIR_STREAM_COMMANDS; i++) {
    const race_pmove_q2_air_stream_t *command = race_pmove_q2_air_stream + i;
    Race_PmoveQ2AirCommand(&game, command->msec, command->forward,
                           command->right, 0, command->angles);
    Race_PmoveQ2AirCommand(&cgame, command->msec, command->forward,
                           command->right, 0, command->angles);

    char name[64];
    snprintf(name, sizeof(name), "Q2 air stream command %zu", i + 1u);
    Race_PmoveAssertVec3(name, "origin", game.s.origin,
                         command->expected_origin);
    Race_PmoveAssertVec3(name, "velocity", game.s.velocity,
                         command->expected_velocity);
    ck_assert_msg(memcmp(&game.s, &cgame.s, sizeof(game.s)) == 0,
                  "%s: GAME and CGAME pm_state_t differ", name);
    ck_assert_msg(memcmp(&game, &cgame, sizeof(game)) == 0,
                  "%s: GAME and CGAME pm_move_t differ", name);
    ck_assert_msg(memcmp(&game.s.params, &params, sizeof(params)) == 0,
                  "%s: Q2 air stream changed replicated parameters", name);
  }
} END_TEST

START_TEST(_Race_PmoveQ2AiDirectAir) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);

  pm_move_t ai = Race_PmoveQ2AirSetup(Vec3(-96.f, -48.f, 128.f),
                                      Vec3(0.f, 500.f, 50.f));
  const pm_params_t params = ai.s.params;

  // AI calls the shared mover directly; no G_PrepareMove dependency is used.
  ai.cmd = (pm_cmd_t) {
    .msec = 16,
    .forward = 300
  };
  Pm_Move(&ai);

  Race_PmoveAssertVec3("direct AI Q2 air", "origin", ai.s.origin,
                       Vec3(-0x1.7fb15cp+6f, -0x1.4p+5f,
                            0x1.01999ap+7f));
  Race_PmoveAssertVec3("direct AI Q2 air", "velocity", ai.s.velocity,
                       Vec3(0x1.333334p+2f, 0x1.f4p+8f, 0x1.9p+5f));
  ck_assert_msg(memcmp(&ai.s.params, &params, sizeof(params)) == 0,
                "direct AI Q2 air changed replicated parameters");
} END_TEST

typedef enum {
  RACE_PMOVE_Q2_COLLISION_EMPTY,
  RACE_PMOVE_Q2_COLLISION_PLANES,
  RACE_PMOVE_Q2_COLLISION_OPPOSING,
  RACE_PMOVE_Q2_COLLISION_ALL_SOLID,
  RACE_PMOVE_Q2_COLLISION_START_SOLID
} race_pmove_q2_collision_world_t;

typedef enum {
  RACE_PMOVE_Q2_COLLISION_REVERSE,
  RACE_PMOVE_Q2_COLLISION_GLANCE_8,
  RACE_PMOVE_Q2_COLLISION_GLANCE_16,
  RACE_PMOVE_Q2_COLLISION_GLANCE_25,
  RACE_PMOVE_Q2_COLLISION_GLANCE_50,
  RACE_PMOVE_Q2_COLLISION_SHALLOW,
  RACE_PMOVE_Q2_COLLISION_HIGH_SPEED,
  RACE_PMOVE_Q2_COLLISION_NEGATIVE,
  RACE_PMOVE_Q2_COLLISION_CORNER_90,
  RACE_PMOVE_Q2_COLLISION_CORNER_ACUTE,
  RACE_PMOVE_Q2_COLLISION_CREASE,
  RACE_PMOVE_Q2_COLLISION_OPPOSING_PLANES,
  RACE_PMOVE_Q2_COLLISION_THREE_PLANES,
  RACE_PMOVE_Q2_COLLISION_ALL_SOLID_CASE,
  RACE_PMOVE_Q2_COLLISION_START_SOLID_CASE,
  RACE_PMOVE_Q2_COLLISION_CASE_TOTAL
} race_pmove_q2_collision_case_id_t;

typedef struct {
  vec3_t normal;
  float dist;
  int32_t entity;
} race_pmove_q2_collision_plane_t;

typedef struct {
  float fraction;
  bool start_solid;
  bool all_solid;
  vec3_t normal;
  int32_t entity;
} race_pmove_q2_collision_trace_t;

typedef struct {
  const char *name;
  vec3_t origin;
  vec3_t velocity;
  uint16_t msec;
  vec3_t expected_origin;
  vec3_t expected_velocity;
  int32_t num_touched;
  int32_t touched[3];
  int32_t num_traces;
  float trace_fractions[4];
  uint8_t start_solid_mask;
  uint8_t all_solid_mask;
  int32_t trace_entities[4];
} race_pmove_q2_collision_case_t;

static race_pmove_q2_collision_world_t race_pmove_q2_collision_world;
static race_pmove_q2_collision_plane_t race_pmove_q2_collision_planes[3];
static size_t race_pmove_q2_collision_num_planes;
static uint8_t race_pmove_q2_collision_entities[8];
static race_pmove_q2_collision_trace_t race_pmove_q2_game_traces[16];
static race_pmove_q2_collision_trace_t race_pmove_q2_cgame_traces[16];
static size_t race_pmove_q2_game_num_traces;
static size_t race_pmove_q2_cgame_num_traces;
static int32_t race_pmove_q2_game_step_up_traces;
static int32_t race_pmove_q2_cgame_step_up_traces;
static vec3_t race_pmove_q2_game_move_start_origin;
static vec3_t race_pmove_q2_cgame_move_start_origin;

static int32_t Race_PmoveQ2CollisionEntityId(const void *entity) {
  for (size_t i = 0; i < sizeof(race_pmove_q2_collision_entities); i++) {
    if (entity == race_pmove_q2_collision_entities + i) {
      return (int32_t) i + 1;
    }
  }
  return 0;
}

static void Race_PmoveQ2CollisionSelectTrace(cm_trace_t *trace,
                                              cm_trace_t candidate) {
  if (candidate.start_solid || candidate.fraction < trace->fraction) {
    *trace = candidate;
  }
}

static void Race_PmoveQ2CollisionTracePlane(
    cm_trace_t *trace, const vec3_t start, const vec3_t end,
    race_pmove_q2_collision_plane_t plane) {
  const float start_distance = Vec3_Dot(start, plane.normal) - plane.dist;
  const float end_distance = Vec3_Dot(end, plane.normal) - plane.dist;
  cm_trace_t candidate = {
    .fraction = 1.f,
    .end = end,
    .plane = {
      .normal = plane.normal,
      .dist = plane.dist
    },
    .contents = CONTENTS_SOLID,
    .ent = race_pmove_q2_collision_entities + plane.entity - 1
  };

  if (start_distance < 0.f) {
    candidate.start_solid = true;
    candidate.all_solid = end_distance < 0.f;
    candidate.fraction = 0.f;
    candidate.end = start;
    Race_PmoveQ2CollisionSelectTrace(trace, candidate);
  } else if (end_distance < 0.f) {
    candidate.fraction = start_distance / (start_distance - end_distance);
    candidate.end = Vec3_Fmaf(start, candidate.fraction,
                              Vec3_Subtract(end, start));
    Race_PmoveQ2CollisionSelectTrace(trace, candidate);
  }
}

static cm_trace_t Race_PmoveQ2CollisionTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds,
    race_pmove_q2_collision_trace_t *trace_log, size_t *num_traces,
    int32_t *step_up_traces, const vec3_t move_start_origin) {
  (void) bounds;
  cm_trace_t trace = {
    .fraction = 1.f,
    .end = end
  };
  const vec3_t delta = Vec3_Subtract(end, start);

  const vec3_t step_occupancy = Vec3_Fmaf(move_start_origin, 18.f, Vec3_Up());
  if (Vec3_Length(delta) < .0001f &&
      Vec3_Distance(start, step_occupancy) < .0001f) {
    (*step_up_traces)++;
    return (cm_trace_t) {
      .all_solid = true,
      .start_solid = true,
      .fraction = 0.f,
      .end = start
    };
  }

  // Current Quetoo's four-unit ladder probe is outside this slide fixture.
  if (fabsf(delta.x - 4.f) < .0001f && fabsf(delta.y) < .0001f &&
      fabsf(delta.z) < .0001f) {
    return trace;
  }

  // Ignore duck and ground probes so these fixtures isolate slide collision.
  if (Vec3_Length(delta) <= 2.f) {
    return trace;
  }

  switch (race_pmove_q2_collision_world) {
    case RACE_PMOVE_Q2_COLLISION_PLANES:
      for (size_t i = 0; i < race_pmove_q2_collision_num_planes; i++) {
        Race_PmoveQ2CollisionTracePlane(
          &trace, start, end, race_pmove_q2_collision_planes[i]);
      }
      break;

    case RACE_PMOVE_Q2_COLLISION_OPPOSING:
      if (delta.x > 0.f) {
        Race_PmoveQ2CollisionTracePlane(
          &trace, start, end,
          (race_pmove_q2_collision_plane_t) {
            Vec3(-1.f, 0.f, 0.f), 0.f, 1
          });
      } else if (delta.x < 0.f) {
        Race_PmoveQ2CollisionTracePlane(
          &trace, start, end,
          (race_pmove_q2_collision_plane_t) {
            Vec3(1.f, 0.f, 0.f), 0.f, 2
          });
      }
      break;

    case RACE_PMOVE_Q2_COLLISION_ALL_SOLID:
      trace = (cm_trace_t) {
        .all_solid = true,
        .start_solid = true,
        .fraction = 0.f,
        .end = start,
        .contents = CONTENTS_SOLID,
        .ent = race_pmove_q2_collision_entities
      };
      break;

    case RACE_PMOVE_Q2_COLLISION_START_SOLID:
      trace = (cm_trace_t) {
        .start_solid = true,
        .fraction = 0.f,
        .end = start,
        .plane = {
          .normal = Vec3(-1.f, 0.f, 0.f)
        },
        .contents = CONTENTS_SOLID,
        .ent = race_pmove_q2_collision_entities
      };
      break;

    case RACE_PMOVE_Q2_COLLISION_EMPTY:
      break;
  }

  if ((trace.fraction < 1.f || trace.start_solid || trace.all_solid) &&
      *num_traces < 16u) {
    trace_log[(*num_traces)++] = (race_pmove_q2_collision_trace_t) {
      .fraction = trace.fraction,
      .start_solid = trace.start_solid,
      .all_solid = trace.all_solid,
      .normal = trace.plane.normal,
      .entity = Race_PmoveQ2CollisionEntityId(trace.ent)
    };
  }

  return trace;
}

static cm_trace_t Race_PmoveQ2CollisionGameTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds) {
  return Race_PmoveQ2CollisionTrace(
    start, end, bounds, race_pmove_q2_game_traces,
    &race_pmove_q2_game_num_traces, &race_pmove_q2_game_step_up_traces,
    race_pmove_q2_game_move_start_origin);
}

static cm_trace_t Race_PmoveQ2CollisionCgameTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds) {
  return Race_PmoveQ2CollisionTrace(
    start, end, bounds, race_pmove_q2_cgame_traces,
    &race_pmove_q2_cgame_num_traces, &race_pmove_q2_cgame_step_up_traces,
    race_pmove_q2_cgame_move_start_origin);
}

static void Race_PmoveQ2CollisionSetPlane(size_t index, vec3_t normal,
                                           float dist, int32_t entity) {
  race_pmove_q2_collision_planes[index] =
    (race_pmove_q2_collision_plane_t) { normal, dist, entity };
  if (race_pmove_q2_collision_num_planes <= index) {
    race_pmove_q2_collision_num_planes = index + 1u;
  }
}

static void Race_PmoveQ2CollisionSetCase(
    race_pmove_q2_collision_case_id_t id) {
  race_pmove_q2_collision_world = RACE_PMOVE_Q2_COLLISION_PLANES;
  race_pmove_q2_collision_num_planes = 0u;

  switch (id) {
    case RACE_PMOVE_Q2_COLLISION_REVERSE:
    case RACE_PMOVE_Q2_COLLISION_GLANCE_8:
    case RACE_PMOVE_Q2_COLLISION_GLANCE_16:
    case RACE_PMOVE_Q2_COLLISION_GLANCE_25:
    case RACE_PMOVE_Q2_COLLISION_GLANCE_50:
    case RACE_PMOVE_Q2_COLLISION_SHALLOW:
    case RACE_PMOVE_Q2_COLLISION_HIGH_SPEED:
      Race_PmoveQ2CollisionSetPlane(0, Vec3(-1.f, 0.f, 0.f), 0.f, 1);
      break;

    case RACE_PMOVE_Q2_COLLISION_NEGATIVE:
      Race_PmoveQ2CollisionSetPlane(0, Vec3(-1.f, 0.f, 0.f), 256.f, 1);
      break;

    case RACE_PMOVE_Q2_COLLISION_CORNER_90:
      Race_PmoveQ2CollisionSetPlane(0, Vec3(-1.f, 0.f, 0.f), 0.f, 1);
      Race_PmoveQ2CollisionSetPlane(1, Vec3(0.f, -1.f, 0.f), 0.f, 2);
      break;

    case RACE_PMOVE_Q2_COLLISION_CORNER_ACUTE:
      Race_PmoveQ2CollisionSetPlane(0, Vec3(-1.f, 0.f, 0.f), 0.f, 1);
      Race_PmoveQ2CollisionSetPlane(
        1, Vec3_Normalize(Vec3(-1.f, -1.f, 0.f)), 0.f, 2);
      break;

    case RACE_PMOVE_Q2_COLLISION_CREASE:
      Race_PmoveQ2CollisionSetPlane(0, Vec3(-1.f, 0.f, 0.f), 0.f, 1);
      Race_PmoveQ2CollisionSetPlane(1, Vec3(0.f, 0.f, -1.f), 0.f, 3);
      break;

    case RACE_PMOVE_Q2_COLLISION_OPPOSING_PLANES:
      race_pmove_q2_collision_world = RACE_PMOVE_Q2_COLLISION_OPPOSING;
      break;

    case RACE_PMOVE_Q2_COLLISION_THREE_PLANES:
      Race_PmoveQ2CollisionSetPlane(0, Vec3(-1.f, 0.f, 0.f), 0.f, 1);
      Race_PmoveQ2CollisionSetPlane(1, Vec3(0.f, -1.f, 0.f), 0.f, 2);
      Race_PmoveQ2CollisionSetPlane(2, Vec3(0.f, 0.f, -1.f), 0.f, 3);
      break;

    case RACE_PMOVE_Q2_COLLISION_ALL_SOLID_CASE:
      race_pmove_q2_collision_world = RACE_PMOVE_Q2_COLLISION_ALL_SOLID;
      break;

    case RACE_PMOVE_Q2_COLLISION_START_SOLID_CASE:
      race_pmove_q2_collision_world = RACE_PMOVE_Q2_COLLISION_START_SOLID;
      break;

    case RACE_PMOVE_Q2_COLLISION_CASE_TOTAL:
      abort();
  }
}

static const race_pmove_q2_collision_case_t race_pmove_q2_collision_cases[] = {
  {
    "perpendicular reverse stop",
    { { -8.f, -4.f, 64.f } }, { { 1000.f, 0.f, 0.f } }, 16,
    { { 0.f, -4.f, 64.f } }, { { 0.f, 0.f, 0.f } },
    1, { 1 }, 1, { 0x1p-1f }, 0x0, 0x0, { 1 }
  },
  {
    "glancing wall 8 ms",
    { { -2.f, -10.f, 64.f } }, { { 500.f, 200.f, 0.f } }, 8,
    { { -0x1.47ae16p-6f, -0x1.0cccccp+3f, 0x1p+6f } },
    { { -0x1.4p+2f, 0x1.9p+7f, 0.f } },
    1, { 1 }, 1, { 0x1p-1f }, 0x0, 0x0, { 1 }
  },
  {
    "glancing wall 16 ms",
    { { -4.f, -10.f, 64.f } }, { { 500.f, 200.f, 0.f } }, 16,
    { { -0x1.47ae16p-5f, -0x1.b33332p+2f, 0x1p+6f } },
    { { -0x1.4p+2f, 0x1.9p+7f, 0.f } },
    1, { 1 }, 1, { 0x1p-1f }, 0x0, 0x0, { 1 }
  },
  {
    "glancing wall 25 ms",
    { { -6.25f, -10.f, 64.f } }, { { 500.f, 200.f, 0.f } }, 25,
    { { -0x1p-4f, -0x1.4p+2f, 0x1p+6f } },
    { { -0x1.4p+2f, 0x1.9p+7f, 0.f } },
    1, { 1 }, 1, { 0x1p-1f }, 0x0, 0x0, { 1 }
  },
  {
    "glancing wall 50 ms",
    { { -12.5f, -10.f, 64.f } }, { { 500.f, 200.f, 0.f } }, 50,
    { { -0x1p-3f, 0x1.4p-24f, 0x1p+6f } },
    { { -0x1.4p+2f, 0x1.9p+7f, 0.f } },
    1, { 1 }, 1, { 0x1p-1f }, 0x0, 0x0, { 1 }
  },
  {
    "shallow repeated wall",
    { { -1.f, -20.f, 64.f } }, { { 100.f, 1000.f, 0.f } }, 25,
    { { 0x1p-26f, -0x1.4p+3f, 0x1p+6f } },
    { { 0.f, 0x1.f4p+9f, 0.f } },
    1, { 1 }, 3, { 0x1.99999ap-2f, 0.f, 0.f }, 0x6, 0x4,
    { 1, 1, 1 }
  },
  {
    "high-speed wall",
    { { -32.f, -20.f, 64.f } }, { { 4000.f, 1000.f, 0.f } }, 16,
    { { -0x1.47ae16p-2f, -0x1.fffffcp+1f, 0x1p+6f } },
    { { -0x1.4p+5f, 0x1.f4p+9f, 0.f } },
    1, { 1 }, 1, { 0x1p-1f }, 0x0, 0x0, { 1 }
  },
  {
    "negative-coordinate wall",
    { { -264.f, -128.f, 64.f } }, { { 1000.f, 400.f, 0.f } }, 16,
    { { -0x1.00147ap+8f, -0x1.e66668p+6f, 0x1p+6f } },
    { { -0x1.4p+3f, 0x1.9p+8f, 0.f } },
    1, { 1 }, 1, { 0x1p-1f }, 0x0, 0x0, { 1 }
  },
  {
    "90-degree corner",
    { { -5.f, -5.f, 64.f } }, { { 400.f, 400.f, 0.f } }, 25,
    { { 0.f, 0.f, 64.f } }, { { 0.f, 0.f, 0.f } },
    2, { 1, 2 }, 2, { 0x1p-1f, 0.f }, 0x0, 0x0, { 1, 2 }
  },
  {
    "acute corner",
    { { -5.f, -5.f, 64.f } }, { { 400.f, 400.f, 100.f } }, 25,
    { { -0x1.433334p+1f, 0x1.3ccccep+1f, 0x1.0ap+6f } },
    { { -0x1.94p+7f, 0x1.8cp+7f, 0x1.9p+6f } },
    2, { 1, 2 }, 2, { 0x1p-1f, 0.f }, 0x0, 0x0, { 1, 2 }
  },
  {
    "two-plane crease",
    { { -50.f, 10.f, -50.f } }, { { 4000.f, -300.f, 4000.f } }, 25,
    { { 0.f, 0x1.4p+1f, 0.f } }, { { 0.f, -0x1.2cp+8f, 0.f } },
    2, { 1, 3 }, 2, { 0x1p-1f, 0.f }, 0x0, 0x0, { 1, 3 }
  },
  {
    "opposing planes retain tangent",
    { { -5.f, 0.f, 64.f } }, { { 400.f, 300.f, 0.f } }, 25,
    { { 0.f, 0x1.ep+2f, 0x1p+6f } }, { { 0.f, 0x1.2cp+8f, 0.f } },
    2, { 1, 2 }, 2, { 0x1p-1f, 0.f }, 0x0, 0x0, { 1, 2 }
  },
  {
    "three-plane impossible motion",
    { { -5.f, -5.f, -5.f } }, { { 400.f, 400.f, 400.f } }, 25,
    { { 0.f, 0.f, 0.f } }, { { 0.f, 0.f, 0.f } },
    3, { 1, 2, 3 }, 3, { 0x1p-1f, 0.f, 0.f }, 0x0, 0x0,
    { 1, 2, 3 }
  },
  {
    "unrecoverable all-solid",
    { { -100.f, -100.f, 64.f } }, { { 300.f, 200.f, -50.f } }, 16,
    { { -100.f, -100.f, 64.f } }, { { 300.f, 200.f, 0.f } },
    0, { 0 }, 1, { 0.f }, 0x1, 0x1, { 1 }
  },
  {
    "start-solid four-bump recovery attempt",
    { { -100.f, -100.f, 64.f } }, { { 300.f, 200.f, 0.f } }, 16,
    { { -100.f, -100.f, 64.f } }, { { 0.f, 200.f, 0.f } },
    1, { 1 }, 4, { 0.f, 0.f, 0.f, 0.f }, 0xf, 0x0,
    { 1, 1, 1, 1 }
  }
};

static vec3_t Race_PmoveQ2CollisionExpectedNormal(int32_t entity) {
  if (race_pmove_q2_collision_world == RACE_PMOVE_Q2_COLLISION_ALL_SOLID) {
    return Vec3_Zero();
  }
  if (race_pmove_q2_collision_world == RACE_PMOVE_Q2_COLLISION_START_SOLID ||
      (race_pmove_q2_collision_world == RACE_PMOVE_Q2_COLLISION_OPPOSING &&
       entity == 1)) {
    return Vec3(-1.f, 0.f, 0.f);
  }
  if (race_pmove_q2_collision_world == RACE_PMOVE_Q2_COLLISION_OPPOSING) {
    return Vec3(1.f, 0.f, 0.f);
  }

  for (size_t i = 0; i < race_pmove_q2_collision_num_planes; i++) {
    if (race_pmove_q2_collision_planes[i].entity == entity) {
      return race_pmove_q2_collision_planes[i].normal;
    }
  }
  abort();
}

static pm_move_t Race_PmoveQ2CollisionSetup(
    const race_pmove_q2_collision_case_t *collision,
    cm_trace_t (*Trace)(vec3_t, vec3_t, box3_t)) {
  pm_move_t pm = Race_PmoveQ2AirSetup(collision->origin,
                                      collision->velocity);
  pm.Trace = Trace;
  pm.cmd.msec = collision->msec;
  return pm;
}

static void Race_PmoveQ2CollisionAssertResult(
    const race_pmove_q2_collision_case_t *collision, const pm_move_t *pm,
    const pm_params_t *params) {
  Race_PmoveAssertVec3(collision->name, "origin", pm->s.origin,
                       collision->expected_origin);
  Race_PmoveAssertVec3(collision->name, "velocity", pm->s.velocity,
                       collision->expected_velocity);
  ck_assert_msg(pm->s.flags == 0u, "%s: flags were 0x%04x",
                collision->name, pm->s.flags);
  ck_assert_msg(pm->s.time == 0u, "%s: timer was %u",
                collision->name, pm->s.time);
  ck_assert_ptr_null(pm->ground.ent);
  ck_assert_int_eq(pm->water_level, WATER_NONE);
  ck_assert_int_eq(pm->water_type, 0);
  Race_PmoveAssertFloat(collision->name, "step", pm->step, 0.f);
  Race_PmoveAssertBounds(collision->name, pm->bounds,
                         Pm_PlayerBounds(false));
  ck_assert_msg(pm->num_touched == collision->num_touched,
                "%s: touched count was %d, expected %d", collision->name,
                pm->num_touched, collision->num_touched);
  for (int32_t i = 0; i < pm->num_touched; i++) {
    ck_assert_msg(Race_PmoveQ2CollisionEntityId(pm->touched[i].ent) ==
                    collision->touched[i],
                  "%s: touched[%d] order mismatch", collision->name, i);
  }
  ck_assert_msg(memcmp(&pm->s.params, params, sizeof(*params)) == 0,
                "%s: collision changed replicated parameters",
                collision->name);
}

static void Race_PmoveQ2CollisionAssertTraces(
    const race_pmove_q2_collision_case_t *collision,
    const race_pmove_q2_collision_trace_t *traces, size_t num_traces) {
  ck_assert_msg(num_traces == (size_t) collision->num_traces,
                "%s: trace count was %zu, expected %d; fractions %a %a; step traces %d",
                collision->name, num_traces, collision->num_traces,
                num_traces > 0u ? (double) traces[0].fraction : -1.0,
                num_traces > 1u ? (double) traces[1].fraction : -1.0,
                race_pmove_q2_game_step_up_traces);
  for (int32_t i = 0; i < collision->num_traces; i++) {
    char name[96];
    snprintf(name, sizeof(name), "%s trace %d", collision->name, i + 1);
    Race_PmoveAssertFloat(name, "fraction", traces[i].fraction,
                          collision->trace_fractions[i]);
    ck_assert_msg(traces[i].start_solid ==
                    !!(collision->start_solid_mask & (1u << i)),
                  "%s: start-solid mismatch", name);
    ck_assert_msg(traces[i].all_solid ==
                    !!(collision->all_solid_mask & (1u << i)),
                  "%s: all-solid mismatch", name);
    ck_assert_msg(traces[i].entity == collision->trace_entities[i],
                  "%s: entity was %d, expected %d", name,
                  traces[i].entity, collision->trace_entities[i]);
    Race_PmoveAssertVec3(name, "normal", traces[i].normal,
      Race_PmoveQ2CollisionExpectedNormal(collision->trace_entities[i]));
  }
}

START_TEST(_Race_PmoveQ2ClipVelocityLegacyCases) {
  static const struct {
    vec3_t in;
    vec3_t expected;
  } cases[] = {
    { { { 1000.f, 400.f, 0.f } }, { { -10.f, 400.f, 0.f } } },
    { { { 100.f, 1000.f, 0.f } }, { { -1.f, 1000.f, 0.f } } },
    { { { -5.f, 7.f, .05f } }, { { 0.f, 7.f, 0.f } } },
    { { { 5.f, 7.f, -.05f } }, { { 0.f, 7.f, 0.f } } }
  };
  const vec3_t normal = Vec3(-1.f, 0.f, 0.f);

  for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
    char name[64];
    snprintf(name, sizeof(name), "Q2 clip legacy case %zu", i + 1u);
    Race_PmoveAssertVec3(name, "velocity",
      Pm_Q2ClipVelocityForTest(cases[i].in, normal), cases[i].expected);
  }
} END_TEST

START_TEST(_Race_PmoveQ2CollisionLegacyCases) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);
  race_pmove_feet_current = false;

  ck_assert_uint_eq(sizeof(race_pmove_q2_collision_cases) /
                      sizeof(*race_pmove_q2_collision_cases),
                    RACE_PMOVE_Q2_COLLISION_CASE_TOTAL);

  for (race_pmove_q2_collision_case_id_t id =
         RACE_PMOVE_Q2_COLLISION_REVERSE;
       id < RACE_PMOVE_Q2_COLLISION_CASE_TOTAL; id++) {
    const race_pmove_q2_collision_case_t *collision =
      race_pmove_q2_collision_cases + id;
    Race_PmoveQ2CollisionSetCase(id);
    race_pmove_q2_game_num_traces = 0u;
    race_pmove_q2_game_step_up_traces = 0;
    pm_move_t pm = Race_PmoveQ2CollisionSetup(
      collision, Race_PmoveQ2CollisionGameTrace);
    race_pmove_q2_game_move_start_origin = pm.s.origin;
    const pm_params_t params = pm.s.params;
    Pm_Move(&pm);

    Race_PmoveQ2CollisionAssertResult(collision, &pm, &params);
    Race_PmoveQ2CollisionAssertTraces(
      collision, race_pmove_q2_game_traces, race_pmove_q2_game_num_traces);
    ck_assert_msg(race_pmove_q2_game_step_up_traces == 1,
                  "%s: Q2 step occupancy was not isolated",
                  collision->name);
  }
} END_TEST

typedef struct {
  uint16_t msec;
  int16_t forward;
  vec3_t expected_origin;
  vec3_t expected_velocity;
  int32_t num_touched;
  int32_t touched[2];
  int32_t num_traces;
  float trace_fractions[2];
} race_pmove_q2_collision_stream_t;

static const race_pmove_q2_collision_stream_t
race_pmove_q2_collision_stream[] = {
  {
    8, 0,
    { { -0x1p+4f, -0x1.b9999ap+4f, -0x1.333334p+5f } },
    { { 0x1.f4p+8f, 0x1.2cp+8f, 0x1.9p+7f } }, 0, { 0 }, 0, { 0.f }
  },
  {
    25, 0,
    { { -0x1.42147ap+3f, -0x1.41999ap+4f, -0x1.0b3334p+5f } },
    { { -0x1.4p+2f, 0x1.2cp+8f, 0x1.9p+7f } }, 1, { 1 }, 1,
    { 0x1.eb851ep-2f }
  },
  {
    50, 300,
    { { -0x1.4p+3f, -0x1.247cfcp+4f, -0x1.766668p+4f } },
    { { 0.f, -0x1.8p+1f, 0x1.9p+7f } }, 2, { 1, 2 }, 2,
    { 0x1.0a3dp-3f, 0.f }
  },
  {
    8, 0,
    { { -0x1.4p+3f, -0x1.24df4ap+4f, -0x1.5ccccep+4f } },
    { { 0.f, -0x1.8p+1f, 0x1.9p+7f } }, 0, { 0 }, 0, { 0.f }
  },
  {
    16, 0,
    { { -0x1.4p+3f, -0x1.25a3e6p+4f, -0x1.29999ap+4f } },
    { { 0.f, -0x1.8p+1f, 0x1.9p+7f } }, 0, { 0 }, 0, { 0.f }
  },
  {
    25, 0,
    { { -0x1.4p+3f, -0x1.26d71ap+4f, -0x1.b33334p+3f } },
    { { 0.f, -0x1.8p+1f, 0x1.9p+7f } }, 0, { 0 }, 0, { 0.f }
  },
  {
    50, 0,
    { { -0x1.4p+3f, -0x1.293d8p+4f, -0x1.cccccep+1f } },
    { { 0.f, -0x1.8p+1f, 0x1.9p+7f } }, 0, { 0 }, 0, { 0.f }
  },
  {
    16, 0,
    { { -0x1.4p+3f, -0x1.2a021cp+4f, -0x1.999998p-2f } },
    { { 0.f, -0x1.8p+1f, 0x1.9p+7f } }, 0, { 0 }, 0, { 0.f }
  }
};

static void Race_PmoveQ2CollisionAssertContextParity(
    const char *name, const pm_move_t *game, const pm_move_t *cgame) {
  pm_move_t game_copy = *game;
  pm_move_t cgame_copy = *cgame;
  game_copy.Trace = NULL;
  cgame_copy.Trace = NULL;
  ck_assert_msg(memcmp(&game_copy, &cgame_copy, sizeof(game_copy)) == 0,
                "%s: GAME-like and CGAME-like movement contexts differ", name);

  ck_assert_msg(race_pmove_q2_game_num_traces ==
                  race_pmove_q2_cgame_num_traces,
                "%s: GAME-like and CGAME-like trace counts differ", name);
  for (size_t i = 0; i < race_pmove_q2_game_num_traces; i++) {
    char trace_name[96];
    snprintf(trace_name, sizeof(trace_name), "%s trace %zu", name, i + 1u);
    Race_PmoveAssertFloat(trace_name, "fraction",
                          race_pmove_q2_game_traces[i].fraction,
                          race_pmove_q2_cgame_traces[i].fraction);
    ck_assert(race_pmove_q2_game_traces[i].start_solid ==
              race_pmove_q2_cgame_traces[i].start_solid);
    ck_assert(race_pmove_q2_game_traces[i].all_solid ==
              race_pmove_q2_cgame_traces[i].all_solid);
    ck_assert_int_eq(race_pmove_q2_game_traces[i].entity,
                     race_pmove_q2_cgame_traces[i].entity);
    Race_PmoveAssertVec3(trace_name, "normal",
                         race_pmove_q2_game_traces[i].normal,
                         race_pmove_q2_cgame_traces[i].normal);
  }
}

START_TEST(_Race_PmoveQ2CollisionLongStreamGameCgame) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);
  race_pmove_feet_current = false;

  pm_move_t game = Race_PmoveQ2AirSetup(
    Vec3(-20.f, -30.f, -40.f), Vec3(500.f, 300.f, 200.f));
  pm_move_t cgame = Race_PmoveQ2AirSetup(
    Vec3(-20.f, -30.f, -40.f), Vec3(500.f, 300.f, 200.f));
  game.Trace = Race_PmoveQ2CollisionGameTrace;
  cgame.Trace = Race_PmoveQ2CollisionCgameTrace;
  const pm_params_t params = game.s.params;

  for (size_t i = 0;
       i < sizeof(race_pmove_q2_collision_stream) /
           sizeof(*race_pmove_q2_collision_stream); i++) {
    const race_pmove_q2_collision_stream_t *command =
      race_pmove_q2_collision_stream + i;

    if (i == 0u || i >= 3u) {
      race_pmove_q2_collision_world = RACE_PMOVE_Q2_COLLISION_EMPTY;
      race_pmove_q2_collision_num_planes = 0u;
    } else if (i == 1u) {
      race_pmove_q2_collision_world = RACE_PMOVE_Q2_COLLISION_PLANES;
      race_pmove_q2_collision_num_planes = 0u;
      Race_PmoveQ2CollisionSetPlane(0, Vec3(-1.f, 0.f, 0.f), 10.f, 1);
    } else {
      const float x_velocity = game.s.velocity.x + 15.f;
      const float hit_time = (-10.f - game.s.origin.x) / x_velocity;
      const float y_wall = game.s.origin.y + game.s.velocity.y * hit_time;
      race_pmove_q2_collision_world = RACE_PMOVE_Q2_COLLISION_PLANES;
      race_pmove_q2_collision_num_planes = 0u;
      Race_PmoveQ2CollisionSetPlane(0, Vec3(-1.f, 0.f, 0.f), 10.f, 1);
      Race_PmoveQ2CollisionSetPlane(
        1, Vec3(0.f, -1.f, 0.f), -y_wall, 2);
    }

    game.cmd = (pm_cmd_t) {
      .msec = command->msec,
      .forward = command->forward
    };
    cgame.cmd = game.cmd;
    race_pmove_q2_game_num_traces = 0u;
    race_pmove_q2_cgame_num_traces = 0u;
    race_pmove_q2_game_step_up_traces = 0;
    race_pmove_q2_cgame_step_up_traces = 0;
    race_pmove_q2_game_move_start_origin = game.s.origin;
    race_pmove_q2_cgame_move_start_origin = cgame.s.origin;
    Pm_Move(&game);
    Pm_Move(&cgame);

    char name[64];
    snprintf(name, sizeof(name), "Q2 collision stream command %zu", i + 1u);
    Race_PmoveAssertVec3(name, "origin", game.s.origin,
                         command->expected_origin);
    Race_PmoveAssertVec3(name, "velocity", game.s.velocity,
                         command->expected_velocity);
    ck_assert_int_eq(game.num_touched, command->num_touched);
    for (int32_t touch = 0; touch < game.num_touched; touch++) {
      ck_assert_int_eq(Race_PmoveQ2CollisionEntityId(
                         game.touched[touch].ent), command->touched[touch]);
    }
    ck_assert_msg(race_pmove_q2_game_num_traces ==
                    (size_t) command->num_traces,
                  "%s: trace count was %zu, expected %d", name,
                  race_pmove_q2_game_num_traces, command->num_traces);
    for (int32_t trace = 0; trace < command->num_traces; trace++) {
      Race_PmoveAssertFloat(name, "trace fraction",
                            race_pmove_q2_game_traces[trace].fraction,
                            command->trace_fractions[trace]);
    }
    ck_assert_int_eq(race_pmove_q2_game_step_up_traces, 1);
    ck_assert_int_eq(race_pmove_q2_cgame_step_up_traces, 1);
    ck_assert_msg(memcmp(&game.s.params, &params, sizeof(params)) == 0,
                  "%s: GAME-like parameters changed", name);
    ck_assert_msg(memcmp(&cgame.s.params, &params, sizeof(params)) == 0,
                  "%s: CGAME-like parameters changed", name);
    Race_PmoveQ2CollisionAssertContextParity(name, &game, &cgame);
  }
} END_TEST

START_TEST(_Race_PmoveQ2AiDirectCollision) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);
  const race_pmove_q2_collision_case_id_t id =
    RACE_PMOVE_Q2_COLLISION_HIGH_SPEED;
  const race_pmove_q2_collision_case_t *collision =
    race_pmove_q2_collision_cases + id;
  Race_PmoveQ2CollisionSetCase(id);
  race_pmove_q2_game_num_traces = 0u;
  race_pmove_q2_game_step_up_traces = 0;
  pm_move_t ai = Race_PmoveQ2CollisionSetup(
    collision, Race_PmoveQ2CollisionGameTrace);
  race_pmove_q2_game_move_start_origin = ai.s.origin;
  const pm_params_t params = ai.s.params;

  // AI calls the shared mover directly, without a G_PrepareMove dependency.
  Pm_Move(&ai);

  Race_PmoveQ2CollisionAssertResult(collision, &ai, &params);
  Race_PmoveQ2CollisionAssertTraces(
    collision, race_pmove_q2_game_traces, race_pmove_q2_game_num_traces);
} END_TEST

typedef enum {
  RACE_PMOVE_Q2_STEP_FLOOR,
  RACE_PMOVE_Q2_STEP_CURB,
  RACE_PMOVE_Q2_STEP_HEADROOM,
  RACE_PMOVE_Q2_STEP_START_SOLID_OCCUPANCY,
  RACE_PMOVE_Q2_STEP_EQUAL_TIE,
  RACE_PMOVE_Q2_STEP_LOWER_WINS,
  RACE_PMOVE_Q2_STEP_STEEP_DOWN,
  RACE_PMOVE_Q2_STEP_ALL_SOLID_DOWN,
  RACE_PMOVE_Q2_STEP_LEDGE,
  RACE_PMOVE_Q2_STEP_STAIRCASE
} race_pmove_q2_step_world_t;

typedef struct {
  box3_t bounds;
  race_pmove_entity_t entity;
} race_pmove_q2_step_solid_t;

typedef struct {
  vec3_t start;
  vec3_t end;
  box3_t bounds;
  float fraction;
  bool start_solid;
  bool all_solid;
  vec3_t result_end;
  vec3_t normal;
  race_pmove_entity_t entity;
} race_pmove_q2_step_trace_t;

typedef struct {
  race_pmove_q2_step_trace_t traces[64];
  size_t num_traces;
} race_pmove_q2_step_context_t;

typedef struct {
  const char *name;
  race_pmove_q2_step_world_t world;
  float curb_height;
  vec3_t origin;
  vec3_t velocity;
  uint16_t msec;
  vec3_t expected_origin;
  vec3_t expected_velocity;
  int32_t num_touched;
  race_pmove_entity_t touched[2];
  size_t num_traces;
  size_t occupancy_trace;
  bool occupancy_start_solid;
  bool occupancy_all_solid;
  race_pmove_entity_t occupancy_entity;
  int32_t down_trace;
  float down_fraction;
  bool down_start_solid;
  bool down_all_solid;
  vec3_t down_normal;
  race_pmove_entity_t down_entity;
} race_pmove_q2_step_case_t;

static race_pmove_q2_step_solid_t race_pmove_q2_step_solids[16];
static size_t race_pmove_q2_step_num_solids;
static bool race_pmove_q2_step_steep_down;
static bool race_pmove_q2_step_all_solid_down;
static bool race_pmove_q2_step_start_solid_occupancy;
static race_pmove_entity_t race_pmove_q2_step_down_entity;
static race_pmove_q2_step_context_t race_pmove_q2_step_game_context;
static race_pmove_q2_step_context_t race_pmove_q2_step_cgame_context;

static void Race_PmoveQ2StepLogTrace(race_pmove_q2_step_context_t *context,
                                      const vec3_t start, const vec3_t end,
                                      const box3_t bounds,
                                      const cm_trace_t trace) {
  if (context->num_traces ==
      sizeof(context->traces) / sizeof(*context->traces)) {
    return;
  }

  context->traces[context->num_traces++] = (race_pmove_q2_step_trace_t) {
    .start = start,
    .end = end,
    .bounds = bounds,
    .fraction = trace.fraction,
    .start_solid = trace.start_solid,
    .all_solid = trace.all_solid,
    .result_end = trace.end,
    .normal = trace.plane.normal,
    .entity = Race_PmoveEntityId(trace.ent)
  };
}

static cm_trace_t Race_PmoveQ2StepTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds,
    race_pmove_q2_step_context_t *context) {
  cm_trace_t trace = {
    .fraction = 2.f,
    .end = end
  };
  const vec3_t delta = Vec3_Subtract(end, start);

  if (race_pmove_q2_step_start_solid_occupancy &&
      Vec3_Length(delta) < .0001f) {
    trace = (cm_trace_t) {
      .start_solid = true,
      .fraction = 0.f,
      .end = start,
      .contents = CONTENTS_SOLID,
      .ent = (void *) Race_PmoveEntityPointer(RACE_PMOVE_ENTITY_CEILING)
    };
    Race_PmoveQ2StepLogTrace(context, start, end, bounds, trace);
    return trace;
  }

  if (race_pmove_q2_step_all_solid_down && delta.z < -17.999f &&
      fabsf(delta.x) < .0001f && fabsf(delta.y) < .0001f) {
    trace = (cm_trace_t) {
      .all_solid = true,
      .start_solid = true,
      .fraction = 0.f,
      .end = start,
      .contents = CONTENTS_SOLID,
      .ent = (void *) Race_PmoveEntityPointer(
        race_pmove_q2_step_down_entity)
    };
    Race_PmoveQ2StepLogTrace(context, start, end, bounds, trace);
    return trace;
  }

  for (size_t i = 0; i < race_pmove_q2_step_num_solids; i++) {
    const race_pmove_q2_step_solid_t *solid =
      race_pmove_q2_step_solids + i;
    Race_PmoveTraceBox(&trace, start, end, bounds, solid->bounds,
                       solid->entity, CONTENTS_SOLID, 0);
  }

  if (trace.fraction > 1.f) {
    trace.fraction = 1.f;
    trace.end = end;
  }

  if (race_pmove_q2_step_steep_down &&
      trace.ent == Race_PmoveEntityPointer(race_pmove_q2_step_down_entity) &&
      delta.z < 0.f && trace.plane.normal.z > 0.f) {
    trace.plane.normal = Race_PmoveSteepNormal();
  }

  Race_PmoveQ2StepLogTrace(context, start, end, bounds, trace);
  return trace;
}

static cm_trace_t Race_PmoveQ2StepGameTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds) {
  return Race_PmoveQ2StepTrace(start, end, bounds,
                               &race_pmove_q2_step_game_context);
}

static cm_trace_t Race_PmoveQ2StepCgameTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds) {
  return Race_PmoveQ2StepTrace(start, end, bounds,
                               &race_pmove_q2_step_cgame_context);
}

static int32_t Race_PmoveQ2StepPointContents(const vec3_t point) {
  (void) point;
  return 0;
}

static int32_t Race_PmoveQ2StepBoxContents(const box3_t box) {
  (void) box;
  return 0;
}

static void Race_PmoveQ2StepResetWorld(void) {
  race_pmove_q2_step_num_solids = 0u;
  race_pmove_q2_step_steep_down = false;
  race_pmove_q2_step_all_solid_down = false;
  race_pmove_q2_step_start_solid_occupancy = false;
  race_pmove_q2_step_down_entity = RACE_PMOVE_ENTITY_STEP;
  memset(&race_pmove_q2_step_game_context, 0,
         sizeof(race_pmove_q2_step_game_context));
  memset(&race_pmove_q2_step_cgame_context, 0,
         sizeof(race_pmove_q2_step_cgame_context));
}

static void Race_PmoveQ2StepAddSolid(const box3_t bounds,
                                     race_pmove_entity_t entity) {
  ck_assert(race_pmove_q2_step_num_solids <
            sizeof(race_pmove_q2_step_solids) /
              sizeof(*race_pmove_q2_step_solids));
  race_pmove_q2_step_solids[race_pmove_q2_step_num_solids++] =
    (race_pmove_q2_step_solid_t) { bounds, entity };
}

static void Race_PmoveQ2StepAddFloor(float max_x) {
  Race_PmoveQ2StepAddSolid(
    Box3(Vec3(-4096.f, -4096.f, -4096.f), Vec3(max_x, 4096.f, 0.f)),
    RACE_PMOVE_ENTITY_FLOOR);
}

static void Race_PmoveQ2StepAddCurb(float height) {
  Race_PmoveQ2StepAddSolid(
    Box3(Vec3(32.f, -128.f, 0.f), Vec3(256.f, 128.f, height)),
    RACE_PMOVE_ENTITY_STEP);
}

static void Race_PmoveQ2StepSetWorld(race_pmove_q2_step_world_t world,
                                     float curb_height) {
  Race_PmoveQ2StepResetWorld();

  switch (world) {
    case RACE_PMOVE_Q2_STEP_FLOOR:
      Race_PmoveQ2StepAddFloor(4096.f);
      break;

    case RACE_PMOVE_Q2_STEP_CURB:
      Race_PmoveQ2StepAddFloor(4096.f);
      Race_PmoveQ2StepAddCurb(curb_height);
      break;

    case RACE_PMOVE_Q2_STEP_HEADROOM:
      Race_PmoveQ2StepAddFloor(4096.f);
      Race_PmoveQ2StepAddCurb(8.f);
      Race_PmoveQ2StepAddSolid(
        Box3(Vec3(-64.f, -128.f, 70.f), Vec3(96.f, 128.f, 128.f)),
        RACE_PMOVE_ENTITY_CEILING);
      break;

    case RACE_PMOVE_Q2_STEP_START_SOLID_OCCUPANCY:
      Race_PmoveQ2StepAddFloor(4096.f);
      Race_PmoveQ2StepAddCurb(8.f);
      race_pmove_q2_step_start_solid_occupancy = true;
      break;

    case RACE_PMOVE_Q2_STEP_EQUAL_TIE:
      Race_PmoveQ2StepAddFloor(4096.f);
      Race_PmoveQ2StepAddSolid(
        Box3(Vec3(-128.f, 20.f, 0.f), Vec3(128.f, 256.f, 8.f)),
        RACE_PMOVE_ENTITY_STEP);
      Race_PmoveQ2StepAddSolid(
        Box3(Vec3(20.f, -128.f, 65.f), Vec3(256.f, 128.f, 80.f)),
        RACE_PMOVE_ENTITY_WALL);
      break;

    case RACE_PMOVE_Q2_STEP_LOWER_WINS:
      Race_PmoveQ2StepAddFloor(4096.f);
      Race_PmoveQ2StepAddSolid(
        Box3(Vec3(-128.f, 20.f, 0.f), Vec3(128.f, 256.f, 8.f)),
        RACE_PMOVE_ENTITY_STEP);
      Race_PmoveQ2StepAddSolid(
        Box3(Vec3(18.f, -128.f, 65.f), Vec3(256.f, 128.f, 80.f)),
        RACE_PMOVE_ENTITY_WALL);
      break;

    case RACE_PMOVE_Q2_STEP_STEEP_DOWN:
      Race_PmoveQ2StepAddFloor(4096.f);
      Race_PmoveQ2StepAddCurb(8.f);
      race_pmove_q2_step_steep_down = true;
      break;

    case RACE_PMOVE_Q2_STEP_ALL_SOLID_DOWN:
      Race_PmoveQ2StepAddFloor(4096.f);
      Race_PmoveQ2StepAddCurb(8.f);
      race_pmove_q2_step_all_solid_down = true;
      break;

    case RACE_PMOVE_Q2_STEP_LEDGE:
      Race_PmoveQ2StepAddFloor(0.f);
      break;

    case RACE_PMOVE_Q2_STEP_STAIRCASE:
      Race_PmoveQ2StepAddFloor(4096.f);
      Race_PmoveQ2StepAddSolid(
        Box3(Vec3(-96.f, -256.f, 0.f), Vec3(-64.f, 256.f, 8.f)),
        RACE_PMOVE_ENTITY_STEP);
      Race_PmoveQ2StepAddSolid(
        Box3(Vec3(-64.f, -256.f, 0.f), Vec3(-32.f, 256.f, 16.f)),
        RACE_PMOVE_ENTITY_WALL);
      Race_PmoveQ2StepAddSolid(
        Box3(Vec3(-32.f, -256.f, 0.f), Vec3(0.f, 256.f, 24.f)),
        RACE_PMOVE_ENTITY_WALL_Y);
      Race_PmoveQ2StepAddSolid(
        Box3(Vec3(0.f, -256.f, 0.f), Vec3(96.f, 256.f, 32.f)),
        RACE_PMOVE_ENTITY_RAMP);
      break;
  }
}

static pm_move_t Race_PmoveQ2StepSetup(const vec3_t origin,
                                       const vec3_t velocity,
                                       uint16_t msec,
                                       cm_trace_t (*Trace)(vec3_t, vec3_t,
                                                           box3_t)) {
  pm_move_t move = Race_PmoveQ2AirSetup(origin, velocity);
  move.s.params = (pm_params_t) {
    .gravity_water = 1.f
  };
  move.s.view_offset = Vec3(0.f, 0.f, 22.f);
  move.bounds = PM_BOUNDS;
  move.cmd.msec = msec;
  move.PointContents = Race_PmoveQ2StepPointContents;
  move.BoxContents = Race_PmoveQ2StepBoxContents;
  move.Trace = Trace;
  return move;
}

static const race_pmove_q2_step_case_t race_pmove_q2_step_cases[] = {
  {
    .name = "no-obstacle upper tie",
    .world = RACE_PMOVE_Q2_STEP_FLOOR,
    .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } },
    .msec = 50,
    .expected_origin = { { 20.f, 0.f, 24.f } },
    .expected_velocity = { { 400.f, 0.f, 0.f } },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 1.f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_FLOOR
  },
  {
    .name = "8-unit curb", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 8.f, .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 20.f, 0.f, 32.f } },
    .expected_velocity = { { 400.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 0x1.1c71c8p-1f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "15-unit curb", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 15.f, .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 20.f, 0.f, 39.f } },
    .expected_velocity = { { 400.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 0x1.555556p-3f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "16-unit curb", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 16.f, .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 20.f, 0.f, 40.f } },
    .expected_velocity = { { 400.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 0x1.c71c72p-4f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "17-unit curb", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 17.f, .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 20.f, 0.f, 41.f } },
    .expected_velocity = { { 400.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 0x1.c71c72p-5f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "18-unit exact boundary", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 18.f, .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 16.f, 0.f, 42.f } },
    .expected_velocity = { { 0.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = -0.f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "19-unit over boundary", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 19.f, .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 16.f, 0.f, 24.f } },
    .expected_velocity = { { 0.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 1.f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_FLOOR
  },
  {
    .name = "31-unit obstacle", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 31.f, .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 16.f, 0.f, 24.f } },
    .expected_velocity = { { 0.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 1.f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_FLOOR
  },
  {
    .name = "32-unit obstacle", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 32.f, .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 16.f, 0.f, 24.f } },
    .expected_velocity = { { 0.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 1.f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_FLOOR
  },
  {
    .name = "33-unit too-high obstacle", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 33.f, .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 16.f, 0.f, 24.f } },
    .expected_velocity = { { 0.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 1.f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_FLOOR
  },
  {
    .name = "headroom occupancy rejection",
    .world = RACE_PMOVE_Q2_STEP_HEADROOM,
    .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 16.f, 0.f, 24.f } },
    .expected_velocity = { { 0.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 2u, .occupancy_trace = 1u,
    .occupancy_start_solid = true, .occupancy_all_solid = true,
    .occupancy_entity = RACE_PMOVE_ENTITY_CEILING,
    .down_trace = -1
  },
  {
    .name = "start-solid-only occupancy continues",
    .world = RACE_PMOVE_Q2_STEP_START_SOLID_OCCUPANCY,
    .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 20.f, 0.f, 32.f } },
    .expected_velocity = { { 400.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .occupancy_start_solid = true,
    .occupancy_entity = RACE_PMOVE_ENTITY_CEILING,
    .down_trace = 3, .down_fraction = 0x1.1c71c8p-1f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "equal-distance upper tie",
    .world = RACE_PMOVE_Q2_STEP_EQUAL_TIE,
    .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 400.f, 0.f } }, .msec = 50,
    .expected_origin = { { 0x1.eb851ep+1f, 20.f, 32.f } },
    .expected_velocity = { { -4.f, 400.f, 0.f } },
    .num_touched = 2,
    .touched = { RACE_PMOVE_ENTITY_STEP, RACE_PMOVE_ENTITY_WALL },
    .num_traces = 6u, .occupancy_trace = 2u,
    .down_trace = 5, .down_fraction = 0x1.1c71c8p-1f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "lower path wins", .world = RACE_PMOVE_Q2_STEP_LOWER_WINS,
    .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 400.f, 0.f } }, .msec = 50,
    .expected_origin = { { 20.f, 0x1.eb851ep+1f, 24.f } },
    .expected_velocity = { { 400.f, -4.f, 0.f } },
    .num_touched = 2,
    .touched = { RACE_PMOVE_ENTITY_STEP, RACE_PMOVE_ENTITY_WALL },
    .num_traces = 6u, .occupancy_trace = 2u,
    .down_trace = 5, .down_fraction = 0x1.1c71c8p-1f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "steep down rejection", .world = RACE_PMOVE_Q2_STEP_STEEP_DOWN,
    .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 16.f, 0.f, 24.f } },
    .expected_velocity = { { 0.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 0x1.1c71c8p-1f,
    .down_normal = { { -0x1.c9f25cp-1f, 0.f, 0x1.c9f25cp-2f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "all-solid down rejection",
    .world = RACE_PMOVE_Q2_STEP_ALL_SOLID_DOWN,
    .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 16.f, 0.f, 24.f } },
    .expected_velocity = { { 0.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_start_solid = true,
    .down_all_solid = true, .down_normal = { { 0.f, 0.f, 0.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "ledge departure", .world = RACE_PMOVE_Q2_STEP_LEDGE,
    .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 20.f, 0.f, 24.f } },
    .expected_velocity = { { 400.f, 0.f, 0.f } },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 1.f,
    .down_normal = { { 0.f, 0.f, 0.f } }
  },
  {
    .name = "8 ms step", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 8.f, .origin = { { 0x1.ccccccp+3f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 8,
    .expected_origin = { { 0x1.19999ap+4f, 0.f, 32.f } },
    .expected_velocity = { { 400.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 0x1.1c71c8p-1f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "16 ms step", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 8.f, .origin = { { 0x1.99999ap+3f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 16,
    .expected_origin = { { 0x1.333334p+4f, 0.f, 32.f } },
    .expected_velocity = { { 400.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 0x1.1c71c8p-1f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "25 ms step", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 8.f, .origin = { { 11.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 25,
    .expected_origin = { { 21.f, 0.f, 32.f } },
    .expected_velocity = { { 400.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 0x1.1c71c8p-1f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "50 ms step", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 8.f, .origin = { { 6.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 0.f } }, .msec = 50,
    .expected_origin = { { 26.f, 0.f, 32.f } },
    .expected_velocity = { { 400.f, 0.f, 0.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 4u, .occupancy_trace = 1u,
    .down_trace = 3, .down_fraction = 0x1.1c71c8p-1f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "upward velocity", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 8.f, .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, 100.f } }, .msec = 50,
    .expected_origin = { { 20.f, 0.f, 32.f } },
    .expected_velocity = { { 400.f, 0.f, 100.f } },
    .num_touched = 1, .touched = { RACE_PMOVE_ENTITY_STEP },
    .num_traces = 5u, .occupancy_trace = 2u,
    .down_trace = 4, .down_fraction = 0x1.aaaaaap-1f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  },
  {
    .name = "downward velocity", .world = RACE_PMOVE_Q2_STEP_CURB,
    .curb_height = 8.f, .origin = { { 0.f, 0.f, 24.f } },
    .velocity = { { 400.f, 0.f, -100.f } }, .msec = 50,
    .expected_origin = { { 20.f, 0.f, 32.f } },
    .expected_velocity = { { 400.f, 0.f, 0.f } },
    .num_touched = 2,
    .touched = { RACE_PMOVE_ENTITY_FLOOR, RACE_PMOVE_ENTITY_STEP },
    .num_traces = 5u, .occupancy_trace = 2u,
    .down_trace = 4, .down_fraction = 0x1.1c71c8p-2f,
    .down_normal = { { 0.f, 0.f, 1.f } },
    .down_entity = RACE_PMOVE_ENTITY_STEP
  }
};

static void Race_PmoveQ2StepAssertLegacyCase(
    const race_pmove_q2_step_case_t *test, const pm_move_t *move,
    const pm_params_t *params, const race_pmove_q2_step_context_t *context) {
  Race_PmoveAssertVec3(test->name, "origin", move->s.origin,
                       test->expected_origin);
  Race_PmoveAssertVec3(test->name, "velocity", move->s.velocity,
                       test->expected_velocity);
  Race_PmoveAssertFloat(test->name, "step", move->step, 0.f);
  ck_assert_msg(move->num_touched == test->num_touched,
                "%s: touched count was %d, expected %d", test->name,
                move->num_touched, test->num_touched);
  for (int32_t i = 0; i < move->num_touched; i++) {
    ck_assert_msg(Race_PmoveEntityId(move->touched[i].ent) == test->touched[i],
                  "%s: touched[%d] order mismatch", test->name, i);
  }
  ck_assert_msg(memcmp(&move->s.params, params, sizeof(*params)) == 0,
                "%s: helper changed replicated parameters", test->name);
  ck_assert_msg(context->num_traces == test->num_traces,
                "%s: trace count was %zu, expected %zu", test->name,
                context->num_traces, test->num_traces);

  const race_pmove_q2_step_trace_t *occupancy =
    context->traces + test->occupancy_trace;
  const vec3_t expected_up = Vec3_Fmaf(test->origin, 18.f, Vec3_Up());
  Race_PmoveAssertVec3(test->name, "occupancy start", occupancy->start,
                       expected_up);
  Race_PmoveAssertVec3(test->name, "occupancy end", occupancy->end,
                       expected_up);
  Race_PmoveAssertFloat(test->name, "occupancy fraction",
                        occupancy->fraction,
                        test->occupancy_start_solid ||
                          test->occupancy_all_solid ? 0.f : 1.f);
  ck_assert(occupancy->start_solid == test->occupancy_start_solid);
  ck_assert(occupancy->all_solid == test->occupancy_all_solid);
  ck_assert_int_eq(occupancy->entity, test->occupancy_entity);

  if (test->down_trace >= 0) {
    const race_pmove_q2_step_trace_t *down =
      context->traces + test->down_trace;
    const vec3_t down_delta = Vec3_Subtract(down->end, down->start);
    Race_PmoveAssertVec3(test->name, "down trace delta", down_delta,
                         Vec3(0.f, 0.f, -18.f));
    Race_PmoveAssertFloat(test->name, "down fraction", down->fraction,
                          test->down_fraction);
    ck_assert(down->start_solid == test->down_start_solid);
    ck_assert(down->all_solid == test->down_all_solid);
    Race_PmoveAssertVec3(test->name, "down normal", down->normal,
                         test->down_normal);
    ck_assert_int_eq(down->entity, test->down_entity);
  }
}

START_TEST(_Race_PmoveQ2StepLegacyCases) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);

  for (size_t i = 0;
       i < sizeof(race_pmove_q2_step_cases) /
             sizeof(*race_pmove_q2_step_cases); i++) {
    const race_pmove_q2_step_case_t *test = race_pmove_q2_step_cases + i;
    Race_PmoveQ2StepSetWorld(test->world, test->curb_height);
    pm_move_t move = Race_PmoveQ2StepSetup(
      test->origin, test->velocity, test->msec, Race_PmoveQ2StepGameTrace);
    const pm_params_t params = move.s.params;

    Pm_Q2StepSlideMoveForTest(&move);

    Race_PmoveQ2StepAssertLegacyCase(
      test, &move, &params, &race_pmove_q2_step_game_context);
  }
} END_TEST

typedef struct {
  uint16_t msec;
  vec3_t expected_origin;
  race_pmove_entity_t expected_ground;
} race_pmove_q2_step_stream_t;

static const race_pmove_q2_step_stream_t race_pmove_q2_step_stream[] = {
  { 8,  { { -0x1.f8p+6f, -64.f, 24.f } }, RACE_PMOVE_ENTITY_FLOOR },
  { 16, { { -0x1.e8p+6f, -64.f, 24.f } }, RACE_PMOVE_ENTITY_FLOOR },
  { 25, { { -0x1.cfp+6f, -64.f, 24.f } }, RACE_PMOVE_ENTITY_FLOOR },
  { 50, { { -0x1.9dp+6f, -64.f, 32.f } }, RACE_PMOVE_ENTITY_STEP },
  { 8,  { { -0x1.95p+6f, -64.f, 32.f } }, RACE_PMOVE_ENTITY_STEP },
  { 16, { { -0x1.85p+6f, -64.f, 32.f } }, RACE_PMOVE_ENTITY_STEP },
  { 25, { { -0x1.6cp+6f, -64.f, 32.f } }, RACE_PMOVE_ENTITY_STEP },
  { 50, { { -0x1.3ap+6f, -64.f, 40.f } }, RACE_PMOVE_ENTITY_WALL },
  { 8,  { { -0x1.32p+6f, -64.f, 40.f } }, RACE_PMOVE_ENTITY_WALL },
  { 16, { { -0x1.22p+6f, -64.f, 40.f } }, RACE_PMOVE_ENTITY_WALL },
  { 25, { { -0x1.09p+6f, -64.f, 40.f } }, RACE_PMOVE_ENTITY_WALL },
  { 50, { { -0x1.aep+5f, -64.f, 40.f } }, RACE_PMOVE_ENTITY_WALL },
  { 8,  { { -0x1.9ep+5f, -64.f, 40.f } }, RACE_PMOVE_ENTITY_WALL },
  { 16, { { -0x1.7ep+5f, -64.f, 48.f } }, RACE_PMOVE_ENTITY_WALL_Y },
  { 25, { { -0x1.4cp+5f, -64.f, 48.f } }, RACE_PMOVE_ENTITY_WALL_Y },
  { 50, { { -0x1.dp+4f, -64.f, 48.f } }, RACE_PMOVE_ENTITY_WALL_Y },
  { 8,  { { -0x1.bp+4f, -64.f, 48.f } }, RACE_PMOVE_ENTITY_WALL_Y },
  { 16, { { -0x1.7p+4f, -64.f, 48.f } }, RACE_PMOVE_ENTITY_WALL_Y },
  { 25, { { -0x1.0cp+4f, -64.f, 48.f } }, RACE_PMOVE_ENTITY_WALL_Y },
  { 50, { { -0x1.1p+2f, -64.f, 56.f } }, RACE_PMOVE_ENTITY_RAMP },
  { 8,  { { -0x1.2p+1f, -64.f, 56.f } }, RACE_PMOVE_ENTITY_RAMP },
  { 16, { { 0x1.c00004p+0f, -64.f, 56.f } }, RACE_PMOVE_ENTITY_RAMP },
  { 25, { { 8.f, -64.f, 56.f } }, RACE_PMOVE_ENTITY_RAMP },
  { 50, { { 0x1.48p+4f, -64.f, 56.f } }, RACE_PMOVE_ENTITY_RAMP }
};

static void Race_PmoveQ2StepAssertTraceParity(
    const char *name, const race_pmove_q2_step_context_t *game,
    const race_pmove_q2_step_context_t *cgame) {
  ck_assert_msg(game->num_traces == cgame->num_traces,
                "%s: GAME/CGAME trace counts differ (%zu != %zu)", name,
                game->num_traces, cgame->num_traces);

  for (size_t i = 0; i < game->num_traces; i++) {
    const race_pmove_q2_step_trace_t *game_trace = game->traces + i;
    const race_pmove_q2_step_trace_t *cgame_trace = cgame->traces + i;
    char trace_name[96];
    snprintf(trace_name, sizeof(trace_name), "%s trace %zu", name, i + 1u);
    Race_PmoveAssertVec3(trace_name, "start", game_trace->start,
                         cgame_trace->start);
    Race_PmoveAssertVec3(trace_name, "end", game_trace->end,
                         cgame_trace->end);
    Race_PmoveAssertVec3(trace_name, "bounds.mins", game_trace->bounds.mins,
                         cgame_trace->bounds.mins);
    Race_PmoveAssertVec3(trace_name, "bounds.maxs", game_trace->bounds.maxs,
                         cgame_trace->bounds.maxs);
    Race_PmoveAssertFloat(trace_name, "fraction", game_trace->fraction,
                          cgame_trace->fraction);
    ck_assert(game_trace->start_solid == cgame_trace->start_solid);
    ck_assert(game_trace->all_solid == cgame_trace->all_solid);
    Race_PmoveAssertVec3(trace_name, "result end", game_trace->result_end,
                         cgame_trace->result_end);
    Race_PmoveAssertVec3(trace_name, "normal", game_trace->normal,
                         cgame_trace->normal);
    ck_assert_int_eq(game_trace->entity, cgame_trace->entity);
  }
}

static void Race_PmoveQ2StepAssertMoveParity(const char *name,
                                              const pm_move_t *game,
                                              const pm_move_t *cgame) {
  ck_assert_msg(memcmp(&game->s, &cgame->s, sizeof(game->s)) == 0,
                "%s: GAME/CGAME pm_state_t differ", name);

  pm_move_t game_copy = *game;
  pm_move_t cgame_copy = *cgame;
  game_copy.Trace = NULL;
  cgame_copy.Trace = NULL;
  ck_assert_msg(memcmp(&game_copy, &cgame_copy, sizeof(game_copy)) == 0,
                "%s: GAME/CGAME pm_move_t differ", name);

  Race_PmoveQ2StepAssertTraceParity(
    name, &race_pmove_q2_step_game_context,
    &race_pmove_q2_step_cgame_context);
}

START_TEST(_Race_PmoveQ2StepLongStreamGameCgame) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);
  Race_PmoveQ2StepSetWorld(RACE_PMOVE_Q2_STEP_STAIRCASE, 0.f);

  pm_move_t game = Race_PmoveQ2StepSetup(
    Vec3(-128.f, -64.f, 24.f), Vec3(250.f, 0.f, 0.f), 0,
    Race_PmoveQ2StepGameTrace);
  pm_move_t cgame = Race_PmoveQ2StepSetup(
    Vec3(-128.f, -64.f, 24.f), Vec3(250.f, 0.f, 0.f), 0,
    Race_PmoveQ2StepCgameTrace);
  const pm_params_t params = game.s.params;

  for (size_t i = 0;
       i < sizeof(race_pmove_q2_step_stream) /
             sizeof(*race_pmove_q2_step_stream); i++) {
    const race_pmove_q2_step_stream_t *command =
      race_pmove_q2_step_stream + i;
    game.cmd = (pm_cmd_t) { .msec = command->msec };
    cgame.cmd = game.cmd;
    memset(&race_pmove_q2_step_game_context, 0,
           sizeof(race_pmove_q2_step_game_context));
    memset(&race_pmove_q2_step_cgame_context, 0,
           sizeof(race_pmove_q2_step_cgame_context));

    Pm_Move(&game);
    Pm_Move(&cgame);

    char name[64];
    snprintf(name, sizeof(name), "Q2 step stream command %zu", i + 1u);
    Race_PmoveAssertVec3(name, "origin", game.s.origin,
                         command->expected_origin);
    Race_PmoveAssertVec3(name, "velocity", game.s.velocity,
                         Vec3(250.f, 0.f, 0.f));
    ck_assert_msg(game.s.flags == PMF_ON_GROUND,
                  "%s: flags were 0x%04x", name, game.s.flags);
    ck_assert_uint_eq(game.s.time, 0u);
    ck_assert_int_eq(game.water_level, WATER_NONE);
    ck_assert_int_eq(game.water_type, 0);
    ck_assert_int_eq(Race_PmoveEntityId(game.ground.ent),
                     command->expected_ground);
    Race_PmoveAssertVec3(name, "ground normal", game.ground.plane.normal,
                         Vec3_Up());
    Race_PmoveAssertFloat(name, "step", game.step, 0.f);
    Race_PmoveAssertFloat(name, "step offset", game.s.step_offset, 0.f);
    Race_PmoveAssertBounds(name, game.bounds, Pm_PlayerBounds(false));
    ck_assert_msg(memcmp(&game.s.params, &params, sizeof(params)) == 0,
                  "%s: GAME parameters changed", name);
    ck_assert_msg(memcmp(&cgame.s.params, &params, sizeof(params)) == 0,
                  "%s: CGAME parameters changed", name);
    Race_PmoveQ2StepAssertMoveParity(name, &game, &cgame);
  }
} END_TEST

START_TEST(_Race_PmoveQ2AiDirectStep) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);
  Race_PmoveQ2StepSetWorld(RACE_PMOVE_Q2_STEP_CURB, 8.f);

  pm_move_t ai = Race_PmoveQ2StepSetup(
    Vec3(0.f, 0.f, 24.f), Vec3(400.f, 0.f, 0.f), 50,
    Race_PmoveQ2StepGameTrace);
  const pm_params_t params = ai.s.params;

  // GAME-side AI calls the shared mover directly, without G_PrepareMove.
  Pm_Move(&ai);

  Race_PmoveAssertVec3("direct AI Q2 step", "origin", ai.s.origin,
                       Vec3(20.f, 0.f, 32.f));
  Race_PmoveAssertVec3("direct AI Q2 step", "velocity", ai.s.velocity,
                       Vec3(400.f, 0.f, 0.f));
  ck_assert_uint_eq(ai.s.flags, PMF_ON_GROUND);
  ck_assert_uint_eq(ai.s.time, 0u);
  ck_assert_int_eq(Race_PmoveEntityId(ai.ground.ent),
                   RACE_PMOVE_ENTITY_STEP);
  Race_PmoveAssertFloat("direct AI Q2 step", "step", ai.step, 0.f);
  Race_PmoveAssertFloat("direct AI Q2 step", "step offset",
                        ai.s.step_offset, 0.f);
  ck_assert_msg(memcmp(&ai.s.params, &params, sizeof(params)) == 0,
                "direct AI Q2 step changed replicated parameters");
} END_TEST

typedef enum {
  RACE_PMOVE_Q2_GROUND_HIT,
  RACE_PMOVE_Q2_GROUND_NO_HIT,
  RACE_PMOVE_Q2_GROUND_START_SOLID,
  RACE_PMOVE_Q2_GROUND_TRICK_ONLY
} race_pmove_q2_ground_result_t;

typedef struct {
  vec3_t start;
  vec3_t end;
  box3_t bounds;
  cm_trace_t result;
} race_pmove_q2_ground_trace_t;

typedef struct {
  race_pmove_q2_ground_result_t result;
  vec3_t normal;
  float fraction;
  race_pmove_q2_ground_trace_t traces[32];
  size_t num_traces;
} race_pmove_q2_ground_context_t;

static race_pmove_q2_ground_context_t race_pmove_q2_ground_game_context;
static race_pmove_q2_ground_context_t race_pmove_q2_ground_cgame_context;

static cm_trace_t Race_PmoveQ2GroundTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds,
    race_pmove_q2_ground_context_t *context) {
  cm_trace_t trace = {
    .fraction = 1.f,
    .end = end
  };

  bool hit = context->result == RACE_PMOVE_Q2_GROUND_HIT ||
             context->result == RACE_PMOVE_Q2_GROUND_START_SOLID;
  if (context->result == RACE_PMOVE_Q2_GROUND_TRICK_ONLY) {
    hit = end.z < start.z && end.x != start.x;
  }

  if (hit) {
    trace.fraction = context->fraction;
    trace.end = Vec3_Fmaf(start, trace.fraction, Vec3_Subtract(end, start));
    trace.plane.normal = context->normal;
    trace.contents = CONTENTS_SOLID;
    trace.ent = (void *) Race_PmoveEntityPointer(RACE_PMOVE_ENTITY_RAMP);
  }

  if (context->result == RACE_PMOVE_Q2_GROUND_START_SOLID) {
    trace.start_solid = true;
    trace.fraction = 0.f;
    trace.end = start;
  }

  ck_assert_msg(context->num_traces <
                  sizeof(context->traces) / sizeof(*context->traces),
                "Q2 ground trace log overflow");
  context->traces[context->num_traces++] =
    (race_pmove_q2_ground_trace_t) { start, end, bounds, trace };
  return trace;
}

static cm_trace_t Race_PmoveQ2GroundGameTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds) {
  return Race_PmoveQ2GroundTrace(
    start, end, bounds, &race_pmove_q2_ground_game_context);
}

static cm_trace_t Race_PmoveQ2GroundCgameTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds) {
  return Race_PmoveQ2GroundTrace(
    start, end, bounds, &race_pmove_q2_ground_cgame_context);
}

static void Race_PmoveQ2GroundSetContext(
    race_pmove_q2_ground_context_t *context,
    race_pmove_q2_ground_result_t result, const vec3_t normal) {
  memset(context, 0, sizeof(*context));
  context->result = result;
  context->normal = normal;
  context->fraction = .5f;
}

static pm_move_t Race_PmoveQ2GroundSetup(
    const vec3_t origin, const vec3_t velocity, uint16_t flags,
    uint16_t msec, int16_t up,
    cm_trace_t (*Trace)(vec3_t, vec3_t, box3_t)) {
  pm_move_t move = Race_PmoveQ2AirSetup(origin, velocity);
  move.s.flags = flags;
  move.cmd = (pm_cmd_t) { .msec = msec, .up = up };
  move.bounds = PM_BOUNDS;
  move.Trace = Trace;
  return move;
}

static void Race_PmoveQ2GroundAssertTraceParity(
    const char *name, const race_pmove_q2_ground_context_t *game,
    const race_pmove_q2_ground_context_t *cgame) {
  ck_assert_msg(game->num_traces == cgame->num_traces,
                "%s: GAME/CGAME trace counts differ (%zu != %zu)", name,
                game->num_traces, cgame->num_traces);

  for (size_t i = 0; i < game->num_traces; i++) {
    const race_pmove_q2_ground_trace_t *game_trace = game->traces + i;
    const race_pmove_q2_ground_trace_t *cgame_trace = cgame->traces + i;
    char trace_name[96];
    snprintf(trace_name, sizeof(trace_name), "%s trace %zu", name, i + 1u);
    Race_PmoveAssertVec3(trace_name, "start", game_trace->start,
                         cgame_trace->start);
    Race_PmoveAssertVec3(trace_name, "end", game_trace->end,
                         cgame_trace->end);
    Race_PmoveAssertVec3(trace_name, "bounds.mins", game_trace->bounds.mins,
                         cgame_trace->bounds.mins);
    Race_PmoveAssertVec3(trace_name, "bounds.maxs", game_trace->bounds.maxs,
                         cgame_trace->bounds.maxs);
    Race_PmoveAssertFloat(trace_name, "fraction", game_trace->result.fraction,
                          cgame_trace->result.fraction);
    ck_assert(game_trace->result.start_solid ==
              cgame_trace->result.start_solid);
    ck_assert(game_trace->result.all_solid == cgame_trace->result.all_solid);
    Race_PmoveAssertVec3(trace_name, "result end", game_trace->result.end,
                         cgame_trace->result.end);
    Race_PmoveAssertVec3(trace_name, "normal",
                         game_trace->result.plane.normal,
                         cgame_trace->result.plane.normal);
    ck_assert_int_eq(game_trace->result.contents,
                     cgame_trace->result.contents);
    ck_assert_ptr_eq(game_trace->result.ent, cgame_trace->result.ent);
  }
}

static void Race_PmoveQ2GroundAssertMoveParity(const char *name,
                                                const pm_move_t *game,
                                                const pm_move_t *cgame) {
  ck_assert_msg(memcmp(&game->s, &cgame->s, sizeof(game->s)) == 0,
                "%s: GAME/CGAME pm_state_t differ", name);

  pm_move_t game_copy = *game;
  pm_move_t cgame_copy = *cgame;
  game_copy.Trace = NULL;
  cgame_copy.Trace = NULL;
  ck_assert_msg(memcmp(&game_copy, &cgame_copy, sizeof(game_copy)) == 0,
                "%s: GAME/CGAME pm_move_t differ", name);

  Race_PmoveQ2GroundAssertTraceParity(
    name, &race_pmove_q2_ground_game_context,
    &race_pmove_q2_ground_cgame_context);
}

typedef struct {
  const char *name;
  race_pmove_q2_ground_result_t result;
  vec3_t normal;
  vec3_t velocity;
  vec3_t previous_velocity;
  uint16_t flags;
  int16_t up;
  size_t num_traces;
  uint16_t expected_flags;
  uint16_t expected_time;
  int32_t expected_ground;
  int32_t expected_touches;
} race_pmove_q2_ground_case_t;

static const race_pmove_q2_ground_case_t race_pmove_q2_ground_cases[] = {
  {
    "flat accepted", RACE_PMOVE_Q2_GROUND_HIT,
    { { 0.f, 0.f, 1.f } }, { { 0.f, 0.f, 0.f } },
    { { 0.f, 0.f, 0.f } }, 0, 0, 1, PMF_ON_GROUND, 0,
    RACE_PMOVE_ENTITY_RAMP, 1
  },
  {
    "normal above threshold", RACE_PMOVE_Q2_GROUND_HIT,
    { { 0.7141428f, 0.f, 0x1.666668p-1f } },
    { { 0.f, 0.f, 0.f } }, { { 0.f, 0.f, 0.f } },
    0, 0, 1, PMF_ON_GROUND, 0, RACE_PMOVE_ENTITY_RAMP, 1
  },
  {
    "normal exact threshold", RACE_PMOVE_Q2_GROUND_HIT,
    { { 0.7141428f, 0.f, 0x1.666666p-1f } },
    { { 0.f, 0.f, 0.f } }, { { 0.f, 0.f, 0.f } },
    0, 0, 1, PMF_ON_GROUND, 0, RACE_PMOVE_ENTITY_RAMP, 1
  },
  {
    "normal below threshold", RACE_PMOVE_Q2_GROUND_HIT,
    { { 0.7141429f, 0.f, 0x1.666664p-1f } },
    { { 0.f, 0.f, 0.f } }, { { 0.f, 0.f, 0.f } },
    PMF_ON_GROUND, 0, 1, 0, 0, RACE_PMOVE_ENTITY_NONE, 1
  },
  {
    "start solid steep exception", RACE_PMOVE_Q2_GROUND_START_SOLID,
    { { 1.f, 0.f, 0.f } }, { { 0.f, 0.f, 0.f } },
    { { 0.f, 0.f, 0.f } }, 0, 0, 1, PMF_ON_GROUND, 0,
    RACE_PMOVE_ENTITY_RAMP, 1
  },
  {
    "no hit clears ground", RACE_PMOVE_Q2_GROUND_NO_HIT,
    { { 0.f, 0.f, 0.f } }, { { 0.f, 0.f, 0.f } },
    { { 0.f, 0.f, 0.f } }, PMF_ON_GROUND, 0, 1, 0, 0,
    RACE_PMOVE_ENTITY_NONE, 0
  },
  {
    "landing retains held jump", RACE_PMOVE_Q2_GROUND_HIT,
    { { 0.f, 0.f, 1.f } }, { { 0.f, 0.f, -201.f } },
    { { 0.f, 0.f, -201.f } }, PMF_JUMP_HELD, 300, 1,
    PMF_ON_GROUND | PMF_JUMP_HELD, 0,
    RACE_PMOVE_ENTITY_RAMP, 1
  },
  {
    "serialized previous ground suppresses landing", RACE_PMOVE_Q2_GROUND_HIT,
    { { 0.f, 0.f, 1.f } }, { { 0.f, 0.f, -500.f } },
    { { 0.f, 0.f, -500.f } }, PMF_ON_GROUND, 0, 1,
    PMF_ON_GROUND, 0, RACE_PMOVE_ENTITY_RAMP, 1
  },
  {
    "ground loss exact threshold stays contact", RACE_PMOVE_Q2_GROUND_HIT,
    { { -0x1.c9f25cp-2f, 0.f, 0x1.c9f25cp-1f } },
    { { 320.f, 0.f, 180.f } }, { { 320.f, 0.f, 180.f } },
    PMF_ON_GROUND, 0, 1, PMF_ON_GROUND, 0,
    RACE_PMOVE_ENTITY_RAMP, 1
  },
  {
    "ground loss above threshold", RACE_PMOVE_Q2_GROUND_HIT,
    { { -0x1.c9f25cp-2f, 0.f, 0x1.c9f25cp-1f } },
    { { 320.f, 0.f, 0x1.680002p+7f } },
    { { 320.f, 0.f, 0x1.680002p+7f } }, PMF_ON_GROUND, 0, 0,
    0, 0, RACE_PMOVE_ENTITY_NONE, 0
  },
  {
    "Q2 disables predictive trick probe", RACE_PMOVE_Q2_GROUND_TRICK_ONLY,
    { { 0.f, 0.f, 1.f } }, { { 100.f, 0.f, 10.f } },
    { { 100.f, 0.f, 10.f } }, 0, 300, 1, 0, 0,
    RACE_PMOVE_ENTITY_NONE, 0
  }
};

START_TEST(_Race_PmoveQ2GroundLegacyCases) {
  Race_PmoveUseQ2TestPhysics();
  const vec3_t origin = Vec3(-64.f, -32.f, 0x1.82p+4f);

  for (size_t i = 0;
       i < sizeof(race_pmove_q2_ground_cases) /
             sizeof(*race_pmove_q2_ground_cases); i++) {
    const race_pmove_q2_ground_case_t *test =
      race_pmove_q2_ground_cases + i;
    Race_PmoveQ2GroundSetContext(&race_pmove_q2_ground_game_context,
                                  test->result, test->normal);
    Race_PmoveQ2GroundSetContext(&race_pmove_q2_ground_cgame_context,
                                  test->result, test->normal);
    pm_move_t move = Race_PmoveQ2GroundSetup(
      origin, test->velocity, test->flags, 16, test->up,
      Race_PmoveQ2GroundGameTrace);
    pm_move_t cgame_move = Race_PmoveQ2GroundSetup(
      origin, test->velocity, test->flags, 16, test->up,
      Race_PmoveQ2GroundCgameTrace);
    const pm_params_t params = move.s.params;

    Pm_Q2CheckGroundForTest(&move, test->previous_velocity);
    Pm_Q2CheckGroundForTest_CgameReference(
      &cgame_move, test->previous_velocity);
    Race_PmoveQ2GroundAssertMoveParity(test->name, &move, &cgame_move);

    ck_assert_msg(race_pmove_q2_ground_game_context.num_traces ==
                    test->num_traces,
                  "%s: trace count was %zu, expected %zu", test->name,
                  race_pmove_q2_ground_game_context.num_traces,
                  test->num_traces);
    Race_PmoveAssertVec3(test->name, "origin", move.s.origin, origin);
    Race_PmoveAssertVec3(test->name, "velocity", move.s.velocity,
                         test->velocity);
    ck_assert_msg(move.s.flags == test->expected_flags,
                  "%s: flags were 0x%04x, expected 0x%04x", test->name,
                  move.s.flags, test->expected_flags);
    ck_assert_uint_eq(move.s.time, test->expected_time);
    ck_assert_int_eq(Race_PmoveEntityId(move.ground.ent),
                     test->expected_ground);
    ck_assert_int_eq(move.num_touched, test->expected_touches);
    ck_assert_msg(memcmp(&move.s.params, &params, sizeof(params)) == 0,
                  "%s: grounding changed replicated parameters", test->name);

    if (test->num_traces) {
      const race_pmove_q2_ground_trace_t *trace =
        race_pmove_q2_ground_game_context.traces;
      Race_PmoveAssertVec3(test->name, "trace start", trace->start, origin);
      Race_PmoveAssertVec3(test->name, "trace bounds.mins",
                           trace->bounds.mins, PM_BOUNDS.mins);
      Race_PmoveAssertVec3(test->name, "trace bounds.maxs",
                           trace->bounds.maxs, PM_BOUNDS.maxs);

      if (test->result != RACE_PMOVE_Q2_GROUND_TRICK_ONLY) {
        Race_PmoveAssertVec3(test->name, "trace end", trace->end,
                             Vec3(-64.f, -32.f, 0x1.7ep+4f));
      }
    }
  }
} END_TEST

START_TEST(_Race_PmoveQ2TrickProbeAndJump) {
  const vec3_t origin = Vec3(-64.f, -32.f, 0x1.82p+4f);
  const vec3_t velocity = Vec3(100.f, 0.f, 10.f);

  Race_PmoveUseQ2FixTestPhysics();
  Race_PmoveQ2GroundSetContext(&race_pmove_q2_ground_game_context,
                                RACE_PMOVE_Q2_GROUND_TRICK_ONLY, Vec3_Up());
  pm_move_t probe = Race_PmoveQ2GroundSetup(
    origin, velocity, 0, 16, 300, Race_PmoveQ2GroundGameTrace);
  Pm_Q2CheckGroundForTest(&probe, velocity);

  ck_assert_uint_eq(race_pmove_q2_ground_game_context.num_traces, 1u);
  Race_PmoveAssertVec3("Quetoo Fix trick probe", "trace end",
                       race_pmove_q2_ground_game_context.traces[0].end,
                       Vec3(-0x1.f33334p+5f, -32.f, 0x1.808f5cp+4f));
  Race_PmoveAssertVec3("Quetoo Fix trick probe", "origin",
                       probe.s.origin, origin);
  ck_assert_uint_eq(probe.s.flags,
                    PMF_ON_GROUND | PMF_TIME_TRICK_JUMP);
  ck_assert_uint_eq(probe.s.time, 32u);
  ck_assert_int_eq(Race_PmoveEntityId(probe.ground.ent),
                   RACE_PMOVE_ENTITY_RAMP);

  Race_PmoveQ2GroundSetContext(&race_pmove_q2_ground_game_context,
                                RACE_PMOVE_Q2_GROUND_TRICK_ONLY, Vec3_Up());
  pm_move_t jump = Race_PmoveQ2GroundSetup(
    origin, velocity, 0, 16, 300, Race_PmoveQ2GroundGameTrace);
  jump.s.params.gravity = 0.f;
  Pm_Move(&jump);

  ck_assert(jump.s.flags & PMF_JUMPED);
  ck_assert(jump.s.flags & PMF_JUMP_HELD);
  ck_assert(!(jump.s.flags & (PMF_ON_GROUND | PMF_TIME_TRICK_JUMP)));
  ck_assert_uint_eq(jump.s.time, 0u);
  ck_assert_ptr_null(jump.ground.ent);
  Race_PmoveAssertFloat("Quetoo Fix trick jump", "velocity.z",
                        jump.s.velocity.z, 320.f);
} END_TEST

START_TEST(_Race_PmoveQ2FixGroundThresholds) {
  Race_PmoveUseQ2FixTestPhysics();
  const vec3_t origin = Vec3(-64.f, -32.f, 0x1.82p+4f);
  const vec3_t normal = Race_PmoveRampNormal();

  Race_PmoveQ2GroundSetContext(&race_pmove_q2_ground_game_context,
                                RACE_PMOVE_Q2_GROUND_HIT, normal);
  pm_move_t exact = Race_PmoveQ2GroundSetup(
    origin, Vec3(320.f, 0.f, 80.f), PMF_ON_GROUND, 16, 0,
    Race_PmoveQ2GroundGameTrace);
  Pm_Q2CheckGroundForTest(&exact, exact.s.velocity);
  ck_assert_uint_eq(race_pmove_q2_ground_game_context.num_traces, 1u);
  ck_assert(exact.s.flags & PMF_ON_GROUND);
  ck_assert_int_eq(Race_PmoveEntityId(exact.ground.ent),
                   RACE_PMOVE_ENTITY_RAMP);

  Race_PmoveQ2GroundSetContext(&race_pmove_q2_ground_game_context,
                                RACE_PMOVE_Q2_GROUND_HIT, normal);
  pm_move_t above = Race_PmoveQ2GroundSetup(
    origin, Vec3(320.f, 0.f, 0x1.400002p+6f), PMF_ON_GROUND,
    16, 0, Race_PmoveQ2GroundGameTrace);
  Pm_Q2CheckGroundForTest(&above, above.s.velocity);
  ck_assert_uint_eq(race_pmove_q2_ground_game_context.num_traces, 0u);
  ck_assert(!(above.s.flags & PMF_ON_GROUND));
  ck_assert_ptr_null(above.ground.ent);

  Race_PmoveQ2GroundSetContext(&race_pmove_q2_ground_game_context,
                                RACE_PMOVE_Q2_GROUND_HIT, Vec3_Up());
  pm_move_t landing = Race_PmoveQ2GroundSetup(
    origin, Vec3(0.f, 0.f, -401.f), 0, 16, 0,
    Race_PmoveQ2GroundGameTrace);
  Pm_Q2CheckGroundForTest(&landing, landing.s.velocity);
  ck_assert_uint_eq(landing.s.flags, PMF_ON_GROUND);
  ck_assert_uint_eq(landing.s.time, 0u);
} END_TEST

typedef enum {
  RACE_PMOVE_Q2_RAMP_INFINITE_UP,
  RACE_PMOVE_Q2_RAMP_COURSE,
  RACE_PMOVE_Q2_RAMP_INFINITE_DOWN,
  RACE_PMOVE_Q2_RAMP_INFINITE_STEEP
} race_pmove_q2_ramp_world_t;

typedef struct {
  vec3_t normal;
  float dist;
  float min_x;
  float max_x;
  race_pmove_entity_t entity;
} race_pmove_q2_ramp_surface_t;

typedef struct {
  race_pmove_q2_ground_trace_t traces[256];
  size_t num_traces;
} race_pmove_q2_ramp_context_t;

static race_pmove_q2_ramp_world_t race_pmove_q2_ramp_world;
static race_pmove_q2_ramp_context_t race_pmove_q2_ramp_game_context;
static race_pmove_q2_ramp_context_t race_pmove_q2_ramp_cgame_context;

static vec3_t Race_PmoveQ2RampUpNormal(void) {
  return Vec3(-0x1.c9f25cp-2f, 0.f, 0x1.c9f25cp-1f);
}

static vec3_t Race_PmoveQ2RampDownNormal(void) {
  return Vec3(0x1.f0b684p-3f, 0.f, 0x1.f0b684p-1f);
}

static vec3_t Race_PmoveQ2RampSteepNormal(void) {
  return Vec3(0x1.6da42p-1f, 0.f, 0x1.666664p-1f);
}

static float Race_PmoveQ2RampUpDist(void) {
  return 64.f * Race_PmoveQ2RampUpNormal().z;
}

static float Race_PmoveQ2RampDownDist(void) {
  return 72.f * Race_PmoveQ2RampDownNormal().z;
}

static vec3_t Race_PmoveQ2RampContactOrigin(
    const vec3_t normal, float dist, float x, float y) {
  const float support = Race_PmovePlaneSupportMin(PM_BOUNDS, normal);
  return Vec3(x, y, (dist - normal.x * x - support) / normal.z);
}

static void Race_PmoveQ2RampTraceSurface(
    cm_trace_t *trace, const vec3_t start, const vec3_t end,
    const box3_t bounds, const race_pmove_q2_ramp_surface_t surface) {
  cm_trace_t candidate = {
    .fraction = 1.f,
    .end = end
  };
  Race_PmoveTracePlane(&candidate, start, end, bounds, surface.normal,
                       surface.dist, surface.entity, CONTENTS_SOLID, 0);

  if (!candidate.ent) {
    return;
  }

  const float footprint_min = candidate.end.x + bounds.mins.x;
  const float footprint_max = candidate.end.x + bounds.maxs.x;
  if (footprint_max < surface.min_x || footprint_min > surface.max_x) {
    return;
  }

  Race_PmoveTraceCandidate(trace, candidate);
}

static cm_trace_t Race_PmoveQ2RampTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds,
    race_pmove_q2_ramp_context_t *context) {
  cm_trace_t trace = {
    .fraction = 1.f,
    .end = end
  };
  const float unbounded = FLT_MAX;

  switch (race_pmove_q2_ramp_world) {
    case RACE_PMOVE_Q2_RAMP_INFINITE_UP:
      Race_PmoveQ2RampTraceSurface(&trace, start, end, bounds,
        (race_pmove_q2_ramp_surface_t) {
          Race_PmoveQ2RampUpNormal(), Race_PmoveQ2RampUpDist(),
          -unbounded, unbounded, RACE_PMOVE_ENTITY_RAMP
        });
      break;

    case RACE_PMOVE_Q2_RAMP_COURSE:
      // Highest-priority surface first resolves each shared convex seam.
      Race_PmoveQ2RampTraceSurface(&trace, start, end, bounds,
        (race_pmove_q2_ramp_surface_t) {
          Race_PmoveQ2RampDownNormal(), Race_PmoveQ2RampDownDist(),
          32.f, 160.f, RACE_PMOVE_ENTITY_RAMP
        });
      Race_PmoveQ2RampTraceSurface(&trace, start, end, bounds,
        (race_pmove_q2_ramp_surface_t) {
          Vec3_Up(), 64.f, 0.f, 32.f, RACE_PMOVE_ENTITY_FLOOR
        });
      Race_PmoveQ2RampTraceSurface(&trace, start, end, bounds,
        (race_pmove_q2_ramp_surface_t) {
          Race_PmoveQ2RampUpNormal(), Race_PmoveQ2RampUpDist(),
          -128.f, 0.f, RACE_PMOVE_ENTITY_RAMP
        });
      Race_PmoveQ2RampTraceSurface(&trace, start, end, bounds,
        (race_pmove_q2_ramp_surface_t) {
          Vec3_Up(), 32.f, 160.f, 192.f, RACE_PMOVE_ENTITY_FLOOR
        });
      Race_PmoveQ2RampTraceSurface(&trace, start, end, bounds,
        (race_pmove_q2_ramp_surface_t) {
          Vec3_Up(), 0.f, -unbounded, -128.f,
          RACE_PMOVE_ENTITY_FLOOR
        });
      break;

    case RACE_PMOVE_Q2_RAMP_INFINITE_DOWN:
      Race_PmoveQ2RampTraceSurface(&trace, start, end, bounds,
        (race_pmove_q2_ramp_surface_t) {
          Race_PmoveQ2RampDownNormal(), Race_PmoveQ2RampDownDist(),
          -unbounded, unbounded, RACE_PMOVE_ENTITY_RAMP
        });
      break;

    case RACE_PMOVE_Q2_RAMP_INFINITE_STEEP:
      Race_PmoveQ2RampTraceSurface(&trace, start, end, bounds,
        (race_pmove_q2_ramp_surface_t) {
          Race_PmoveQ2RampSteepNormal(), 0.f,
          -unbounded, unbounded, RACE_PMOVE_ENTITY_STEEP
        });
      break;
  }

  ck_assert_msg(context->num_traces <
                  sizeof(context->traces) / sizeof(*context->traces),
                "Q2 ramp trace log overflow");
  context->traces[context->num_traces++] =
    (race_pmove_q2_ground_trace_t) { start, end, bounds, trace };
  return trace;
}

static cm_trace_t Race_PmoveQ2RampGameTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds) {
  return Race_PmoveQ2RampTrace(
    start, end, bounds, &race_pmove_q2_ramp_game_context);
}

static cm_trace_t Race_PmoveQ2RampCgameTrace(
    const vec3_t start, const vec3_t end, const box3_t bounds) {
  return Race_PmoveQ2RampTrace(
    start, end, bounds, &race_pmove_q2_ramp_cgame_context);
}

static void Race_PmoveQ2RampResetContexts(void) {
  memset(&race_pmove_q2_ramp_game_context, 0,
         sizeof(race_pmove_q2_ramp_game_context));
  memset(&race_pmove_q2_ramp_cgame_context, 0,
         sizeof(race_pmove_q2_ramp_cgame_context));
}

static pm_move_t Race_PmoveQ2RampSetup(
    const vec3_t origin, const vec3_t velocity, uint16_t flags,
    cm_trace_t (*Trace)(vec3_t, vec3_t, box3_t)) {
  pm_move_t move = Race_PmoveQ2AirSetup(origin, velocity);
  move.s.flags = flags;
  move.s.params.gravity = 800.f;
  move.Trace = Trace;
  return move;
}

static void Race_PmoveQ2RampAssertParity(
    const char *name, const pm_move_t *game, const pm_move_t *cgame) {
  ck_assert_msg(memcmp(&game->s, &cgame->s, sizeof(game->s)) == 0,
                "%s: GAME/CGAME pm_state_t differ", name);

  pm_move_t game_copy = *game;
  pm_move_t cgame_copy = *cgame;
  game_copy.Trace = NULL;
  cgame_copy.Trace = NULL;
  ck_assert_msg(memcmp(&game_copy, &cgame_copy, sizeof(game_copy)) == 0,
                "%s: GAME/CGAME pm_move_t differ", name);
  ck_assert_uint_eq(race_pmove_q2_ramp_game_context.num_traces,
                    race_pmove_q2_ramp_cgame_context.num_traces);

  for (size_t i = 0;
       i < race_pmove_q2_ramp_game_context.num_traces; i++) {
    const race_pmove_q2_ground_trace_t *game_trace =
      race_pmove_q2_ramp_game_context.traces + i;
    const race_pmove_q2_ground_trace_t *cgame_trace =
      race_pmove_q2_ramp_cgame_context.traces + i;
    ck_assert_msg(memcmp(game_trace, cgame_trace, sizeof(*game_trace)) == 0,
                  "%s: GAME/CGAME trace %zu differs", name, i + 1u);
  }
}

START_TEST(_Race_PmoveQ2RampEntryClimbCrestDown) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);
  race_pmove_q2_ramp_world = RACE_PMOVE_Q2_RAMP_COURSE;

  pm_move_t game = Race_PmoveQ2RampSetup(
    Vec3(-160.f, -64.f, 24.f), Vec3(300.f, 0.f, 0.f),
    PMF_ON_GROUND, Race_PmoveQ2RampGameTrace);
  pm_move_t cgame = Race_PmoveQ2RampSetup(
    Vec3(-160.f, -64.f, 24.f), Vec3(300.f, 0.f, 0.f),
    PMF_ON_GROUND, Race_PmoveQ2RampCgameTrace);
  const pm_params_t params = game.s.params;
  bool saw_flat = false;
  bool saw_ramp = false;
  bool saw_crest_loss = false;
  bool saw_down = false;
  bool saw_air = false;
  vec3_t previous_ground_normal = Vec3_Zero();

  for (size_t i = 0; i < 72u; i++) {
    Race_PmoveQ2RampResetContexts();
    game.cmd = (pm_cmd_t) {
      .msec = (uint16_t[]) { 8, 16, 25, 16 }[i % 4u],
      .forward = 300
    };
    cgame.cmd = game.cmd;
    Pm_Move(&game);
    Pm_Move(&cgame);

    char name[64];
    snprintf(name, sizeof(name), "Q2 course command %zu", i + 1u);
    Race_PmoveQ2RampAssertParity(name, &game, &cgame);

    if (Race_PmoveEntityId(game.ground.ent) == RACE_PMOVE_ENTITY_FLOOR &&
        game.s.origin.z < 40.f) {
      saw_flat = true;
    }
    if (Race_PmoveEntityId(game.ground.ent) == RACE_PMOVE_ENTITY_RAMP &&
        game.ground.plane.normal.x < 0.f) {
      saw_ramp = true;
    }
    if (!(game.s.flags & PMF_ON_GROUND) &&
        previous_ground_normal.x < 0.f &&
        game.s.origin.x > 0.f && game.s.origin.x < 32.f) {
      saw_crest_loss = true;
    }
    if (Race_PmoveEntityId(game.ground.ent) == RACE_PMOVE_ENTITY_RAMP &&
        game.ground.plane.normal.x > 0.f) {
      saw_down = true;
    }
    if (!(game.s.flags & PMF_ON_GROUND)) {
      saw_air = true;
    }

    ck_assert_msg(memcmp(&game.s.params, &params, sizeof(params)) == 0 &&
                  memcmp(&cgame.s.params, &params, sizeof(params)) == 0,
                  "Q2 course command %zu changed replicated parameters",
                  i + 1u);
    Race_PmoveAssertFloat("Q2 course", "step", game.step, 0.f);
    previous_ground_normal = game.ground.plane.normal;
  }

  ck_assert_msg(saw_flat, "Q2 course never established initial flat ground");
  ck_assert_msg(saw_ramp, "Q2 course never established upward-ramp ground");
  ck_assert_msg(saw_crest_loss,
                "Q2 course never exercised convex-crest ground loss");
  ck_assert_msg(saw_down, "Q2 course never established downhill contact");
  ck_assert_msg(saw_air, "Q2 course never exercised ramp or gap ground loss");
} END_TEST

START_TEST(_Race_PmoveQ2RampNoAutohopGameCgame) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);
  race_pmove_q2_ramp_world = RACE_PMOVE_Q2_RAMP_INFINITE_UP;
  const vec3_t origin = Race_PmoveQ2RampContactOrigin(
    Race_PmoveQ2RampUpNormal(), Race_PmoveQ2RampUpDist(),
    -144.f, -64.f);

  pm_move_t game = Race_PmoveQ2RampSetup(
    origin, Vec3(320.f, 0.f, 120.f),
    PMF_ON_GROUND | PMF_JUMP_HELD, Race_PmoveQ2RampGameTrace);
  pm_move_t cgame = Race_PmoveQ2RampSetup(
    origin, Vec3(320.f, 0.f, 120.f),
    PMF_ON_GROUND | PMF_JUMP_HELD, Race_PmoveQ2RampCgameTrace);
  pm_move_t states[4];

  static const pm_cmd_t commands[] = {
    { .msec = 16, .forward = 300, .up = 10 },
    { .msec = 8, .forward = 300, .up = 0 },
    { .msec = 8, .forward = 300, .up = 10 },
    { .msec = 16, .forward = 300, .up = 10 }
  };

  for (size_t i = 0; i < sizeof(commands) / sizeof(*commands); i++) {
    Race_PmoveQ2RampResetContexts();
    game.cmd = cgame.cmd = commands[i];
    Pm_Move(&game);
    Pm_Move(&cgame);
    states[i] = game;

    char name[64];
    snprintf(name, sizeof(name), "Q2 ramp jump command %zu", i + 1u);
    Race_PmoveQ2RampAssertParity(name, &game, &cgame);
  }

  // Held contact stays blocked; release clears the existing edge; the fresh
  // press creates exactly one jump; continued hold creates no further event.
  ck_assert(states[0].s.flags & PMF_JUMP_HELD);
  ck_assert(!(states[0].s.flags & PMF_JUMPED));
  ck_assert(states[0].s.flags & PMF_ON_GROUND);
  ck_assert_ptr_nonnull(states[0].ground.ent);
  ck_assert(!(states[1].s.flags & PMF_JUMP_HELD));
  ck_assert(states[1].s.flags & PMF_ON_GROUND);
  ck_assert_ptr_nonnull(states[1].ground.ent);
  ck_assert(states[2].s.flags & PMF_JUMPED);
  ck_assert(states[2].s.flags & PMF_JUMP_HELD);
  ck_assert(!(states[2].s.flags & PMF_ON_GROUND));
  ck_assert_ptr_null(states[2].ground.ent);
  ck_assert(states[3].s.flags & PMF_JUMP_HELD);
  ck_assert(!(states[3].s.flags & PMF_JUMPED));
  ck_assert_msg(!!(states[3].s.flags & PMF_ON_GROUND) ==
                  !!states[3].ground.ent,
                "continued hold produced inconsistent ground state");
} END_TEST

START_TEST(_Race_PmoveQ2AiDirectRamp) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);
  race_pmove_q2_ramp_world = RACE_PMOVE_Q2_RAMP_INFINITE_UP;
  Race_PmoveQ2RampResetContexts();
  const vec3_t origin = Race_PmoveQ2RampContactOrigin(
    Race_PmoveQ2RampUpNormal(), Race_PmoveQ2RampUpDist(),
    -144.f, -64.f);
  pm_move_t ai = Race_PmoveQ2RampSetup(
    origin, Vec3(300.f, 0.f, 0.f), PMF_ON_GROUND,
    Race_PmoveQ2RampGameTrace);
  ai.cmd = (pm_cmd_t) { .msec = 16, .forward = 300 };
  const pm_params_t params = ai.s.params;

  // GAME-side AI invokes Pm_Move directly without G_PrepareMove.
  Pm_Move(&ai);

  ck_assert(ai.s.flags & PMF_ON_GROUND);
  ck_assert_int_eq(Race_PmoveEntityId(ai.ground.ent),
                   RACE_PMOVE_ENTITY_RAMP);
  Race_PmoveAssertVec3("direct AI Q2 ramp", "ground normal",
                       ai.ground.plane.normal, Race_PmoveQ2RampUpNormal());
  ck_assert(ai.s.origin.z > origin.z);
  Race_PmoveAssertFloat("direct AI Q2 ramp", "velocity.z",
                        ai.s.velocity.z, 0.f);
  Race_PmoveAssertFloat("direct AI Q2 ramp", "step", ai.step, 0.f);
  ck_assert_msg(memcmp(&ai.s.params, &params, sizeof(params)) == 0,
                "direct AI Q2 ramp changed replicated parameters");
} END_TEST

START_TEST(_Race_PmoveQ2DownhillAndSteepRamp) {
  Race_PmoveUseQ2TestPhysics();
  Pm_SetQ2AirWishspeedCapForTest(0.f);

  race_pmove_q2_ramp_world = RACE_PMOVE_Q2_RAMP_INFINITE_DOWN;
  Race_PmoveQ2RampResetContexts();
  const vec3_t downhill_origin = Race_PmoveQ2RampContactOrigin(
    Race_PmoveQ2RampDownNormal(), Race_PmoveQ2RampDownDist(),
    48.f, -64.f);
  pm_move_t downhill = Race_PmoveQ2RampSetup(
    downhill_origin, Vec3(100.f, 0.f, 0.f), PMF_ON_GROUND,
    Race_PmoveQ2RampGameTrace);
  downhill.s.params.accel_ground = 0.f;
  downhill.s.params.friction_ground = 0.f;
  downhill.cmd = (pm_cmd_t) { .msec = 8 };

  Pm_Move(&downhill);
  ck_assert(downhill.s.flags & PMF_ON_GROUND);
  ck_assert_int_eq(Race_PmoveEntityId(downhill.ground.ent),
                   RACE_PMOVE_ENTITY_RAMP);
  Race_PmoveAssertVec3("Q2 downhill contact", "ground normal",
                       downhill.ground.plane.normal,
                       Race_PmoveQ2RampDownNormal());
  Race_PmoveAssertFloat("Q2 downhill contact", "origin.z",
                        downhill.s.origin.z, downhill_origin.z);

  Race_PmoveQ2RampResetContexts();
  downhill.cmd = (pm_cmd_t) { .msec = 8 };
  Pm_Move(&downhill);
  ck_assert(!(downhill.s.flags & PMF_ON_GROUND));
  ck_assert_ptr_null(downhill.ground.ent);
  Race_PmoveAssertFloat("Q2 downhill loss", "origin.z",
                        downhill.s.origin.z, downhill_origin.z);

  race_pmove_q2_ramp_world = RACE_PMOVE_Q2_RAMP_INFINITE_STEEP;
  Race_PmoveQ2RampResetContexts();
  vec3_t steep_origin = Race_PmoveQ2RampContactOrigin(
    Race_PmoveQ2RampSteepNormal(), 0.f, 0.f, -64.f);
  steep_origin.z += .125f;
  pm_move_t steep = Race_PmoveQ2RampSetup(
    steep_origin, Vec3_Zero(), PMF_ON_GROUND,
    Race_PmoveQ2RampGameTrace);
  steep.s.params.gravity = 0.f;
  steep.cmd = (pm_cmd_t) { .msec = 8 };
  Pm_Move(&steep);

  ck_assert(!(steep.s.flags & PMF_ON_GROUND));
  ck_assert_ptr_null(steep.ground.ent);
  ck_assert_msg(steep.num_touched > 0,
                "non-walkable steep ramp was not retained as a touch");
} END_TEST

typedef struct {
  vec3_t origins[32];
  box3_t bounds[32];
  size_t num_traces;
  size_t accept_trace;
} race_pmove_q2_snap_context_t;

static race_pmove_q2_snap_context_t race_pmove_q2_snap_context;

static cm_trace_t Race_PmoveQ2SnapTrace(const vec3_t start,
                                        const vec3_t end,
                                        const box3_t bounds) {
  (void) end;

  race_pmove_q2_snap_context.origins[
    race_pmove_q2_snap_context.num_traces] = start;
  race_pmove_q2_snap_context.bounds[
    race_pmove_q2_snap_context.num_traces] = bounds;
  const bool accepted = race_pmove_q2_snap_context.num_traces++ ==
                        race_pmove_q2_snap_context.accept_trace;

  return (cm_trace_t) {
    .all_solid = !accepted,
    .start_solid = !accepted,
    .fraction = accepted ? 1.f : 0.f,
    .end = start
  };
}

static void Race_PmoveQ2SnapReset(size_t accept_trace) {
  memset(&race_pmove_q2_snap_context, 0,
         sizeof(race_pmove_q2_snap_context));
  race_pmove_q2_snap_context.accept_trace = accept_trace;
}

static pm_move_t Race_PmoveQ2SnapSetup(vec3_t origin, vec3_t velocity) {
  pm_move_t move = Race_PmoveSetup(RACE_PMOVE_EMPTY_IDLE);
  move.s.origin = origin;
  move.s.velocity = velocity;
  move.bounds = PM_BOUNDS;
  move.Trace = Race_PmoveQ2SnapTrace;
  move.num_touched = 0;
  return move;
}

START_TEST(_Race_PmoveQ2CommandTimeQuantization) {
  static const struct {
    uint16_t msec;
    uint16_t ticks;
  } cases[] = {
    { 1, 1 }, { 7, 1 }, { 8, 1 }, { 9, 1 }, { 15, 1 },
    { 16, 2 }, { 17, 2 }, { 25, 3 }, { 50, 6 }, { 100, 12 }
  };

  for (size_t i = 0; i < lengthof(cases); i++) {
    ck_assert_uint_eq(Pm_Q2TimeForTest(cases[i].msec), cases[i].ticks);
  }
  ck_assert_uint_eq(Pm_Q2TimeForTest(0), 1u);
  ck_assert_uint_eq(Pm_Q2TimeForTest(144), 18u);
  ck_assert_uint_eq(Pm_Q2TimeForTest(200), 25u);
  ck_assert_uint_eq(Pm_Q2TimeForTest(2040), 255u);

  // Timer ticks must not quantize the continuous movement time.
  Race_PmoveUseQ2TestPhysics();
  pm_move_t move = Race_PmoveSetup(RACE_PMOVE_EMPTY_IDLE);
  move.s.params.gravity = 0.f;
  move.s.velocity = Vec3(100.f, 0.f, 0.f);
  const vec3_t start = move.s.origin;
  move.cmd.msec = 7;
  Pm_Move(&move);
  Race_PmoveAssertFloat("Q2 raw command seconds", "origin.x",
                        move.s.origin.x, start.x + 100.f * .007f);
} END_TEST

START_TEST(_Race_PmoveQ2MovementTimerConsumers) {
  static const struct {
    uint16_t flags;
    uint16_t time;
  } consumers[] = {
    { PMF_TIME_TELEPORT, 20 },
    { PMF_TIME_WATER_JUMP, 255 },
    { PMF_TIME_LAND, 18 },
    { PMF_TIME_TRICK_JUMP, 32 },
    { PMF_TIME_TRICK_START, 32 },
    { PMF_TIME_PUSHED, 120 },
    { PMF_TIME_PUSHED, 240 }
  };

  Race_PmoveUseQ2TestPhysics();
  for (size_t i = 0; i < lengthof(consumers); i++) {
    pm_move_t move = Race_PmoveSetup(RACE_PMOVE_EMPTY_IDLE);
    move.s.params.gravity = 0.f;
    move.s.velocity.z = consumers[i].flags == PMF_TIME_WATER_JUMP
      ? 100.f
      : 0.f;
    move.s.flags = consumers[i].flags;
    move.s.time = consumers[i].time;
    move.cmd.msec = 16;
    Pm_Move(&move);
    ck_assert_uint_eq(move.s.time, consumers[i].time - 2u);
    ck_assert(move.s.flags & consumers[i].flags);
  }

  pm_move_t water_jump = Race_PmoveSetup(RACE_PMOVE_EMPTY_IDLE);
  water_jump.s.params.gravity = 0.f;
  water_jump.s.params.friction_air = 100.f;
  water_jump.s.params.speed_jump = 270.f;
  water_jump.s.velocity = Vec3(50.f, 0.f, 350.f);
  water_jump.s.flags = PMF_TIME_WATER_JUMP;
  water_jump.s.time = 2;
  water_jump.cmd.msec = 8;
  Pm_Move(&water_jump);
  ck_assert_uint_eq(water_jump.s.time, 1u);
  ck_assert(water_jump.s.flags & PMF_TIME_WATER_JUMP);
  Race_PmoveAssertVec3("Q2 timed water jump", "velocity",
                       water_jump.s.velocity, Vec3(50.f, 0.f, 350.f));

  water_jump.s.velocity.z = -1.f;
  water_jump.s.time = 2;
  water_jump.s.flags = PMF_TIME_WATER_JUMP;
  water_jump.cmd.msec = 8;
  Pm_Move(&water_jump);
  ck_assert_uint_eq(water_jump.s.time, 0u);
  ck_assert(!(water_jump.s.flags & PMF_TIME_MASK));

  pm_move_t timed_wall = Race_PmoveSetup(RACE_PMOVE_WALL_CLIP);
  timed_wall.s.flags |= PMF_TIME_LAND;
  timed_wall.s.time = 10;
  pm_move_t untimed_wall = Race_PmoveSetup(RACE_PMOVE_WALL_CLIP);
  Pm_Move(&timed_wall);
  Pm_Move(&untimed_wall);
  ck_assert_uint_eq(timed_wall.s.time, 4u);
  ck_assert(timed_wall.s.flags & PMF_TIME_LAND);
  ck_assert_msg(timed_wall.s.velocity.x > untimed_wall.s.velocity.x,
                "Q2 active movement timer did not restore primal velocity");

  // Named Q2 presets retain the timer consumer for serialized state, but never
  // create a landing penalty at either the initial or post-move ground probe.
  static const race_physics_preset_id_t landing_presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  for (size_t i = 0; i < lengthof(landing_presets); i++) {
    Race_PmoveUseQ2NamedTestPhysics(landing_presets[i], false);
    pm_move_t landing = Race_PmoveSetup(RACE_PMOVE_LANDING);
    landing.s.params = Race_PmoveQ2NamedParams(landing_presets[i]);
    landing.s.origin = Vec3(-64.f, -32.f, 24.f);
    landing.s.velocity = Vec3(0.f, 0.f, -800.f);
    landing.s.flags = 0;
    landing.s.time = 0;
    landing.cmd.msec = 8;
    Pm_Move(&landing);
    ck_assert(landing.s.flags & PMF_ON_GROUND);
    ck_assert(!(landing.s.flags & PMF_TIME_LAND));
    ck_assert_uint_eq(landing.s.time, 0u);

    landing = Race_PmoveQ2Landing(
      landing_presets[i], -800.f, 8, 0, 0);
    ck_assert(landing.s.flags & PMF_ON_GROUND);
    ck_assert(!(landing.s.flags & PMF_TIME_LAND));
    ck_assert_uint_eq(landing.s.time, 0u);
  }

  // Expiry happens before dispatch: active teleport pauses, while the expiry
  // command moves immediately and preserves non-time flags.
  pm_move_t paused = Race_PmoveSetup(RACE_PMOVE_EMPTY_IDLE);
  paused.s.params.gravity = 0.f;
  paused.s.velocity = Vec3(100.f, 0.f, 0.f);
  paused.s.flags = PMF_TIME_TELEPORT | PMF_JUMP_HELD;
  paused.s.time = 2;
  paused.cmd.msec = 8;
  paused.cmd.up = 10;
  const vec3_t paused_origin = paused.s.origin;
  Pm_Move(&paused);
  Race_PmoveAssertVec3("Q2 active teleport", "origin", paused.s.origin,
                       paused_origin);
  ck_assert_uint_eq(paused.s.time, 1u);

  paused.cmd.msec = 8;
  paused.cmd.up = 10;
  Pm_Move(&paused);
  ck_assert_uint_eq(paused.s.time, 0u);
  ck_assert(!(paused.s.flags & PMF_TIME_MASK));
  ck_assert(paused.s.flags & PMF_JUMP_HELD);
  Race_PmoveAssertFloat("Q2 teleport expiry", "origin.x",
                        paused.s.origin.x, paused_origin.x + .8f);

  pm_move_t expired_water = Race_PmoveSetup(RACE_PMOVE_EMPTY_IDLE);
  expired_water.s.params.gravity = 0.f;
  expired_water.s.velocity = Vec3(100.f, 0.f, 0.f);
  expired_water.s.flags = PMF_TIME_WATER_JUMP;
  expired_water.s.time = 1;
  expired_water.cmd.msec = 8;
  pm_move_t ordinary = expired_water;
  ordinary.s.flags = 0;
  ordinary.s.time = 0;
  Pm_Move(&expired_water);
  Pm_Move(&ordinary);
  ck_assert_int_eq(memcmp(&expired_water.s, &ordinary.s,
                          sizeof(ordinary.s)), 0);

  Race_PmoveUseQ2FixTestPhysics();
  pm_move_t trick = Race_PmoveSetup(RACE_PMOVE_JUMP);
  trick.s.params.gravity = 0.f;
  trick.s.flags |= PMF_TIME_TRICK_JUMP | PMF_JUMP_HELD;
  trick.s.time = 1;
  Race_PmoveCommand(&trick, 8, 10);
  ck_assert_uint_eq(trick.s.time, 0u);
  ck_assert(!(trick.s.flags & PMF_JUMPED));
  ck_assert(trick.s.flags & PMF_JUMP_HELD);
  Race_PmoveCommand(&trick, 8, 0);
  ck_assert(!(trick.s.flags & PMF_JUMP_HELD));
  Race_PmoveCommand(&trick, 8, 10);
  ck_assert(trick.s.flags & PMF_JUMPED);
  Race_PmoveAssertFloat("Q2 trick expiry fresh press", "velocity.z",
                        trick.s.velocity.z, PM_SPEED_JUMP);
  Race_PmoveCommand(&trick, 8, 10);
  ck_assert(!(trick.s.flags & PMF_JUMPED));
} END_TEST

START_TEST(_Race_PmoveQ2ContinuousToSnappedState) {
  const race_pmove_q2_air_case_t *air = race_pmove_q2_air_cases;

  Race_PmoveUseQ2TestPhysics();
  pm_move_t continuous = Race_PmoveQ2AirSetup(
    Vec3(-96.f, -48.f, 128.f), air->velocity);
  continuous.s.params.gravity = air->gravity;
  Race_PmoveQ2AirCommand(&continuous, air->msec, air->forward,
                          air->right, 0, air->angles);
  Race_PmoveAssertVec3("Phase 8H continuous state", "origin",
                       continuous.s.origin, air->expected_origin);
  Race_PmoveAssertVec3("Phase 8H continuous state", "velocity",
                       continuous.s.velocity, air->expected_velocity);

  Race_PmoveUseQ2SnapTestPhysics();
  pm_move_t snapped = Race_PmoveQ2AirSetup(
    Vec3(-96.f, -48.f, 128.f), air->velocity);
  snapped.s.params.gravity = air->gravity;
  Race_PmoveQ2AirCommand(&snapped, air->msec, air->forward,
                          air->right, 0, air->angles);
  const vec3_t expected_origin = Vec3(
    Pm_Q2SnapFloatForTest(air->expected_origin.x),
    Pm_Q2SnapFloatForTest(air->expected_origin.y),
    Pm_Q2SnapFloatForTest(air->expected_origin.z));
  const vec3_t expected_velocity = Vec3(
    Pm_Q2SnapFloatForTest(air->expected_velocity.x),
    Pm_Q2SnapFloatForTest(air->expected_velocity.y),
    Pm_Q2SnapFloatForTest(air->expected_velocity.z));
  Race_PmoveAssertVec3("Phase 8H snapped state", "origin",
                       snapped.s.origin, expected_origin);
  Race_PmoveAssertVec3("Phase 8H snapped state", "velocity",
                       snapped.s.velocity, expected_velocity);
} END_TEST

START_TEST(_Race_PmoveQ2SnapRounding) {
  static const struct {
    float value;
    float expected;
  } cases[] = {
    { 0.f, 0.f }, { -.0f, -.0f }, { .01f, 0.f }, { -.01f, -.0f },
    { .0625f, .125f }, { -.0625f, -.125f },
    { .1249f, .125f }, { -.1249f, -.125f },
    { .125f, .125f }, { -.125f, -.125f },
    { .1875f, .25f }, { -.1875f, -.25f },
    { .2499f, .25f }, { -.2499f, -.25f },
    { .25f, .25f }, { -.25f, -.25f },
    { 1.0625f, 1.125f }, { -1.0625f, -1.125f },
    { 1.125f, 1.125f }, { -1.125f, -1.125f },
    { 12345.0625f, 12345.125f }, { -12345.0625f, -12345.125f }
  };

  Pm_SetQ2SnapModeForTest(RACE_PHYSICS_Q2_SNAP_NEAREST);
  Pm_SetQ2SnapModeForTest_CgameReference(RACE_PHYSICS_Q2_SNAP_NEAREST);
  for (size_t i = 0; i < lengthof(cases); i++) {
    Race_PmoveAssertFloat("Q2 snap rounding", "value",
                           Pm_Q2SnapFloatForTest(cases[i].value),
                           cases[i].expected);
    Race_PmoveAssertFloat("Q2 snap rounding cgame", "value",
                          Pm_Q2SnapFloatForTest_CgameReference(cases[i].value),
                          cases[i].expected);
  }

  static const struct {
    float value;
    float expected;
  } truncate_cases[] = {
    { .01f, 0.f }, { -.01f, 0.f },
    { .1249f, 0.f }, { -.1249f, 0.f },
    { .125f, .125f }, { -.125f, -.125f },
    { .1875f, .125f }, { -.1875f, -.125f },
    { 1.0625f, 1.f }, { -1.0625f, -1.f }
  };
  Pm_SetQ2SnapModeForTest(RACE_PHYSICS_Q2_SNAP_TRUNCATE);
  Pm_SetQ2SnapModeForTest_CgameReference(RACE_PHYSICS_Q2_SNAP_TRUNCATE);
  for (size_t i = 0; i < lengthof(truncate_cases); i++) {
    Race_PmoveAssertFloat("Q2 snap truncation", "value",
                          Pm_Q2SnapFloatForTest(truncate_cases[i].value),
                          truncate_cases[i].expected);
    Race_PmoveAssertFloat(
      "Q2 snap truncation cgame", "value",
      Pm_Q2SnapFloatForTest_CgameReference(truncate_cases[i].value),
      truncate_cases[i].expected);
  }

  Pm_SetQ2SnapModeForTest(RACE_PHYSICS_Q2_SNAP_OFF);
  Pm_SetQ2SnapModeForTest_CgameReference(RACE_PHYSICS_Q2_SNAP_OFF);
  Race_PmoveAssertFloat("Q2 snap disabled", "value",
                        Pm_Q2SnapFloatForTest(-1.0625f), -1.0625f);
  Race_PmoveAssertFloat(
    "Q2 snap disabled cgame", "value",
    Pm_Q2SnapFloatForTest_CgameReference(-1.0625f), -1.0625f);
} END_TEST

START_TEST(_Race_PmoveQ2InitialSnapCandidates) {
  Race_PmoveUseQ2SnapTestPhysics();
  const vec3_t base = Vec3(1.f, 2.f, 3.f);
  static const float offsets[] = { 0.f, -.125f, .125f };

  Race_PmoveQ2SnapReset(26);
  pm_move_t move = Race_PmoveQ2SnapSetup(base, Vec3_Zero());
  Pm_Q2InitialSnapPositionForTest(&move, Vec3(9.f, 8.f, 7.f));
  ck_assert_uint_eq(race_pmove_q2_snap_context.num_traces, 27u);

  size_t candidate = 0;
  for (size_t z = 0; z < lengthof(offsets); z++) {
    for (size_t y = 0; y < lengthof(offsets); y++) {
      for (size_t x = 0; x < lengthof(offsets); x++, candidate++) {
        const vec3_t expected = Vec3(base.x + offsets[x],
                                     base.y + offsets[y],
                                     base.z + offsets[z]);
        Race_PmoveAssertVec3("Q2 initial snap", "candidate",
          race_pmove_q2_snap_context.origins[candidate], expected);
        ck_assert_int_eq(memcmp(
          race_pmove_q2_snap_context.bounds + candidate,
          &PM_BOUNDS, sizeof(PM_BOUNDS)), 0);
      }
    }
  }
  Race_PmoveAssertVec3("Q2 initial snap", "origin", move.s.origin,
                       Vec3(1.125f, 2.125f, 3.125f));
  ck_assert_int_eq(move.num_touched, 0);

  Race_PmoveQ2SnapReset(SIZE_MAX);
  move = Race_PmoveQ2SnapSetup(base, Vec3_Zero());
  Pm_Q2InitialSnapPositionForTest(&move, Vec3(9.f, 8.f, 7.f));
  ck_assert_uint_eq(race_pmove_q2_snap_context.num_traces, 27u);
  Race_PmoveAssertVec3("Q2 initial snap failure", "origin",
                       move.s.origin, base);
  ck_assert_int_eq(move.num_touched, 0);
} END_TEST

START_TEST(_Race_PmoveQ2FinalSnapCandidatesAndFallback) {
  Race_PmoveUseQ2SnapTestPhysics();
  static const vec3_t expected[] = {
    { { 1.125f, -2.125f, 3.125f } },
    { { 1.125f, -2.125f, 3.25f } },
    { { 1.25f, -2.125f, 3.125f } },
    { { 1.125f, -2.25f, 3.125f } },
    { { 1.25f, -2.25f, 3.125f } },
    { { 1.25f, -2.125f, 3.25f } },
    { { 1.125f, -2.25f, 3.25f } },
    { { 1.25f, -2.25f, 3.25f } }
  };
  const vec3_t origin = Vec3(1.0625f, -2.0625f, 3.0625f);
  const vec3_t velocity = Vec3(.1875f, -.1875f, .2499f);
  const vec3_t fallback = Vec3(9.f, 8.f, 7.f);

  for (size_t accepted = 0; accepted < lengthof(expected); accepted++) {
    Race_PmoveQ2SnapReset(accepted);
    pm_move_t move = Race_PmoveQ2SnapSetup(origin, velocity);
    Pm_Q2SnapPositionForTest(&move, fallback);
    ck_assert_uint_eq(race_pmove_q2_snap_context.num_traces,
                      accepted + 1u);
    Race_PmoveAssertVec3("Q2 final snap", "origin", move.s.origin,
                         expected[accepted]);
    Race_PmoveAssertVec3("Q2 final snap", "velocity", move.s.velocity,
                         Vec3(.25f, -.25f, .25f));
    ck_assert_int_eq(move.num_touched, 0);
  }

  Race_PmoveQ2SnapReset(SIZE_MAX);
  pm_move_t move = Race_PmoveQ2SnapSetup(origin, velocity);
  Pm_Q2SnapPositionForTest(&move, fallback);
  ck_assert_uint_eq(race_pmove_q2_snap_context.num_traces, 8u);
  for (size_t i = 0; i < lengthof(expected); i++) {
    Race_PmoveAssertVec3("Q2 final snap", "candidate",
                         race_pmove_q2_snap_context.origins[i], expected[i]);
  }
  Race_PmoveAssertVec3("Q2 final snap fallback", "origin",
                       move.s.origin, fallback);
  Race_PmoveAssertVec3("Q2 final snap fallback", "velocity",
                       move.s.velocity, Vec3(.25f, -.25f, .25f));
  ck_assert_int_eq(move.num_touched, 0);

  // An already exact component has zero jitter sign even when its bit is set.
  Race_PmoveQ2SnapReset(2);
  move = Race_PmoveQ2SnapSetup(
    Vec3(1.125f, -2.0625f, 3.0625f), velocity);
  Pm_Q2SnapPositionForTest(&move, fallback);
  Race_PmoveAssertVec3("Q2 zero-sign jitter", "origin", move.s.origin,
                       Vec3(1.125f, -2.125f, 3.125f));
} END_TEST

static void Race_PmoveQ2SnapSequence(
    const race_physics_preset_id_t preset,
    pm_move_t states[RACE_PMOVE_Q2_SEQUENCE_COMMANDS]) {
  Race_PmoveQ2Sequence(preset, true, states);
}

START_TEST(_Race_PmoveQ2SnapGameCgameAiNoAutohop) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    pm_move_t game[RACE_PMOVE_Q2_SEQUENCE_COMMANDS];
    pm_move_t cgame[RACE_PMOVE_Q2_SEQUENCE_COMMANDS];
    Race_PmoveQ2SnapSequence(presets[preset], game);
    Race_PmoveQ2SnapSequence(presets[preset], cgame);

    for (size_t i = 0; i < RACE_PMOVE_Q2_SEQUENCE_COMMANDS; i++) {
      ck_assert_msg(memcmp(game + i, cgame + i, sizeof(*game)) == 0,
                    "Q2 preset %d snapped state differs after command %zu",
                    presets[preset], i + 1u);
      const float scaled[] = {
        game[i].s.origin.x * 8.f, game[i].s.origin.y * 8.f,
        game[i].s.origin.z * 8.f, game[i].s.velocity.x * 8.f,
        game[i].s.velocity.y * 8.f, game[i].s.velocity.z * 8.f
      };
      for (size_t component = 0; component < lengthof(scaled); component++) {
        ck_assert_msg(scaled[component] == truncf(scaled[component]),
                      "Q2 command %zu component %zu is off the 1/8 grid",
                      i + 1u, component);
      }
    }

    for (size_t i = 2; i <= 5; i++) {
      ck_assert(game[i].s.flags & PMF_JUMP_HELD);
      ck_assert(!(game[i].s.flags & (PMF_JUMPED | PMF_TIME_LAND)));
      ck_assert_uint_eq(game[i].s.time, 0u);
    }
    ck_assert(!(game[6].s.flags & PMF_JUMP_HELD));
    ck_assert(game[7].s.flags & PMF_JUMPED);

    // GAME-side AI calls this same mover symbol directly.
    pm_move_t ai[RACE_PMOVE_Q2_SEQUENCE_COMMANDS];
    Race_PmoveQ2SnapSequence(presets[preset], ai);
    ck_assert_int_eq(memcmp(game, ai, sizeof(game)), 0);
  }
} END_TEST

START_TEST(_Race_PmoveQ2SnapPresetAndWorldBoundaries) {
  static const race_pmove_fixture_id_t fixtures[] = {
    RACE_PMOVE_WALL_CLIP,
    RACE_PMOVE_STEP_UP_LOW,
    RACE_PMOVE_RAMP_CLIMB,
    RACE_PMOVE_ALL_SOLID_RECOVERY
  };

  for (size_t i = 0; i < lengthof(fixtures); i++) {
    Race_PmoveUseQ2SnapTestPhysics();
    pm_move_t game = Race_PmoveSetup(fixtures[i]);
    pm_move_t cgame = Race_PmoveSetup(fixtures[i]);
    Pm_Move(&game);
    Pm_Move(&cgame);
    ck_assert_msg(memcmp(&game, &cgame, sizeof(game)) == 0,
                  "%s: snapped GAME/CGAME boundary result differs",
                  race_pmove_fixture_names[fixtures[i]]);

    // Direct AI movement has no G_PrepareMove phase and must match the same
    // collision, ramp, and initial-recovery result.
    Race_PmoveUseQ2SnapTestPhysics();
    pm_move_t ai = Race_PmoveSetup(fixtures[i]);
    Pm_Move(&ai);
    ck_assert_msg(memcmp(&game, &ai, sizeof(game)) == 0,
                  "%s: direct AI snapped result differs",
                  race_pmove_fixture_names[fixtures[i]]);
  }

  Race_PmoveUseQ2SnapTestPhysics();
  pm_move_t q2 = Race_PmoveSetup(RACE_PMOVE_EMPTY_FORWARD_16);
  Pm_Move(&q2);
  Race_PmoveUseQ2FixSnapTestPhysics();
  pm_move_t fix = Race_PmoveSetup(RACE_PMOVE_EMPTY_FORWARD_16);
  Pm_Move(&fix);
  ck_assert_msg(memcmp(&q2, &fix, sizeof(q2)) == 0,
                "named Q2 presets do not share nearest 1/8 snapping");

  // Representative start/checkpoint/finish coordinates freeze exact outgoing
  // positions. Candidate probes must not add synthetic movement touches.
  static const struct {
    const char *name;
    float input;
    float expected;
  } boundaries[] = {
    { "start", -32.0625f, -32.125f },
    { "checkpoint", .0625f, .125f },
    { "finish", 32.0625f, 32.125f }
  };

  Race_PmoveUseQ2SnapTestPhysics();
  for (size_t i = 0; i < lengthof(boundaries); i++) {
    Race_PmoveQ2SnapReset(0);
    pm_move_t move = Race_PmoveQ2SnapSetup(
      Vec3(boundaries[i].input, 0.f, 24.f), Vec3_Zero());
    Pm_Q2SnapPositionForTest(&move, Vec3(99.f, 99.f, 99.f));
    Race_PmoveAssertFloat(boundaries[i].name, "origin.x",
                          move.s.origin.x, boundaries[i].expected);
    ck_assert_int_eq(move.num_touched, 0);
  }
} END_TEST

static pm_move_t Race_PmoveQ2NamedFixture(
    const race_physics_preset_id_t preset,
    const race_pmove_fixture_id_t fixture) {
  if (preset == RACE_PHYSICS_PRESET_Q2) {
    Race_PmoveUseQ2SnapTestPhysics();
  } else {
    ck_assert_int_eq(preset, RACE_PHYSICS_PRESET_QUETOO_FIX_V1);
    Race_PmoveUseQ2FixSnapTestPhysics();
  }

  pm_move_t move = Race_PmoveSetup(fixture);
  move.s.params = Race_PmoveQ2NamedParams(preset);
  Pm_Move(&move);
  return move;
}

START_TEST(_Race_PmoveQ2NamedParameterContracts) {
  const pm_params_t q2 = Race_PmoveQ2NamedParams(
    RACE_PHYSICS_PRESET_Q2);
  const pm_params_t q2fix = Race_PmoveQ2NamedParams(
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1);

  ck_assert_msg(Race_Physics_ParamsHash(&q2) ==
                  UINT64_C(0xa0238a6a2bf3be4f),
                "q2-v1 exact parameter bits changed");
  ck_assert_msg(Race_Physics_ParamsHash(&q2fix) ==
                  UINT64_C(0xc7ab3817eaf25c1f),
                "quetoo-fix-v1 exact parameter bits changed");
  ck_assert(!Race_Physics_ParamsEqual(&q2, &q2fix));
  ck_assert(Race_Physics_ParamsEqual(&q2, &q2));
  ck_assert_uint_eq(Race_Physics_ParamsHash(NULL), 0u);

#define RACE_PMOVE_ASSERT_PARAM_EQUAL(field) \
  ck_assert_msg(q2.field == q2fix.field, #field " must match")
  RACE_PMOVE_ASSERT_PARAM_EQUAL(gravity);
  RACE_PMOVE_ASSERT_PARAM_EQUAL(accel_ground);
  RACE_PMOVE_ASSERT_PARAM_EQUAL(accel_ground_slick);
  RACE_PMOVE_ASSERT_PARAM_EQUAL(friction_ground);
  RACE_PMOVE_ASSERT_PARAM_EQUAL(friction_ground_slick);
  RACE_PMOVE_ASSERT_PARAM_EQUAL(speed_ground);
  RACE_PMOVE_ASSERT_PARAM_EQUAL(speed_stop);
  RACE_PMOVE_ASSERT_PARAM_EQUAL(speed_jump);
#undef RACE_PMOVE_ASSERT_PARAM_EQUAL

#define RACE_PMOVE_ASSERT_PARAM_DIFFERENT(field) \
  ck_assert_msg(q2.field != q2fix.field, #field " must differ")
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(gravity_water);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(accel_air);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(accel_water);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(accel_spectator);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(accel_ladder);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(friction_air);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(friction_water);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(friction_spectator);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(friction_ladder);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(speed_air);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(speed_water);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(speed_ladder);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(speed_spectator);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(speed_ducked);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(speed_duck_stand);
  RACE_PMOVE_ASSERT_PARAM_DIFFERENT(speed_water_jump);
#undef RACE_PMOVE_ASSERT_PARAM_DIFFERENT

  const float q2_floats[] = {
    q2.gravity_water,
    q2.accel_ground, q2.accel_ground_slick, q2.accel_air,
    q2.accel_water, q2.accel_spectator, q2.accel_ladder,
    q2.friction_ground, q2.friction_ground_slick,
    q2.friction_air, q2.friction_water,
    q2.friction_spectator, q2.friction_ladder,
    q2.speed_ground, q2.speed_air, q2.speed_water,
    q2.speed_ladder, q2.speed_spectator, q2.speed_stop,
    q2.speed_jump, q2.speed_ducked, q2.speed_duck_stand,
    q2.speed_water_jump
  };
  const float q2fix_floats[] = {
    q2fix.gravity_water,
    q2fix.accel_ground, q2fix.accel_ground_slick, q2fix.accel_air,
    q2fix.accel_water, q2fix.accel_spectator, q2fix.accel_ladder,
    q2fix.friction_ground, q2fix.friction_ground_slick,
    q2fix.friction_air, q2fix.friction_water,
    q2fix.friction_spectator, q2fix.friction_ladder,
    q2fix.speed_ground, q2fix.speed_air, q2fix.speed_water,
    q2fix.speed_ladder, q2fix.speed_spectator, q2fix.speed_stop,
    q2fix.speed_jump, q2fix.speed_ducked, q2fix.speed_duck_stand,
    q2fix.speed_water_jump
  };
  const float *vectors[] = { q2_floats, q2fix_floats };
  const size_t float_count = lengthof(q2_floats);
  for (size_t vector = 0; vector < lengthof(vectors); vector++) {
    for (size_t field = 0; field < float_count; field++) {
      ck_assert_msg(isfinite(vectors[vector][field]),
                    "preset %zu field %zu is not finite", vector, field);
    }
  }

  // Execute the existing spectator and ladder fixtures with each complete
  // named vector. This proves those subsystems consume the selected params;
  // it deliberately does not claim Q2 ladder algorithm parity.
  const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  pm_move_t spectator[lengthof(presets)];
  pm_move_t ladder[lengthof(presets)];
  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const pm_params_t expected = Race_PmoveQ2NamedParams(presets[preset]);

    spectator[preset] = Race_PmoveQ2NamedFixture(
      presets[preset], RACE_PMOVE_SPECTATOR);
    const pm_move_t spectator_prediction = Race_PmoveQ2NamedFixture(
      presets[preset], RACE_PMOVE_SPECTATOR);
    ck_assert_int_eq(spectator[preset].s.type, PM_SPECTATOR);
    ck_assert(Race_Physics_ParamsEqual(&spectator[preset].s.params,
                                       &expected));
    ck_assert_msg(memcmp(&spectator[preset].s, &spectator_prediction.s,
                         sizeof(spectator[preset].s)) == 0,
                  "preset %d spectator GAME/CGAME state differs",
                  presets[preset]);

    ladder[preset] = Race_PmoveQ2NamedFixture(
      presets[preset], RACE_PMOVE_LADDER);
    const pm_move_t ladder_prediction = Race_PmoveQ2NamedFixture(
      presets[preset], RACE_PMOVE_LADDER);
    ck_assert(ladder[preset].s.flags & PMF_ON_LADDER);
    ck_assert(Race_Physics_ParamsEqual(&ladder[preset].s.params,
                                       &expected));
    ck_assert_msg(memcmp(&ladder[preset].s, &ladder_prediction.s,
                         sizeof(ladder[preset].s)) == 0,
                  "preset %d ladder GAME/CGAME state differs",
                  presets[preset]);
  }
  ck_assert(!Vec3_Equal(spectator[0].s.velocity,
                        spectator[1].s.velocity));
  ck_assert(!Vec3_Equal(ladder[0].s.velocity, ladder[1].s.velocity));

  pm_params_t rejected;
  memset(&rejected, 0x5a, sizeof(rejected));
  const pm_params_t unchanged = rejected;
  ck_assert(!Race_Physics_FixedParamsForPreset(
    RACE_PHYSICS_PRESET_INVALID, &rejected));
  ck_assert(Race_Physics_ParamsEqual(&rejected, &unchanged));
  ck_assert(!Race_Physics_FixedParamsForPreset(
    RACE_PHYSICS_PRESET_QUETOO_COMMON_V1, &rejected));
  ck_assert(Race_Physics_ParamsEqual(&rejected, &unchanged));
  ck_assert(!Race_Physics_FixedParamsForPreset(
    RACE_PHYSICS_PRESET_Q2, NULL));
} END_TEST

typedef enum {
  RACE_PMOVE_CROSS_JUMP_AIR_LAND,
  RACE_PMOVE_CROSS_RAMP_STEP_COLLISION,
  RACE_PMOVE_CROSS_NEGATIVE_COLLISION,
  RACE_PMOVE_CROSS_WATER
} race_pmove_cross_stream_t;

typedef struct {
  pm_state_t state;
  race_pmove_entity_t ground;
  pm_water_level_t water_level;
  int32_t water_type;
  uint32_t touched;
  uint32_t step_bits;
} race_pmove_cross_sample_t;

#define RACE_PMOVE_CROSS_COMMANDS 180u

static race_pmove_cross_stream_t race_pmove_cross_stream;

static void Race_PmoveCrossRampSurface(cm_trace_t *trace,
                                       const vec3_t start,
                                       const vec3_t end,
                                       const box3_t bounds,
                                       const vec3_t normal,
                                       float dist,
                                       float min_x,
                                       float max_x,
                                       race_pmove_entity_t entity) {
  Race_PmoveQ2RampTraceSurface(trace, start, end, bounds,
    (race_pmove_q2_ramp_surface_t) {
      normal, dist, min_x, max_x, entity
    });
}

static cm_trace_t Race_PmoveCrossTrace(const vec3_t start,
                                       const vec3_t end,
                                       const box3_t bounds) {
  cm_trace_t trace = {
    .fraction = 1.f,
    .end = end
  };

  if (race_pmove_cross_stream == RACE_PMOVE_CROSS_JUMP_AIR_LAND) {
    Race_PmoveTracePlane(&trace, start, end, bounds, Vec3_Up(), 0.f,
                         RACE_PMOVE_ENTITY_FLOOR, CONTENTS_SOLID, 0);
    Race_PmoveTracePlane(&trace, start, end, bounds, Vec3(-1.f, 0.f, 0.f),
                         -96.f, RACE_PMOVE_ENTITY_WALL, CONTENTS_SOLID, 0);
    Race_PmoveTracePlane(&trace, start, end, bounds, Vec3(0.f, 1.f, 0.f),
                         -96.f, RACE_PMOVE_ENTITY_WALL_Y,
                         CONTENTS_SOLID, 0);
    return trace;
  }

  if (race_pmove_cross_stream == RACE_PMOVE_CROSS_WATER) {
    Race_PmoveTracePlane(&trace, start, end, bounds, Vec3_Up(), 0.f,
                         RACE_PMOVE_ENTITY_FLOOR, CONTENTS_SOLID, 0);
    Race_PmoveTraceBox(&trace, start, end, bounds,
                       Box3(Vec3(64.f, -2048.f, 0.f),
                            Vec3(160.f, 2048.f, 32.f)),
                       RACE_PMOVE_ENTITY_STEP, CONTENTS_SOLID, 0);
    Race_PmoveTracePlane(&trace, start, end, bounds, Vec3(-1.f, 0.f, 0.f),
                         -256.f, RACE_PMOVE_ENTITY_WALL,
                         CONTENTS_SOLID, 0);
    return trace;
  }

  const float unbounded = FLT_MAX;
  Race_PmoveCrossRampSurface(&trace, start, end, bounds,
                             Vec3_Up(), 0.f, -unbounded, -128.f,
                             RACE_PMOVE_ENTITY_FLOOR);
  Race_PmoveCrossRampSurface(&trace, start, end, bounds,
                             Race_PmoveQ2RampUpNormal(),
                             Race_PmoveQ2RampUpDist(), -128.f, 0.f,
                             RACE_PMOVE_ENTITY_RAMP);
  Race_PmoveCrossRampSurface(&trace, start, end, bounds,
                             Vec3_Up(), 64.f, 0.f, 32.f,
                             RACE_PMOVE_ENTITY_FLOOR);
  Race_PmoveCrossRampSurface(&trace, start, end, bounds,
                             Race_PmoveQ2RampDownNormal(),
                             Race_PmoveQ2RampDownDist(), 32.f, 160.f,
                             RACE_PMOVE_ENTITY_RAMP);
  Race_PmoveCrossRampSurface(&trace, start, end, bounds,
                             Vec3_Up(), 32.f, 160.f, unbounded,
                             RACE_PMOVE_ENTITY_FLOOR);

  Race_PmoveTraceBox(&trace, start, end, bounds,
                     Box3(Vec3(208.f, -96.f, 32.f),
                          Vec3(256.f, 96.f, 49.5f)),
                     RACE_PMOVE_ENTITY_STEP, CONTENTS_SOLID, 0);
  Race_PmoveTraceBox(&trace, start, end, bounds,
                     Box3(Vec3(400.f, -96.f, 32.f),
                          Vec3(448.f, 96.f, 49.5f)),
                     RACE_PMOVE_ENTITY_STEP, CONTENTS_SOLID, 0);
  if (race_pmove_cross_stream == RACE_PMOVE_CROSS_NEGATIVE_COLLISION) {
    Race_PmoveTraceBox(&trace, start, end, bounds,
                       Box3(Vec3(-288.f, -96.f, 0.f),
                            Vec3(-240.f, 96.f, 17.5f)),
                       RACE_PMOVE_ENTITY_STEP, CONTENTS_SOLID, 0);
  }
  Race_PmoveTracePlane(&trace, start, end, bounds, Vec3(-1.f, 0.f, 0.f),
                       -600.f, RACE_PMOVE_ENTITY_WALL, CONTENTS_SOLID, 0);
  Race_PmoveTracePlane(&trace, start, end, bounds, Vec3(0.f, 1.f, 0.f),
                       -96.f, RACE_PMOVE_ENTITY_WALL_Y, CONTENTS_SOLID, 0);
  Race_PmoveTracePlane(&trace, start, end, bounds, Vec3(0.f, -1.f, 0.f),
                       -96.f, RACE_PMOVE_ENTITY_WALL_Y, CONTENTS_SOLID, 0);
  return trace;
}

static int32_t Race_PmoveCrossPointContents(const vec3_t point) {
  if (race_pmove_cross_stream == RACE_PMOVE_CROSS_WATER &&
      point.x >= 64.f && point.x <= 160.f && point.z < 32.f) {
    return CONTENTS_SOLID;
  }
  if (race_pmove_cross_stream == RACE_PMOVE_CROSS_WATER &&
      point.x > -64.f && point.x < 64.f &&
      point.y > -2048.f && point.y < 2048.f && point.z < 40.f) {
    return CONTENTS_WATER | CONTENTS_CURRENT_90;
  }
  return 0;
}

static int32_t Race_PmoveCrossBoxContents(const box3_t box) {
  return Race_PmoveCrossPointContents(Box3_Center(box));
}

static void Race_PmoveCrossSelectPreset(
    const race_physics_preset_id_t preset) {
  if (preset == RACE_PHYSICS_PRESET_Q2) {
    Race_PmoveUseQ2SnapTestPhysics();
  } else {
    ck_assert_int_eq(preset, RACE_PHYSICS_PRESET_QUETOO_FIX_V1);
    Race_PmoveUseQ2FixSnapTestPhysics();
  }
  Pm_SetQ2AirWishspeedCapForTest(0.f);
}

static pm_move_t Race_PmoveCrossSetup(
    const race_physics_preset_id_t preset,
    const race_pmove_cross_stream_t stream) {
  pm_move_t move;
  memset(&move, 0, sizeof(move));
  move.s.type = PM_NORMAL;
  move.s.params = Race_PmoveQ2NamedParams(preset);
  move.s.view_offset = Vec3(0.f, 0.f, 30.f);
  move.PointContents = Race_PmoveCrossPointContents;
  move.BoxContents = Race_PmoveCrossBoxContents;
  move.Trace = Race_PmoveCrossTrace;
  move.DebugMask = Race_PmoveDebugMask;
  move.Debug = Race_PmoveDebug;

  switch (stream) {
    case RACE_PMOVE_CROSS_JUMP_AIR_LAND:
      move.s.origin = Vec3(-48.0625f, 48.0625f, 24.0625f);
      break;
    case RACE_PMOVE_CROSS_RAMP_STEP_COLLISION:
      move.s.origin = Vec3(-160.0625f, -64.0625f, 24.0625f);
      move.s.velocity = Vec3(300.f, 0.f, 0.f);
      break;
    case RACE_PMOVE_CROSS_NEGATIVE_COLLISION:
      move.s.origin = Vec3(-336.0625f, 64.0625f, 24.0625f);
      move.s.velocity = Vec3(500.f, -120.f, 0.f);
      break;
    case RACE_PMOVE_CROSS_WATER:
      move.s.origin = Vec3(33.875f, 0.125f, 24.125f);
      break;
  }

  Race_PmoveSetGround(&move, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
  return move;
}

static pm_cmd_t Race_PmoveCrossCommand(
    const race_pmove_cross_stream_t stream, const size_t command) {
  pm_cmd_t cmd = {
    .msec = (uint16_t[]) { 8, 16, 25, 11 }[command % 4u],
    .forward = 300
  };

  switch (stream) {
    case RACE_PMOVE_CROSS_JUMP_AIR_LAND:
      cmd.right = command < 52u ? 120 : 0;
      cmd.up = command == 90u ? 0 : 10;
      break;
    case RACE_PMOVE_CROSS_RAMP_STEP_COLLISION:
      cmd.right = command > 105u ? 180 : 0;
      break;
    case RACE_PMOVE_CROSS_NEGATIVE_COLLISION:
      cmd.right = command < 90u ? 180 : -180;
      break;
    case RACE_PMOVE_CROSS_WATER:
      cmd.up = 0;
      break;
  }
  return cmd;
}

static void Race_PmoveCrossRun(
    const race_physics_preset_id_t preset,
    const race_pmove_cross_stream_t stream,
    race_pmove_cross_sample_t samples[RACE_PMOVE_CROSS_COMMANDS]) {
  Race_PmoveCrossSelectPreset(preset);
  race_pmove_cross_stream = stream;
  pm_move_t move = Race_PmoveCrossSetup(preset, stream);

  for (size_t i = 0; i < RACE_PMOVE_CROSS_COMMANDS; i++) {
    move.cmd = Race_PmoveCrossCommand(stream, i);
    Pm_Move(&move);

    race_pmove_cross_sample_t sample;
    memset(&sample, 0, sizeof(sample));
    sample.state = move.s;
    sample.ground = Race_PmoveEntityId(move.ground.ent);
    sample.water_level = move.water_level;
    sample.water_type = move.water_type;
    sample.step_bits = Race_PmoveFloatBits(move.step);
    for (int32_t touched = 0; touched < move.num_touched; touched++) {
      const race_pmove_entity_t id =
        Race_PmoveEntityId(move.touched[touched].ent);
      if (id > RACE_PMOVE_ENTITY_NONE) {
        sample.touched |= 1u << id;
      }
    }
    samples[i] = sample;
  }
}

static void Race_PmoveCrossAssertGrid(
    const char *name, const pm_state_t *state, const size_t command) {
  const float values[] = {
    state->origin.x, state->origin.y, state->origin.z,
    state->velocity.x, state->velocity.y, state->velocity.z
  };
  for (size_t component = 0; component < lengthof(values); component++) {
    const float scaled = values[component] * 8.f;
    ck_assert_msg(scaled == truncf(scaled),
                  "%s command %zu component %zu left the 1/8 grid",
                  name, command + 1u, component);
  }
}

#define RACE_PMOVE_FINAL_COMMANDS 640u

static cm_trace_t Race_PmoveFinalTrace(const vec3_t start,
                                       const vec3_t end,
                                       const box3_t bounds) {
  cm_trace_t trace = {
    .fraction = 1.f,
    .end = end
  };
  const float unbounded = FLT_MAX;

  // Highest surfaces are evaluated first at shared seams.
  Race_PmoveCrossRampSurface(&trace, start, end, bounds,
                             Race_PmoveQ2RampDownNormal(),
                             Race_PmoveQ2RampDownDist(), 32.f, 160.f,
                             RACE_PMOVE_ENTITY_RAMP);
  Race_PmoveCrossRampSurface(&trace, start, end, bounds,
                             Vec3_Up(), 64.f, 0.f, 32.f,
                             RACE_PMOVE_ENTITY_FLOOR);
  Race_PmoveCrossRampSurface(&trace, start, end, bounds,
                             Race_PmoveQ2RampUpNormal(),
                             Race_PmoveQ2RampUpDist(), -128.f, 0.f,
                             RACE_PMOVE_ENTITY_RAMP);
  Race_PmoveCrossRampSurface(&trace, start, end, bounds,
                             Vec3_Up(), 32.f, 160.f, 328.f,
                             RACE_PMOVE_ENTITY_FLOOR);
  Race_PmoveCrossRampSurface(&trace, start, end, bounds,
                             Vec3_Up(), 0.f, -unbounded, -128.f,
                             RACE_PMOVE_ENTITY_FLOOR);
  Race_PmoveCrossRampSurface(&trace, start, end, bounds,
                             Vec3_Up(), 128.f, 328.f, unbounded,
                             RACE_PMOVE_ENTITY_FLOOR);

  Race_PmoveTraceBox(&trace, start, end, bounds,
                     Box3(Vec3(-224.f, -96.f, 0.f),
                          Vec3(-176.f, 96.f, 17.5f)),
                     RACE_PMOVE_ENTITY_STEP, CONTENTS_SOLID, 0);
  Race_PmoveTraceBox(&trace, start, end, bounds,
                     Box3(Vec3(208.f, -96.f, 64.f),
                          Vec3(256.f, 96.f, 256.f)),
                     RACE_PMOVE_ENTITY_CEILING, CONTENTS_SOLID, 0);
  Race_PmoveTraceBox(&trace, start, end, bounds,
                     Box3(Vec3(320.f, -96.f, 32.f),
                          Vec3(328.f, 96.f, 128.f)),
                     RACE_PMOVE_ENTITY_LADDER,
                     CONTENTS_SOLID | CONTENTS_LADDER, 0);
  Race_PmoveTraceBox(&trace, start, end, bounds,
                     Box3(Vec3(576.f, -256.f, 128.f),
                          Vec3(800.f, 256.f, 172.f)),
                     RACE_PMOVE_ENTITY_LEDGE, CONTENTS_SOLID, 0);
  Race_PmoveTracePlane(&trace, start, end, bounds,
                       Vec3(0.f, 1.f, 0.f), -96.f,
                       RACE_PMOVE_ENTITY_WALL_Y, CONTENTS_SOLID, 0);
  return trace;
}

static int32_t Race_PmoveFinalPointContents(const vec3_t point) {
  if (point.x >= 576.f && point.x <= 800.f &&
      point.y > -256.f && point.y < 256.f &&
      point.z >= 128.f && point.z < 172.f) {
    return CONTENTS_SOLID;
  }

  if (point.x > 416.f && point.x < 576.f &&
      point.y > -256.f && point.y < 256.f &&
      point.z > 128.f && point.z < 168.f) {
    if (point.x >= 448.f && point.x < 496.f) {
      return CONTENTS_WATER | CONTENTS_CURRENT_90;
    }
    return CONTENTS_WATER;
  }

  return 0;
}

static int32_t Race_PmoveFinalBoxContents(const box3_t box) {
  return Race_PmoveFinalPointContents(Box3_Center(box));
}

static pm_move_t Race_PmoveFinalSetup(
    const race_physics_preset_id_t preset) {
  pm_move_t move;
  memset(&move, 0, sizeof(move));
  move.s.type = PM_NORMAL;
  move.s.params = Race_PmoveQ2NamedParams(preset);
  move.s.origin = Vec3(-512.125f, -48.125f, 24.125f);
  move.s.view_offset = Vec3(0.f, 0.f, 22.f);
  move.PointContents = Race_PmoveFinalPointContents;
  move.BoxContents = Race_PmoveFinalBoxContents;
  move.Trace = Race_PmoveFinalTrace;
  move.DebugMask = Race_PmoveDebugMask;
  move.Debug = Race_PmoveDebug;
  Race_PmoveSetGround(&move, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
  return move;
}

static size_t Race_PmoveFinalFirst(const size_t current,
                                   const bool observed,
                                   const size_t command) {
  return current == SIZE_MAX && observed ? command : current;
}

START_TEST(_Race_PmoveQ2FinalCombinedGameCgameAi) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const uint16_t msec[] = { 8, 16, 25, 50, 11 };

  for (size_t preset_index = 0; preset_index < lengthof(presets);
       preset_index++) {
    const race_physics_preset_id_t preset = presets[preset_index];
    const pm_params_t params = Race_PmoveQ2NamedParams(preset);

    Race_PmoveUseQ2NamedTestPhysics(preset, true);
    Pm_SetQ2AirWishspeedCapForTest(0.f);
    Pm_SetQ2SnapEnabledForTest_CgameReference(true);
    Pm_SetQ2AirWishspeedCapForTest_CgameReference(0.f);

    pm_move_t game = Race_PmoveFinalSetup(preset);
    pm_move_t cgame = game;
    pm_move_t ai = game;
    bool saw_current = false;
    bool sent_liquid_impulse = false;
    size_t jump = SIZE_MAX, air = SIZE_MAX, wall = SIZE_MAX;
    size_t step = SIZE_MAX, ramp = SIZE_MAX, duck = SIZE_MAX;
    size_t ladder = SIZE_MAX, water = SIZE_MAX, current = SIZE_MAX;
    size_t liquid_impulse = SIZE_MAX, water_jump = SIZE_MAX;
    size_t landing = SIZE_MAX, finish = SIZE_MAX;

    for (size_t command = 0; command < RACE_PMOVE_FINAL_COMMANDS;
         command++) {
      pm_cmd_t cmd = {
        .msec = msec[command % lengthof(msec)],
        .forward = 300
      };

      if (command == 0u) {
        cmd.up = 10;
      }
      if (game.s.origin.x < -300.f) {
        cmd.right = 200;
      } else if (game.s.origin.y < -8.f) {
        cmd.right = -200;
      }
      if (game.s.origin.x >= 176.f && game.s.origin.x <= 280.f) {
        cmd.up = -300;
      }
      if (game.s.origin.x >= 303.5f && game.s.origin.x < 328.f &&
          game.s.origin.z < 154.f) {
        cmd.up = 300;
      }

      const bool request_liquid_impulse =
        !sent_liquid_impulse && saw_current &&
        game.s.origin.x >= 500.f && game.water_level >= WATER_WAIST;
      if (request_liquid_impulse) {
        cmd.up = 10;
        sent_liquid_impulse = true;
      }

      game.cmd = cgame.cmd = ai.cmd = cmd;
      Pm_Move(&game);
      Pm_Move_CgameReference(&cgame);
      Pm_Move(&ai);

      ck_assert_msg(memcmp(&game, &cgame, sizeof(game)) == 0,
                    "preset %d final stream command %zu GAME/CGAME differs",
                    preset, command + 1u);
      ck_assert_msg(memcmp(&game, &ai, sizeof(game)) == 0,
                    "preset %d final stream command %zu GAME/direct AI differs",
                    preset, command + 1u);
      ck_assert_msg(memcmp(&game.s.params, &params, sizeof(params)) == 0,
                    "preset %d final stream command %zu changed named params",
                    preset, command + 1u);
      Race_PmoveCrossAssertGrid("Q2 final combined GAME", &game.s,
                                command);
      Race_PmoveCrossAssertGrid("Q2 final combined CGAME", &cgame.s,
                                command);
      Race_PmoveCrossAssertGrid("Q2 final combined direct AI", &ai.s,
                                command);

      uint32_t touched = 0u;
      for (int32_t i = 0; i < game.num_touched; i++) {
        const race_pmove_entity_t id =
          Race_PmoveEntityId(game.touched[i].ent);
        if (id > RACE_PMOVE_ENTITY_NONE) {
          touched |= 1u << id;
        }
      }

      jump = Race_PmoveFinalFirst(jump,
        !!(game.s.flags & PMF_JUMPED), command);
      air = Race_PmoveFinalFirst(air,
        jump != SIZE_MAX && command > jump &&
        !(game.s.flags & PMF_ON_GROUND), command);
      wall = Race_PmoveFinalFirst(wall,
        air != SIZE_MAX && !!(touched &
          (1u << RACE_PMOVE_ENTITY_WALL_Y)), command);
      step = Race_PmoveFinalFirst(step,
        wall != SIZE_MAX &&
        (Race_PmoveEntityId(game.ground.ent) == RACE_PMOVE_ENTITY_STEP ||
         !!(touched & (1u << RACE_PMOVE_ENTITY_STEP))), command);
      ramp = Race_PmoveFinalFirst(ramp,
        step != SIZE_MAX &&
        Race_PmoveEntityId(game.ground.ent) == RACE_PMOVE_ENTITY_RAMP,
        command);
      duck = Race_PmoveFinalFirst(duck,
        ramp != SIZE_MAX && !!(game.s.flags & PMF_DUCKED) &&
        game.s.origin.x >= 192.f && game.s.origin.x <= 280.f,
        command);
      ladder = Race_PmoveFinalFirst(ladder,
        duck != SIZE_MAX && !!(game.s.flags & PMF_ON_LADDER), command);
      water = Race_PmoveFinalFirst(water,
        ladder != SIZE_MAX && game.water_level >= WATER_WAIST, command);
      current = Race_PmoveFinalFirst(current,
        water != SIZE_MAX &&
        !!(game.water_type & CONTENTS_CURRENT_90), command);
      saw_current |= !!(game.water_type & CONTENTS_CURRENT_90);
      liquid_impulse = Race_PmoveFinalFirst(liquid_impulse,
        request_liquid_impulse &&
        !!(game.s.flags & PMF_JUMP_HELD) &&
        !(game.s.flags & PMF_TIME_WATER_JUMP), command);
      water_jump = Race_PmoveFinalFirst(water_jump,
        liquid_impulse != SIZE_MAX &&
        !!(game.s.flags & PMF_TIME_WATER_JUMP), command);
      landing = Race_PmoveFinalFirst(landing,
        water_jump != SIZE_MAX && game.water_level == WATER_NONE &&
        !!(game.s.flags & PMF_ON_GROUND) && game.s.origin.x >= 560.f,
        command);
      finish = Race_PmoveFinalFirst(finish,
        landing != SIZE_MAX && game.s.origin.x > 700.f, command);
    }

    ck_assert_msg(jump < air && air < wall && wall < step && step < ramp &&
                  ramp < duck && duck < ladder && ladder < water &&
                  water < current && current < liquid_impulse &&
                  liquid_impulse < water_jump && water_jump < landing &&
                  landing < finish,
                  "preset %d incomplete final stream: "
                  "jump=%zu air=%zu wall=%zu step=%zu ramp=%zu duck=%zu "
                  "ladder=%zu water=%zu current=%zu impulse=%zu "
                  "water-jump=%zu landing=%zu finish=%zu "
                  "final=(%a %a %a) flags=0x%x water=%d",
                  preset, jump, air, wall, step, ramp, duck, ladder, water,
                  current, liquid_impulse, water_jump, landing, finish,
                  (double) game.s.origin.x, (double) game.s.origin.y,
                  (double) game.s.origin.z, game.s.flags, game.water_level);
  }
} END_TEST

START_TEST(_Race_PmoveQ2CrossDomainStreams) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const char *stream_names[] = {
    "A jump-air-land-snap",
    "B ramp-step-collision",
    "C negative-collision",
    "D water-current-exit"
  };

  for (size_t preset_index = 0; preset_index < lengthof(presets);
       preset_index++) {
    const race_physics_preset_id_t preset = presets[preset_index];
    const pm_params_t params = Race_PmoveQ2NamedParams(preset);

    for (race_pmove_cross_stream_t stream =
           RACE_PMOVE_CROSS_JUMP_AIR_LAND;
         stream <= RACE_PMOVE_CROSS_WATER; stream++) {
      race_pmove_cross_sample_t game[RACE_PMOVE_CROSS_COMMANDS];
      race_pmove_cross_sample_t cgame[RACE_PMOVE_CROSS_COMMANDS];
      Race_PmoveCrossRun(preset, stream, game);
      Race_PmoveCrossRun(preset, stream, cgame);

      for (size_t command = 0; command < RACE_PMOVE_CROSS_COMMANDS;
           command++) {
        ck_assert_msg(memcmp(game + command, cgame + command,
                             sizeof(*game)) == 0,
                      "%s preset %d differs after command %zu",
                      stream_names[stream], preset, command + 1u);
        ck_assert_msg(memcmp(&game[command].state.params, &params,
                             sizeof(params)) == 0,
                      "%s preset %d changed named params at command %zu",
                      stream_names[stream], preset, command + 1u);
        Race_PmoveCrossAssertGrid(stream_names[stream],
                                  &game[command].state, command);
      }

      if (stream == RACE_PMOVE_CROSS_JUMP_AIR_LAND) {
        size_t jumps = 0;
        bool landed_held = false;
        bool wall_graze = false;
        for (size_t i = 0; i < RACE_PMOVE_CROSS_COMMANDS; i++) {
          jumps += !!(game[i].state.flags & PMF_JUMPED);
          landed_held |= !!((game[i].state.flags &
                            (PMF_ON_GROUND | PMF_JUMP_HELD)) ==
                           (PMF_ON_GROUND | PMF_JUMP_HELD));
          wall_graze |= !!(game[i].touched &
                           (1u << RACE_PMOVE_ENTITY_WALL));
        }
        ck_assert_uint_eq(jumps, 2u);
        ck_assert(landed_held);
        ck_assert(wall_graze);
      } else if (stream == RACE_PMOVE_CROSS_RAMP_STEP_COLLISION) {
        bool ramp = false, air = false, step = false, collision = false;
        for (size_t i = 0; i < RACE_PMOVE_CROSS_COMMANDS; i++) {
          ramp |= game[i].ground == RACE_PMOVE_ENTITY_RAMP;
          air |= !(game[i].state.flags & PMF_ON_GROUND);
          step |= game[i].ground == RACE_PMOVE_ENTITY_STEP &&
                  game[i].state.origin.x >= 384.f;
          collision |= !!(game[i].touched &
                           ((1u << RACE_PMOVE_ENTITY_WALL) |
                            (1u << RACE_PMOVE_ENTITY_WALL_Y)));
        }
        ck_assert(ramp);
        ck_assert(air);
        ck_assert_msg(step,
                      "preset %d stream B never stepped; final=(%a %a %a)",
                      preset,
                      (double) game[RACE_PMOVE_CROSS_COMMANDS - 1u].state.origin.x,
                      (double) game[RACE_PMOVE_CROSS_COMMANDS - 1u].state.origin.y,
                      (double) game[RACE_PMOVE_CROSS_COMMANDS - 1u].state.origin.z);
        ck_assert(collision);
        ck_assert(game[RACE_PMOVE_CROSS_COMMANDS - 1u].state.flags &
                  PMF_ON_GROUND);
      } else if (stream == RACE_PMOVE_CROSS_NEGATIVE_COLLISION) {
        bool negative = false, ramp = false, step = false, collision = false;
        for (size_t i = 0; i < RACE_PMOVE_CROSS_COMMANDS; i++) {
          negative |= game[i].state.origin.x < 0.f;
          ramp |= game[i].ground == RACE_PMOVE_ENTITY_RAMP;
          step |= game[i].ground == RACE_PMOVE_ENTITY_STEP;
          collision |= !!(game[i].touched &
                           ((1u << RACE_PMOVE_ENTITY_WALL) |
                            (1u << RACE_PMOVE_ENTITY_WALL_Y)));
        }
        ck_assert(negative);
        ck_assert(ramp);
        ck_assert_msg(step,
                      "preset %d stream C never stepped; final=(%a %a %a)",
                      preset,
                      (double) game[RACE_PMOVE_CROSS_COMMANDS - 1u].state.origin.x,
                      (double) game[RACE_PMOVE_CROSS_COMMANDS - 1u].state.origin.y,
                      (double) game[RACE_PMOVE_CROSS_COMMANDS - 1u].state.origin.z);
        ck_assert(collision);
      } else {
        bool water = false, current = false, water_jump = false;
        bool dry_landing = false;
        float max_x = -FLT_MAX;
        float max_z = -FLT_MAX;
        for (size_t i = 0; i < RACE_PMOVE_CROSS_COMMANDS; i++) {
          water |= game[i].water_level >= WATER_WAIST;
          current |= !!(game[i].water_type & CONTENTS_CURRENT_90);
          water_jump |= !!(game[i].state.flags & PMF_TIME_WATER_JUMP);
          dry_landing |= game[i].water_level == WATER_NONE &&
                          !!(game[i].state.flags & PMF_ON_GROUND) &&
                          game[i].state.origin.x > 64.f;
          max_x = Maxf(max_x, game[i].state.origin.x);
          max_z = Maxf(max_z, game[i].state.origin.z);
        }
        ck_assert(water);
        ck_assert(current);
        ck_assert_msg(water_jump,
                      "preset %d water stream missed ledge jump; "
                      "max=(%a %a) final=(%a %a %a) water=%d flags=0x%x",
                      preset, (double) max_x, (double) max_z,
                      (double) game[RACE_PMOVE_CROSS_COMMANDS - 1u].state.origin.x,
                      (double) game[RACE_PMOVE_CROSS_COMMANDS - 1u].state.origin.y,
                      (double) game[RACE_PMOVE_CROSS_COMMANDS - 1u].state.origin.z,
                      game[RACE_PMOVE_CROSS_COMMANDS - 1u].water_level,
                      game[RACE_PMOVE_CROSS_COMMANDS - 1u].state.flags);
        ck_assert(dry_landing);
      }
    }
  }
} END_TEST

typedef enum {
  RACE_PMOVE_STAND_CLEAR,
  RACE_PMOVE_STAND_CLEAR_MISLEADING_HIT,
  RACE_PMOVE_STAND_BLOCK_START_SOLID,
  RACE_PMOVE_STAND_BLOCK_ALL_SOLID,
  RACE_PMOVE_STAND_CEILING
} race_pmove_stand_trace_mode_t;

static race_pmove_stand_trace_mode_t race_pmove_stand_trace_mode;
static size_t race_pmove_stand_trace_calls;
static box3_t race_pmove_stand_trace_bounds;
static float race_pmove_stand_ceiling_z;

static bool Race_PmoveVec3Equal(const vec3_t left, const vec3_t right) {
  return memcmp(&left, &right, sizeof(left)) == 0;
}

static cm_trace_t Race_PmoveStandTrace(const vec3_t start,
                                       const vec3_t end,
                                       const box3_t bounds) {
  cm_trace_t trace = {
    .fraction = 1.f,
    .end = end
  };

  if (!Race_PmoveVec3Equal(start, end) || bounds.maxs.z != 32.f ||
      bounds.mins.z != -24.f) {
    return trace;
  }

  if (race_pmove_stand_trace_calls) {
    return trace;
  }

  race_pmove_stand_trace_calls++;
  race_pmove_stand_trace_bounds = bounds;

  switch (race_pmove_stand_trace_mode) {
    case RACE_PMOVE_STAND_CLEAR:
      break;
    case RACE_PMOVE_STAND_CLEAR_MISLEADING_HIT:
      trace.fraction = 0.f;
      trace.ent = (void *) Race_PmoveEntityPointer(
        RACE_PMOVE_ENTITY_CEILING);
      break;
    case RACE_PMOVE_STAND_BLOCK_START_SOLID:
      trace.start_solid = true;
      trace.fraction = 0.f;
      trace.ent = (void *) Race_PmoveEntityPointer(
        RACE_PMOVE_ENTITY_CEILING);
      break;
    case RACE_PMOVE_STAND_BLOCK_ALL_SOLID:
      trace.all_solid = true;
      trace.fraction = 1.f;
      break;
    case RACE_PMOVE_STAND_CEILING:
      if (start.z + bounds.maxs.z > race_pmove_stand_ceiling_z) {
        trace.start_solid = true;
        trace.all_solid = true;
        trace.fraction = 0.f;
        trace.ent = (void *) Race_PmoveEntityPointer(
          RACE_PMOVE_ENTITY_CEILING);
      }
      break;
  }

  return trace;
}

static pm_move_t Race_PmoveQ2DuckMove(
    const race_physics_preset_id_t preset, const uint16_t flags,
    const int16_t up, const uint16_t msec,
    const race_pmove_stand_trace_mode_t trace_mode) {
  Race_PmoveUseQ2NamedTestPhysics(preset, false);

  pm_move_t move = Race_PmoveSetup(RACE_PMOVE_EMPTY_IDLE);
  move.s.params = Race_PmoveQ2NamedParams(preset);
  move.s.params.gravity = 0.f;
  move.s.origin = Vec3(128.f, 128.f, 128.f);
  move.s.velocity = Vec3_Zero();
  move.s.flags = flags;
  move.s.view_offset.z = 91.f;
  move.cmd = (pm_cmd_t) {
    .msec = msec,
    .up = up
  };
  move.Trace = Race_PmoveStandTrace;

  race_pmove_stand_trace_mode = trace_mode;
  race_pmove_stand_trace_calls = 0u;
  race_pmove_stand_trace_bounds = Box3_Zero();
  Pm_Move(&move);
  return move;
}

static void Race_PmoveAssertBounds(const char *fixture,
                                   const box3_t actual,
                                   const box3_t expected) {
  Race_PmoveAssertVec3(fixture, "bounds.mins", actual.mins,
                       expected.mins);
  Race_PmoveAssertVec3(fixture, "bounds.maxs", actual.maxs,
                       expected.maxs);
}

START_TEST(_Race_PmovePlayerBoundsContract) {
  Race_PmoveUseCommonPhysics();
  Race_PmoveAssertBounds("default standing", Pm_PlayerBounds(false),
                         Box3(Vec3(-16.f, -16.f, -24.f),
                              Vec3(16.f, 16.f, 36.f)));
  Race_PmoveAssertBounds("default crouched", Pm_PlayerBounds(true),
                         Box3(Vec3(-16.f, -16.f, -24.f),
                              Vec3(16.f, 16.f, 6.f)));
  Race_PmoveAssertBounds("common standing",
                         Pm_PlayerBounds_CommonReference(false),
                         Pm_PlayerBounds(false));
  Race_PmoveAssertBounds("common crouched",
                         Pm_PlayerBounds_CommonReference(true),
                         Pm_PlayerBounds(true));

  Race_PmoveUseQ2TestPhysics();
  Race_PmoveAssertBounds("Q2 standing", Pm_PlayerBounds(false),
                         Box3(Vec3(-16.f, -16.f, -24.f),
                              Vec3(16.f, 16.f, 32.f)));
  Race_PmoveAssertBounds("Q2 crouched", Pm_PlayerBounds(true),
                         Box3(Vec3(-16.f, -16.f, -24.f),
                              Vec3(16.f, 16.f, 4.f)));

  Race_PmoveUseQ2FixTestPhysics();
  Race_PmoveAssertBounds("Quetoo Fix standing", Pm_PlayerBounds(false),
                         Box3(Vec3(-16.f, -16.f, -24.f),
                              Vec3(16.f, 16.f, 32.f)));
  Race_PmoveAssertBounds("Quetoo Fix crouched", Pm_PlayerBounds(true),
                         Box3(Vec3(-16.f, -16.f, -24.f),
                              Vec3(16.f, 16.f, 6.f)));
} END_TEST

START_TEST(_Race_PmoveQ2DuckStateMachine) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const uint16_t command_msec[] = { 0u, 1u, 8u, 16u, 50u, 100u };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const box3_t standing = Box3(Vec3(-16.f, -16.f, -24.f),
                                 Vec3(16.f, 16.f, 32.f));
    const box3_t crouched = Box3(
      Vec3(-16.f, -16.f, -24.f),
      Vec3(16.f, 16.f,
           presets[preset] == RACE_PHYSICS_PRESET_Q2 ? 4.f : 6.f));

    for (size_t command = 0; command < lengthof(command_msec); command++) {
      pm_move_t move = Race_PmoveQ2DuckMove(
        presets[preset], PMF_ON_GROUND, -1, command_msec[command],
        RACE_PMOVE_STAND_CLEAR);
      ck_assert(move.s.flags & PMF_DUCKED);
      Race_PmoveAssertBounds("grounded duck", move.bounds, crouched);
      Race_PmoveAssertFloat("grounded duck", "view_offset.z",
                            move.s.view_offset.z, -2.f);
      ck_assert_uint_eq(race_pmove_stand_trace_calls, 0u);

      move = Race_PmoveQ2DuckMove(
        presets[preset], 0, -1, command_msec[command],
        RACE_PMOVE_STAND_CLEAR);
      ck_assert(!(move.s.flags & PMF_DUCKED));
      Race_PmoveAssertBounds("airborne duck request", move.bounds,
                             standing);
      Race_PmoveAssertFloat("airborne duck request", "view_offset.z",
                            move.s.view_offset.z, 22.f);

      move = Race_PmoveQ2DuckMove(
        presets[preset], PMF_DUCKED, -1, command_msec[command],
        RACE_PMOVE_STAND_CLEAR);
      ck_assert(!(move.s.flags & PMF_DUCKED));
      Race_PmoveAssertBounds("airborne held-down stand", move.bounds,
                             standing);
      Race_PmoveAssertFloat("airborne held-down stand", "view_offset.z",
                            move.s.view_offset.z, 22.f);
      ck_assert_uint_eq(race_pmove_stand_trace_calls, 1u);
    }

    Race_PmoveUseQ2NamedTestPhysics(presets[preset], false);
    pm_move_t ladder = Race_PmoveSetup(RACE_PMOVE_LADDER);
    ladder.s.params = Race_PmoveQ2NamedParams(presets[preset]);
    ladder.s.params.gravity = 0.f;
    ladder.s.flags = PMF_ON_GROUND;
    ladder.cmd.msec = 0;
    ladder.cmd.up = -300;
    Pm_Move(&ladder);
    ck_assert(ladder.s.flags & PMF_ON_LADDER);
    ck_assert(!(ladder.s.flags & PMF_DUCKED));
    Race_PmoveAssertBounds("ladder duck request", ladder.bounds, standing);
    Race_PmoveAssertFloat("ladder duck request", "view_offset.z",
                          ladder.s.view_offset.z, 22.f);
  }
} END_TEST

START_TEST(_Race_PmoveQ2StandTraceContract) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const race_pmove_stand_trace_mode_t modes[] = {
    RACE_PMOVE_STAND_CLEAR,
    RACE_PMOVE_STAND_CLEAR_MISLEADING_HIT,
    RACE_PMOVE_STAND_BLOCK_START_SOLID,
    RACE_PMOVE_STAND_BLOCK_ALL_SOLID
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    for (size_t mode = 0; mode < lengthof(modes); mode++) {
      const pm_move_t move = Race_PmoveQ2DuckMove(
        presets[preset], PMF_DUCKED | PMF_ON_GROUND, 0, 16,
        modes[mode]);
      const bool blocked = modes[mode] ==
                             RACE_PMOVE_STAND_BLOCK_START_SOLID ||
                           modes[mode] ==
                             RACE_PMOVE_STAND_BLOCK_ALL_SOLID;

      ck_assert_uint_eq(race_pmove_stand_trace_calls, 1u);
      Race_PmoveAssertBounds(
        "stand trace hull", race_pmove_stand_trace_bounds,
        Box3(Vec3(-16.f, -16.f, -24.f), Vec3(16.f, 16.f, 32.f)));
      ck_assert_int_eq(move.num_touched, 0);
      ck_assert_int_eq(!!(move.s.flags & PMF_DUCKED), blocked);
      Race_PmoveAssertFloat("stand trace", "view_offset.z",
                            move.s.view_offset.z, blocked ? -2.f : 22.f);
      Race_PmoveAssertBounds(
        "stand trace final", move.bounds,
        blocked ? Pm_PlayerBounds(true) : Pm_PlayerBounds(false));
    }

    static const float ceiling_z[] = { 159.875f, 160.f, 160.125f };
    for (size_t ceiling = 0; ceiling < lengthof(ceiling_z); ceiling++) {
      race_pmove_stand_ceiling_z = ceiling_z[ceiling];
      const pm_move_t move = Race_PmoveQ2DuckMove(
        presets[preset], PMF_DUCKED, 0, 16, RACE_PMOVE_STAND_CEILING);
      const bool blocked = ceiling_z[ceiling] < 160.f;
      ck_assert_int_eq(!!(move.s.flags & PMF_DUCKED), blocked);
      Race_PmoveAssertFloat("standing ceiling boundary", "view_offset.z",
                            move.s.view_offset.z, blocked ? -2.f : 22.f);
      ck_assert_uint_eq(race_pmove_stand_trace_calls, 1u);
    }
  }
} END_TEST

static pm_move_t Race_PmoveTunnelRun(
    const race_physics_preset_id_t preset, const float height,
    const bool ducked) {
  if (preset == RACE_PHYSICS_PRESET_INVALID) {
    Race_PmoveUseCommonPhysics();
  } else {
    Race_PmoveUseQ2NamedTestPhysics(preset, false);
  }

  pm_move_t move = Race_PmoveSetup(RACE_PMOVE_EMPTY_IDLE);
  move.s.params = preset == RACE_PHYSICS_PRESET_INVALID
    ? Race_PmoveDefaultParams()
    : Race_PmoveQ2NamedParams(preset);
  move.s.params.gravity = 0.f;
  move.s.params.friction_ground = 0.f;
  move.s.params.friction_ground_slick = 0.f;
  move.s.origin = Vec3(-32.f, 0.f, 24.f);
  move.s.velocity = Vec3(300.f, 0.f, 0.f);
  move.s.flags = PMF_ON_GROUND;
  move.cmd = (pm_cmd_t) {
    .msec = 100,
    .up = ducked ? -300 : 0
  };
  race_pmove_world = RACE_PMOVE_WORLD_TUNNEL;
  race_pmove_tunnel_height = height;
  Pm_Move(&move);
  return move;
}

START_TEST(_Race_PmoveQ2HullGeometryDiscriminators) {
  static const race_physics_preset_id_t identities[] = {
    RACE_PHYSICS_PRESET_INVALID,
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };

  for (size_t i = 0; i < lengthof(identities); i++) {
    const pm_move_t tunnel31 = Race_PmoveTunnelRun(
      identities[i], 31.f, true);
    ck_assert_msg(tunnel31.s.origin.x > -16.f,
                  "identity %d failed the 31-unit crouch tunnel",
                  identities[i]);
    ck_assert(tunnel31.s.flags & PMF_DUCKED);
  }

  const pm_move_t q229 = Race_PmoveTunnelRun(
    RACE_PHYSICS_PRESET_Q2, 29.f, true);
  const pm_move_t fix29 = Race_PmoveTunnelRun(
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1, 29.f, true);
  ck_assert_msg(q229.s.origin.x > -16.f,
                "Q2 did not pass the 29-unit discriminator");
  ck_assert_msg(q229.s.origin.x - fix29.s.origin.x > 8.f,
                "29-unit discriminator did not separate Q2 (%a) from Fix (%a)",
                (double) q229.s.origin.x, (double) fix29.s.origin.x);
  Race_PmoveAssertFloat("Q2 discriminator", "bounds.maxs.z",
                        q229.bounds.maxs.z, 4.f);
  Race_PmoveAssertFloat("Fix discriminator", "bounds.maxs.z",
                        fix29.bounds.maxs.z, 6.f);

  for (size_t i = 1; i < lengthof(identities); i++) {
    const pm_move_t canopy57 = Race_PmoveTunnelRun(
      identities[i], 57.f, false);
    ck_assert_msg(canopy57.s.origin.x > -16.f,
                  "identity %d failed the 57-unit standing canopy",
                  identities[i]);
    ck_assert(!(canopy57.s.flags & PMF_DUCKED));
  }
} END_TEST

static void Race_PmoveQ2DuckStream(
    const race_physics_preset_id_t preset, pm_move_t output[4]) {
  output[0] = Race_PmoveQ2DuckMove(
    preset, PMF_ON_GROUND, -300, 8, RACE_PMOVE_STAND_CLEAR);
  output[1] = Race_PmoveQ2DuckMove(
    preset, PMF_DUCKED, -300, 16, RACE_PMOVE_STAND_CLEAR);
  output[2] = Race_PmoveQ2DuckMove(
    preset, PMF_ON_GROUND, -300, 50, RACE_PMOVE_STAND_CLEAR);
  output[3] = Race_PmoveQ2DuckMove(
    preset, PMF_DUCKED, 0, 16,
    RACE_PMOVE_STAND_BLOCK_START_SOLID);
}

START_TEST(_Race_PmoveQ2DuckGameCgameParity) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    pm_move_t game[4], cgame[4];
    Race_PmoveQ2DuckStream(presets[preset], game);
    Race_PmoveQ2DuckStream(presets[preset], cgame);

    for (size_t command = 0; command < lengthof(game); command++) {
      ck_assert_msg(memcmp(game + command, cgame + command,
                           sizeof(*game)) == 0,
                    "preset %d duck stream differs after command %zu",
                    presets[preset], command + 1u);
    }
  }
} END_TEST

static pm_move_t Race_PmoveQ2LadderProbe(
    const race_physics_preset_id_t preset, const float origin_x) {
  Race_PmoveUseQ2NamedTestPhysics(preset, false);

  pm_move_t move = Race_PmoveSetup(RACE_PMOVE_LADDER);
  move.s.params = Race_PmoveQ2NamedParams(preset);
  move.s.params.gravity = 0.f;
  move.s.origin = Vec3(origin_x, 0.f, 40.f);
  move.s.velocity = Vec3_Zero();
  move.cmd = (pm_cmd_t) {
    .msec = 0
  };
  Pm_Move(&move);
  return move;
}

static pm_move_t Race_PmoveQ2LadderProbeCase(
    const race_physics_preset_id_t preset,
    const race_pmove_world_t world, const vec3_t origin,
    const vec3_t angles, const uint16_t flags, const uint16_t time,
    const pm_type_t type, const int16_t up) {
  Race_PmoveUseQ2NamedTestPhysics(preset, false);

  pm_move_t move = Race_PmoveSetup(RACE_PMOVE_LADDER);
  move.s.params = Race_PmoveQ2NamedParams(preset);
  move.s.type = type;
  move.s.origin = origin;
  move.s.velocity = Vec3_Zero();
  move.s.flags = flags;
  move.s.time = time;
  move.cmd = (pm_cmd_t) {
    .msec = 0,
    .up = up,
    .angles = angles
  };
  if (flags & PMF_ON_GROUND) {
    Race_PmoveSetGround(&move, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
  }
  race_pmove_world = world;
  race_pmove_ladder_num_traces = 0u;
  move.Trace = Race_PmoveLadderTrace;
  Pm_Move(&move);
  return move;
}

static size_t Race_PmoveQ2LadderProbeCount(void) {
  size_t count = 0u;
  for (size_t i = 0; i < race_pmove_ladder_num_traces; i++) {
    const vec3_t delta = Vec3_Subtract(race_pmove_ladder_traces[i].end,
                                      race_pmove_ladder_traces[i].start);
    if (fabsf(delta.x) == 1.f && delta.z == 0.f) {
      count++;
    }
  }
  return count;
}

START_TEST(_Race_PmoveQ2LadderProbeBoundary) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const pm_move_t inside = Race_PmoveQ2LadderProbe(
      presets[preset], 15.125f);
    const pm_move_t endpoint = Race_PmoveQ2LadderProbe(
      presets[preset], 15.f);

    ck_assert_msg(inside.s.flags & PMF_ON_LADDER,
                  "preset %d missed a ladder inside the one-unit probe",
                  presets[preset]);
    ck_assert_msg(!(endpoint.s.flags & PMF_ON_LADDER),
                  "preset %d accepted a ladder only touching the probe endpoint",
                  presets[preset]);
  }
} END_TEST

START_TEST(_Race_PmoveQ2LadderProbeContract) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const uint16_t timed_flags[] = {
    PMF_TIME_PUSHED,
    PMF_TIME_TRICK_JUMP,
    PMF_TIME_WATER_JUMP,
    PMF_TIME_LAND,
    PMF_TIME_TELEPORT,
    PMF_TIME_TRICK_START
  };
  static const pm_type_t hook_types[] = {
    PM_HOOK_PULL,
    PM_HOOK_SWING_MANUAL,
    PM_HOOK_SWING_AUTO
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const pm_move_t positive = Race_PmoveQ2LadderProbeCase(
      presets[preset], RACE_PMOVE_WORLD_LADDER,
      Vec3(15.125f, -8.125f, 40.125f), Vec3_Zero(),
      PMF_ON_GROUND | PMF_DUCKED, 0, PM_NORMAL, 1);
    ck_assert_uint_eq(Race_PmoveQ2LadderProbeCount(), 1u);
    const race_pmove_ladder_trace_t positive_probe =
      race_pmove_ladder_traces[0];
    Race_PmoveAssertVec3("positive ladder probe", "start",
                         positive_probe.start,
                         Vec3(15.125f, -8.125f, 40.125f));
    Race_PmoveAssertVec3("positive ladder probe", "end",
                         positive_probe.end,
                         Vec3(16.125f, -8.125f, 40.125f));
    Race_PmoveAssertFloat("positive ladder probe", "fraction",
                          positive_probe.result.fraction, .875f);
    Race_PmoveAssertBounds("positive ladder probe", positive_probe.bounds,
                           Pm_PlayerBounds(false));
    Race_PmoveAssertFloat("positive ladder standing hull", "maxs.z",
                          positive_probe.bounds.maxs.z, 32.f);
    ck_assert(positive_probe.result.contents & CONTENTS_LADDER);
    ck_assert(positive.s.flags & PMF_ON_LADDER);
    ck_assert(positive.s.flags & PMF_JUMP_HELD);
    ck_assert(!(positive.s.flags & (PMF_ON_GROUND | PMF_DUCKED |
                                    PMF_JUMPED)));
    ck_assert_ptr_null(positive.ground.ent);

    const pm_move_t exact = Race_PmoveQ2LadderProbeCase(
      presets[preset], RACE_PMOVE_WORLD_LADDER,
      Vec3(15.f, 0.f, 40.f), Vec3_Zero(), 0, 0, PM_NORMAL, 0);
    ck_assert_uint_eq(Race_PmoveQ2LadderProbeCount(), 1u);
    Race_PmoveAssertFloat("exact ladder endpoint", "fraction",
                          race_pmove_ladder_traces[0].result.fraction, 1.f);
    ck_assert(!(exact.s.flags & PMF_ON_LADDER));

    const pm_move_t outside = Race_PmoveQ2LadderProbeCase(
      presets[preset], RACE_PMOVE_WORLD_LADDER,
      Vec3(14.875f, 0.f, 40.f), Vec3_Zero(), 0, 0, PM_NORMAL, 0);
    ck_assert_uint_eq(Race_PmoveQ2LadderProbeCount(), 1u);
    Race_PmoveAssertFloat("outside ladder endpoint", "fraction",
                          race_pmove_ladder_traces[0].result.fraction, 1.f);
    ck_assert(!(outside.s.flags & PMF_ON_LADDER));

    const pm_move_t negative = Race_PmoveQ2LadderProbeCase(
      presets[preset], RACE_PMOVE_WORLD_LADDER_NEGATIVE,
      Vec3(-15.125f, -8.125f, 40.125f), Vec3(0.f, 180.f, 0.f),
      0, 0, PM_NORMAL, 0);
    ck_assert_uint_eq(Race_PmoveQ2LadderProbeCount(), 1u);
    Race_PmoveAssertFloat("negative ladder probe", "end.x",
                          race_pmove_ladder_traces[0].end.x, -16.125f);
    Race_PmoveAssertFloat("negative ladder probe", "fraction",
                          race_pmove_ladder_traces[0].result.fraction,
                          .875f);
    ck_assert(negative.s.flags & PMF_ON_LADDER);

    const pm_move_t start_solid = Race_PmoveQ2LadderProbeCase(
      presets[preset], RACE_PMOVE_WORLD_LADDER,
      Vec3(16.125f, 0.f, 40.f), Vec3(0.f, 180.f, 0.f),
      0, 0, PM_NORMAL, 0);
    ck_assert(race_pmove_ladder_traces[0].result.start_solid);
    ck_assert(!race_pmove_ladder_traces[0].result.all_solid);
    ck_assert(start_solid.s.flags & PMF_ON_LADDER);

    const pm_move_t all_solid = Race_PmoveQ2LadderProbeCase(
      presets[preset], RACE_PMOVE_WORLD_LADDER,
      Vec3(16.125f, 0.f, 40.f), Vec3_Zero(),
      0, 0, PM_NORMAL, 0);
    ck_assert(race_pmove_ladder_traces[0].result.start_solid);
    ck_assert(race_pmove_ladder_traces[0].result.all_solid);
    ck_assert(all_solid.s.flags & PMF_ON_LADDER);

    for (size_t flag = 0; flag < lengthof(timed_flags); flag++) {
      const pm_move_t timed = Race_PmoveQ2LadderProbeCase(
        presets[preset], RACE_PMOVE_WORLD_LADDER,
        Vec3(15.125f, 0.f, 40.f), Vec3_Zero(),
        timed_flags[flag], 16, PM_NORMAL, 0);
      ck_assert_uint_eq(Race_PmoveQ2LadderProbeCount(), 0u);
      ck_assert(!(timed.s.flags & PMF_ON_LADDER));
    }

    for (size_t type = 0; type < lengthof(hook_types); type++) {
      const pm_move_t hooked = Race_PmoveQ2LadderProbeCase(
        presets[preset], RACE_PMOVE_WORLD_LADDER,
        Vec3(15.125f, 0.f, 40.f), Vec3_Zero(),
        0, 0, hook_types[type], 0);
      ck_assert_uint_eq(Race_PmoveQ2LadderProbeCount(), 0u);
      ck_assert(!(hooked.s.flags & PMF_ON_LADDER));
    }
  }

  Race_PmoveUseCommonPhysics();
  pm_move_t common_only = Race_PmoveSetup(RACE_PMOVE_LADDER);
  common_only.s.origin = Vec3(12.125f, 0.f, 40.f);
  common_only.s.velocity = Vec3_Zero();
  common_only.cmd = (pm_cmd_t) { .msec = 0 };
  Pm_Move(&common_only);
  ck_assert(common_only.s.flags & PMF_ON_LADDER);
} END_TEST

static pm_move_t Race_PmoveQ2LadderDirectState(
    const race_physics_preset_id_t preset) {
  pm_move_t move = Race_PmoveSetup(RACE_PMOVE_EMPTY_IDLE);
  move.s.params = Race_PmoveQ2NamedParams(preset);
  move.s.origin = Vec3(0.f, 0.f, 128.f);
  move.s.velocity = Vec3_Zero();
  move.s.flags = PMF_ON_LADDER;
  move.s.time = 0;
  move.cmd = (pm_cmd_t) { 0 };
  race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
  race_pmove_feet_current = false;
  race_pmove_ladder_liquid = false;
  return move;
}

static vec3_t Race_PmoveQ2LadderPairedWish(
    const race_physics_preset_id_t preset, const pm_move_t input,
    const vec3_t angles) {
  Race_PmoveUseQ2NamedTestPhysics(preset, false);
  pm_move_t game = input;
  pm_move_t cgame = input;
  const vec3_t game_wish = Pm_Q2LadderWishForTest(&game, angles);
  const vec3_t cgame_wish =
    Pm_Q2LadderWishForTest_CgameReference(&cgame, angles);
  Race_PmoveAssertVec3("Q2 ladder GAME/CGAME wish", "wish",
                       game_wish, cgame_wish);
  ck_assert_msg(memcmp(&game.s.params, &input.s.params,
                       sizeof(input.s.params)) == 0 &&
                memcmp(&cgame.s.params, &input.s.params,
                       sizeof(input.s.params)) == 0,
                "Q2 ladder wish changed the named parameter vector");
  return game_wish;
}

START_TEST(_Race_PmoveQ2LadderWishContract) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const float ladder_speeds[] = { 200.f, 125.f };
  static const float water_speeds[] = { 400.f, 140.f };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const float ladder_speed = ladder_speeds[preset];
    const float horizontal = ladder_speed * .125f;
    pm_move_t move = Race_PmoveQ2LadderDirectState(presets[preset]);

    move.cmd.up = 300;
    Race_PmoveAssertVec3("ladder up", "wish",
      Race_PmoveQ2LadderPairedWish(presets[preset], move, Vec3_Zero()),
      Vec3(0.f, 0.f, ladder_speed));
    move.cmd.up = 1;
    Race_PmoveAssertVec3("ladder low up 1", "wish",
      Race_PmoveQ2LadderPairedWish(presets[preset], move, Vec3_Zero()),
      Vec3(0.f, 0.f, ladder_speed));
    move.cmd.up = 9;
    Race_PmoveAssertVec3("ladder low up 9", "wish",
      Race_PmoveQ2LadderPairedWish(presets[preset], move, Vec3_Zero()),
      Vec3(0.f, 0.f, ladder_speed));
    move.cmd.up = -300;
    Race_PmoveAssertVec3("ladder down", "wish",
      Race_PmoveQ2LadderPairedWish(presets[preset], move, Vec3_Zero()),
      Vec3(0.f, 0.f, -ladder_speed));

    move.cmd = (pm_cmd_t) { .right = 300 };
    Race_PmoveAssertVec3("ladder side", "wish",
      Race_PmoveQ2LadderPairedWish(presets[preset], move, Vec3_Zero()),
      Vec3(0.f, -horizontal, 0.f));
    move.cmd = (pm_cmd_t) { .forward = 300, .right = -300 };
    Race_PmoveAssertVec3("ladder diagonal", "wish",
      Race_PmoveQ2LadderPairedWish(presets[preset], move, Vec3_Zero()),
      Vec3(horizontal, horizontal, 0.f));

    move.cmd = (pm_cmd_t) { .forward = 300, .up = -300 };
    Race_PmoveAssertFloat("pitch climb precedence", "wish.z",
      Race_PmoveQ2LadderPairedWish(
        presets[preset], move, Vec3(-15.f, 0.f, 0.f)).z,
      ladder_speed);
    Race_PmoveAssertFloat("pitch descend precedence", "wish.z",
      Race_PmoveQ2LadderPairedWish(
        presets[preset], move, Vec3(15.f, 0.f, 0.f)).z,
      -ladder_speed);
    move.cmd.up = 0;
    Race_PmoveAssertFloat("pitch climb inside", "wish.z",
      Race_PmoveQ2LadderPairedWish(
        presets[preset], move, Vec3(-14.875f, 0.f, 0.f)).z,
      0.f);
    Race_PmoveAssertFloat("pitch descend inside", "wish.z",
      Race_PmoveQ2LadderPairedWish(
        presets[preset], move, Vec3(14.875f, 0.f, 0.f)).z,
      0.f);

    move.cmd = (pm_cmd_t) { .forward = 10 };
    const vec3_t divided = Race_PmoveQ2LadderPairedWish(
      presets[preset], move, Vec3(12.f, 0.f, 0.f));
    Race_PmoveAssertFloat("pitch divided basis", "wish.x", divided.x,
                          10.f * cosf(Radians(4.f)));
    Race_PmoveAssertFloat("pitch divided basis", "wish.z", divided.z,
                          0.f);

    move.cmd = (pm_cmd_t) { .forward = 300 };
    const float gate_values[] = {
      ladder_speed - .125f,
      ladder_speed,
      -ladder_speed,
      ladder_speed + .125f,
      -ladder_speed - .125f
    };
    for (size_t gate = 0; gate < lengthof(gate_values); gate++) {
      move.s.velocity.z = gate_values[gate];
      const vec3_t wish = Race_PmoveQ2LadderPairedWish(
        presets[preset], move, Vec3_Zero());
      Race_PmoveAssertFloat("ladder velocity gate", "wish.x", wish.x,
                            gate < 3 ? horizontal : 300.f);
      Race_PmoveAssertFloat("ladder velocity gate", "wish.z", wish.z,
                            0.f);
    }

    static const int32_t current_bits[] = {
      CONTENTS_CURRENT_0,
      CONTENTS_CURRENT_90,
      CONTENTS_CURRENT_180,
      CONTENTS_CURRENT_270,
      CONTENTS_CURRENT_UP,
      CONTENTS_CURRENT_DOWN
    };
    const vec3_t current_axes[] = {
      Vec3(1.f, 0.f, 0.f),
      Vec3(0.f, 1.f, 0.f),
      Vec3(-1.f, 0.f, 0.f),
      Vec3(0.f, -1.f, 0.f),
      Vec3(0.f, 0.f, 1.f),
      Vec3(0.f, 0.f, -1.f)
    };
    move = Race_PmoveQ2LadderDirectState(presets[preset]);
    move.water_level = WATER_FEET;
    for (size_t current = 0; current < lengthof(current_bits); current++) {
      move.water_type = CONTENTS_WATER | current_bits[current];
      Race_PmoveAssertVec3("raw ladder current", "wish",
        Race_PmoveQ2LadderPairedWish(presets[preset], move, Vec3_Zero()),
        Vec3_Scale(current_axes[current], water_speeds[preset]));
    }

    move.water_type = CONTENTS_WATER | CONTENTS_CURRENT_0 |
                      CONTENTS_CURRENT_180 | CONTENTS_CURRENT_UP |
                      CONTENTS_CURRENT_DOWN;
    Race_PmoveAssertVec3("opposing ladder currents", "wish",
      Race_PmoveQ2LadderPairedWish(presets[preset], move, Vec3_Zero()),
      Vec3_Zero());
    move.water_type = CONTENTS_WATER | CONTENTS_CURRENT_0 |
                      CONTENTS_CURRENT_90 | CONTENTS_CURRENT_UP;
    Race_PmoveAssertVec3("diagonal raw ladder current", "wish",
      Race_PmoveQ2LadderPairedWish(presets[preset], move, Vec3_Zero()),
      Vec3(water_speeds[preset], water_speeds[preset],
           water_speeds[preset]));

    move = Race_PmoveQ2LadderDirectState(presets[preset]);
    move.cmd.forward = 300;
    move.water_level = WATER_FEET;
    move.water_type = CONTENTS_WATER | CONTENTS_CURRENT_0;
    Race_PmoveAssertFloat("current order after ladder clamp", "wish.x",
      Race_PmoveQ2LadderPairedWish(presets[preset], move, Vec3_Zero()).x,
      horizontal + water_speeds[preset]);

    move = Race_PmoveQ2LadderDirectState(presets[preset]);
    move.ground = (cm_trace_t) {
      .ent = (void *) Race_PmoveEntityPointer(RACE_PMOVE_ENTITY_FLOOR),
      .contents = CONTENTS_CURRENT_270
    };
    Race_PmoveAssertVec3("raw ground conveyor", "wish",
      Race_PmoveQ2LadderPairedWish(presets[preset], move, Vec3_Zero()),
      Vec3(0.f, -100.f, 0.f));
  }
} END_TEST

static pm_move_t Race_PmoveQ2LadderPairedMove(
    const race_physics_preset_id_t preset, const pm_move_t input,
    const vec3_t angles) {
  Race_PmoveUseQ2NamedTestPhysics(preset, false);
  pm_move_t game = input;
  pm_move_t cgame = input;
  Pm_Q2LadderMoveForTest(&game, angles);
  Pm_Q2LadderMoveForTest_CgameReference(&cgame, angles);
  ck_assert_msg(memcmp(&game, &cgame, sizeof(game)) == 0,
                "preset %d direct GAME/CGAME ladder movement differs",
                preset);
  ck_assert_msg(memcmp(&game.s.params, &input.s.params,
                       sizeof(input.s.params)) == 0,
                "preset %d direct ladder movement changed parameters",
                preset);
  return game;
}

START_TEST(_Race_PmoveQ2LadderFrictionAccelerationAndRelaxation) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const uint16_t durations[] = {
    0, 1, 7, 8, 9, 16, 25, 33, 50, 62, 63, 100, 200
  };
  static const float q2_vertical[] = {
    0x0p+0f, 0x1.000002p+1f, 0x1.cp+3f, 0x1.000002p+4f,
    0x1.2p+4f, 0x1.000002p+5f, 0x1.9p+5f, 0x1.08p+6f,
    0x1.9p+6f, 0x1.fp+6f, 0x1.f8p+6f, 0x1.9p+7f, 0x1.9p+7f
  };
  static const float fix_vertical[] = {
    0x0p+0f, 0x1p+1f, 0x1.cp+3f, 0x1p+4f, 0x1.200002p+4f,
    0x1p+5f, 0x1.9p+5f, 0x1.08p+6f, 0x1.9p+6f,
    0x1.f00002p+6f, 0x1.f4p+6f, 0x1.f4p+6f, 0x1.f4p+6f
  };
  static const float q2_tangential[] = {
    0x0p+0f, 0x1.000002p-2f, 0x1.cp+0f, 0x1.000002p+1f,
    0x1.2p+1f, 0x1.000002p+2f, 0x1.9p+2f, 0x1.08p+3f,
    0x1.9p+3f, 0x1.fp+3f, 0x1.f8p+3f, 0x1.9p+4f, 0x1.9p+4f
  };
  static const float fix_tangential[] = {
    0x0p+0f, 0x1p-2f, 0x1.cp+0f, 0x1p+1f, 0x1.200002p+1f,
    0x1p+2f, 0x1.9p+2f, 0x1.08p+3f, 0x1.9p+3f,
    0x1.f00002p+3f, 0x1.f4p+3f, 0x1.f4p+3f, 0x1.f4p+3f
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const pm_params_t named = Race_PmoveQ2NamedParams(presets[preset]);
    Race_PmoveAssertFloat("named ladder speed", "speed_ladder",
                          named.speed_ladder,
                          preset ? 125.f : 200.f);
    Race_PmoveAssertFloat("named ladder accel", "accel_ladder",
                          named.accel_ladder,
                          preset ? 16.f : 10.f);
    Race_PmoveAssertFloat("effective ladder friction", "friction_ground",
                          named.friction_ground, 6.f);

    for (size_t duration = 0; duration < lengthof(durations); duration++) {
      pm_move_t vertical = Race_PmoveQ2LadderDirectState(presets[preset]);
      vertical.cmd.msec = durations[duration];
      vertical.cmd.up = 300;
      vertical = Race_PmoveQ2LadderPairedMove(
        presets[preset], vertical, Vec3_Zero());
      Race_PmoveAssertVec3("ladder vertical startup", "velocity",
        vertical.s.velocity,
        Vec3(0.f, 0.f, preset ? fix_vertical[duration]
                              : q2_vertical[duration]));

      pm_move_t tangential = Race_PmoveQ2LadderDirectState(presets[preset]);
      tangential.cmd.msec = durations[duration];
      tangential.cmd.right = 300;
      tangential = Race_PmoveQ2LadderPairedMove(
        presets[preset], tangential, Vec3_Zero());
      Race_PmoveAssertVec3("ladder tangential startup", "velocity",
        tangential.s.velocity,
        Vec3(0.f, -(preset ? fix_tangential[duration]
                              : q2_tangential[duration]), 0.f));
    }

    pm_move_t idle_positive = Race_PmoveQ2LadderDirectState(presets[preset]);
    idle_positive.cmd.msec = 16;
    idle_positive.s.velocity.z = 100.f;
    idle_positive = Race_PmoveQ2LadderPairedMove(
      presets[preset], idle_positive, Vec3_Zero());
    Race_PmoveAssertVec3("positive idle ladder relaxation", "velocity",
                         idle_positive.s.velocity,
                         Vec3(0.f, 0.f, 77.6f));

    pm_move_t idle_negative = Race_PmoveQ2LadderDirectState(presets[preset]);
    idle_negative.cmd.msec = 16;
    idle_negative.s.velocity.z = -100.f;
    idle_negative = Race_PmoveQ2LadderPairedMove(
      presets[preset], idle_negative, Vec3_Zero());
    Race_PmoveAssertVec3("negative idle ladder relaxation", "velocity",
                         idle_negative.s.velocity,
                         Vec3(0.f, 0.f, -77.6f));

    pm_move_t inert_a = Race_PmoveQ2LadderDirectState(presets[preset]);
    inert_a.cmd.msec = 16;
    inert_a.s.params.gravity = 0.f;
    inert_a.s.params.friction_ladder = 0.f;
    inert_a.s.velocity.x = 100.f;
    pm_move_t inert_b = inert_a;
    inert_b.s.params.friction_ladder = 999.f;
    inert_a = Race_PmoveQ2LadderPairedMove(
      presets[preset], inert_a, Vec3_Zero());
    inert_b = Race_PmoveQ2LadderPairedMove(
      presets[preset], inert_b, Vec3_Zero());
    Race_PmoveAssertVec3("friction_ladder inertness", "velocity",
                         inert_a.s.velocity, inert_b.s.velocity);
    Race_PmoveAssertFloat("ground friction ladder result", "velocity.x",
                          inert_a.s.velocity.x, 90.4f);

    pm_move_t no_ground_friction =
      Race_PmoveQ2LadderDirectState(presets[preset]);
    no_ground_friction.cmd.msec = 16;
    no_ground_friction.s.params.gravity = 0.f;
    no_ground_friction.s.params.friction_ground = 0.f;
    no_ground_friction.s.velocity.x = 100.f;
    no_ground_friction = Race_PmoveQ2LadderPairedMove(
      presets[preset], no_ground_friction, Vec3_Zero());
    Race_PmoveAssertFloat("zero ground friction ladder", "velocity.x",
                          no_ground_friction.s.velocity.x, 100.f);

    pm_move_t water_friction_a = inert_a;
    water_friction_a.s.origin = Vec3(0.f, 0.f, 128.f);
    water_friction_a.s.velocity = Vec3(100.f, 0.f, 0.f);
    water_friction_a.water_level = WATER_FEET;
    water_friction_a.s.params.friction_water = 0.f;
    pm_move_t water_friction_b = water_friction_a;
    water_friction_b.s.params.friction_water = 999.f;
    water_friction_a = Race_PmoveQ2LadderPairedMove(
      presets[preset], water_friction_a, Vec3_Zero());
    water_friction_b = Race_PmoveQ2LadderPairedMove(
      presets[preset], water_friction_b, Vec3_Zero());
    Race_PmoveAssertVec3("ladder excludes water friction", "velocity",
                         water_friction_a.s.velocity,
                         water_friction_b.s.velocity);

    pm_move_t ground_cap = Race_PmoveQ2LadderDirectState(presets[preset]);
    ground_cap.cmd.msec = 16;
    ground_cap.water_level = WATER_FEET;
    ground_cap.water_type = CONTENTS_WATER | CONTENTS_CURRENT_0;
    ground_cap.s.params.speed_water = 1000.f;
    ground_cap.s.params.accel_ladder = 1000.f;
    ground_cap = Race_PmoveQ2LadderPairedMove(
      presets[preset], ground_cap, Vec3_Zero());
    Race_PmoveAssertFloat("ladder ground-speed cap", "velocity.x",
                          ground_cap.s.velocity.x, 300.f);

    pm_move_t duck_cap = Race_PmoveQ2LadderDirectState(presets[preset]);
    duck_cap.s.flags |= PMF_DUCKED;
    duck_cap.cmd.msec = 16;
    duck_cap.water_level = WATER_FEET;
    duck_cap.water_type = CONTENTS_WATER | CONTENTS_CURRENT_0;
    duck_cap.s.params.speed_water = 1000.f;
    duck_cap.s.params.accel_ladder = 1000.f;
    duck_cap = Race_PmoveQ2LadderPairedMove(
      presets[preset], duck_cap, Vec3_Zero());
    Race_PmoveAssertFloat("ladder duck-speed cap", "velocity.x",
                          duck_cap.s.velocity.x,
                          preset ? 140.f : 100.f);

    pm_move_t current_gravity = Race_PmoveQ2LadderDirectState(
      presets[preset]);
    current_gravity.cmd.msec = 16;
    current_gravity.s.velocity.z = 100.f;
    current_gravity.water_level = WATER_FEET;
    current_gravity.water_type = CONTENTS_WATER | CONTENTS_CURRENT_UP;
    pm_move_t current_zero_gravity = current_gravity;
    current_zero_gravity.s.params.gravity = 0.f;
    current_gravity = Race_PmoveQ2LadderPairedMove(
      presets[preset], current_gravity, Vec3_Zero());
    current_zero_gravity = Race_PmoveQ2LadderPairedMove(
      presets[preset], current_zero_gravity, Vec3_Zero());
    Race_PmoveAssertVec3("vertical current suppresses relaxation", "velocity",
                         current_gravity.s.velocity,
                         current_zero_gravity.s.velocity);
  }
} END_TEST

START_TEST(_Race_PmoveQ2LadderStepSlide) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const vec3_t expected_origins[] = {
    { { 0x1.1ap+4f, 0x0p+0f, 0x1.4p+5f } },
    { { 0x1.0bp+4f, 0x0p+0f, 0x1.4p+5f } }
  };
  static const vec3_t expected_velocities[] = {
    { { 0x1.9p+4f, 0x0p+0f, 0x0p+0f } },
    { { 0x1.f4p+3f, 0x0p+0f, 0x0p+0f } }
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    pm_move_t step = Race_PmoveQ2LadderDirectState(presets[preset]);
    step.s.origin = Vec3(15.125f, 0.f, 24.f);
    step.cmd.msec = 100;
    step.cmd.forward = 300;
    race_pmove_world = RACE_PMOVE_WORLD_STEP_16;
    step = Race_PmoveQ2LadderPairedMove(
      presets[preset], step, Vec3_Zero());
    Race_PmoveAssertVec3("Q2 ladder 16-unit step", "origin",
                         step.s.origin, expected_origins[preset]);
    Race_PmoveAssertVec3("Q2 ladder 16-unit step", "velocity",
                         step.s.velocity, expected_velocities[preset]);
    Race_PmoveAssertFloat("Q2 ladder 16-unit step", "step",
                          step.step, 0.f);
    ck_assert_int_eq(step.num_touched, 1);
    ck_assert_int_eq(Race_PmoveEntityId(step.touched[0].ent),
                     RACE_PMOVE_ENTITY_STEP);
  }
} END_TEST

static pm_move_t Race_PmoveQ2GroundedLadderExitState(
    const race_physics_preset_id_t preset, const int16_t up,
    const bool held) {
  pm_move_t move = Race_PmoveSetup(RACE_PMOVE_JUMP);
  move.s.params = Race_PmoveQ2NamedParams(preset);
  move.s.origin = Vec3(0.f, 0.f, 24.f);
  move.s.velocity = Vec3_Zero();
  move.s.flags = held ? PMF_JUMP_HELD : 0;
  Race_PmoveSetGround(&move, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
  move.cmd = (pm_cmd_t) {
    .msec = 16,
    .up = up
  };
  race_pmove_world = RACE_PMOVE_WORLD_FLAT;
  Pm_Move(&move);
  return move;
}

START_TEST(_Race_PmoveQ2LadderNoAutohopExitState) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    Race_PmoveUseQ2NamedTestPhysics(presets[preset], false);

    for (int16_t up = 1; up <= 9; up += 8) {
      const pm_move_t attached = Race_PmoveQ2LadderProbeCase(
        presets[preset], RACE_PMOVE_WORLD_LADDER,
        Vec3(15.125f, 0.f, 40.f), Vec3_Zero(),
        0, 0, PM_NORMAL, up);
      ck_assert(attached.s.flags & PMF_ON_LADDER);
      ck_assert(attached.s.flags & PMF_JUMP_HELD);
      ck_assert(!(attached.s.flags & PMF_JUMPED));

      const pm_move_t held_threshold = Race_PmoveQ2GroundedLadderExitState(
        presets[preset], up, true);
      ck_assert(held_threshold.s.flags & PMF_JUMP_HELD);
      ck_assert(!(held_threshold.s.flags & PMF_JUMPED));
      ck_assert(held_threshold.s.flags & PMF_ON_GROUND);
    }

    pm_move_t attached = Race_PmoveSetup(RACE_PMOVE_LADDER);
    attached.s.params = Race_PmoveQ2NamedParams(presets[preset]);
    attached.s.origin = Vec3(15.125f, 0.f, 40.f);
    attached.s.velocity = Vec3_Zero();
    attached.cmd = (pm_cmd_t) {
      .msec = 16,
      .up = 300
    };
    race_pmove_world = RACE_PMOVE_WORLD_LADDER;
    Pm_Move(&attached);
    ck_assert(attached.s.flags & PMF_ON_LADDER);
    ck_assert(attached.s.flags & PMF_JUMP_HELD);
    ck_assert(!(attached.s.flags & PMF_JUMPED));

    size_t held_jumps = 0u;
    pm_move_t held = Race_PmoveQ2GroundedLadderExitState(
      presets[preset], 10, true);
    held_jumps += !!(held.s.flags & PMF_JUMPED);
    for (size_t command = 1; command < 32u; command++) {
      held = Race_PmoveQ2GroundedLadderExitState(
        presets[preset], 10, true);
      held_jumps += !!(held.s.flags & PMF_JUMPED);
    }
    ck_assert_uint_eq(held_jumps, 0u);
    ck_assert(held.s.flags & PMF_JUMP_HELD);

    pm_move_t released = Race_PmoveQ2GroundedLadderExitState(
      presets[preset], 0, true);
    ck_assert(!(released.s.flags & (PMF_JUMP_HELD | PMF_JUMPED)));
    ck_assert(released.s.flags & PMF_ON_GROUND);

    pm_move_t fresh = Race_PmoveQ2GroundedLadderExitState(
      presets[preset], 10, false);
    ck_assert(fresh.s.flags & PMF_JUMPED);
    ck_assert(fresh.s.flags & PMF_JUMP_HELD);
    ck_assert(!(fresh.s.flags & PMF_ON_GROUND));
    Race_PmoveAssertFloat("fresh ladder-exit jump", "velocity.z",
                          fresh.s.velocity.z, 0x1.013334p+8f);

    pm_move_t landed_held = Race_PmoveQ2GroundedLadderExitState(
      presets[preset], 300, true);
    ck_assert(!(landed_held.s.flags & PMF_JUMPED));
    ck_assert(landed_held.s.flags & PMF_JUMP_HELD);
    ck_assert(landed_held.s.flags & PMF_ON_GROUND);

    released = Race_PmoveQ2GroundedLadderExitState(
      presets[preset], 0, true);
    ck_assert(!(released.s.flags & PMF_JUMP_HELD));
    fresh = Race_PmoveQ2GroundedLadderExitState(
      presets[preset], 300, false);
    ck_assert(fresh.s.flags & PMF_JUMPED);
    Race_PmoveAssertFloat("second fresh ladder-exit jump", "velocity.z",
                          fresh.s.velocity.z, 0x1.013334p+8f);

    pm_move_t pitch_only = Race_PmoveSetup(RACE_PMOVE_LADDER);
    pitch_only.s.params = Race_PmoveQ2NamedParams(presets[preset]);
    pitch_only.s.origin = Vec3(15.125f, 0.f, 40.f);
    pitch_only.s.velocity = Vec3_Zero();
    pitch_only.cmd = (pm_cmd_t) {
      .msec = 16,
      .forward = 300,
      .angles = Vec3(345.f, 0.f, 0.f)
    };
    race_pmove_world = RACE_PMOVE_WORLD_LADDER;
    Pm_Move(&pitch_only);
    ck_assert(pitch_only.s.flags & PMF_ON_LADDER);
    ck_assert(!(pitch_only.s.flags & (PMF_JUMP_HELD | PMF_JUMPED)));
    Race_PmoveAssertFloat("wrapped pitch ladder", "angles.x",
                          pitch_only.angles.x, -15.f);
    ck_assert(pitch_only.s.velocity.z > 0.f);
    fresh = Race_PmoveQ2GroundedLadderExitState(
      presets[preset], 10, false);
    ck_assert(fresh.s.flags & PMF_JUMPED);
  }
} END_TEST

static void Race_PmoveQ2LadderNormalizeCallbacks(pm_move_t *move) {
  move->Trace = NULL;
}

START_TEST(_Race_PmoveQ2LadderIndependentGameCgame) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const uint16_t durations[] = { 8, 16, 25, 50 };
  static const pm_cmd_t commands[] = {
    { .up = 0 },
    { .up = 300 },
    { .right = 300 },
    { .up = -300 }
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    for (size_t orientation = 0; orientation < 2u; orientation++) {
      Race_PmoveUseQ2NamedTestPhysics(presets[preset], true);
      Pm_SetQ2SnapEnabledForTest_CgameReference(true);
      const pm_params_t params = Race_PmoveQ2NamedParams(presets[preset]);
      pm_move_t game = Race_PmoveSetup(RACE_PMOVE_LADDER);
      game.s.params = params;
      game.s.origin = Vec3(orientation ? -15.125f : 15.125f,
                           -8.125f, 40.125f);
      game.s.velocity = Vec3_Zero();
      game.Trace = Race_PmoveLadderTrace;
      pm_move_t cgame = game;
      cgame.Trace = Race_PmoveLadderTraceCgame;
      race_pmove_world = orientation
        ? RACE_PMOVE_WORLD_LADDER_NEGATIVE
        : RACE_PMOVE_WORLD_LADDER;
      race_pmove_ladder_num_traces = 0u;
      race_pmove_ladder_cgame_num_traces = 0u;

      for (size_t command = 0; command < lengthof(commands); command++) {
        game.cmd = cgame.cmd = commands[command];
        game.cmd.msec = cgame.cmd.msec = durations[command];
        game.cmd.angles.y = cgame.cmd.angles.y = orientation ? 180.f : 0.f;
        Pm_Move(&game);
        Pm_Move_CgameReference(&cgame);

        pm_move_t game_copy = game;
        pm_move_t cgame_copy = cgame;
        Race_PmoveQ2LadderNormalizeCallbacks(&game_copy);
        Race_PmoveQ2LadderNormalizeCallbacks(&cgame_copy);
        ck_assert_msg(memcmp(&game_copy, &cgame_copy,
                             sizeof(game_copy)) == 0,
                      "preset %d orientation %zu command %zu GAME/CGAME differs",
                      presets[preset], orientation, command + 1u);
        Race_PmoveCrossAssertGrid("Q2 ladder GAME", &game.s, command);
        Race_PmoveCrossAssertGrid("Q2 ladder CGAME", &cgame.s, command);
        ck_assert_msg(memcmp(&game.s.params, &params, sizeof(params)) == 0,
                      "GAME ladder stream changed named parameters");
        ck_assert_msg(memcmp(&cgame.s.params, &params, sizeof(params)) == 0,
                      "CGAME ladder stream changed named parameters");
      }

      ck_assert_uint_eq(race_pmove_ladder_num_traces,
                        race_pmove_ladder_cgame_num_traces);
      ck_assert_msg(memcmp(race_pmove_ladder_traces,
                           race_pmove_ladder_cgame_traces,
                           race_pmove_ladder_num_traces *
                             sizeof(*race_pmove_ladder_traces)) == 0,
                    "preset %d orientation %zu trace streams differ",
                    presets[preset], orientation);
    }
  }
} END_TEST

START_TEST(_Race_PmoveQ2LadderAiAndWaterBoundary) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    Race_PmoveUseQ2NamedTestPhysics(presets[preset], false);
    const pm_params_t params = Race_PmoveQ2NamedParams(presets[preset]);

    pm_move_t ai = Race_PmoveSetup(RACE_PMOVE_LADDER);
    ai.s.params = params;
    ai.s.origin = Vec3(15.125f, 0.f, 40.f);
    ai.s.velocity = Vec3_Zero();
    ai.cmd = (pm_cmd_t) {
      .msec = 100,
      .up = PM_SPEED_JUMP
    };
    race_pmove_world = RACE_PMOVE_WORLD_LADDER;
    Pm_Move(&ai);
    ck_assert(ai.s.flags & PMF_ON_LADDER);
    ck_assert(ai.s.flags & PMF_JUMP_HELD);
    ck_assert(!(ai.s.flags & PMF_JUMPED));
    Race_PmoveAssertFloat("direct AI ladder climb", "velocity.z",
                          ai.s.velocity.z, preset ? 125.f : 200.f);
    ck_assert_msg(memcmp(&ai.s.params, &params, sizeof(params)) == 0,
                  "direct AI ladder move changed named parameters");

    race_pmove_feet_current = true;
    race_pmove_current_contents = CONTENTS_CURRENT_90;
    pm_move_t feet = Race_PmoveSetup(RACE_PMOVE_LADDER);
    feet.s.params = params;
    feet.s.origin = Vec3(15.125f, 0.f, 24.f);
    feet.s.velocity = Vec3_Zero();
    feet.cmd = (pm_cmd_t) { .msec = 100 };
    race_pmove_world = RACE_PMOVE_WORLD_LADDER;
    Pm_Move(&feet);
    race_pmove_feet_current = false;
    race_pmove_current_contents = CONTENTS_CURRENT_90;
    ck_assert_int_eq(feet.water_level, WATER_FEET);
    ck_assert(feet.s.flags & PMF_ON_LADDER);
    Race_PmoveAssertFloat("feet-depth ladder current", "velocity.y",
                          feet.s.velocity.y,
                          preset ? 140.f : 300.f);

    race_pmove_ladder_liquid = true;
    race_pmove_ladder_liquid_surface = 24.f;
    pm_move_t waist = Race_PmoveSetup(RACE_PMOVE_LADDER);
    waist.s.params = params;
    waist.s.origin = Vec3(15.125f, 0.f, 24.f);
    waist.s.velocity = Vec3_Zero();
    waist.cmd = (pm_cmd_t) { .msec = 16 };
    race_pmove_world = RACE_PMOVE_WORLD_LADDER;
    Pm_Move(&waist);
    race_pmove_ladder_liquid = false;
    ck_assert_int_eq(waist.water_level, WATER_WAIST);
    ck_assert(waist.s.flags & PMF_ON_LADDER);
    ck_assert_msg(!(waist.s.flags & PMF_JUMPED),
                  "known-incomplete waist ladder synthesized a jump");
  }
} END_TEST

static pm_move_t Race_PmoveQ2WaterDirectState(
    const race_physics_preset_id_t preset) {
  Race_PmoveUseQ2NamedTestPhysics(preset, false);

  pm_move_t move = Race_PmoveSetup(RACE_PMOVE_EMPTY_IDLE);
  move.s.params = Race_PmoveQ2NamedParams(preset);
  move.s.origin = Vec3(0.f, 0.f, 24.f);
  move.s.velocity = Vec3_Zero();
  move.s.flags = 0;
  move.s.time = 0;
  move.s.view_offset = Vec3(0.f, 0.f, 22.f);
  move.cmd = (pm_cmd_t) { .msec = 16 };
  race_pmove_world = RACE_PMOVE_WORLD_EMPTY;
  return move;
}

START_TEST(_Race_PmoveQ2WaterClassification) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const struct {
    int32_t samples[3];
    size_t sample_count;
    size_t query_count;
    pm_water_level_t level;
    int32_t type;
    bool under;
  } cases[] = {
    { { 0 }, 1u, 1u, WATER_NONE, 0, false },
    { { CONTENTS_WATER, 0 }, 2u, 2u,
      WATER_FEET, CONTENTS_WATER, false },
    { { CONTENTS_WATER, CONTENTS_SLIME, 0 }, 3u, 3u,
      WATER_WAIST, CONTENTS_WATER | CONTENTS_SLIME, false },
    { { CONTENTS_WATER | CONTENTS_CURRENT_0,
        CONTENTS_SLIME | CONTENTS_CURRENT_90,
        CONTENTS_LAVA | CONTENTS_CURRENT_UP }, 3u, 3u,
      WATER_UNDER,
      CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA |
        CONTENTS_CURRENT_0 | CONTENTS_CURRENT_90 | CONTENTS_CURRENT_UP,
      true }
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    for (size_t i = 0; i < lengthof(cases); i++) {
      pm_move_t game = Race_PmoveQ2WaterDirectState(presets[preset]);
      game.PointContents = Race_PmoveQ2WaterScriptPointContents;
      pm_move_t cgame = game;

      memcpy(race_pmove_q2_water_script, cases[i].samples,
             sizeof(cases[i].samples));
      race_pmove_q2_water_script_count = cases[i].sample_count;
      race_pmove_q2_water_point_count = 0u;
      Pm_Q2CheckWaterForTest(&game);

      ck_assert_uint_eq(race_pmove_q2_water_point_count,
                        cases[i].query_count);
      Race_PmoveAssertVec3("Q2 water feet query", "point",
                           race_pmove_q2_water_points[0],
                           Vec3(0.f, 0.f, .25f));
      if (cases[i].query_count > 1u) {
        Race_PmoveAssertVec3("Q2 water waist query", "point",
                             race_pmove_q2_water_points[1],
                             Vec3(0.f, 0.f, 24.f));
      }
      if (cases[i].query_count > 2u) {
        Race_PmoveAssertVec3("Q2 water head query", "point",
                             race_pmove_q2_water_points[2],
                             Vec3(0.f, 0.f, 47.f));
      }
      ck_assert_int_eq(game.water_level, cases[i].level);
      ck_assert_int_eq(game.water_type, cases[i].type);
      ck_assert_int_eq(!!(game.s.flags & PMF_UNDER_WATER), cases[i].under);

      race_pmove_q2_water_point_count = 0u;
      Pm_Q2CheckWaterForTest_CgameReference(&cgame);
      ck_assert_msg(memcmp(&game, &cgame, sizeof(game)) == 0,
                    "preset %d classification case %zu GAME/CGAME differs",
                    presets[preset], i);
    }

    pm_move_t ducked = Race_PmoveQ2WaterDirectState(presets[preset]);
    ducked.s.flags = PMF_DUCKED;
    ducked.s.view_offset.z = -2.f;
    ducked.PointContents = Race_PmoveQ2WaterScriptPointContents;
    race_pmove_q2_water_script[0] = CONTENTS_WATER;
    race_pmove_q2_water_script[1] = CONTENTS_WATER;
    race_pmove_q2_water_script[2] = CONTENTS_WATER;
    race_pmove_q2_water_script_count = 3u;
    race_pmove_q2_water_point_count = 0u;
    Pm_Q2CheckWaterForTest(&ducked);
    ck_assert_int_eq(ducked.water_level, WATER_UNDER);
    Race_PmoveAssertVec3("Q2 ducked water head query", "point",
                         race_pmove_q2_water_points[2],
                         Vec3(0.f, 0.f, 23.f));
    Race_PmoveAssertFloat("Q2 ducked water hull", "maxs.z",
                          ducked.bounds.maxs.z, preset ? 6.f : 4.f);
  }
} END_TEST

START_TEST(_Race_PmoveQ2WaterRawCurrents) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const int32_t bits[] = {
    CONTENTS_CURRENT_0, CONTENTS_CURRENT_90,
    CONTENTS_CURRENT_180, CONTENTS_CURRENT_270,
    CONTENTS_CURRENT_UP, CONTENTS_CURRENT_DOWN
  };
  static const vec3_t axes[] = {
    { { 1.f, 0.f, 0.f } }, { { 0.f, 1.f, 0.f } },
    { { -1.f, 0.f, 0.f } }, { { 0.f, -1.f, 0.f } },
    { { 0.f, 0.f, 1.f } }, { { 0.f, 0.f, -1.f } }
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const float water_speed = preset ? 140.f : 400.f;
    for (size_t axis = 0; axis < lengthof(bits); axis++) {
      pm_move_t game = Race_PmoveQ2WaterDirectState(presets[preset]);
      game.water_level = WATER_WAIST;
      game.water_type = CONTENTS_WATER | bits[axis];
      pm_move_t cgame = game;
      const vec3_t game_wish = Pm_Q2AddCurrentsForTest(
        &game, Vec3_Zero(), Vec3_Zero());
      const vec3_t cgame_wish = Pm_Q2AddCurrentsForTest_CgameReference(
        &cgame, Vec3_Zero(), Vec3_Zero());
      Race_PmoveAssertVec3("Q2 raw water current", "wish",
                           game_wish, Vec3_Scale(axes[axis], water_speed));
      Race_PmoveAssertVec3("Q2 raw current GAME/CGAME", "wish",
                           game_wish, cgame_wish);
    }

    pm_move_t opposing = Race_PmoveQ2WaterDirectState(presets[preset]);
    opposing.water_level = WATER_WAIST;
    opposing.water_type = CONTENTS_WATER | CONTENTS_CURRENT_0 |
                          CONTENTS_CURRENT_180 | CONTENTS_CURRENT_UP |
                          CONTENTS_CURRENT_DOWN;
    Race_PmoveAssertVec3("Q2 opposing currents", "wish",
                         Pm_Q2AddCurrentsForTest(
                           &opposing, Vec3_Zero(), Vec3_Zero()),
                         Vec3_Zero());

    pm_move_t feet = Race_PmoveQ2WaterDirectState(presets[preset]);
    feet.water_level = WATER_FEET;
    feet.water_type = CONTENTS_WATER | CONTENTS_CURRENT_90;
    Race_PmoveSetGround(&feet, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
    Race_PmoveAssertVec3("Q2 grounded feet half current", "wish",
                         Pm_Q2AddCurrentsForTest(
                           &feet, Vec3_Zero(), Vec3_Zero()),
                         Vec3(0.f, water_speed * .5f, 0.f));

    pm_move_t combined = Race_PmoveQ2WaterDirectState(presets[preset]);
    combined.water_level = WATER_WAIST;
    combined.water_type = CONTENTS_WATER | CONTENTS_CURRENT_0 |
                          CONTENTS_CURRENT_90;
    Race_PmoveSetGround(&combined, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
    combined.ground.contents |= CONTENTS_CURRENT_0;
    Race_PmoveAssertVec3("Q2 water and conveyor current", "wish",
                         Pm_Q2AddCurrentsForTest(
                           &combined, Vec3_Zero(), Vec3_Zero()),
                         Vec3(water_speed + 100.f, water_speed, 0.f));
  }
} END_TEST

START_TEST(_Race_PmoveQ2WaterFriction) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const float water_friction = preset ? 2.f : 1.f;
    for (int32_t level = WATER_FEET; level <= WATER_UNDER; level++) {
      pm_move_t move = Race_PmoveQ2WaterDirectState(presets[preset]);
      move.s.velocity = Vec3(100.f, 0.f, 0.f);
      move.water_level = level;
      Pm_Q2FrictionForTest(&move, Vec3_Zero());
      const float speed = 100.f;
      const float drop = speed * water_friction * level * .016f;
      const float expected = speed * (Maxf(0.f, speed - drop) / speed);
      Race_PmoveAssertFloat("Q2 water friction by level", "velocity.x",
                            move.s.velocity.x, expected);
    }

    pm_move_t ground = Race_PmoveQ2WaterDirectState(presets[preset]);
    ground.s.velocity = Vec3(100.f, 0.f, 0.f);
    ground.water_level = WATER_FEET;
    Race_PmoveSetGround(&ground, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
    Pm_Q2FrictionForTest(&ground, Vec3_Zero());
    Race_PmoveAssertFloat("Q2 combined ground-water friction", "velocity.x",
                          ground.s.velocity.x,
                          100.f * (1.f - (6.f + water_friction) * .016f));

    pm_move_t ladder = Race_PmoveQ2WaterDirectState(presets[preset]);
    ladder.s.velocity = Vec3(100.f, 0.f, 0.f);
    ladder.s.flags = PMF_ON_LADDER;
    ladder.water_level = WATER_UNDER;
    pm_move_t ladder_cgame = ladder;
    Pm_Q2FrictionForTest(&ladder, Vec3_Zero());
    Pm_Q2FrictionForTest_CgameReference(&ladder_cgame, Vec3_Zero());
    Race_PmoveAssertFloat("Q2 ladder excludes water friction", "velocity.x",
                          ladder.s.velocity.x, 90.4f);
    ck_assert_msg(memcmp(&ladder, &ladder_cgame, sizeof(ladder)) == 0,
                  "preset %d Q2 friction GAME/CGAME differs",
                  presets[preset]);

    pm_move_t low = Race_PmoveQ2WaterDirectState(presets[preset]);
    low.s.velocity = Vec3(.5f, -.25f, .75f);
    low.water_level = WATER_UNDER;
    Pm_Q2FrictionForTest(&low, Vec3_Zero());
    Race_PmoveAssertVec3("Q2 low-speed friction", "velocity",
                         low.s.velocity, Vec3(0.f, 0.f, .75f));
  }
} END_TEST

START_TEST(_Race_PmoveQ2Swimming) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const uint16_t durations[] = { 8, 16, 25 };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const float accel = preset ? 3.f : 10.f;
    for (size_t duration = 0; duration < lengthof(durations); duration++) {
      pm_move_t game = Race_PmoveQ2WaterDirectState(presets[preset]);
      game.water_level = WATER_WAIST;
      game.water_type = CONTENTS_WATER;
      game.cmd = (pm_cmd_t) {
        .msec = durations[duration],
        .forward = 300
      };
      pm_move_t cgame = game;
      Pm_Q2WaterMoveForTest(&game, Vec3_Zero());
      Pm_Q2WaterMoveForTest_CgameReference(&cgame, Vec3_Zero());
      const float expected = Minf(150.f,
        accel * durations[duration] * .001f * 150.f);
      Race_PmoveAssertVec3("Q2 forward swim", "velocity",
                           game.s.velocity, Vec3(expected, 0.f, 0.f));
      ck_assert_msg(memcmp(&game, &cgame, sizeof(game)) == 0,
                    "preset %d swim duration %u GAME/CGAME differs",
                    presets[preset], durations[duration]);
    }

    pm_move_t idle = Race_PmoveQ2WaterDirectState(presets[preset]);
    idle.water_level = WATER_UNDER;
    idle.water_type = CONTENTS_WATER;
    Pm_Q2WaterMoveForTest(&idle, Vec3_Zero());
    Race_PmoveAssertFloat("Q2 idle swim sink", "velocity.z",
                          idle.s.velocity.z, -accel * .016f * 30.f);

    pm_move_t capped = Race_PmoveQ2WaterDirectState(presets[preset]);
    capped.water_level = WATER_WAIST;
    capped.water_type = CONTENTS_WATER;
    capped.cmd.forward = 1000;
    Pm_Q2WaterMoveForTest(&capped, Vec3_Zero());
    Race_PmoveAssertFloat("Q2 clamp before half", "velocity.x",
                          capped.s.velocity.x,
                          Minf(150.f, accel * .016f * 150.f));

    pm_move_t pitched = Race_PmoveQ2WaterDirectState(presets[preset]);
    pitched.water_level = WATER_UNDER;
    pitched.water_type = CONTENTS_WATER;
    pitched.cmd.forward = 300;
    Pm_Q2WaterMoveForTest(&pitched, Vec3(45.f, 0.f, 0.f));
    ck_assert(pitched.s.velocity.x > 0.f);
    ck_assert(pitched.s.velocity.z < 0.f);

    pm_move_t up = Race_PmoveQ2WaterDirectState(presets[preset]);
    up.water_level = WATER_UNDER;
    up.water_type = CONTENTS_WATER | CONTENTS_CURRENT_UP;
    up.cmd.up = 300;
    Pm_Q2WaterMoveForTest(&up, Vec3_Zero());
    ck_assert(up.s.velocity.z > 0.f);

    pm_move_t down = Race_PmoveQ2WaterDirectState(presets[preset]);
    down.water_level = WATER_UNDER;
    down.water_type = CONTENTS_WATER | CONTENTS_CURRENT_DOWN;
    down.cmd.up = -300;
    Pm_Q2WaterMoveForTest(&down, Vec3_Zero());
    ck_assert(down.s.velocity.z < 0.f);
  }
} END_TEST

START_TEST(_Race_PmoveQ2OrdinaryLiquidImpulse) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const int32_t liquids[] = {
    CONTENTS_WATER, CONTENTS_SLIME, CONTENTS_LAVA
  };
  static const float impulses[] = { 100.f, 80.f, 50.f };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    for (size_t liquid = 0; liquid < lengthof(liquids); liquid++) {
      pm_move_t game = Race_PmoveQ2WaterDirectState(presets[preset]);
      game.water_level = WATER_WAIST;
      game.water_type = liquids[liquid];
      game.cmd.up = 10;
      Race_PmoveSetGround(&game, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
      pm_move_t cgame = game;
      ck_assert(!Pm_Q2CheckJumpForTest(&game, Vec3_Zero()));
      ck_assert(!Pm_Q2CheckJumpForTest_CgameReference(
        &cgame, Vec3_Zero()));
      Race_PmoveAssertFloat("Q2 ordinary liquid impulse", "velocity.z",
                            game.s.velocity.z, impulses[liquid]);
      ck_assert(game.s.flags & PMF_JUMP_HELD);
      ck_assert(!(game.s.flags & (PMF_JUMPED | PMF_ON_GROUND)));
      ck_assert_ptr_null(game.ground.ent);
      ck_assert_msg(memcmp(&game, &cgame, sizeof(game)) == 0,
                    "preset %d liquid %zu GAME/CGAME impulse differs",
                    presets[preset], liquid);

      game.s.velocity.z = 17.f;
      game.cmd.up = 10;
      ck_assert(!Pm_Q2CheckJumpForTest(&game, Vec3_Zero()));
      Race_PmoveAssertFloat("Q2 held liquid no repeat", "velocity.z",
                            game.s.velocity.z, 17.f);
      game.cmd.up = 0;
      ck_assert(!Pm_Q2CheckJumpForTest(&game, Vec3_Zero()));
      ck_assert(!(game.s.flags & PMF_JUMP_HELD));
      game.cmd.up = 10;
      ck_assert(!Pm_Q2CheckJumpForTest(&game, Vec3_Zero()));
      Race_PmoveAssertFloat("Q2 liquid re-press", "velocity.z",
                            game.s.velocity.z, impulses[liquid]);
      ck_assert(game.s.flags & PMF_JUMP_HELD);
    }

    pm_move_t threshold = Race_PmoveQ2WaterDirectState(presets[preset]);
    threshold.water_level = WATER_WAIST;
    threshold.water_type = CONTENTS_WATER;
    threshold.s.flags = PMF_JUMP_HELD;
    threshold.cmd.up = 9;
    Race_PmoveSetGround(&threshold, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
    ck_assert(!Pm_Q2CheckJumpForTest(&threshold, Vec3_Zero()));
    ck_assert(!(threshold.s.flags & PMF_JUMP_HELD));
    ck_assert(threshold.s.flags & PMF_ON_GROUND);

    pm_move_t landed = threshold;
    landed.s.flags |= PMF_JUMP_HELD | PMF_TIME_LAND;
    landed.s.time = 1;
    ck_assert(!Pm_Q2CheckJumpForTest(&landed, Vec3_Zero()));
    ck_assert(landed.s.flags & PMF_JUMP_HELD);

    pm_move_t falling = Race_PmoveQ2WaterDirectState(presets[preset]);
    falling.water_level = WATER_UNDER;
    falling.water_type = CONTENTS_WATER;
    falling.s.velocity.z = -300.f;
    falling.cmd.up = 10;
    Race_PmoveSetGround(&falling, RACE_PMOVE_ENTITY_FLOOR, Vec3_Up());
    ck_assert(!Pm_Q2CheckJumpForTest(&falling, Vec3_Zero()));
    Race_PmoveAssertFloat("Q2 exact falling rejection", "velocity.z",
                          falling.s.velocity.z, -300.f);
    ck_assert(!(falling.s.flags & PMF_ON_GROUND));
    ck_assert(!(falling.s.flags & PMF_JUMP_HELD));

    falling = Race_PmoveQ2WaterDirectState(presets[preset]);
    falling.water_level = WATER_UNDER;
    falling.water_type = CONTENTS_WATER;
    falling.s.velocity.z = nextafterf(-300.f, INFINITY);
    falling.cmd.up = 10;
    ck_assert(!Pm_Q2CheckJumpForTest(&falling, Vec3_Zero()));
    Race_PmoveAssertFloat("Q2 falling boundary acceptance", "velocity.z",
                          falling.s.velocity.z, 100.f);
    ck_assert(falling.s.flags & PMF_JUMP_HELD);

    pm_move_t tagged = Race_PmoveQ2WaterDirectState(presets[preset]);
    tagged.water_level = WATER_WAIST;
    tagged.water_type = CONTENTS_WATER | CONTENTS_CURRENT_0;
    tagged.cmd.up = 10;
    ck_assert(!Pm_Q2CheckJumpForTest(&tagged, Vec3_Zero()));
    Race_PmoveAssertFloat("Q2 tagged liquid equality edge", "velocity.z",
                          tagged.s.velocity.z, 50.f);
  }
} END_TEST

START_TEST(_Race_PmoveQ2LedgeWaterJump) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };
  static const uint16_t durations[] = { 1, 7, 8, 15, 16, 25, 50 };

  race_pmove_q2_water_ledge_lower = Vec3(30.f, 0.f, 28.f);
  race_pmove_q2_water_ledge_upper = Vec3(30.f, 0.f, 44.f);

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const pm_params_t params = Race_PmoveQ2NamedParams(presets[preset]);
    pm_move_t game = Race_PmoveQ2WaterDirectState(presets[preset]);
    game.water_level = WATER_WAIST;
    game.PointContents = Race_PmoveQ2WaterLedgePointContents;
    game.s.flags = PMF_JUMP_HELD;
    game.cmd.up = 10;
    pm_move_t cgame = game;
    race_pmove_q2_water_ledge_lower_contents = CONTENTS_SOLID;
    race_pmove_q2_water_ledge_upper_contents = 0;
    race_pmove_q2_water_point_count = 0u;
    ck_assert(Pm_Q2CheckWaterJumpForTest(&game, Vec3_Zero()));
    ck_assert_uint_eq(race_pmove_q2_water_point_count, 2u);
    Race_PmoveAssertVec3("Q2 ledge water jump velocity", "velocity",
                         game.s.velocity,
                         Vec3(50.f, 0.f, params.speed_water_jump));
    ck_assert(game.s.flags & PMF_TIME_WATER_JUMP);
    ck_assert(game.s.flags & PMF_JUMP_HELD);
    ck_assert_uint_eq(game.s.time, 255u);

    race_pmove_q2_water_point_count = 0u;
    ck_assert(Pm_Q2CheckWaterJumpForTest_CgameReference(
      &cgame, Vec3_Zero()));
    ck_assert_msg(memcmp(&game, &cgame, sizeof(game)) == 0,
                  "preset %d ledge helper GAME/CGAME differs",
                  presets[preset]);

    pm_move_t blocked = Race_PmoveQ2WaterDirectState(presets[preset]);
    blocked.water_level = WATER_WAIST;
    blocked.PointContents = Race_PmoveQ2WaterLedgePointContents;
    race_pmove_q2_water_ledge_lower_contents = 0;
    race_pmove_q2_water_point_count = 0u;
    ck_assert(!Pm_Q2CheckWaterJumpForTest(&blocked, Vec3_Zero()));
    ck_assert_uint_eq(race_pmove_q2_water_point_count, 1u);

    static const int32_t upper_blockers[] = {
      CONTENTS_SOLID, CONTENTS_WATER, CONTENTS_CURRENT_0
    };
    for (size_t i = 0; i < lengthof(upper_blockers); i++) {
      blocked = Race_PmoveQ2WaterDirectState(presets[preset]);
      blocked.water_level = WATER_WAIST;
      blocked.PointContents = Race_PmoveQ2WaterLedgePointContents;
      race_pmove_q2_water_ledge_lower_contents = CONTENTS_SOLID;
      race_pmove_q2_water_ledge_upper_contents = upper_blockers[i];
      race_pmove_q2_water_point_count = 0u;
      ck_assert(!Pm_Q2CheckWaterJumpForTest(&blocked, Vec3_Zero()));
      ck_assert_uint_eq(race_pmove_q2_water_point_count, 2u);
    }

    blocked.s.time = 1;
    race_pmove_q2_water_point_count = 0u;
    ck_assert(!Pm_Q2CheckWaterJumpForTest(&blocked, Vec3_Zero()));
    ck_assert_uint_eq(race_pmove_q2_water_point_count, 0u);

    for (size_t i = 0; i < lengthof(durations); i++) {
      Race_PmoveUseQ2NamedTestPhysics(presets[preset], false);
      pm_move_t full = Race_PmoveQ2WaterDirectState(presets[preset]);
      full.PointContents = Race_PmoveQ2WaterCoursePointContents;
      full.BoxContents = Race_PmoveQ2WaterCourseBoxContents;
      full.cmd.msec = durations[i];
      Pm_Move(&full);
      ck_assert(full.s.flags & PMF_TIME_WATER_JUMP);
      ck_assert(!(full.s.flags & PMF_JUMP_HELD));
      ck_assert_uint_eq(full.s.time,
                        255u - Pm_Q2TimeForTest(durations[i]));
      Race_PmoveAssertVec3("Q2 full ledge XY", "velocity.xy",
                           Vec3(full.s.velocity.x, full.s.velocity.y, 0.f),
                           Vec3(50.f, 0.f, 0.f));
      Race_PmoveAssertFloat("Q2 timed ledge gravity", "velocity.z",
                            full.s.velocity.z,
                            params.speed_water_jump -
                              params.gravity * durations[i] * .001f);
    }
  }
} END_TEST

START_TEST(_Race_PmoveQ2WaterCoveredLadder) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    const pm_params_t params = Race_PmoveQ2NamedParams(presets[preset]);
    pm_move_t game = Race_PmoveQ2WaterDirectState(presets[preset]);
    game.s.flags = PMF_ON_LADDER;
    game.water_level = WATER_WAIST;
    game.water_type = CONTENTS_WATER;
    game.cmd.up = 300;
    pm_move_t cgame = game;
    Pm_Q2WaterMoveForTest(&game, Vec3_Zero());
    Pm_Q2WaterMoveForTest_CgameReference(&cgame, Vec3_Zero());
    Race_PmoveAssertFloat("Q2 deep ladder water acceleration", "velocity.z",
                          game.s.velocity.z,
                          params.accel_water * .016f *
                            params.speed_ladder * .5f);
    ck_assert_msg(memcmp(&game, &cgame, sizeof(game)) == 0,
                  "preset %d deep ladder GAME/CGAME differs",
                  presets[preset]);

    pm_move_t idle = Race_PmoveQ2WaterDirectState(presets[preset]);
    idle.s.flags = PMF_ON_LADDER;
    idle.s.velocity.z = 100.f;
    idle.water_level = WATER_UNDER;
    idle.water_type = CONTENTS_WATER;
    Pm_Q2FrictionForTest(&idle, Vec3_Zero());
    Pm_Q2WaterMoveForTest(&idle, Vec3_Zero());
    Race_PmoveAssertFloat("Q2 deep ladder no idle gravity", "velocity.z",
                          idle.s.velocity.z, 90.4f);

    race_pmove_ladder_liquid = true;
    race_pmove_ladder_liquid_surface = 40.f;
    race_pmove_current_contents = 0;
    pm_move_t attached = Race_PmoveSetup(RACE_PMOVE_LADDER);
    attached.s.params = params;
    attached.s.origin = Vec3(15.125f, 0.f, 24.f);
    attached.s.velocity = Vec3_Zero();
    attached.cmd = (pm_cmd_t) { .msec = 16, .up = 300 };
    race_pmove_world = RACE_PMOVE_WORLD_LADDER;
    Pm_Move(&attached);
    race_pmove_ladder_liquid = false;
    race_pmove_current_contents = CONTENTS_CURRENT_90;
    ck_assert_int_eq(attached.water_level, WATER_WAIST);
    ck_assert(attached.s.flags & PMF_ON_LADDER);
    ck_assert(attached.s.flags & PMF_JUMP_HELD);
    ck_assert(!(attached.s.flags & (PMF_JUMPED | PMF_TIME_WATER_JUMP)));
  }
} END_TEST

START_TEST(_Race_PmoveQ2WaterLongStreamGameCgameAi) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1
  };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    pm_move_t game = Race_PmoveQ2WaterDirectState(presets[preset]);
    Pm_SetQ2SnapEnabledForTest(true);
    Pm_SetQ2SnapEnabledForTest_CgameReference(true);
    game.PointContents = Race_PmoveQ2WaterPoolPointContents;
    game.BoxContents = Race_PmoveQ2WaterPoolBoxContents;
    game.s.origin = Vec3(-32.125f, -16.125f, 24.125f);
    pm_move_t cgame = game;
    race_pmove_q2_pool_surface = 96.f;
    race_pmove_q2_pool_contents = CONTENTS_WATER | CONTENTS_CURRENT_90;
    size_t held_impulses = 0u;

    for (size_t command = 0; command < 96u; command++) {
      pm_cmd_t cmd = {
        .msec = (uint16_t[]) { 8, 16, 25, 11 }[command % 4u],
        .forward = command < 64u ? 300 : -200,
        .right = command & 1u ? 120 : -120,
        .up = command == 0u || command == 24u ? 10 :
              command == 23u ? 0 : 10,
        .angles = Vec3(command > 48u ? 20.f : 0.f,
                       command > 72u ? 90.f : 0.f, 0.f)
      };
      game.cmd = cgame.cmd = cmd;
      const bool was_held = game.s.flags & PMF_JUMP_HELD;
      Pm_Move(&game);
      Pm_Move_CgameReference(&cgame);
      if (!was_held && cmd.up >= 10 &&
          (game.s.flags & PMF_JUMP_HELD)) {
        held_impulses++;
      }
      ck_assert_msg(memcmp(&game, &cgame, sizeof(game)) == 0,
                    "preset %d water stream command %zu GAME/CGAME differs",
                    presets[preset], command + 1u);
      Race_PmoveCrossAssertGrid("Q2 water stream GAME", &game.s, command);
      Race_PmoveCrossAssertGrid("Q2 water stream CGAME", &cgame.s, command);
    }
    ck_assert_uint_eq(held_impulses, 2u);
    ck_assert_int_eq(game.water_level, WATER_UNDER);
    ck_assert(game.water_type & CONTENTS_CURRENT_90);

    Race_PmoveUseQ2NamedTestPhysics(presets[preset], false);
    pm_move_t ai = Race_PmoveQ2WaterDirectState(presets[preset]);
    ai.PointContents = Race_PmoveQ2WaterPoolPointContents;
    ai.BoxContents = Race_PmoveQ2WaterPoolBoxContents;
    ai.cmd = (pm_cmd_t) { .msec = 100, .forward = 300, .up = 10 };
    race_pmove_q2_pool_surface = 40.f;
    race_pmove_q2_pool_contents = CONTENTS_WATER;
    const pm_params_t params = ai.s.params;
    Pm_Move(&ai);
    ck_assert(ai.s.flags & PMF_JUMP_HELD);
    ck_assert_msg(memcmp(&ai.s.params, &params, sizeof(params)) == 0,
                  "preset %d direct AI water changed named parameters",
                  presets[preset]);
  }
} END_TEST

static void Race_PmovePrintVec3(const vec3_t value) {
  printf("{ { %af, %af, %af } }", (double) value.x,
         (double) value.y, (double) value.z);
}

static void Race_PmoveDump(void) {
  for (race_pmove_fixture_id_t id = RACE_PMOVE_EMPTY_IDLE;
       id < RACE_PMOVE_FIXTURE_TOTAL; id++) {
    const pm_move_t pm = Race_PmoveRun(id, Pm_Move_CommonReference);
    printf("  [%d] = { /* %s */\n", id, race_pmove_fixture_names[id]);
    printf("    .type = %d,\n", pm.s.type);
    printf("    .origin = "); Race_PmovePrintVec3(pm.s.origin); printf(",\n");
    printf("    .velocity = "); Race_PmovePrintVec3(pm.s.velocity); printf(",\n");
    printf("    .flags = 0x%04x, .time = %u,\n", pm.s.flags, pm.s.time);
    printf("    .view_offset = "); Race_PmovePrintVec3(pm.s.view_offset); printf(",\n");
    printf("    .step_offset = %af,\n", (double) pm.s.step_offset);
    printf("    .view_angles = "); Race_PmovePrintVec3(pm.s.view_angles); printf(",\n");
    printf("    .angles = "); Race_PmovePrintVec3(pm.angles); printf(",\n");
    printf("    .bounds = { .mins = "); Race_PmovePrintVec3(pm.bounds.mins);
    printf(", .maxs = "); Race_PmovePrintVec3(pm.bounds.maxs); printf(" },\n");
    printf("    .ground_entity = %d, .ground_fraction = %af,\n",
           Race_PmoveEntityId(pm.ground.ent), (double) pm.ground.fraction);
    printf("    .ground_end = "); Race_PmovePrintVec3(pm.ground.end); printf(",\n");
    printf("    .ground_normal = "); Race_PmovePrintVec3(pm.ground.plane.normal); printf(",\n");
    printf("    .ground_contents = %d, .ground_surface = %d,\n",
           pm.ground.contents, pm.ground.surface);
    printf("    .water_type = %d, .water_level = %d, .step = %af,\n",
           pm.water_type, pm.water_level, (double) pm.step);
    printf("    .num_touched = %d, .touched = {", pm.num_touched);
    for (int32_t i = 0; i < pm.num_touched; i++) {
      printf("%s%d", i ? ", " : " ", Race_PmoveEntityId(pm.touched[i].ent));
    }
    printf(" }\n  },\n");
  }
}

int32_t main(int32_t argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--dump") == 0) {
    Race_PmoveDump();
    return 0;
  }

  Suite *suite = suite_create("check_race_pmove");
  TCase *tcase = tcase_create("parity");
  tcase_add_test(tcase, _Race_PmoveCommonParity);
  tcase_add_test(tcase, _Race_PmoveGoldenBaseline);
  tcase_add_test(tcase, _Race_PmoveReadsPhysicsIdentityOnce);
  tcase_add_test(tcase, _Race_PmoveTrainingObserverContract);
  tcase_add_test(tcase, _Race_PmoveQ2GroundJumpContract);
  tcase_add_test(tcase, _Race_PmoveQ2LandingThresholds);
  tcase_add_test(tcase, _Race_PmoveQ2LandingDurations);
  tcase_add_test(tcase, _Race_PmoveQ2NoAutohopStateMachine);
  tcase_add_test(tcase, _Race_PmoveQ2GameCgameCommandParity);
  tcase_add_test(tcase, _Race_PmoveQ2AiDirectAndDefaultWaterRegression);
  tcase_add_test(tcase, _Race_PmoveQ2AirLegacyCases);
  tcase_add_test(tcase, _Race_PmoveQ2AirTransitionAndCurrent);
  tcase_add_test(tcase, _Race_PmoveQ2AirLongStreamGameCgame);
  tcase_add_test(tcase, _Race_PmoveQ2AiDirectAir);
  tcase_add_test(tcase, _Race_PmoveQ2ClipVelocityLegacyCases);
  tcase_add_test(tcase, _Race_PmoveQ2CollisionLegacyCases);
  tcase_add_test(tcase, _Race_PmoveQ2CollisionLongStreamGameCgame);
  tcase_add_test(tcase, _Race_PmoveQ2AiDirectCollision);
  tcase_add_test(tcase, _Race_PmoveQ2StepLegacyCases);
  tcase_add_test(tcase, _Race_PmoveQ2StepLongStreamGameCgame);
  tcase_add_test(tcase, _Race_PmoveQ2AiDirectStep);
  tcase_add_test(tcase, _Race_PmoveQ2GroundLegacyCases);
  tcase_add_test(tcase, _Race_PmoveQ2TrickProbeAndJump);
  tcase_add_test(tcase, _Race_PmoveQ2FixGroundThresholds);
  tcase_add_test(tcase, _Race_PmoveQ2RampEntryClimbCrestDown);
  tcase_add_test(tcase, _Race_PmoveQ2RampNoAutohopGameCgame);
  tcase_add_test(tcase, _Race_PmoveQ2AiDirectRamp);
  tcase_add_test(tcase, _Race_PmoveQ2DownhillAndSteepRamp);
  tcase_add_test(tcase, _Race_PmoveQ2CommandTimeQuantization);
  tcase_add_test(tcase, _Race_PmoveQ2MovementTimerConsumers);
  tcase_add_test(tcase, _Race_PmoveQ2ContinuousToSnappedState);
  tcase_add_test(tcase, _Race_PmoveQ2SnapRounding);
  tcase_add_test(tcase, _Race_PmoveQ2InitialSnapCandidates);
  tcase_add_test(tcase, _Race_PmoveQ2FinalSnapCandidatesAndFallback);
  tcase_add_test(tcase, _Race_PmoveQ2SnapGameCgameAiNoAutohop);
  tcase_add_test(tcase, _Race_PmoveQ2SnapPresetAndWorldBoundaries);
  tcase_add_test(tcase, _Race_PmoveQ2NamedParameterContracts);
  tcase_add_test(tcase, _Race_PmoveQ2CrossDomainStreams);
  tcase_add_test(tcase, _Race_PmoveQ2FinalCombinedGameCgameAi);
  tcase_add_test(tcase, _Race_PmovePlayerBoundsContract);
  tcase_add_test(tcase, _Race_PmoveQ2DuckStateMachine);
  tcase_add_test(tcase, _Race_PmoveQ2StandTraceContract);
  tcase_add_test(tcase, _Race_PmoveQ2HullGeometryDiscriminators);
  tcase_add_test(tcase, _Race_PmoveQ2DuckGameCgameParity);
  tcase_add_test(tcase, _Race_PmoveQ2LadderProbeBoundary);
  tcase_add_test(tcase, _Race_PmoveQ2LadderProbeContract);
  tcase_add_test(tcase, _Race_PmoveQ2LadderWishContract);
  tcase_add_test(tcase,
                 _Race_PmoveQ2LadderFrictionAccelerationAndRelaxation);
  tcase_add_test(tcase, _Race_PmoveQ2LadderStepSlide);
  tcase_add_test(tcase, _Race_PmoveQ2LadderNoAutohopExitState);
  tcase_add_test(tcase, _Race_PmoveQ2LadderIndependentGameCgame);
  tcase_add_test(tcase, _Race_PmoveQ2LadderAiAndWaterBoundary);
  tcase_add_test(tcase, _Race_PmoveQ2WaterClassification);
  tcase_add_test(tcase, _Race_PmoveQ2WaterRawCurrents);
  tcase_add_test(tcase, _Race_PmoveQ2WaterFriction);
  tcase_add_test(tcase, _Race_PmoveQ2Swimming);
  tcase_add_test(tcase, _Race_PmoveQ2OrdinaryLiquidImpulse);
  tcase_add_test(tcase, _Race_PmoveQ2LedgeWaterJump);
  tcase_add_test(tcase, _Race_PmoveQ2WaterCoveredLadder);
  tcase_add_test(tcase, _Race_PmoveQ2WaterLongStreamGameCgameAi);
  Race_CgameBounds_AddTests(tcase);
  Race_PhysicsService_AddTests(tcase);
  suite_add_tcase(suite, tcase);

  SRunner *runner = srunner_create(suite);
  srunner_run_all(runner, CK_VERBOSE);
  const int32_t failed = srunner_ntests_failed(runner);
  srunner_free(runner);
  return failed;
}
