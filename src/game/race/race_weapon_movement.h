/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "g_types.h"
#include "race_physics.h"
#include "race_weapon_tuning_types.h"

typedef struct {
  race_weapon_profile_id_t id;
  bool live_cvars;
} race_weapon_movement_profile_t;

const race_weapon_movement_profile_t *Race_WeaponMovement_Profile(
  race_weapon_profile_id_t id);
bool Race_WeaponMovement_ProfileComplete(race_weapon_profile_id_t id);

/**
 * @brief Replaces the six movement-relevant player weapon callbacks after
 * common item initialization has populated them.
 */
void Race_WeaponMovement_InitItem(g_item_t *item);

/**
 * @brief Opens one source-bound player-fire scope and delegates to the exact
 * common weapon callback.
 */
void Race_FirePresetWeapon(g_client_t *cl);

/**
 * @brief Stamps the first projectile whose constructor and sources match the
 * active player-fire scope.
 * @return True only when `projectile` received a valid player preset stamp.
 */
bool Race_WeaponMovement_StampProjectile(race_weapon_fire_kind_t fire_kind,
                                         const g_entity_t *emitter,
                                         const g_entity_t *attacker,
                                         g_entity_t *projectile);

/**
 * @brief Resolves a valid stamped projectile back to its semantic descriptor.
 */
const race_physics_preset_descriptor_t *Race_WeaponMovement_ProjectilePreset(
  const g_entity_t *projectile);

/**
 * @brief Computes the bounded Hyperblaster climb velocity delta for one
 * structural impact.
 * @return True only for a finite impact strictly inside the configured range.
 */
bool Race_WeaponMovement_HyperClimbDelta(
  vec3_t impact_to_owner, vec3_t normal, vec3_t owner_velocity,
  const race_hyperblaster_climb_parameters_t *parameters, vec3_t *delta);
