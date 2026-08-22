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

#include "race_map_state.h"

#define RACE_SETTINGS_MAGIC "QUETOO_RACE_SETTINGS_V1"
#define RACE_SETTINGS_DIRECTORY "settings"
#define RACE_SETTINGS_MAP_DIRECTORY RACE_SETTINGS_DIRECTORY "/maps"
#define RACE_SETTINGS_GLOBAL_COMMITTED RACE_SETTINGS_DIRECTORY "/global.settings"
#define RACE_SETTINGS_GLOBAL_CANDIDATE RACE_SETTINGS_DIRECTORY "/global.candidate"

#define RACE_SETTINGS_MAX 16u
#define RACE_SETTING_KEY_MAX 47u
#define RACE_SETTING_KEY_SIZE (RACE_SETTING_KEY_MAX + 1u)
#define RACE_SETTING_ENUM_MAX 31u
#define RACE_SETTING_ENUM_SIZE (RACE_SETTING_ENUM_MAX + 1u)
#define RACE_SETTINGS_MAX_FILE_BYTES (64u * 1024u)

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
  RACE_SETTING_TOTAL
} race_setting_id_t;

typedef enum {
  RACE_SETTING_BOOL,
  RACE_SETTING_INT,
  RACE_SETTING_ENUM
} race_setting_type_t;

typedef enum {
  RACE_SETTING_SCOPE_GLOBAL = 1u << 0,
  RACE_SETTING_SCOPE_MAP = 1u << 1,
  RACE_SETTING_SCOPE_RUNTIME = 1u << 2
} race_setting_scope_t;

typedef enum {
  RACE_SETTING_SOURCE_DEFAULT,
  RACE_SETTING_SOURCE_GLOBAL,
  RACE_SETTING_SOURCE_MAP,
  RACE_SETTING_SOURCE_RUNTIME,
  RACE_SETTING_SOURCE_TOTAL
} race_setting_source_t;

typedef enum {
  RACE_SETTING_OWNER_FINISH_PRESENTATION,
  RACE_SETTING_OWNER_CHECKPOINT_PRESENTATION,
  RACE_SETTING_OWNER_VOTE_POLICY,
  RACE_SETTING_OWNER_WEAPON_POLICY
} race_setting_owner_t;

typedef enum {
  RACE_SETTING_RANKING_COMPATIBLE,
  RACE_SETTING_RANKING_INVALIDATES_QUETOO_COMMON
} race_setting_ranking_impact_t;

typedef enum {
  RACE_SETTING_WIRE_NONE,
  RACE_SETTING_WIRE_EXISTING_CHANNEL
} race_setting_wire_impact_t;

typedef union {
  bool boolean;
  int32_t integer;
  char enumeration[RACE_SETTING_ENUM_SIZE];
} race_setting_value_t;

typedef struct {
  uint16_t id;
  const char *key;
  race_setting_type_t type;
  race_setting_value_t default_value;
  int32_t minimum;
  int32_t maximum;
  const char *const *enum_values;
  size_t enum_count;
  uint32_t scopes;
  bool persistent;
  bool map_overridable;
  bool runtime_mutable;
  bool next_map;
  race_setting_owner_t owner;
  race_setting_ranking_impact_t ranking_impact;
  race_setting_wire_impact_t wire_impact;
  bool affects_prediction;
} race_setting_descriptor_t;

typedef struct {
  bool present;
  race_setting_value_t value;
} race_setting_source_value_t;

typedef struct {
  race_setting_value_t effective;
  race_setting_source_t source;
  bool differs_from_default;
} race_setting_entry_t;

typedef struct {
  uint64_t revision;
  race_setting_source_value_t sources[RACE_SETTING_TOTAL][RACE_SETTING_SOURCE_TOTAL];
  race_setting_entry_t entries[RACE_SETTING_TOTAL];
} race_settings_state_t;

typedef struct {
  bool source_changed;
  bool effective_changed;
} race_settings_change_t;

