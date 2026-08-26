/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_physics.h"

#include <stdio.h>
#include <string.h>

#define RACE_PHYSICS_Q2_SNAP_OFF_KEY "off"
#define RACE_PHYSICS_Q2_SNAP_NEAREST_KEY "nearest"
#define RACE_PHYSICS_Q2_SNAP_TRUNCATE_KEY "truncate"

static bool Race_Physics_FieldEquals(const char *field, size_t length,
                                     const char *expected);

/**
 * @brief Complete map-fixed movement vectors for named Q2 presets.
 * @details GAME hydrates these values. CGAME consumes the authoritative
 * `pm_state_t.params` snapshot and compares it with the selected descriptor.
 */
static const pm_params_t race_physics_q2_v1_params = {
  .gravity = 800,
  .gravity_water = 1.f,
  .accel_ground = 10.f,
  .accel_ground_slick = 10.f,
  .accel_air = 1.f,
  .accel_water = 10.f,
  .accel_spectator = 10.f,
  .accel_ladder = 10.f,
  .friction_ground = 6.f,
  .friction_ground_slick = 0.f,
  .friction_air = 0.f,
  .friction_water = 1.f,
  .friction_spectator = 4.f,
  .friction_ladder = 6.f,
  .speed_ground = 300.f,
  .speed_air = 300.f,
  .speed_water = 400.f,
  .speed_ladder = 200.f,
  .speed_spectator = 300.f,
  .speed_stop = 100.f,
  .speed_jump = 270.f,
  .speed_ducked = 100.f,
  .speed_duck_stand = 9999.f,
  .speed_water_jump = 350.f
};

static const pm_params_t race_physics_quetoo_fix_v1_params = {
  .gravity = 800,
  .gravity_water = .33f,
  .accel_ground = 10.f,
  .accel_ground_slick = 10.f,
  .accel_air = 2.f,
  .accel_water = 3.f,
  .accel_spectator = 2.5f,
  .accel_ladder = 16.f,
  .friction_ground = 6.f,
  .friction_ground_slick = 0.f,
  .friction_air = .125f,
  .friction_water = 2.f,
  .friction_spectator = 2.5f,
  .friction_ladder = 5.f,
  .speed_ground = 300.f,
  .speed_air = 350.f,
  .speed_water = 140.f,
  .speed_ladder = 125.f,
  .speed_spectator = 500.f,
  .speed_stop = 100.f,
  .speed_jump = 270.f,
  .speed_ducked = 140.f,
  .speed_duck_stand = 200.f,
  .speed_water_jump = 420.f
};

typedef struct {
  const char *alias;
  const char *preset_key;
} race_physics_selector_alias_t;

static const race_physics_selector_alias_t race_physics_selector_aliases[] = {
  {
    .alias = RACE_PHYSICS_SELECTOR_Q2_KEY,
    .preset_key = RACE_PHYSICS_PRESET_Q2_V1_KEY
  },
  {
    .alias = RACE_PHYSICS_SELECTOR_QUAKE2_KEY,
    .preset_key = RACE_PHYSICS_PRESET_Q2_V1_KEY
  },
  {
    .alias = RACE_PHYSICS_SELECTOR_DP2_KEY,
    .preset_key = RACE_PHYSICS_PRESET_DP2_V1_KEY
  }
};

static const race_physics_family_descriptor_t race_physics_families[] = {
  {
    .id = RACE_PHYSICS_FAMILY_QUETOO,
    .key = RACE_PHYSICS_FAMILY_QUETOO_KEY,
    .name = "Current Quetoo",
    .available = true
  },
  {
    .id = RACE_PHYSICS_FAMILY_Q2,
    .key = RACE_PHYSICS_FAMILY_Q2_KEY,
    .name = "Q2",
    .available = true
  }
};

