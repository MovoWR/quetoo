/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <limits.h>
#include <math.h>
#include <string.h>

#include "race_logic.h"

void Race_Course_Reset(race_course_t *course) {
  memset(course, 0, sizeof(*course));
}

static bool Race_Course_AddSequence(uint64_t *mask, bool *malformed,
                                    const int32_t value, const int32_t minimum) {
  if (value < minimum || value > RACE_MAX_CHECKPOINTS) {
    *malformed = true;
    return false;
  }

  *mask |= UINT64_C(1) << (value - 1);
  return true;
}

static bool Race_Course_ValidateSequence(const uint64_t mask,
                                         const bool malformed,
                                         const int32_t minimum,
                                         uint16_t *count) {
  uint16_t highest = minimum == 2 ? 1u : 0u;
  for (int32_t value = RACE_MAX_CHECKPOINTS; value >= minimum; value--) {
    if (mask & (UINT64_C(1) << (value - 1))) {
      highest = (uint16_t) value;
      break;
    }
  }

  uint64_t expected = highest == RACE_MAX_CHECKPOINTS
    ? UINT64_MAX
    : highest == 0
      ? UINT64_C(0)
      : (UINT64_C(1) << highest) - 1u;
  if (minimum == 2) {
    expected &= ~UINT64_C(1);
  }

  *count = highest;
  return !malformed && mask == expected;
}

bool Race_Course_AddCheckpoint(race_course_t *course, int32_t checkpoint) {
  if (!Race_Course_AddSequence(&course->checkpoint_mask, &course->malformed,
                               checkpoint, 1)) {
    course->valid = false;
    return false;
  }
  course->valid = false;
  return true;
}

bool Race_Course_AddSplit(race_course_t *course, const int32_t split) {
  course->splits_valid = false;
  return Race_Course_AddSequence(&course->split_mask,
                                 &course->splits_malformed, split, 1);
}

bool Race_Course_AddStage(race_course_t *course, const int32_t stage) {
  course->stages_valid = false;
  return Race_Course_AddSequence(&course->stage_mask,
                                 &course->stages_malformed, stage, 2);
}

void Race_Course_AddStart(race_course_t *course) {
  if (course->start_count < UINT16_MAX) {
    course->start_count++;
  }
}

void Race_Course_AddFinish(race_course_t *course) {
  if (course->finish_count < UINT16_MAX) {
    course->finish_count++;
  }
}

void Race_Course_InvalidateSplits(race_course_t *course) {
  course->splits_malformed = true;
  course->splits_valid = false;
}

void Race_Course_InvalidateStages(race_course_t *course) {
  course->stages_malformed = true;
  course->stages_valid = false;
}

void Race_Course_InvalidateBarrier(race_course_t *course) {
  course->barriers_malformed = true;
  course->valid = false;
}

bool Race_Course_Validate(race_course_t *course) {
  const bool checkpoints_valid = Race_Course_ValidateSequence(
    course->checkpoint_mask, course->malformed, 1, &course->checkpoint_count);
  course->splits_valid = Race_Course_ValidateSequence(
    course->split_mask, course->splits_malformed, 1, &course->split_count);
  course->stages_valid = Race_Course_ValidateSequence(
    course->stage_mask, course->stages_malformed, 2, &course->stage_count);
  course->valid = checkpoints_valid && course->finish_count > 0u &&
                  !course->barriers_malformed;
  return course->valid;
}

void Race_Run_Reset(race_run_t *run) {
  memset(run, 0, sizeof(*run));
  run->state = RACE_RUN_IDLE;
}

bool Race_Run_Start(race_run_t *run, bool course_valid, uint32_t time) {

  if (!course_valid) {
    return false;
  }

  Race_Run_Reset(run);
  run->state = RACE_RUN_ACTIVE;
  run->stage = 1u;
  run->start_time = time;
  return true;
}

bool Race_Run_Split(race_run_t *run, const uint16_t total,
                    const uint16_t split, const uint32_t time) {
  if (run->state != RACE_RUN_ACTIVE || split > total ||
      split != run->split_count + 1u) {
    return false;
  }

  run->split_times[run->split_count] = time - run->start_time;
  run->split_count++;
  return true;
}

bool Race_Run_Stage(race_run_t *run, const uint16_t total,
                    const uint16_t stage, const uint32_t time) {
  if (run->state != RACE_RUN_ACTIVE || stage > total ||
      stage != run->stage + 1u) {
    return false;
  }

  run->stage_times[stage - 2u] = time - run->start_time;
  run->stage = stage;
  return true;
}

bool Race_Run_Checkpoint(race_run_t *run, uint16_t total, uint16_t checkpoint, uint32_t time) {

  if (run->state != RACE_RUN_ACTIVE || checkpoint > total ||
      checkpoint != run->checkpoint_count + 1) {
    return false;
  }

  run->checkpoint_times[run->checkpoint_count] = time - run->start_time;
  run->checkpoint_count++;
  return true;
}

