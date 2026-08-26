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
#include <stddef.h>
#include <stdint.h>

#include "race_weapon_tuning_types.h"

#define RACE_WEAPON_TUNING_VALUE_TEXT_MAX 31u
#define RACE_WEAPON_TUNING_VALUE_TEXT_SIZE \
  (RACE_WEAPON_TUNING_VALUE_TEXT_MAX + 1u)
#define RACE_WEAPON_TUNING_ERROR_MAX 127u
#define RACE_WEAPON_TUNING_ERROR_SIZE \
  (RACE_WEAPON_TUNING_ERROR_MAX + 1u)
const race_weapon_tuning_descriptor_t *Race_WeaponTuning_Catalog(
  size_t *count);
const race_weapon_tuning_descriptor_t *Race_WeaponTuning_Descriptor(
  race_weapon_tuning_id_t id);
const race_weapon_tuning_descriptor_t *Race_WeaponTuning_DescriptorForKey(
  const char *key);
bool Race_WeaponTuning_CatalogValid(char *error, size_t error_size);
uint64_t Race_WeaponTuning_CatalogHash(void);

void Race_WeaponTuning_DefaultSnapshot(
  race_weapon_tuning_snapshot_t *snapshot);
bool Race_WeaponTuning_SnapshotValid(
  const race_weapon_tuning_snapshot_t *snapshot,
  char *error, size_t error_size);
bool Race_WeaponTuning_SnapshotEqual(
  const race_weapon_tuning_snapshot_t *left,
  const race_weapon_tuning_snapshot_t *right);
uint64_t Race_WeaponTuning_SnapshotHash(
  const race_weapon_tuning_snapshot_t *snapshot);

bool Race_WeaponTuning_ParseValue(
  const race_weapon_tuning_descriptor_t *descriptor, const char *text,
  race_weapon_tuning_scalar_t *value, char *error, size_t error_size);
bool Race_WeaponTuning_FormatValue(
  const race_weapon_tuning_descriptor_t *descriptor,
  race_weapon_tuning_scalar_t value, char *output, size_t capacity);
bool Race_WeaponTuning_SetText(
  race_weapon_tuning_snapshot_t *snapshot, const char *key, const char *text,
  char *error, size_t error_size);

int32_t Race_WeaponTuning_Int(
  const race_weapon_tuning_snapshot_t *snapshot,
  race_weapon_tuning_id_t id);
uint32_t Race_WeaponTuning_Uint(
  const race_weapon_tuning_snapshot_t *snapshot,
  race_weapon_tuning_id_t id);
float Race_WeaponTuning_Float(
  const race_weapon_tuning_snapshot_t *snapshot,
  race_weapon_tuning_id_t id);