static const race_physics_preset_descriptor_t race_physics_presets[] = {
  {
    .id = RACE_PHYSICS_PRESET_QUETOO_COMMON_V1,
    .family = RACE_PHYSICS_FAMILY_QUETOO,
    .key = RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY,
    .name = "Current common Quetoo",
    .short_name = "Quetoo",
    .ruleset = RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY,
    .fixed_params = NULL,
    .pm_policy = RACE_PM_POLICY_QUETOO_COMMON_V1,
    .weapon_profile = RACE_WEAPON_PROFILE_LEGACY_LIVE_CVARS_V1,
    .hyperblaster_climb_range = 32.f,
    .available = true,
    .rankable = true
  },
  {
    .id = RACE_PHYSICS_PRESET_Q2,
    .family = RACE_PHYSICS_FAMILY_Q2,
    .key = RACE_PHYSICS_PRESET_Q2_V1_KEY,
    .name = "Quake II",
    .short_name = "Q2",
    .ruleset = RACE_PHYSICS_PRESET_Q2_V1_KEY,
    .ruleset_snap_off = "q2-v1-snap-off",
    .ruleset_snap_truncate = "q2-v1-snap-truncate",
    .fixed_params = &race_physics_q2_v1_params,
    .pm_policy = RACE_PM_POLICY_Q2_V1,
    .weapon_profile = RACE_WEAPON_PROFILE_LEGACY_LIVE_CVARS_V1,
    .hyperblaster_climb_range = 32.f,
    .available = true,
    .rankable = true
  },
  {
    .id = RACE_PHYSICS_PRESET_QUETOO_FIX_V1,
    .family = RACE_PHYSICS_FAMILY_Q2,
    .key = RACE_PHYSICS_PRESET_QUETOO_FIX_V1_KEY,
    .name = "Legacy Quetoo Fix hybrid",
    .short_name = "Quetoo Fix",
    .ruleset = RACE_PHYSICS_PRESET_QUETOO_FIX_V1_KEY,
    .ruleset_snap_off = "quetoo-fix-v1-snap-off",
    .ruleset_snap_truncate = "quetoo-fix-v1-snap-truncate",
    .fixed_params = &race_physics_quetoo_fix_v1_params,
    .pm_policy = RACE_PM_POLICY_QUETOO_FIX_V1,
    .weapon_profile = RACE_WEAPON_PROFILE_LEGACY_LIVE_CVARS_V1,
    .hyperblaster_climb_range = 32.f,
    .available = true,
    .rankable = true
  },
  {
    .id = RACE_PHYSICS_PRESET_DP2_V1,
    .family = RACE_PHYSICS_FAMILY_Q2,
    .key = RACE_PHYSICS_PRESET_DP2_V1_KEY,
    .name = "Digital Paint: Paintball 2",
    .short_name = "DP2",
    .ruleset = RACE_PHYSICS_PRESET_DP2_V1_KEY,
    .ruleset_snap_off = "dp2-v1-snap-off",
    .ruleset_snap_truncate = "dp2-v1-snap-truncate",
    .fixed_params = &race_physics_q2_v1_params,
    .pm_policy = RACE_PM_POLICY_DP2_V1,
    .weapon_profile = RACE_WEAPON_PROFILE_LEGACY_LIVE_CVARS_V1,
    .hyperblaster_climb_range = 32.f,
    .available = true,
    .rankable = true
  }
};

static const race_physics_config_t race_physics_default = {
  .version = RACE_PHYSICS_CONFIG_VERSION,
  .family = RACE_PHYSICS_FAMILY_Q2,
  .preset = RACE_PHYSICS_PRESET_Q2,
  .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_TRUNCATE
};

static race_physics_config_t race_physics_active = {
  .version = RACE_PHYSICS_CONFIG_VERSION,
  .family = RACE_PHYSICS_FAMILY_Q2,
  .preset = RACE_PHYSICS_PRESET_Q2,
  .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_TRUNCATE
};
static race_physics_config_provider_t race_physics_provider;

