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
#include "race_weapon_tuning.h"

typedef struct g_client_s g_client_t;

void Race_WeaponTuningService_Init(void);
void Race_WeaponTuningService_PostInit(void);
void Race_WeaponTuningService_ConfigureLevel(void);
void Race_WeaponTuningService_Shutdown(void);
void Race_WeaponTuningService_ClientBegin(g_client_t *cl);

bool Race_WeaponTuningService_ClientCommand(g_client_t *cl,
                                            const char *cmd);
bool Race_WeaponTuningService_Active(void);
bool Race_WeaponTuningService_Rankable(void);
uint64_t Race_WeaponTuningService_Generation(void);
const race_weapon_tuning_snapshot_t *Race_WeaponTuningService_Current(void);
const race_weapon_tuning_snapshot_t *Race_WeaponTuningService_CurrentForPreset(
  race_physics_preset_id_t preset);

int32_t Race_WeaponTuningService_Int(race_weapon_tuning_id_t id,
                                     int32_t legacy_value);
uint32_t Race_WeaponTuningService_Uint(race_weapon_tuning_id_t id,
                                       uint32_t legacy_value);
float Race_WeaponTuningService_Float(race_weapon_tuning_id_t id,
                                     float legacy_value);
int32_t Race_WeaponTuningService_IntForPreset(
  race_physics_preset_id_t preset, race_weapon_tuning_id_t id,
  int32_t legacy_value);
uint32_t Race_WeaponTuningService_UintForPreset(
  race_physics_preset_id_t preset, race_weapon_tuning_id_t id,
  uint32_t legacy_value);
float Race_WeaponTuningService_FloatForPreset(
  race_physics_preset_id_t preset, race_weapon_tuning_id_t id,
  float legacy_value);

void Race_WeaponTuningService_StampProjectile(g_entity_t *projectile);
float Race_WeaponTuningService_ProjectileSelfKnockback(
  const g_entity_t *projectile, float legacy_value);
float Race_WeaponTuningService_ProjectileHyperRange(
  const g_entity_t *projectile, float legacy_value);
float Race_WeaponTuningService_ProjectileHyperImpulse(
  const g_entity_t *projectile, float legacy_value);
bool Race_WeaponTuningService_ProjectileHyperParameters(
  const g_entity_t *projectile, float legacy_range, float legacy_climb_up,
  race_hyperblaster_climb_parameters_t *parameters);
