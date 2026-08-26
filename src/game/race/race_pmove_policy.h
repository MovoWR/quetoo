/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "race_physics.h"

typedef enum {
  RACE_PM_STEP_INVALID = 0,
  RACE_PM_STEP_Q2,
  RACE_PM_STEP_DP2
} race_pm_step_variant_t;

typedef enum {
  RACE_PM_GROUND_INVALID = 0,
  RACE_PM_GROUND_Q2,
  RACE_PM_GROUND_DP2
} race_pm_ground_variant_t;

typedef enum {
  RACE_PM_JUMP_INVALID = 0,
  RACE_PM_JUMP_STANDARD,
  RACE_PM_JUMP_DP2
} race_pm_jump_variant_t;

/**
 * @brief Immutable locomotion differences used by the shared movement kernel.
 * @details The semantic descriptor owns family and external identity. This
 * record contains only differences already present in `bg_pmove.c`.
 */
typedef struct {
  race_pm_policy_id_t id;
  float standing_max_z;
  float ducked_max_z;
  float ramp_ground_loss_speed;
  bool ramp_ground_loss_requires_jump_held;
  bool ramp_contact_slide;
  float ramp_slide_accel;
  float ramp_slide_direction_min;
  bool trick_probe;
  float trick_jump_speed;
  race_pm_jump_variant_t jump_variant;
  float jump_impulse;
  float jump_speed_max;
  race_pm_ground_variant_t ground_variant;
  float bump_up_speed_max;
  bool ladder_retains_ground;
  race_pm_step_variant_t step_variant;
  float step_down_extra;
  float step_inset;
} race_pmove_policy_t;

const race_pmove_policy_t *Race_PmovePolicy(race_pm_policy_id_t id);
bool Race_PmovePolicyComplete(const race_physics_preset_descriptor_t *preset);