const race_physics_family_descriptor_t *Race_Physics_Families(size_t *count) {
  if (count) {
    *count = sizeof(race_physics_families) / sizeof(*race_physics_families);
  }
  return race_physics_families;
}

const race_physics_preset_descriptor_t *Race_Physics_Presets(size_t *count) {
  if (count) {
    *count = sizeof(race_physics_presets) / sizeof(*race_physics_presets);
  }
  return race_physics_presets;
}

const race_physics_family_descriptor_t *Race_Physics_Family(
  race_physics_family_id_t id) {
  for (size_t i = 0; i < sizeof(race_physics_families) /
                           sizeof(*race_physics_families); i++) {
    if (race_physics_families[i].id == id) {
      return race_physics_families + i;
    }
  }
  return NULL;
}

const race_physics_preset_descriptor_t *Race_Physics_Preset(
  race_physics_preset_id_t id) {
  for (size_t i = 0; i < sizeof(race_physics_presets) /
                           sizeof(*race_physics_presets); i++) {
    if (race_physics_presets[i].id == id) {
      return race_physics_presets + i;
    }
  }
  return NULL;
}

const race_physics_family_descriptor_t *Race_Physics_FamilyForKey(
  const char *key) {
  if (!key) {
    return NULL;
  }
  for (size_t i = 0; i < sizeof(race_physics_families) /
                           sizeof(*race_physics_families); i++) {
    if (!strcmp(race_physics_families[i].key, key)) {
      return race_physics_families + i;
    }
  }
  return NULL;
}

const race_physics_preset_descriptor_t *Race_Physics_PresetForKey(
  const char *key) {
  if (!key) {
    return NULL;
  }
  for (size_t i = 0; i < sizeof(race_physics_presets) /
                           sizeof(*race_physics_presets); i++) {
    if (!strcmp(race_physics_presets[i].key, key)) {
      return race_physics_presets + i;
    }
  }
  return NULL;
}

const char *Race_Physics_Q2SnapModeKey(
    const race_physics_q2_snap_mode_t mode) {
  switch (mode) {
    case RACE_PHYSICS_Q2_SNAP_OFF:
      return RACE_PHYSICS_Q2_SNAP_OFF_KEY;
    case RACE_PHYSICS_Q2_SNAP_NEAREST:
      return RACE_PHYSICS_Q2_SNAP_NEAREST_KEY;
    case RACE_PHYSICS_Q2_SNAP_TRUNCATE:
      return RACE_PHYSICS_Q2_SNAP_TRUNCATE_KEY;
    default:
      return NULL;
  }
}

static bool Race_Physics_Q2SnapModeForField(
    const char *field, const size_t length,
    race_physics_q2_snap_mode_t *mode) {
  if (Race_Physics_FieldEquals(field, length,
                               RACE_PHYSICS_Q2_SNAP_OFF_KEY)) {
    *mode = RACE_PHYSICS_Q2_SNAP_OFF;
    return true;
  }
  if (Race_Physics_FieldEquals(field, length,
                               RACE_PHYSICS_Q2_SNAP_NEAREST_KEY)) {
    *mode = RACE_PHYSICS_Q2_SNAP_NEAREST;
    return true;
  }
  if (Race_Physics_FieldEquals(field, length,
                               RACE_PHYSICS_Q2_SNAP_TRUNCATE_KEY)) {
    *mode = RACE_PHYSICS_Q2_SNAP_TRUNCATE;
    return true;
  }
  return false;
}

const race_physics_config_t *Race_Physics_Default(void) {
  return &race_physics_default;
}

