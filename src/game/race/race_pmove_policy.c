/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_pmove_policy.h"

static const race_pmove_policy_t race_pmove_policies[] = {
  {
    .id = RACE_PM_POLICY_QUETOO_COMMON_V1,
    .standing_max_z = 36.f,
    .ducked_max_z = 6.f
  },
  {
    .id = RACE_PM_POLICY_Q2_V1,
    .standing_max_z = 32.f,
    .ducked_max_z = 4.f,
    .ramp_ground_loss_speed = 180.f,
    .jump_variant = RACE_PM_JUMP_STANDARD,
    .ground_variant = RACE_PM_GROUND_Q2,
    .step_variant = RACE_PM_STEP_Q2
  },
  {
    .id = RACE_PM_POLICY_QUETOO_FIX_V1,
    .standing_max_z = 32.f,
    .ducked_max_z = 6.f,
    .ramp_ground_loss_speed = 80.f,
    .trick_probe = true,
    .trick_jump_speed = 40.f,
    .jump_variant = RACE_PM_JUMP_STANDARD,
    .ground_variant = RACE_PM_GROUND_Q2,
    .step_variant = RACE_PM_STEP_Q2
  },
  {
    .id = RACE_PM_POLICY_DP2_V1,
    .standing_max_z = 32.f,
    .ducked_max_z = 4.f,
    .ramp_ground_loss_speed = 180.f,
    .ramp_ground_loss_requires_jump_held = true,
    .ramp_contact_slide = true,
    .ramp_slide_accel = 1.f,
    .ramp_slide_direction_min = .01f,
    .jump_variant = RACE_PM_JUMP_DP2,
    .jump_impulse = 270.f,
    .jump_speed_max = 450.f,
    .ground_variant = RACE_PM_GROUND_DP2,
    .bump_up_speed_max = 150.f,
    .ladder_retains_ground = true,
    .step_variant = RACE_PM_STEP_DP2,
    .step_down_extra = 1.f,
    .step_inset = 1.f
  }
};

const race_pmove_policy_t *Race_PmovePolicy(const race_pm_policy_id_t id) {
  for (size_t i = 0; i < lengthof(race_pmove_policies); i++) {
    if (race_pmove_policies[i].id == id) {
      return race_pmove_policies + i;
    }
  }

  return NULL;
}

bool Race_PmovePolicyComplete(
    const race_physics_preset_descriptor_t *preset) {
  if (!preset) {
    return false;
  }

  const race_pmove_policy_t *policy = Race_PmovePolicy(preset->pm_policy);
  if (!policy || policy->standing_max_z <= policy->ducked_max_z) {
    return false;
  }

  if (preset->family != RACE_PHYSICS_FAMILY_Q2) {
    return policy->jump_variant == RACE_PM_JUMP_INVALID &&
           policy->ground_variant == RACE_PM_GROUND_INVALID &&
           policy->step_variant == RACE_PM_STEP_INVALID;
  }

  if (policy->jump_variant == RACE_PM_JUMP_INVALID ||
      policy->ground_variant == RACE_PM_GROUND_INVALID ||
      policy->step_variant == RACE_PM_STEP_INVALID ||
      policy->ramp_ground_loss_speed <= 0.f) {
    return false;
  }

  if (policy->jump_variant == RACE_PM_JUMP_DP2 &&
      (policy->jump_impulse <= 0.f ||
       policy->jump_speed_max < policy->jump_impulse)) {
    return false;
  }

  return !policy->ramp_contact_slide ||
         (policy->ramp_slide_accel > 0.f &&
          policy->ramp_slide_direction_min > 0.f);
}
