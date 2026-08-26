/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <check.h>
#include <string.h>

#include "cgame/common/cg_local.h"
#include "game/common/bg_pmove.h"
#include "race_physics.h"

box3_t Pm_PlayerBounds(bool ducked);

/* Unused cg_entity.c class-table dependencies for this isolated consumer. */
const cg_entity_class_t cg_misc_dust = { 0 };
const cg_entity_class_t cg_misc_flame = { 0 };
const cg_entity_class_t cg_misc_model = { 0 };
const cg_entity_class_t cg_misc_sound = { 0 };
const cg_entity_class_t cg_misc_sparks = { 0 };
const cg_entity_class_t cg_misc_sprite = { 0 };
const cg_entity_class_t cg_misc_steam = { 0 };
const cg_entity_class_t cg_misc_weather = { 0 };

static void Race_CgameBoundsUsePreset(
    const race_physics_preset_id_t preset) {
  if (preset == RACE_PHYSICS_PRESET_INVALID) {
    const race_physics_config_t common = {
      .version = RACE_PHYSICS_CONFIG_VERSION,
      .family = RACE_PHYSICS_FAMILY_QUETOO,
      .preset = RACE_PHYSICS_PRESET_QUETOO_COMMON_V1,
      .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_OFF
    };
    Race_Physics_SetProvider(NULL);
    ck_assert(Race_Physics_SetActive(&common));
    return;
  }

  const race_physics_config_t config = {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = RACE_PHYSICS_FAMILY_Q2,
    .preset = preset,
    .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_NEAREST
  };
  ck_assert(Race_Physics_SetActive(&config));
}

START_TEST(_Race_CgameDuckingUsesEffectiveStandingHull) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_INVALID,
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1,
    RACE_PHYSICS_PRESET_DP2_V1
  };

  for (size_t i = 0; i < lengthof(presets); i++) {
    Race_CgameBoundsUsePreset(presets[i]);

    cl_entity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.current.bounds = Pm_PlayerBounds(false);
    ck_assert(!Cg_IsDucking(&ent));

    ent.current.bounds = Pm_PlayerBounds(true);
    ck_assert(Cg_IsDucking(&ent));
  }
} END_TEST

void Race_CgameBounds_AddTests(TCase *tcase) {
  tcase_add_test(tcase, _Race_CgameDuckingUsesEffectiveStandingHull);
}
