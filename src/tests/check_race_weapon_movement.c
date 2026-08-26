/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifdef _WIN32
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define START_TEST(name) static void name(void)
#define END_TEST
static int32_t race_native_failures;
static void Race_NativeAssert(const bool condition, const char *expression,
                              const char *file, const int32_t line) {
  if (!condition) {
    fprintf(stderr, "%s:%" PRId32 ": assertion failed: %s\n",
            file, line, expression);
    race_native_failures++;
  }
}
#define RACE_NATIVE_ASSERT(expression) \
  Race_NativeAssert((expression), #expression, __FILE__, __LINE__)
#define ck_assert(expression) RACE_NATIVE_ASSERT(!!(expression))
#define ck_assert_float_eq(actual, expected) \
  RACE_NATIVE_ASSERT((actual) == (expected))
#define ck_assert_int_eq(actual, expected) \
  RACE_NATIVE_ASSERT((int64_t) (actual) == (int64_t) (expected))
#define ck_assert_ptr_eq(actual, expected) \
  RACE_NATIVE_ASSERT((const void *) (actual) == (const void *) (expected))
#define ck_assert_ptr_nonnull(actual) \
  RACE_NATIVE_ASSERT((const void *) (actual) != NULL)
#define ck_assert_ptr_null(actual) \
  RACE_NATIVE_ASSERT((const void *) (actual) == NULL)
#define ck_assert_uint_eq(actual, expected) \
  RACE_NATIVE_ASSERT((uint64_t) (actual) == (uint64_t) (expected))
#else
#include <check.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "g_local.h"
#include "race_physics.h"
#include "race_weapon_movement.h"
#include "race_weapon_tuning_service.h"

typedef enum {
  FIRE_TEST_NO_SHOT,
  FIRE_TEST_VALID,
  FIRE_TEST_WRONG_THEN_VALID,
  FIRE_TEST_DOUBLE
} fire_test_mode_t;

typedef struct {
  g_item_tag_t tag;
  race_weapon_fire_kind_t kind;
  race_weapon_tuning_id_t refire_id;
  bool tunable;
  void (*callback)(g_client_t *cl);
} fire_test_path_t;

g_level_t g_level;

static fire_test_mode_t fire_test_mode;
static race_weapon_fire_kind_t fire_test_kind;
static g_entity_t fire_test_projectile;
static g_entity_t fire_test_second_projectile;
static g_entity_t fire_test_wrong_entity;
static uint32_t fire_test_callback_count;
static uint32_t tuning_test_uint_count;
static race_weapon_tuning_id_t tuning_test_uint_id;
static uint32_t tuning_test_uint_legacy;
static bool tuning_test_uint_override;
static uint32_t tuning_test_uint_value;
static race_physics_preset_id_t tuning_test_uint_preset;
static uint32_t tuning_test_stamp_count;
static g_entity_t *tuning_test_stamped_projectile;

uint32_t Race_WeaponTuningService_UintForPreset(
    const race_physics_preset_id_t preset,
    const race_weapon_tuning_id_t id, const uint32_t legacy_value) {
  tuning_test_uint_count++;
  tuning_test_uint_preset = preset;
  tuning_test_uint_id = id;
  tuning_test_uint_legacy = legacy_value;
  return tuning_test_uint_override ? tuning_test_uint_value : legacy_value;
}

void Race_WeaponTuningService_StampProjectile(g_entity_t *projectile) {
  tuning_test_stamp_count++;
  tuning_test_stamped_projectile = projectile;
}

static void FireTest_Common(g_client_t *cl,
                            const race_weapon_fire_kind_t kind) {
  fire_test_callback_count++;
  fire_test_kind = kind;

  if (fire_test_mode == FIRE_TEST_NO_SHOT) {
    return;
  }

  const bool standard = kind == RACE_WEAPON_FIRE_GRENADE ||
                        kind == RACE_WEAPON_FIRE_ROCKET;
  if (kind == RACE_WEAPON_FIRE_HAND_GRENADE) {
    cl->held_grenade = &fire_test_projectile;
  }

  if (fire_test_mode == FIRE_TEST_WRONG_THEN_VALID) {
    ck_assert(!Race_WeaponMovement_StampProjectile(
      kind, &fire_test_wrong_entity,
      standard ? &fire_test_wrong_entity : NULL,
      &fire_test_second_projectile));
  }

  ck_assert(Race_WeaponMovement_StampProjectile(
    kind, cl->entity, standard ? cl->entity : NULL,
    &fire_test_projectile));

  if (fire_test_mode == FIRE_TEST_DOUBLE) {
    ck_assert(!Race_WeaponMovement_StampProjectile(
      kind, cl->entity, standard ? cl->entity : NULL,
      &fire_test_second_projectile));
  }

  // Emulate common accepted-shot bookkeeping. The legacy profile must not
  // replace either timestamp after the callback returns.
  cl->weapon_fire_time += 125u;
  cl->weapon_fired_time += 25u;
  if (kind == RACE_WEAPON_FIRE_HAND_GRENADE) {
    cl->held_grenade = NULL;
  }
}

void G_FireHandGrenade(g_client_t *cl) {
  FireTest_Common(cl, RACE_WEAPON_FIRE_HAND_GRENADE);
}

void G_FireGrenadeLauncher(g_client_t *cl) {
  FireTest_Common(cl, RACE_WEAPON_FIRE_GRENADE);
}

void G_FireQuakeGrenadeLauncher(g_client_t *cl) {
  FireTest_Common(cl, RACE_WEAPON_FIRE_QUAKE_GRENADE);
}

void G_FireRocketLauncher(g_client_t *cl) {
  FireTest_Common(cl, RACE_WEAPON_FIRE_ROCKET);
}

void G_FireQuakeRocketLauncher(g_client_t *cl) {
  FireTest_Common(cl, RACE_WEAPON_FIRE_QUAKE_ROCKET);
}

void G_FireHyperblaster(g_client_t *cl) {
  FireTest_Common(cl, RACE_WEAPON_FIRE_HYPERBLASTER);
}

static const fire_test_path_t fire_test_paths[] = {
  {
    WEAPON_HAND_GRENADE,
    RACE_WEAPON_FIRE_HAND_GRENADE,
    RACE_WEAPON_TUNING_HAND_GRENADE_REFIRE_MS,
    true,
    G_FireHandGrenade
  },
  {
    WEAPON_GRENADE_LAUNCHER,
    RACE_WEAPON_FIRE_GRENADE,
    RACE_WEAPON_TUNING_STANDARD_GRENADE_REFIRE_MS,
    true,
    G_FireGrenadeLauncher
  },
  {
    WEAPON_QUAKE_GRENADE_LAUNCHER,
    RACE_WEAPON_FIRE_QUAKE_GRENADE,
    RACE_WEAPON_TUNING_VALUE_TOTAL,
    false,
    G_FireQuakeGrenadeLauncher
  },
  {
    WEAPON_ROCKET_LAUNCHER,
    RACE_WEAPON_FIRE_ROCKET,
    RACE_WEAPON_TUNING_STANDARD_ROCKET_REFIRE_MS,
    true,
    G_FireRocketLauncher
  },
  {
    WEAPON_QUAKE_ROCKET_LAUNCHER,
    RACE_WEAPON_FIRE_QUAKE_ROCKET,
    RACE_WEAPON_TUNING_VALUE_TOTAL,
    false,
    G_FireQuakeRocketLauncher
  },
  {
    WEAPON_HYPERBLASTER,
    RACE_WEAPON_FIRE_HYPERBLASTER,
    RACE_WEAPON_TUNING_HYPER_REFIRE_MS,
    true,
    G_FireHyperblaster
  }
};

static void FireTest_Reset(void) {
  memset(&fire_test_projectile, 0, sizeof(fire_test_projectile));
  memset(&fire_test_second_projectile, 0,
         sizeof(fire_test_second_projectile));
  memset(&fire_test_wrong_entity, 0, sizeof(fire_test_wrong_entity));
  fire_test_kind = RACE_WEAPON_FIRE_INVALID;
  fire_test_callback_count = 0u;
  g_level.time = 0u;
  tuning_test_uint_count = 0u;
  tuning_test_uint_id = RACE_WEAPON_TUNING_VALUE_TOTAL;
  tuning_test_uint_legacy = 0u;
  tuning_test_uint_override = false;
  tuning_test_uint_value = 0u;
  tuning_test_uint_preset = RACE_PHYSICS_PRESET_INVALID;
  tuning_test_stamp_count = 0u;
  tuning_test_stamped_projectile = NULL;
}

START_TEST(_Race_WeaponProfiles) {
  ck_assert_ptr_null(Race_WeaponMovement_Profile(
    RACE_WEAPON_PROFILE_INVALID));
  ck_assert(!Race_WeaponMovement_ProfileComplete(
    RACE_WEAPON_PROFILE_INVALID));

  const race_weapon_movement_profile_t *profile =
    Race_WeaponMovement_Profile(RACE_WEAPON_PROFILE_LEGACY_LIVE_CVARS_V1);
  ck_assert_ptr_nonnull(profile);
  ck_assert_int_eq(profile->id,
                   RACE_WEAPON_PROFILE_LEGACY_LIVE_CVARS_V1);
  ck_assert(profile->live_cvars);
  ck_assert(Race_WeaponMovement_ProfileComplete(profile->id));

  size_t count;
  const race_physics_preset_descriptor_t *presets =
    Race_Physics_Presets(&count);
  ck_assert_uint_eq(count, 4u);
  for (size_t i = 0u; i < count; i++) {
    ck_assert_int_eq(presets[i].weapon_profile, profile->id);
    ck_assert_float_eq(presets[i].hyperblaster_climb_range, 32.f);
  }
}
END_TEST

START_TEST(_Race_WeaponItemWrapping) {
  for (size_t i = 0u; i < lengthof(fire_test_paths); i++) {
    g_item_t item = {
      .def.tag = fire_test_paths[i].tag,
      .Think = fire_test_paths[i].callback
    };
    Race_WeaponMovement_InitItem(&item);
    ck_assert_ptr_eq(item.Think, Race_FirePresetWeapon);
  }

  g_item_t unrelated = { .def.tag = WEAPON_SHOTGUN };
  Race_WeaponMovement_InitItem(&unrelated);
  ck_assert_ptr_null(unrelated.Think);
  Race_WeaponMovement_InitItem(NULL);
}
END_TEST

START_TEST(_Race_WeaponScopedStamping) {
  size_t presetCount;
  const race_physics_preset_descriptor_t *presets =
    Race_Physics_Presets(&presetCount);

  for (size_t presetIndex = 0u; presetIndex < presetCount; presetIndex++) {
    const race_physics_config_t config = {
      .version = RACE_PHYSICS_CONFIG_VERSION,
      .family = presets[presetIndex].family,
      .preset = presets[presetIndex].id,
      .q2_snap_mode = presets[presetIndex].family == RACE_PHYSICS_FAMILY_Q2
        ? RACE_PHYSICS_Q2_SNAP_NEAREST
        : RACE_PHYSICS_Q2_SNAP_OFF
    };
    ck_assert(Race_Physics_SetActive(&config));

    for (size_t pathIndex = 0u;
         pathIndex < lengthof(fire_test_paths); pathIndex++) {
      for (fire_test_mode = FIRE_TEST_VALID;
           fire_test_mode <= FIRE_TEST_DOUBLE; fire_test_mode++) {
        FireTest_Reset();

        g_entity_t player = { 0 };
        g_client_t client = {
          .entity = &player,
          .weapon_fire_time = 1000u,
          .weapon_fired_time = 500u
        };
        g_item_t item = {
          .def.tag = fire_test_paths[pathIndex].tag,
          .Think = fire_test_paths[pathIndex].callback
        };
        client.weapon = &item;

        Race_WeaponMovement_InitItem(&item);
        item.Think(&client);

        ck_assert_uint_eq(fire_test_callback_count, 1u);
        ck_assert_int_eq(fire_test_kind, fire_test_paths[pathIndex].kind);
        ck_assert_uint_eq(client.weapon_fire_time, 1125u);
        ck_assert_uint_eq(client.weapon_fired_time, 525u);
        const bool tunable = fire_test_paths[pathIndex].tunable;
        ck_assert_uint_eq(tuning_test_uint_count, tunable ? 1u : 0u);
        ck_assert_int_eq(tuning_test_uint_id,
                         fire_test_paths[pathIndex].refire_id);
        ck_assert_uint_eq(tuning_test_uint_legacy,
                          tunable ? 1125u : 0u);
        ck_assert_uint_eq(tuning_test_stamp_count, tunable ? 1u : 0u);
        if (tunable) {
          ck_assert_ptr_eq(tuning_test_stamped_projectile,
                           &fire_test_projectile);
        } else {
          ck_assert_ptr_null(tuning_test_stamped_projectile);
        }
        ck_assert_int_eq(fire_test_projectile.race_physics_preset,
                         presets[presetIndex].id);
        ck_assert_int_eq(fire_test_projectile.race_weapon_profile,
                         presets[presetIndex].weapon_profile);
        ck_assert_int_eq(fire_test_projectile.race_weapon_fire_kind,
                         fire_test_paths[pathIndex].kind);
        ck_assert_ptr_eq(
          Race_WeaponMovement_ProjectilePreset(&fire_test_projectile),
          &presets[presetIndex]);

        ck_assert_int_eq(fire_test_second_projectile.race_physics_preset,
                         RACE_PHYSICS_PRESET_INVALID);
        ck_assert_int_eq(fire_test_second_projectile.race_weapon_profile,
                         RACE_WEAPON_PROFILE_INVALID);
        ck_assert_int_eq(fire_test_second_projectile.race_weapon_fire_kind,
                         RACE_WEAPON_FIRE_INVALID);

        g_entity_t afterScope = { 0 };
        ck_assert(!Race_WeaponMovement_StampProjectile(
          fire_test_paths[pathIndex].kind, &player, &player, &afterScope));
      }
    }
  }
}
END_TEST

START_TEST(_Race_WeaponNoShotPassthrough) {
  FireTest_Reset();
  fire_test_mode = FIRE_TEST_NO_SHOT;

  g_entity_t player = { 0 };
  g_client_t client = {
    .entity = &player,
    .weapon_fire_time = 1000u,
    .weapon_fired_time = 500u
  };
  g_item_t item = {
    .def.tag = WEAPON_ROCKET_LAUNCHER,
    .Think = G_FireRocketLauncher
  };
  client.weapon = &item;

  Race_WeaponMovement_InitItem(&item);
  item.Think(&client);

  ck_assert_uint_eq(fire_test_callback_count, 1u);
  ck_assert_uint_eq(tuning_test_uint_count, 0u);
  ck_assert_uint_eq(tuning_test_stamp_count, 0u);
  ck_assert_uint_eq(client.weapon_fire_time, 1000u);
  ck_assert_uint_eq(client.weapon_fired_time, 500u);
  ck_assert_int_eq(fire_test_projectile.race_physics_preset,
                   RACE_PHYSICS_PRESET_INVALID);
  ck_assert_ptr_null(Race_WeaponMovement_ProjectilePreset(
    &fire_test_projectile));
}
END_TEST

START_TEST(_Race_WeaponTunedRefire) {
  const race_physics_preset_descriptor_t *preset =
    Race_Physics_Preset(RACE_PHYSICS_PRESET_Q2);
  const race_physics_config_t config = {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = preset->family,
    .preset = preset->id,
    .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_NEAREST
  };
  ck_assert(Race_Physics_SetActive(&config));

  for (size_t pathIndex = 0u;
       pathIndex < lengthof(fire_test_paths); pathIndex++) {
    FireTest_Reset();
    fire_test_mode = FIRE_TEST_VALID;
    g_level.time = 4000u;
    tuning_test_uint_override = true;
    tuning_test_uint_value = 250u + (uint32_t) pathIndex;

    g_entity_t player = { 0 };
    g_client_t client = {
      .entity = &player,
      .weapon_fire_time = g_level.time,
      .weapon_fired_time = 500u
    };
    g_item_t item = {
      .def.tag = fire_test_paths[pathIndex].tag,
      .Think = fire_test_paths[pathIndex].callback
    };
    client.weapon = &item;

    Race_WeaponMovement_InitItem(&item);
    item.Think(&client);

    const bool tunable = fire_test_paths[pathIndex].tunable;
    ck_assert_uint_eq(tuning_test_uint_count, tunable ? 1u : 0u);
    ck_assert_int_eq(tuning_test_uint_id,
                     fire_test_paths[pathIndex].refire_id);
    ck_assert_int_eq(tuning_test_uint_preset,
                     tunable ? RACE_PHYSICS_PRESET_Q2
                             : RACE_PHYSICS_PRESET_INVALID);
    ck_assert_uint_eq(tuning_test_uint_legacy, tunable ? 125u : 0u);
    ck_assert_uint_eq(client.weapon_fire_time, tunable
      ? g_level.time + tuning_test_uint_value : g_level.time + 125u);
    ck_assert_uint_eq(client.weapon_fired_time, 525u);
    ck_assert_uint_eq(tuning_test_stamp_count, tunable ? 1u : 0u);
    if (tunable) {
      ck_assert_ptr_eq(tuning_test_stamped_projectile,
                       &fire_test_projectile);
    } else {
      ck_assert_ptr_null(tuning_test_stamped_projectile);
    }
  }
}
END_TEST

START_TEST(_Race_WeaponHyperClimbDelta) {
  const race_hyperblaster_climb_parameters_t compatibility = {
    .range = 32.f,
    .climb_up = 68.f,
    .climb_3d_up = 1.f,
    .climb_3d_side = 1.f
  };
  vec3_t delta = Vec3_Zero();
  ck_assert(Race_WeaponMovement_HyperClimbDelta(
    Vec3(16.f, 0.f, 0.f), Vec3(1.f, 0.f, 0.f), Vec3_Zero(),
    &compatibility, &delta));
  ck_assert(fabsf(delta.x) < .001f);
  ck_assert(fabsf(delta.y) < .001f);
  ck_assert(fabsf(delta.z - 68.f) < .001f);

  ck_assert(!Race_WeaponMovement_HyperClimbDelta(
    Vec3(32.f, 0.f, 0.f), Vec3(1.f, 0.f, 0.f), Vec3_Zero(),
    &compatibility, &delta));

  const race_hyperblaster_climb_parameters_t inward = {
    .range = 32.f,
    .climb_in = 68.f,
    .climb_3d = 1.f,
    .climb_3d_up = 1.f,
    .climb_3d_side = 1.f,
    .climb_3d_in = 1.f
  };
  ck_assert(Race_WeaponMovement_HyperClimbDelta(
    Vec3(16.f, 0.f, 0.f), Vec3(1.f, 0.f, 0.f), Vec3_Zero(),
    &inward, &delta));
  ck_assert(fabsf(delta.x + 136.f) < .001f);
  ck_assert(fabsf(delta.y) < .001f);
  ck_assert(fabsf(delta.z) < .001f);

  const race_hyperblaster_climb_parameters_t velocity = {
    .range = 32.f,
    .velocity_boost = .5f,
    .climb_3d_up = 1.f,
    .climb_3d_side = 1.f
  };
  ck_assert(Race_WeaponMovement_HyperClimbDelta(
    Vec3(16.f, 0.f, 0.f), Vec3(1.f, 0.f, 0.f),
    Vec3(4800.f, 0.f, 0.f), &velocity, &delta));
  ck_assert(fabsf(delta.x - 1200.f) < .001f);

  vec3_t invalid = Vec3(16.f, 0.f, 0.f);
  invalid.x = NAN;
  ck_assert(!Race_WeaponMovement_HyperClimbDelta(
    invalid, Vec3(1.f, 0.f, 0.f), Vec3_Zero(),
    &compatibility, &delta));
}
END_TEST

START_TEST(_Race_WeaponRejectsInvalidStamp) {
  g_entity_t projectile = {
    .race_physics_preset = RACE_PHYSICS_PRESET_Q2,
    .race_weapon_profile = RACE_WEAPON_PROFILE_INVALID,
    .race_weapon_fire_kind = RACE_WEAPON_FIRE_ROCKET
  };
  ck_assert_ptr_null(Race_WeaponMovement_ProjectilePreset(&projectile));

  projectile.race_weapon_profile =
    RACE_WEAPON_PROFILE_LEGACY_LIVE_CVARS_V1;
  projectile.race_weapon_fire_kind = RACE_WEAPON_FIRE_INVALID;
  ck_assert_ptr_null(Race_WeaponMovement_ProjectilePreset(&projectile));

  projectile.race_weapon_fire_kind = RACE_WEAPON_FIRE_ROCKET;
  projectile.race_physics_preset = (race_physics_preset_id_t) 99;
  ck_assert_ptr_null(Race_WeaponMovement_ProjectilePreset(&projectile));

  projectile.race_physics_preset = RACE_PHYSICS_PRESET_Q2;
  projectile.race_weapon_profile = (race_weapon_profile_id_t) 99;
  ck_assert_ptr_null(Race_WeaponMovement_ProjectilePreset(&projectile));

  projectile.race_weapon_profile =
    RACE_WEAPON_PROFILE_LEGACY_LIVE_CVARS_V1;
  projectile.race_weapon_fire_kind = (race_weapon_fire_kind_t) 99;
  ck_assert_ptr_null(Race_WeaponMovement_ProjectilePreset(&projectile));
  ck_assert_ptr_null(Race_WeaponMovement_ProjectilePreset(NULL));
}
END_TEST

#ifdef _WIN32
#undef main
#endif
int main(void) {
#ifdef _WIN32
  _Race_WeaponProfiles();
  _Race_WeaponItemWrapping();
  _Race_WeaponScopedStamping();
  _Race_WeaponNoShotPassthrough();
  _Race_WeaponTunedRefire();
  _Race_WeaponRejectsInvalidStamp();
  _Race_WeaponHyperClimbDelta();
  if (race_native_failures == 0) {
    puts("100%: Checks: 7, Failures: 0, Errors: 0");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "0%%: Checks: 7, Failures: %" PRId32 ", Errors: 0\n",
          race_native_failures);
  return EXIT_FAILURE;
#else
  Suite *suite = suite_create("Race weapon movement");
  TCase *testCase = tcase_create("weapon profiles");
  tcase_add_test(testCase, _Race_WeaponProfiles);
  tcase_add_test(testCase, _Race_WeaponItemWrapping);
  tcase_add_test(testCase, _Race_WeaponScopedStamping);
  tcase_add_test(testCase, _Race_WeaponNoShotPassthrough);
  tcase_add_test(testCase, _Race_WeaponTunedRefire);
  tcase_add_test(testCase, _Race_WeaponRejectsInvalidStamp);
  tcase_add_test(testCase, _Race_WeaponHyperClimbDelta);
  suite_add_tcase(suite, testCase);

  SRunner *runner = srunner_create(suite);
  srunner_run_all(runner, CK_NORMAL);
  const int32_t failed = srunner_ntests_failed(runner);
  srunner_free(runner);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
#endif
}