bool Race_Run_Finish(race_run_t *run, uint16_t total, uint32_t time) {

  if (run->state != RACE_RUN_ACTIVE || run->checkpoint_count != total) {
    return false;
  }

  run->state = RACE_RUN_FINISHED;
  run->end_time = time;
  run->elapsed_time = time - run->start_time;
  return true;
}

uint32_t Race_Run_Elapsed(const race_run_t *run, uint32_t time) {
  switch (run->state) {
    case RACE_RUN_ACTIVE:
      return time - run->start_time;
    case RACE_RUN_FINISHED:
      return run->elapsed_time;
    default:
      return 0;
  }
}

void Race_Run_ObserveSpeed(race_run_t *run, float speed) {

  if (!run || run->state != RACE_RUN_ACTIVE || !isfinite(speed) || speed < 0.f) {
    return;
  }

  run->current_speed = speed;
  run->top_speed = Maxf(run->top_speed, speed);
  run->speed_accum += speed;
  run->speed_samples++;
}

float Race_Run_AverageSpeed(const race_run_t *run) {
  return run && run->speed_samples
    ? run->speed_accum / (float) run->speed_samples
    : 0.f;
}

void Race_Run_MarkInvalid(race_run_t *run, race_invalid_flags_t flag) {

  if (run) {
    run->invalid_flags |= flag;
  }
}

bool Race_Run_IsValid(const race_run_t *run) {
  return run && run->invalid_flags == RACE_INVALID_NONE;
}

bool Race_Run_ShouldAutoStart(race_mode_t mode, const race_run_t *run, bool course_valid,
                              bool client_eligible, int16_t forward, int16_t right,
                              int16_t up) {
  return (mode == RACE_MODE_RACE || mode == RACE_MODE_PRACTICE) &&
         run->state == RACE_RUN_IDLE && course_valid && client_eligible &&
         (forward != 0 || right != 0 || up != 0);
}

bool Race_Mode_Transition(race_mode_t *mode, race_run_t *run, race_mode_t next) {

  if (next < RACE_MODE_RACE || next >= RACE_MODE_TOTAL || *mode == next) {
    return false;
  }

  Race_Run_Reset(run);
  *mode = next;
  return true;
}

bool Race_Mode_AllowsHook(race_mode_t mode) {
  return mode == RACE_MODE_PRACTICE;
}

bool Race_DamagePolicy(bool target_client, bool attacker_client,
                       bool same_entity, int32_t *damage,
                       int32_t *knockback) {

  if (!damage || !knockback || !target_client) {
    return true;
  }

  if (attacker_client && !same_entity) {
    *damage = 0;
    *knockback = 0;
    return false;
  }

  *damage = 0;
  return true;
}

void Race_StoredSpawn_Clear(race_stored_spawn_t *spawn) {
  memset(spawn, 0, sizeof(*spawn));
}

bool Race_StoredSpawn_Capture(race_stored_spawn_t *spawn, race_mode_t mode,
                              bool spectator, bool alive, vec3_t origin, vec3_t angles) {

  if (!spawn || mode != RACE_MODE_PRACTICE || spectator || !alive) {
    return false;
  }

  spawn->origin = origin;
  spawn->angles = angles;
  spawn->set = true;
  return true;
}

bool Race_StoredSpawn_Get(const race_stored_spawn_t *spawn, race_mode_t mode,
                          bool spectator, vec3_t *origin, vec3_t *angles) {

  if (!spawn || !origin || !angles || !spawn->set ||
      mode != RACE_MODE_PRACTICE || spectator) {
    return false;
  }

  *origin = spawn->origin;
  *angles = spawn->angles;
  return true;
}

bool Race_Trigger_Debounced(uint32_t *last_touch, uint32_t time, uint32_t wait) {

  if (*last_touch != UINT32_MAX && time - *last_touch < wait) {
    return true;
  }

  *last_touch = time;
  return false;
}

bool Race_StartMode_Parse(const char *value, race_start_mode_t *mode) {
  if (!mode) {
    return false;
  }
  if (!value || !*value || !strcmp(value, "touch")) {
    *mode = RACE_START_TOUCH;
  } else if (!strcmp(value, "exit")) {
    *mode = RACE_START_EXIT;
  } else if (!strcmp(value, "jump")) {
    *mode = RACE_START_JUMP;
  } else {
    return false;
  }
  return true;
}

bool Race_StartJumpEdge(const int16_t up, const int16_t previous_up) {
  return up > 0 && previous_up <= 0;
}

bool Race_StartExitTransition(const race_start_mode_t mode,
                              const bool still_inside) {
  return mode == RACE_START_EXIT && !still_inside;
}