bool Race_Physics_ConfigForPresetKey(const char *key,
                                     race_physics_config_t *config) {
  if (!key || !config) {
    return false;
  }

  const race_physics_preset_descriptor_t *preset =
    Race_Physics_PresetForKey(key);
  const race_physics_family_descriptor_t *family = preset
    ? Race_Physics_Family(preset->family)
    : NULL;
  if (!family || !family->available || !preset->available) {
    return false;
  }

  *config = (race_physics_config_t) {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = family->id,
    .preset = preset->id,
    .q2_snap_mode = family->id == RACE_PHYSICS_FAMILY_Q2
      ? RACE_PHYSICS_Q2_SNAP_TRUNCATE
      : RACE_PHYSICS_Q2_SNAP_OFF
  };
  return true;
}

bool Race_Physics_ConfigForSelector(const char *selector,
                                    race_physics_config_t *config) {
  if (!selector) {
    return false;
  }

  const char *key = selector;
  for (size_t i = 0; i < lengthof(race_physics_selector_aliases); i++) {
    if (!strcmp(selector, race_physics_selector_aliases[i].alias)) {
      key = race_physics_selector_aliases[i].preset_key;
      break;
    }
  }

  return Race_Physics_ConfigForPresetKey(key, config);
}

bool Race_Physics_ConfigEquals(const race_physics_config_t *left,
                               const race_physics_config_t *right) {
  return left && right &&
         left->version == right->version &&
         left->family == right->family &&
         left->preset == right->preset &&
         left->q2_snap_mode == right->q2_snap_mode;
}

bool Race_Physics_ConfigValid(const race_physics_config_t *config) {
  if (!config || config->version != RACE_PHYSICS_CONFIG_VERSION) {
    return false;
  }
  const race_physics_family_descriptor_t *family =
    Race_Physics_Family(config->family);
  const race_physics_preset_descriptor_t *preset =
    Race_Physics_Preset(config->preset);
  const bool snap_mode_valid =
    Race_Physics_Q2SnapModeKey(config->q2_snap_mode) != NULL;
  return family && preset && preset->family == family->id &&
         snap_mode_valid &&
         (family->id == RACE_PHYSICS_FAMILY_Q2 ||
          config->q2_snap_mode == RACE_PHYSICS_Q2_SNAP_OFF);
}

bool Race_Physics_ConfigAvailable(const race_physics_config_t *config) {
  if (!Race_Physics_ConfigValid(config)) {
    return false;
  }
  return Race_Physics_Family(config->family)->available &&
         Race_Physics_Preset(config->preset)->available;
}

bool Race_Physics_ConfigRankable(const race_physics_config_t *config) {
  return Race_Physics_ConfigAvailable(config) &&
         Race_Physics_Preset(config->preset)->rankable;
}

const char *Race_Physics_ConfigRuleset(const race_physics_config_t *config) {
  const race_physics_preset_descriptor_t *preset = config
    ? Race_Physics_Preset(config->preset)
    : NULL;
  if (!Race_Physics_ConfigValid(config) || !preset) {
    return NULL;
  }
  if (config->family != RACE_PHYSICS_FAMILY_Q2 ||
      config->q2_snap_mode == RACE_PHYSICS_Q2_SNAP_NEAREST) {
    return preset->ruleset;
  }
  return config->q2_snap_mode == RACE_PHYSICS_Q2_SNAP_OFF
    ? preset->ruleset_snap_off
    : preset->ruleset_snap_truncate;
}

bool Race_Physics_FixedParamsForPreset(const race_physics_preset_id_t preset,
                                       pm_params_t *params) {
  if (!params) {
    return false;
  }

  const race_physics_preset_descriptor_t *descriptor =
    Race_Physics_Preset(preset);
  const pm_params_t *fixed = descriptor ? descriptor->fixed_params : NULL;
  if (!fixed) {
    return false;
  }

  *params = *fixed;
  return true;
}

bool Race_Physics_ParamsEqual(const pm_params_t *left,
                              const pm_params_t *right) {
  return left && right && left->gravity == right->gravity &&
         !memcmp(&left->gravity_water, &right->gravity_water,
                 sizeof(*left) - offsetof(pm_params_t, gravity_water));
}

