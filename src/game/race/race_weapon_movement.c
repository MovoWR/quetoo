/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "g_local.h"

#include "race_physics.h"
#include "race_weapon_movement.h"
#include "race_weapon_tuning_service.h"

#include <math.h>

typedef void (*race_weapon_fire_callback_t)(g_client_t *cl);

typedef struct {
  bool active;
  bool matched_spawn;
  bool shot_spawned;
  g_client_t *client;
  g_entity_t *entity;
  race_physics_preset_id_t preset;
  race_weapon_profile_id_t profile;
  race_weapon_fire_kind_t fire_kind;
  uint32_t previous_fire_time;
  uint32_t previous_fired_time;
} race_weapon_fire_scope_t;

static const race_weapon_movement_profile_t race_weapon_profiles[] = {
  {
    .id = RACE_WEAPON_PROFILE_LEGACY_LIVE_CVARS_V1,
    .live_cvars = true
  }
};

static race_weapon_fire_scope_t race_weapon_fire_scope;

const race_weapon_movement_profile_t *Race_WeaponMovement_Profile(
    const race_weapon_profile_id_t id) {
  for (size_t i = 0u; i < lengthof(race_weapon_profiles); i++) {
    if (race_weapon_profiles[i].id == id) {
      return &race_weapon_profiles[i];
    }
  }
  return NULL;
}

bool Race_WeaponMovement_ProfileComplete(const race_weapon_profile_id_t id) {
  const race_weapon_movement_profile_t *profile =
    Race_WeaponMovement_Profile(id);
  return profile && profile->id != RACE_WEAPON_PROFILE_INVALID;
}

static race_weapon_fire_kind_t Race_WeaponMovement_FireKind(
    const g_item_tag_t tag) {
  switch (tag) {
    case WEAPON_HAND_GRENADE:
      return RACE_WEAPON_FIRE_HAND_GRENADE;
    case WEAPON_GRENADE_LAUNCHER:
      return RACE_WEAPON_FIRE_GRENADE;
    case WEAPON_QUAKE_GRENADE_LAUNCHER:
      return RACE_WEAPON_FIRE_QUAKE_GRENADE;
    case WEAPON_ROCKET_LAUNCHER:
      return RACE_WEAPON_FIRE_ROCKET;
    case WEAPON_QUAKE_ROCKET_LAUNCHER:
      return RACE_WEAPON_FIRE_QUAKE_ROCKET;
    case WEAPON_HYPERBLASTER:
      return RACE_WEAPON_FIRE_HYPERBLASTER;
    default:
      return RACE_WEAPON_FIRE_INVALID;
  }
}

static race_weapon_fire_callback_t Race_WeaponMovement_CommonCallback(
    const race_weapon_fire_kind_t fire_kind) {
  switch (fire_kind) {
    case RACE_WEAPON_FIRE_HAND_GRENADE:
      return G_FireHandGrenade;
    case RACE_WEAPON_FIRE_GRENADE:
      return G_FireGrenadeLauncher;
    case RACE_WEAPON_FIRE_QUAKE_GRENADE:
      return G_FireQuakeGrenadeLauncher;
    case RACE_WEAPON_FIRE_ROCKET:
      return G_FireRocketLauncher;
    case RACE_WEAPON_FIRE_QUAKE_ROCKET:
      return G_FireQuakeRocketLauncher;
    case RACE_WEAPON_FIRE_HYPERBLASTER:
      return G_FireHyperblaster;
    default:
      return NULL;
  }
}

static race_weapon_tuning_id_t Race_WeaponMovement_RefireId(
    const race_weapon_fire_kind_t fire_kind) {
  switch (fire_kind) {
    case RACE_WEAPON_FIRE_HAND_GRENADE:
      return RACE_WEAPON_TUNING_HAND_GRENADE_REFIRE_MS;
    case RACE_WEAPON_FIRE_GRENADE:
      return RACE_WEAPON_TUNING_STANDARD_GRENADE_REFIRE_MS;
    case RACE_WEAPON_FIRE_ROCKET:
      return RACE_WEAPON_TUNING_STANDARD_ROCKET_REFIRE_MS;
    case RACE_WEAPON_FIRE_HYPERBLASTER:
      return RACE_WEAPON_TUNING_HYPER_REFIRE_MS;
    default:
      return RACE_WEAPON_TUNING_VALUE_TOTAL;
  }
}

void Race_WeaponMovement_InitItem(g_item_t *item) {
  if (!item) {
    return;
  }

  const race_weapon_fire_kind_t fire_kind =
    Race_WeaponMovement_FireKind(item->def.tag);
  if (fire_kind == RACE_WEAPON_FIRE_INVALID) {
    return;
  }

  const race_weapon_fire_callback_t callback =
    Race_WeaponMovement_CommonCallback(fire_kind);
  assert(callback);
  assert(item->Think == callback);
  if (!callback || item->Think != callback) {
    return;
  }
  item->Think = Race_FirePresetWeapon;
}

