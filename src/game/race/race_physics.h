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

#include "shared/shared.h"

#define RACE_PHYSICS_CONFIG_VERSION 2u
#define RACE_PHYSICS_CONFIG_STRING_SIZE 96u

#define RACE_PHYSICS_FAMILY_QUETOO_KEY "quetoo"
#define RACE_PHYSICS_FAMILY_Q2_KEY "q2"

#define RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY "quetoo-common-v1"
#define RACE_PHYSICS_PRESET_Q2_V1_KEY "q2-v1"
#define RACE_PHYSICS_PRESET_QUETOO_FIX_V1_KEY "quetoo-fix-v1"

#define RACE_PHYSICS_SELECTOR_Q2_KEY "q2"
#define RACE_PHYSICS_SELECTOR_QUAKE2_KEY "quake2"

/*
 * These explicit values are stable internal identifiers. The wire format uses
 * the semantic keys above and never serializes enum ordinals.
 */
typedef enum {
  RACE_PHYSICS_FAMILY_INVALID = 0,
  RACE_PHYSICS_FAMILY_QUETOO = 1,
  RACE_PHYSICS_FAMILY_Q2 = 2
} race_physics_family_id_t;

typedef enum {
  RACE_PHYSICS_PRESET_INVALID = 0,
  RACE_PHYSICS_PRESET_QUETOO_COMMON_V1 = 1,
  RACE_PHYSICS_PRESET_Q2 = 2,
  RACE_PHYSICS_PRESET_QUETOO_FIX_V1 = 3
} race_physics_preset_id_t;

/**
 * @brief Legacy-compatible Q2 position and velocity state snapping.
 */
typedef enum {
  RACE_PHYSICS_Q2_SNAP_OFF = 0,
  RACE_PHYSICS_Q2_SNAP_NEAREST = 1,
  RACE_PHYSICS_Q2_SNAP_TRUNCATE = 2
} race_physics_q2_snap_mode_t;

typedef struct {
  race_physics_family_id_t id;
  const char *key;
  const char *name;
  bool available;
} race_physics_family_descriptor_t;

typedef struct {
  race_physics_preset_id_t id;
  race_physics_family_id_t family;
  const char *key;
  const char *name;
  const char *short_name;
  const char *ruleset;
  bool available;
  bool rankable;
} race_physics_preset_descriptor_t;

typedef struct {
  uint16_t version;
  race_physics_family_id_t family;
  race_physics_preset_id_t preset;
  race_physics_q2_snap_mode_t q2_snap_mode;
} race_physics_config_t;

typedef enum {
  RACE_PHYSICS_PARSE_OK,
  RACE_PHYSICS_PARSE_INVALID_ARGUMENT,
  RACE_PHYSICS_PARSE_MISSING,
  RACE_PHYSICS_PARSE_TOO_LARGE,
  RACE_PHYSICS_PARSE_MALFORMED,
  RACE_PHYSICS_PARSE_UNKNOWN_VERSION,
  RACE_PHYSICS_PARSE_UNKNOWN_FAMILY,
  RACE_PHYSICS_PARSE_UNKNOWN_PRESET,
  RACE_PHYSICS_PARSE_UNKNOWN_SNAP_MODE,
  RACE_PHYSICS_PARSE_FAMILY_MISMATCH,
  RACE_PHYSICS_PARSE_SNAP_MODE_MISMATCH,
  RACE_PHYSICS_PARSE_UNAVAILABLE,
  RACE_PHYSICS_PARSE_PARAMETER_MISMATCH
} race_physics_parse_result_t;

typedef race_physics_parse_result_t (*race_physics_config_provider_t)(
  race_physics_config_t *config);

const race_physics_family_descriptor_t *Race_Physics_Families(size_t *count);
const race_physics_preset_descriptor_t *Race_Physics_Presets(size_t *count);
const race_physics_family_descriptor_t *Race_Physics_Family(
  race_physics_family_id_t id);
const race_physics_preset_descriptor_t *Race_Physics_Preset(
  race_physics_preset_id_t id);
const race_physics_family_descriptor_t *Race_Physics_FamilyForKey(
  const char *key);
const race_physics_preset_descriptor_t *Race_Physics_PresetForKey(
  const char *key);
const char *Race_Physics_Q2SnapModeKey(race_physics_q2_snap_mode_t mode);

const race_physics_config_t *Race_Physics_Default(void);
bool Race_Physics_ConfigForPresetKey(const char *key,
                                     race_physics_config_t *config);
bool Race_Physics_ConfigForSelector(const char *selector,
                                    race_physics_config_t *config);
bool Race_Physics_ConfigEquals(const race_physics_config_t *left,
                               const race_physics_config_t *right);
bool Race_Physics_ConfigValid(const race_physics_config_t *config);
bool Race_Physics_ConfigAvailable(const race_physics_config_t *config);
bool Race_Physics_ConfigRankable(const race_physics_config_t *config);
const char *Race_Physics_ConfigRuleset(const race_physics_config_t *config);

/**
 * @brief Resolves the exact immutable movement vector owned by a named Race
 * Q2 preset.
 * @return True for a supported fixed preset. On failure, `params` is unchanged.
 */
bool Race_Physics_FixedParamsForPreset(race_physics_preset_id_t preset,
                                       pm_params_t *params);
bool Race_Physics_ParamsEqual(const pm_params_t *left,
                              const pm_params_t *right);
bool Race_Physics_ConfigParamsAgree(const race_physics_config_t *config,
                                    const pm_params_t *params);
/**
 * @brief Returns true only when a decoded identity and a current authoritative
 * snapshot form a safe client-prediction contract.
 */
bool Race_Physics_PredictionReady(race_physics_parse_result_t decoded,
                                  bool snapshot_valid,
                                  const race_physics_config_t *config,
                                  const pm_params_t *params);
uint64_t Race_Physics_ParamsHash(const pm_params_t *params);

bool Race_Physics_Encode(const race_physics_config_t *config,
                         char output[RACE_PHYSICS_CONFIG_STRING_SIZE]);
race_physics_parse_result_t Race_Physics_Decode(
  const char *input, race_physics_config_t *config);
const char *Race_Physics_ParseResultName(race_physics_parse_result_t result);

void Race_Physics_Reset(void);
bool Race_Physics_SetActive(const race_physics_config_t *config);
void Race_Physics_SetProvider(race_physics_config_provider_t provider);
const race_physics_config_t *Race_Physics_Current(void);
