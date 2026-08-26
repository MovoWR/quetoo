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

#define RACE_WEAPON_TUNING_CATALOG_VERSION 3u
#define RACE_WEAPON_TUNING_VALUE_COUNT 22u
#define RACE_WEAPON_TUNING_KEY_MAX 31u
#define RACE_WEAPON_TUNING_KEY_SIZE (RACE_WEAPON_TUNING_KEY_MAX + 1u)
#define RACE_WEAPON_TUNING_GROUP_KEY_MAX 23u
#define RACE_WEAPON_TUNING_GROUP_KEY_SIZE \
  (RACE_WEAPON_TUNING_GROUP_KEY_MAX + 1u)
#define RACE_WEAPON_TUNING_LABEL_MAX 31u
#define RACE_WEAPON_TUNING_LABEL_SIZE (RACE_WEAPON_TUNING_LABEL_MAX + 1u)
#define RACE_WEAPON_TUNING_UNIT_MAX 15u
#define RACE_WEAPON_TUNING_UNIT_SIZE (RACE_WEAPON_TUNING_UNIT_MAX + 1u)
#define RACE_WEAPON_TUNING_IDENTITY_MAX 63u
#define RACE_WEAPON_TUNING_IDENTITY_SIZE \
  (RACE_WEAPON_TUNING_IDENTITY_MAX + 1u)
#define RACE_WEAPON_TUNING_HASH_HEX_SIZE 17u
#define RACE_WEAPON_TUNING_PRESET_KEY_MAX 31u
#define RACE_WEAPON_TUNING_PRESET_KEY_SIZE \
  (RACE_WEAPON_TUNING_PRESET_KEY_MAX + 1u)
#define RACE_WEAPON_TUNING_MAX_HYPER_IMPULSE_PER_SECOND 2000.f
#define RACE_WEAPON_TUNING_HYPER_VELOCITY_SOURCE_CAP 2400.f

typedef enum {
  RACE_WEAPON_TUNING_STATE_INACTIVE,
  RACE_WEAPON_TUNING_STATE_ACTIVE,
  RACE_WEAPON_TUNING_STATE_TRANSITION,
  RACE_WEAPON_TUNING_STATE_RECOVERY,
  RACE_WEAPON_TUNING_STATE_ERROR,

  RACE_WEAPON_TUNING_STATE_TOTAL
} race_weapon_tuning_state_t;

typedef enum {
  RACE_WEAPON_TUNING_TYPE_INT32,
  RACE_WEAPON_TUNING_TYPE_UINT32,
  RACE_WEAPON_TUNING_TYPE_FLOAT,

  RACE_WEAPON_TUNING_TYPE_TOTAL
} race_weapon_tuning_type_t;

typedef enum {
  RACE_WEAPON_TUNING_GROUP_HYPER,
  RACE_WEAPON_TUNING_GROUP_STANDARD_ROCKET,
  RACE_WEAPON_TUNING_GROUP_STANDARD_GRENADE,
  RACE_WEAPON_TUNING_GROUP_HAND_GRENADE,
  RACE_WEAPON_TUNING_GROUP_GLOBAL,

  RACE_WEAPON_TUNING_GROUP_TOTAL
} race_weapon_tuning_group_t;

typedef enum {
  RACE_WEAPON_TUNING_HYPER_KNOCKBACK,
  RACE_WEAPON_TUNING_HYPER_REFIRE_MS,
  RACE_WEAPON_TUNING_HYPER_SPEED,
  RACE_WEAPON_TUNING_HYPER_CLIMB_IMPULSE_Z,
  RACE_WEAPON_TUNING_HYPER_CLIMB_IN,
  RACE_WEAPON_TUNING_HYPER_CLIMB_VELOCITY_BOOST,
  RACE_WEAPON_TUNING_HYPER_CLIMB_3D,
  RACE_WEAPON_TUNING_HYPER_CLIMB_3D_UP,
  RACE_WEAPON_TUNING_HYPER_CLIMB_3D_SIDE,
  RACE_WEAPON_TUNING_HYPER_CLIMB_3D_IN,
  RACE_WEAPON_TUNING_HYPER_CLIMB_RANGE,
  RACE_WEAPON_TUNING_STANDARD_ROCKET_SPEED,
  RACE_WEAPON_TUNING_STANDARD_ROCKET_KNOCKBACK,
  RACE_WEAPON_TUNING_STANDARD_ROCKET_REFIRE_MS,
  RACE_WEAPON_TUNING_STANDARD_GRENADE_SPEED,
  RACE_WEAPON_TUNING_STANDARD_GRENADE_KNOCKBACK,
  RACE_WEAPON_TUNING_STANDARD_GRENADE_FUSE_MS,
  RACE_WEAPON_TUNING_STANDARD_GRENADE_REFIRE_MS,
  RACE_WEAPON_TUNING_HAND_GRENADE_KNOCKBACK,
  RACE_WEAPON_TUNING_HAND_GRENADE_REFIRE_MS,
  RACE_WEAPON_TUNING_GLOBAL_SELF_KNOCKBACK,
  RACE_WEAPON_TUNING_GRENADE_INHERIT_FRACTION,

  RACE_WEAPON_TUNING_VALUE_TOTAL
} race_weapon_tuning_id_t;

_Static_assert(RACE_WEAPON_TUNING_VALUE_TOTAL ==
                 RACE_WEAPON_TUNING_VALUE_COUNT,
               "Weapon tuning catalog count changed");
_Static_assert(RACE_WEAPON_TUNING_VALUE_COUNT < 32u,
               "Weapon tuning descriptor mask requires fewer than 32 values");

typedef union {
  int32_t integer;
  uint32_t unsigned_integer;
  float real;
} race_weapon_tuning_scalar_t;

_Static_assert(sizeof(float) == 4u,
               "Weapon tuning wire requires 32-bit float");
_Static_assert(sizeof(race_weapon_tuning_scalar_t) == 4u,
               "Weapon tuning wire requires 32-bit scalar storage");

typedef struct {
  race_weapon_tuning_id_t id;
  race_weapon_tuning_group_t group;
  race_weapon_tuning_type_t type;
  char key[RACE_WEAPON_TUNING_KEY_SIZE];
  char group_key[RACE_WEAPON_TUNING_GROUP_KEY_SIZE];
  char group_label[RACE_WEAPON_TUNING_LABEL_SIZE];
  char label[RACE_WEAPON_TUNING_LABEL_SIZE];
  char unit[RACE_WEAPON_TUNING_UNIT_SIZE];
  race_weapon_tuning_scalar_t compiled_default;
  race_weapon_tuning_scalar_t minimum;
  race_weapon_tuning_scalar_t maximum;
  race_weapon_tuning_scalar_t step;
} race_weapon_tuning_descriptor_t;

typedef struct {
  race_weapon_tuning_scalar_t values[RACE_WEAPON_TUNING_VALUE_COUNT];
} race_weapon_tuning_snapshot_t;

typedef struct {
  float range;
  float climb_up;
  float climb_in;
  float velocity_boost;
  float climb_3d;
  float climb_3d_up;
  float climb_3d_side;
  float climb_3d_in;
} race_hyperblaster_climb_parameters_t;

typedef struct {
  race_weapon_tuning_state_t state;
  uint64_t generation;
  uint64_t hash;
  char preset_key[RACE_WEAPON_TUNING_PRESET_KEY_SIZE];
  char identity[RACE_WEAPON_TUNING_IDENTITY_SIZE];
  float hyper_climb_range;
} race_weapon_tuning_status_t;