bool Race_Physics_ConfigParamsAgree(const race_physics_config_t *config,
                                    const pm_params_t *params) {
  if (!Race_Physics_ConfigAvailable(config) || !params) {
    return false;
  }

  pm_params_t expected;
  if (Race_Physics_FixedParamsForPreset(config->preset, &expected)) {
    return Race_Physics_ParamsEqual(params, &expected);
  }

  // quetoo-common-v1 deliberately retains Quetoo's authoritative, networked
  // movement-cvar model instead of pretending that it is a fixed vector.
  return config->family == RACE_PHYSICS_FAMILY_QUETOO &&
         config->preset == RACE_PHYSICS_PRESET_QUETOO_COMMON_V1;
}

bool Race_Physics_PredictionReady(const race_physics_parse_result_t decoded,
                                  const bool snapshot_valid,
                                  const race_physics_config_t *config,
                                  const pm_params_t *params) {
  return decoded == RACE_PHYSICS_PARSE_OK && snapshot_valid &&
         Race_Physics_ConfigParamsAgree(config, params);
}

static uint64_t Race_Physics_HashBytes(uint64_t hash, uint32_t value,
                                      size_t bytes) {
  for (size_t i = 0; i < bytes; i++) {
    hash ^= value & UINT32_C(0xff);
    hash *= UINT64_C(1099511628211);
    value >>= 8u;
  }
  return hash;
}

static uint64_t Race_Physics_HashFloat(uint64_t hash, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return Race_Physics_HashBytes(hash, bits, sizeof(bits));
}

uint64_t Race_Physics_ParamsHash(const pm_params_t *params) {
  if (!params) {
    return 0u;
  }

  uint64_t hash = UINT64_C(14695981039346656037);
  hash = Race_Physics_HashBytes(hash, (uint16_t) params->gravity,
                               sizeof(params->gravity));
#define RACE_PHYSICS_HASH_FIELD(field) \
  hash = Race_Physics_HashFloat(hash, params->field)
  RACE_PHYSICS_HASH_FIELD(gravity_water);
  RACE_PHYSICS_HASH_FIELD(accel_ground);
  RACE_PHYSICS_HASH_FIELD(accel_ground_slick);
  RACE_PHYSICS_HASH_FIELD(accel_air);
  RACE_PHYSICS_HASH_FIELD(accel_water);
  RACE_PHYSICS_HASH_FIELD(accel_spectator);
  RACE_PHYSICS_HASH_FIELD(accel_ladder);
  RACE_PHYSICS_HASH_FIELD(friction_ground);
  RACE_PHYSICS_HASH_FIELD(friction_ground_slick);
  RACE_PHYSICS_HASH_FIELD(friction_air);
  RACE_PHYSICS_HASH_FIELD(friction_water);
  RACE_PHYSICS_HASH_FIELD(friction_spectator);
  RACE_PHYSICS_HASH_FIELD(friction_ladder);
  RACE_PHYSICS_HASH_FIELD(speed_ground);
  RACE_PHYSICS_HASH_FIELD(speed_air);
  RACE_PHYSICS_HASH_FIELD(speed_water);
  RACE_PHYSICS_HASH_FIELD(speed_ladder);
  RACE_PHYSICS_HASH_FIELD(speed_spectator);
  RACE_PHYSICS_HASH_FIELD(speed_stop);
  RACE_PHYSICS_HASH_FIELD(speed_jump);
  RACE_PHYSICS_HASH_FIELD(speed_ducked);
  RACE_PHYSICS_HASH_FIELD(speed_duck_stand);
  RACE_PHYSICS_HASH_FIELD(speed_water_jump);
#undef RACE_PHYSICS_HASH_FIELD
  return hash;
}