void Race_FirePresetWeapon(g_client_t *cl) {
  if (!cl || !cl->entity || !cl->weapon) {
    return;
  }

  const race_weapon_fire_kind_t fire_kind =
    Race_WeaponMovement_FireKind(cl->weapon->def.tag);
  const race_weapon_fire_callback_t callback =
    Race_WeaponMovement_CommonCallback(fire_kind);
  if (!callback) {
    assert(false);
    return;
  }

  if (race_weapon_fire_scope.active) {
    assert(false);
    callback(cl);
    return;
  }

  const race_physics_config_t *config = Race_Physics_Current();
  const race_physics_preset_descriptor_t *preset = config
    ? Race_Physics_Preset(config->preset)
    : NULL;
  const race_weapon_movement_profile_t *profile = preset
    ? Race_WeaponMovement_Profile(preset->weapon_profile)
    : NULL;
  assert(config);
  assert(preset);
  assert(profile);
  if (!config || !preset || !profile) {
    callback(cl);
    return;
  }

  race_weapon_fire_scope = (race_weapon_fire_scope_t) {
    .active = true,
    .client = cl,
    .entity = cl->entity,
    .preset = preset->id,
    .profile = profile->id,
    .fire_kind = fire_kind,
    .previous_fire_time = cl->weapon_fire_time,
    .previous_fired_time = cl->weapon_fired_time
  };

  callback(cl);

  // Only replace common's accepted-shot timestamp after the scoped constructor
  // proved that this callback spawned the expected player projectile.
  assert(profile->live_cvars);
  const race_weapon_tuning_id_t refire_id =
    Race_WeaponMovement_RefireId(fire_kind);
  if (race_weapon_fire_scope.shot_spawned &&
      refire_id < RACE_WEAPON_TUNING_VALUE_TOTAL &&
      (cl->weapon_fire_time != race_weapon_fire_scope.previous_fire_time ||
       cl->weapon_fired_time != race_weapon_fire_scope.previous_fired_time)) {
    const uint32_t legacy_interval = cl->weapon_fire_time >= g_level.time
      ? cl->weapon_fire_time - g_level.time : 0u;
    cl->weapon_fire_time = g_level.time +
      Race_WeaponTuningService_UintForPreset(
        race_weapon_fire_scope.preset, refire_id, legacy_interval);
  }
  memset(&race_weapon_fire_scope, 0, sizeof(race_weapon_fire_scope));
}

static bool Race_WeaponMovement_SourceMatches(
    const race_weapon_fire_kind_t fire_kind, const g_entity_t *emitter,
    const g_entity_t *attacker, const g_entity_t *projectile) {
  if (emitter != race_weapon_fire_scope.entity) {
    return false;
  }

  switch (fire_kind) {
    case RACE_WEAPON_FIRE_HAND_GRENADE:
      return projectile == race_weapon_fire_scope.client->held_grenade;
    case RACE_WEAPON_FIRE_GRENADE:
    case RACE_WEAPON_FIRE_ROCKET:
      return attacker == race_weapon_fire_scope.entity;
    case RACE_WEAPON_FIRE_QUAKE_GRENADE:
    case RACE_WEAPON_FIRE_QUAKE_ROCKET:
    case RACE_WEAPON_FIRE_HYPERBLASTER:
      return true;
    default:
      return false;
  }
}

bool Race_WeaponMovement_StampProjectile(
    const race_weapon_fire_kind_t fire_kind, const g_entity_t *emitter,
    const g_entity_t *attacker, g_entity_t *projectile) {
  if (!race_weapon_fire_scope.active ||
      race_weapon_fire_scope.matched_spawn || !projectile ||
      race_weapon_fire_scope.fire_kind != fire_kind ||
      !Race_WeaponMovement_SourceMatches(fire_kind, emitter, attacker,
                                         projectile)) {
    return false;
  }

  race_weapon_fire_scope.matched_spawn = true;
  projectile->race_physics_preset = race_weapon_fire_scope.preset;
  projectile->race_weapon_profile = race_weapon_fire_scope.profile;
  projectile->race_weapon_fire_kind = fire_kind;
  if (Race_WeaponMovement_RefireId(fire_kind) <
      RACE_WEAPON_TUNING_VALUE_TOTAL) {
    Race_WeaponTuningService_StampProjectile(projectile);
  }
  race_weapon_fire_scope.shot_spawned = true;
  return true;
}

