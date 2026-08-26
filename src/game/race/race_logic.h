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
#include <stdint.h>

#include "race_types.h"

typedef struct {
  uint64_t checkpoint_mask;
  uint16_t checkpoint_count;
  uint64_t split_mask;
  uint16_t split_count;
  uint64_t stage_mask;
  uint64_t split_layout;
  uint16_t stage_count;
  uint16_t start_count;
  uint16_t finish_count;
  uint16_t barrier_count;
  bool malformed;
  bool splits_malformed;
  bool splits_valid;
  bool stages_malformed;
  bool stages_valid;
  bool barriers_malformed;
  bool valid;
} race_course_t;

typedef struct {
  race_run_state_t state;
  race_mode_t mode;
  uint16_t checkpoint_count;
  uint16_t split_count;
  uint16_t stage;
  uint32_t start_time;
  uint32_t end_time;
  uint32_t elapsed_time;
  uint32_t checkpoint_times[RACE_MAX_CHECKPOINTS];
  uint32_t split_times[RACE_MAX_CHECKPOINTS];
  uint32_t stage_times[RACE_MAX_CHECKPOINTS];
  float start_speed;
  float end_speed;
  float current_speed;
  float top_speed;
  float speed_accum;
  uint32_t speed_samples;
  race_invalid_flags_t invalid_flags;
} race_run_t;

/**
 * @brief A single authoritative Practice respawn transform.
 * @details Common respawn owns velocity, movement flags and view snapping; the
 * stored contract intentionally preserves only the legacy origin and angles.
 */
typedef struct {
  vec3_t origin;
  vec3_t angles;
  bool set;
} race_stored_spawn_t;

void Race_Course_Reset(race_course_t *course);
bool Race_Course_AddCheckpoint(race_course_t *course, int32_t checkpoint);
bool Race_Course_AddSplit(race_course_t *course, int32_t split);
bool Race_Course_AddStage(race_course_t *course, int32_t stage);
void Race_Course_AddStart(race_course_t *course);
void Race_Course_AddFinish(race_course_t *course);
void Race_Course_InvalidateSplits(race_course_t *course);
void Race_Course_InvalidateStages(race_course_t *course);
void Race_Course_InvalidateBarrier(race_course_t *course);
bool Race_Course_Validate(race_course_t *course);

void Race_Run_Reset(race_run_t *run);
bool Race_Run_Start(race_run_t *run, bool course_valid, uint32_t time);
bool Race_Run_Checkpoint(race_run_t *run, uint16_t total, uint16_t checkpoint, uint32_t time);
bool Race_Run_Split(race_run_t *run, uint16_t total, uint16_t split, uint32_t time);
bool Race_Run_Stage(race_run_t *run, uint16_t total, uint16_t stage, uint32_t time);
bool Race_Run_Finish(race_run_t *run, uint16_t total, uint32_t time);
uint32_t Race_Run_Elapsed(const race_run_t *run, uint32_t time);
void Race_Run_ObserveSpeed(race_run_t *run, float speed);
float Race_Run_AverageSpeed(const race_run_t *run);
void Race_Run_MarkInvalid(race_run_t *run, race_invalid_flags_t flag);
bool Race_Run_IsValid(const race_run_t *run);
bool Race_Run_ShouldAutoStart(race_mode_t mode, const race_run_t *run, bool course_valid,
                              bool client_eligible, int16_t forward, int16_t right,
                              int16_t up);

bool Race_Mode_Transition(race_mode_t *mode, race_run_t *run, race_mode_t next);
bool Race_Mode_AllowsHook(race_mode_t mode);

bool Race_DamagePolicy(bool target_client, bool attacker_client,
                       bool same_entity, int32_t *damage,
                       int32_t *knockback);

void Race_StoredSpawn_Clear(race_stored_spawn_t *spawn);
bool Race_StoredSpawn_Capture(race_stored_spawn_t *spawn, race_mode_t mode,
                              bool spectator, bool alive, vec3_t origin, vec3_t angles);
bool Race_StoredSpawn_Get(const race_stored_spawn_t *spawn, race_mode_t mode,
                          bool spectator, vec3_t *origin, vec3_t *angles);

bool Race_Trigger_Debounced(uint32_t *last_touch, uint32_t time, uint32_t wait);
bool Race_StartMode_Parse(const char *value, race_start_mode_t *mode);
bool Race_StartJumpEdge(int16_t up, int16_t previous_up);
bool Race_StartExitTransition(race_start_mode_t mode, bool still_inside);