static bool Race_Physics_EncodeValid(
    const race_physics_config_t *config,
    char output[RACE_PHYSICS_CONFIG_STRING_SIZE]) {
  if (!output || !Race_Physics_ConfigValid(config)) {
    return false;
  }
  const race_physics_family_descriptor_t *family =
    Race_Physics_Family(config->family);
  const race_physics_preset_descriptor_t *preset =
    Race_Physics_Preset(config->preset);
  const int32_t length = snprintf(
    output, RACE_PHYSICS_CONFIG_STRING_SIZE, "v%u\\%s\\%s\\%s",
    config->version, family->key, preset->key,
    Race_Physics_Q2SnapModeKey(config->q2_snap_mode));
  return length > 0 && (size_t) length < RACE_PHYSICS_CONFIG_STRING_SIZE;
}

bool Race_Physics_Encode(const race_physics_config_t *config,
                         char output[RACE_PHYSICS_CONFIG_STRING_SIZE]) {
  return Race_Physics_ConfigAvailable(config) &&
         Race_Physics_EncodeValid(config, output);
}

static bool Race_Physics_NextField(const char **cursor,
                                   const char **field, size_t *length) {
  if (!cursor || !*cursor || !field || !length) {
    return false;
  }
  *field = *cursor;
  const char *separator = strchr(*cursor, '\\');
  if (separator) {
    *length = (size_t) (separator - *cursor);
    *cursor = separator + 1;
  } else {
    *length = strlen(*cursor);
    *cursor = NULL;
  }
  return *length > 0;
}

static bool Race_Physics_FieldEquals(const char *field, size_t length,
                                     const char *expected) {
  return strlen(expected) == length && !memcmp(field, expected, length);
}

race_physics_parse_result_t Race_Physics_Decode(
  const char *input, race_physics_config_t *config) {
  if (!input || !config) {
    return RACE_PHYSICS_PARSE_INVALID_ARGUMENT;
  }
  const size_t input_length = strlen(input);
  if (!input_length) {
    return RACE_PHYSICS_PARSE_MISSING;
  }
  if (input_length >= RACE_PHYSICS_CONFIG_STRING_SIZE) {
    return RACE_PHYSICS_PARSE_TOO_LARGE;
  }

  const char *cursor = input;
  const char *version_field, *family_field, *preset_field;
  size_t version_length, family_length, preset_length;
  if (!Race_Physics_NextField(&cursor, &version_field, &version_length) ||
      !Race_Physics_NextField(&cursor, &family_field, &family_length) ||
      !Race_Physics_NextField(&cursor, &preset_field, &preset_length)) {
    return RACE_PHYSICS_PARSE_MALFORMED;
  }

  const bool legacy = Race_Physics_FieldEquals(
    version_field, version_length, "v1");
  if (!legacy && !Race_Physics_FieldEquals(
        version_field, version_length, "v2")) {
    return RACE_PHYSICS_PARSE_UNKNOWN_VERSION;
  }

  const char *snap_mode_field = NULL;
  size_t snap_mode_length = 0u;
  if (legacy) {
    if (cursor) {
      return RACE_PHYSICS_PARSE_MALFORMED;
    }
  } else if (!Race_Physics_NextField(
               &cursor, &snap_mode_field, &snap_mode_length) || cursor) {
    return RACE_PHYSICS_PARSE_MALFORMED;
  }

  const race_physics_family_descriptor_t *family = NULL;
  for (size_t i = 0; i < sizeof(race_physics_families) /
                           sizeof(*race_physics_families); i++) {
    if (Race_Physics_FieldEquals(family_field, family_length,
                                 race_physics_families[i].key)) {
      family = race_physics_families + i;
      break;
    }
  }
  if (!family) {
    return RACE_PHYSICS_PARSE_UNKNOWN_FAMILY;
  }

  const race_physics_preset_descriptor_t *preset = NULL;
  for (size_t i = 0; i < sizeof(race_physics_presets) /
                           sizeof(*race_physics_presets); i++) {
    if (Race_Physics_FieldEquals(preset_field, preset_length,
                                 race_physics_presets[i].key)) {
      preset = race_physics_presets + i;
      break;
    }
  }
  if (!preset) {
    return RACE_PHYSICS_PARSE_UNKNOWN_PRESET;
  }
  if (preset->family != family->id) {
    return RACE_PHYSICS_PARSE_FAMILY_MISMATCH;
  }

  race_physics_q2_snap_mode_t snap_mode;
  if (legacy) {
    snap_mode = family->id == RACE_PHYSICS_FAMILY_Q2
      ? RACE_PHYSICS_Q2_SNAP_NEAREST
      : RACE_PHYSICS_Q2_SNAP_OFF;
  } else if (!Race_Physics_Q2SnapModeForField(
               snap_mode_field, snap_mode_length, &snap_mode)) {
    return RACE_PHYSICS_PARSE_UNKNOWN_SNAP_MODE;
  }
  if (family->id != RACE_PHYSICS_FAMILY_Q2 &&
      snap_mode != RACE_PHYSICS_Q2_SNAP_OFF) {
    return RACE_PHYSICS_PARSE_SNAP_MODE_MISMATCH;
  }

  const race_physics_config_t decoded = {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = family->id,
    .preset = preset->id,
    .q2_snap_mode = snap_mode
  };
  if (!Race_Physics_ConfigAvailable(&decoded)) {
    return RACE_PHYSICS_PARSE_UNAVAILABLE;
  }

  *config = decoded;
  return RACE_PHYSICS_PARSE_OK;
}