const race_physics_preset_descriptor_t *Race_WeaponMovement_ProjectilePreset(
    const g_entity_t *projectile) {
  if (!projectile ||
      projectile->race_physics_preset == RACE_PHYSICS_PRESET_INVALID ||
      projectile->race_weapon_profile == RACE_WEAPON_PROFILE_INVALID ||
      projectile->race_weapon_fire_kind == RACE_WEAPON_FIRE_INVALID) {
    return NULL;
  }

  const race_physics_preset_descriptor_t *preset =
    Race_Physics_Preset(projectile->race_physics_preset);
  return preset &&
         Race_WeaponMovement_Profile(projectile->race_weapon_profile) &&
         Race_WeaponMovement_CommonCallback(
           projectile->race_weapon_fire_kind) &&
         preset->weapon_profile == projectile->race_weapon_profile
    ? preset
    : NULL;
}

static bool Race_WeaponMovement_FiniteVec3(const vec3_t value) {
  return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

bool Race_WeaponMovement_HyperClimbDelta(
    const vec3_t impact_to_owner, const vec3_t normal,
    const vec3_t owner_velocity,
    const race_hyperblaster_climb_parameters_t *parameters,
    vec3_t *delta) {
  if (!parameters || !delta ||
      !Race_WeaponMovement_FiniteVec3(impact_to_owner) ||
      !Race_WeaponMovement_FiniteVec3(normal) ||
      !Race_WeaponMovement_FiniteVec3(owner_velocity) ||
      !isfinite(parameters->range) || parameters->range <= 0.f ||
      !isfinite(parameters->climb_up) ||
      !isfinite(parameters->climb_in) ||
      !isfinite(parameters->velocity_boost) ||
      !isfinite(parameters->climb_3d) ||
      !isfinite(parameters->climb_3d_up) ||
      !isfinite(parameters->climb_3d_side) ||
      !isfinite(parameters->climb_3d_in)) {
    return false;
  }
  const float distance = Vec3_Length(impact_to_owner);
  const float normal_length = Vec3_Length(normal);
  if (!isfinite(distance) || distance >= parameters->range ||
      !isfinite(normal_length) || normal_length <= .001f) {
    return false;
  }

  const vec3_t surface_normal = Vec3_Scale(normal, 1.f / normal_length);
  const vec3_t aim_dir = distance > .001f
    ? Vec3_Scale(Vec3_Negate(impact_to_owner), 1.f / distance)
    : Vec3_Zero();
  const float aim_dot_normal = Vec3_Dot(aim_dir, surface_normal);
  const float into_wall = Maxf(0.f, -aim_dot_normal);
  const float wall = Clampf(1.f - fabsf(surface_normal.z), 0.f, 1.f);
  const float in_factor = 1.f + parameters->climb_3d *
    parameters->climb_3d_in * into_wall;
  vec3_t push = Vec3_Scale(
    surface_normal, -parameters->climb_in * wall * in_factor);

  vec3_t up_dir = Vec3_Up();
  if (parameters->climb_3d > 0.f) {
    vec3_t aim_tangent = Vec3_Subtract(
      aim_dir, Vec3_Scale(surface_normal, aim_dot_normal));
    const float tangent_length = Vec3_Length(aim_tangent);
    if (tangent_length > .001f) {
      aim_tangent = Vec3_Scale(aim_tangent, 1.f / tangent_length);
      const float up_amount = Vec3_Dot(aim_tangent, Vec3_Up());
      const vec3_t aim_up = Vec3_Scale(Vec3_Up(), up_amount);
      const vec3_t aim_side = Vec3_Subtract(aim_tangent, aim_up);
      vec3_t desired = Vec3_Scale(
        Vec3_Up(), 1.f - parameters->climb_3d * parameters->climb_3d_up);
      desired = Vec3_Add(desired, Vec3_Scale(
        aim_up, parameters->climb_3d * parameters->climb_3d_up));
      desired = Vec3_Add(desired, Vec3_Scale(
        aim_side, parameters->climb_3d * parameters->climb_3d_side));
      const float desired_length = Vec3_Length(desired);
      if (desired_length > .001f) {
        up_dir = Vec3_Scale(desired, 1.f / desired_length);
      }
    }
  }
  push = Vec3_Add(push, Vec3_Scale(up_dir, parameters->climb_up));

  if (parameters->velocity_boost != 0.f) {
    vec3_t source = owner_velocity;
    const float source_speed = Vec3_Length(source);
    if (source_speed > RACE_WEAPON_TUNING_HYPER_VELOCITY_SOURCE_CAP) {
      source = Vec3_Scale(
        source, RACE_WEAPON_TUNING_HYPER_VELOCITY_SOURCE_CAP / source_speed);
    }
    push = Vec3_Add(
      push, Vec3_Scale(source, parameters->velocity_boost));
  }
  if (!Race_WeaponMovement_FiniteVec3(push)) {
    return false;
  }
  *delta = push;
  return true;
}
