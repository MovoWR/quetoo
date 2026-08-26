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

#define RACE_SETTINGS_MAX 16u
#define RACE_SETTING_NAME_MAX 63u
#define RACE_SETTING_NAME_SIZE (RACE_SETTING_NAME_MAX + 1u)
// Parse_Token appends both payload and its terminator through the bounded
// helper, so one MAX_TOKEN_CHARS command token carries 510 setting bytes.
#define RACE_SETTING_VALUE_MAX 510u
#define RACE_SETTING_VALUE_SIZE (RACE_SETTING_VALUE_MAX + 1u)
#define RACE_SETTINGS_MAX_FILE_BYTES (64u * 1024u)

#define RACE_SETTINGS_GSET_COMMITTED "gset.cfg"
#define RACE_SETTINGS_GSET_CANDIDATE "gset.candidate"
#define RACE_SETTINGS_LEGACY_DIRECTORY "settings"

typedef enum {
  RACE_SETTING_FINISH_CUE_ENABLED,
  RACE_SETTING_FINISH_CUE_GAIN,
  RACE_SETTING_CHECKPOINT_FEEDBACK,
  RACE_SETTING_VOTING_TIME,
  RACE_SETTING_MAX_VOTES,
  RACE_SETTING_VOTE_MENU_DURATION,
  RACE_SETTING_VOTE_MENU_CHOICES,
  RACE_SETTING_VOTE_ALLOW_SPECTATORS,
  RACE_SETTING_WEAPONS,
  RACE_SETTING_GRAVITY,
  RACE_SETTING_GAMEPLAY,
  RACE_SETTING_MIN_CLIENTS,
  RACE_SETTING_FRAG_LIMIT,
  RACE_SETTING_TIME_LIMIT,
  RACE_SETTING_MUSIC,

  RACE_SETTING_TOTAL
} race_setting_id_t;

typedef enum {
  RACE_SETTING_BOOL,
  RACE_SETTING_INT,
  RACE_SETTING_FLOAT,
  RACE_SETTING_ENUM,
  RACE_SETTING_STRING
} race_setting_type_t;

typedef enum {
  RACE_SETTING_ACTIVATION_IMMEDIATE,
  RACE_SETTING_ACTIVATION_RESTART,
  RACE_SETTING_ACTIVATION_NEXT_MAP
} race_setting_activation_t;

typedef struct {
  bool boolean;
  int32_t integer;
  double real;
  char string[RACE_SETTING_VALUE_SIZE];
} race_setting_value_t;

typedef struct {
  uint16_t id;
  const char *alias;
  const char *cvar;
  const char *map_key;
  race_setting_type_t type;
  const char *default_value;
  double minimum;
  double maximum;
  const char *const *enum_values;
  size_t enum_count;
  uint32_t cvar_flags;
  bool map_overridable;
  race_setting_activation_t activation;
  const char *description;
} race_setting_descriptor_t;

const race_setting_descriptor_t *Race_Settings_Catalog(size_t *count);
const race_setting_descriptor_t *Race_Settings_DescriptorForName(const char *name);
const race_setting_descriptor_t *Race_Settings_DescriptorForCvar(const char *name);
const race_setting_descriptor_t *Race_Settings_DescriptorForMapKey(const char *key);

bool Race_Settings_ValidateCatalog(const race_setting_descriptor_t *catalog,
                                   size_t count, char *error,
                                   size_t error_size);
bool Race_Settings_ParseValue(const race_setting_descriptor_t *descriptor,
                              const char *text, race_setting_value_t *value,
                              char *error, size_t error_size);
bool Race_Settings_FormatValue(const race_setting_descriptor_t *descriptor,
                               const race_setting_value_t *value,
                               char *output, size_t output_size);
bool Race_Settings_CanonicalizeValue(
  const race_setting_descriptor_t *descriptor, const char *text,
  char *output, size_t output_size, char *error, size_t error_size);

const char *Race_Settings_TypeName(race_setting_type_t type);
const char *Race_Settings_ActivationName(race_setting_activation_t activation);