const char *Race_Physics_ParseResultName(race_physics_parse_result_t result) {
  switch (result) {
    case RACE_PHYSICS_PARSE_OK:
      return "ok";
    case RACE_PHYSICS_PARSE_INVALID_ARGUMENT:
      return "invalid-argument";
    case RACE_PHYSICS_PARSE_MISSING:
      return "missing";
    case RACE_PHYSICS_PARSE_TOO_LARGE:
      return "too-large";
    case RACE_PHYSICS_PARSE_MALFORMED:
      return "malformed";
    case RACE_PHYSICS_PARSE_UNKNOWN_VERSION:
      return "unknown-version";
    case RACE_PHYSICS_PARSE_UNKNOWN_FAMILY:
      return "unknown-family";
    case RACE_PHYSICS_PARSE_UNKNOWN_PRESET:
      return "unknown-preset";
    case RACE_PHYSICS_PARSE_UNKNOWN_SNAP_MODE:
      return "unknown-snap-mode";
    case RACE_PHYSICS_PARSE_FAMILY_MISMATCH:
      return "family-mismatch";
    case RACE_PHYSICS_PARSE_SNAP_MODE_MISMATCH:
      return "snap-mode-mismatch";
    case RACE_PHYSICS_PARSE_UNAVAILABLE:
      return "unavailable";
    case RACE_PHYSICS_PARSE_PARAMETER_MISMATCH:
      return "parameter-mismatch";
    default:
      return "unknown";
  }
}

void Race_Physics_Reset(void) {
  race_physics_active = race_physics_default;
}

bool Race_Physics_SetActive(const race_physics_config_t *config) {
  if (!Race_Physics_ConfigAvailable(config)) {
    return false;
  }
  race_physics_active = *config;
  return true;
}

void Race_Physics_SetProvider(race_physics_config_provider_t provider) {
  race_physics_provider = provider;
  Race_Physics_Reset();
}

const race_physics_config_t *Race_Physics_Current(void) {
  if (race_physics_provider) {
    race_physics_config_t provided;
    if (race_physics_provider(&provided) == RACE_PHYSICS_PARSE_OK &&
        Race_Physics_ConfigAvailable(&provided)) {
      race_physics_active = provided;
    }
  }
  return &race_physics_active;
}