typedef struct {
  race_setting_scope_t scope;
  char map[RACE_MAP_IDENTITY_SIZE];
  uint64_t generation;
  race_setting_source_value_t values[RACE_SETTING_TOTAL];
} race_settings_document_t;

typedef enum {
  RACE_SETTINGS_PARSE_OK,
  RACE_SETTINGS_PARSE_MALFORMED,
  RACE_SETTINGS_PARSE_UNKNOWN_VERSION,
  RACE_SETTINGS_PARSE_LEGACY_UNSUPPORTED,
  RACE_SETTINGS_PARSE_UNKNOWN_KEY,
  RACE_SETTINGS_PARSE_DUPLICATE_KEY,
  RACE_SETTINGS_PARSE_WRONG_TYPE,
  RACE_SETTINGS_PARSE_INVALID_VALUE,
  RACE_SETTINGS_PARSE_UNSUPPORTED_SCOPE,
  RACE_SETTINGS_PARSE_CHECKSUM,
  RACE_SETTINGS_PARSE_TOO_LARGE,
  RACE_SETTINGS_PARSE_BOUNDS
} race_settings_parse_result_t;

const race_setting_descriptor_t *Race_Settings_Catalog(size_t *count);
const race_setting_descriptor_t *Race_Settings_DescriptorForKey(const char *key);

bool Race_Settings_ValidateCatalog(const race_setting_descriptor_t *catalog,
                                   size_t count, char *error, size_t error_size);
bool Race_Settings_CatalogRankCompatible(const race_setting_descriptor_t *catalog,
                                         size_t count, const char *ruleset,
                                         char *error, size_t error_size);
bool Race_Settings_ParseValue(const race_setting_descriptor_t *descriptor,
                              const char *text, race_setting_value_t *value,
                              char *error, size_t error_size);
bool Race_Settings_FormatValue(const race_setting_descriptor_t *descriptor,
                               const race_setting_value_t *value,
                               char *output, size_t output_size);

bool Race_Settings_StateInit(race_settings_state_t *state);
bool Race_Settings_StateValid(const race_settings_state_t *state);
bool Race_Settings_StateResolve(const race_settings_state_t *current,
                                race_settings_state_t *candidate,
                                race_settings_change_t *change,
                                char *error, size_t error_size);
bool Race_Settings_StateSet(const race_settings_state_t *current,
                            race_setting_source_t source,
                            const char *key, const char *text,
                            race_settings_state_t *candidate,
                            race_settings_change_t *change,
                            char *error, size_t error_size);
bool Race_Settings_StateUnset(const race_settings_state_t *current,
                              race_setting_source_t source,
                              const char *key,
                              race_settings_state_t *candidate,
                              race_settings_change_t *change,
                              char *error, size_t error_size);

bool Race_Settings_DocumentInit(race_settings_document_t *document,
                                race_setting_scope_t scope,
                                const char *map, uint64_t generation);
bool Race_Settings_DocumentFromState(race_settings_document_t *document,
                                     race_setting_scope_t scope,
                                     const char *map, uint64_t generation,
                                     const race_settings_state_t *state);
bool Race_Settings_DocumentValid(const race_settings_document_t *document,
                                 bool require_generation);
bool Race_Settings_DocumentEquals(const race_settings_document_t *left,
                                  const race_settings_document_t *right);
bool Race_Settings_DocumentApply(const race_settings_document_t *document,
                                 race_settings_state_t *state);

bool Race_Settings_Paths(race_setting_scope_t scope, const char *map,
                         char *committed, size_t committed_size,
                         char *candidate, size_t candidate_size);
bool Race_Settings_Serialize(const race_settings_document_t *document,
                             char *output, size_t output_size,
                             size_t *output_length);
race_settings_parse_result_t Race_Settings_Parse(
  const void *data, size_t length,
  race_setting_scope_t expected_scope, const char *expected_map,
  race_settings_document_t *document);
const char *Race_Settings_ParseResultName(race_settings_parse_result_t result);
const char *Race_Settings_SourceName(race_setting_source_t source);
const char *Race_Settings_TypeName(race_setting_type_t type);
