/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_settings.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RACE_SETTINGS_VERSION_PREFIX "QUETOO_RACE_SETTINGS_V"
#define RACE_SETTINGS_LEGACY_HEADER "// Race admin settings"

typedef struct {
  const char *data;
  size_t length;
} race_settings_span_t;

typedef struct {
  const char *data;
  size_t length;
  size_t position;
} race_settings_reader_t;

typedef struct {
  char *data;
  size_t capacity;
  size_t length;
  bool failed;
} race_settings_writer_t;

static const char *const race_checkpoint_feedback_values[] = {
  "time",
  "silent"
};

static const race_setting_descriptor_t race_settings_catalog[] = {
  {
    .id = RACE_SETTING_FINISH_CUE_ENABLED,
    .key = "finish_cue_enabled",
    .type = RACE_SETTING_BOOL,
    .default_value.boolean = true,
    .scopes = RACE_SETTING_SCOPE_GLOBAL | RACE_SETTING_SCOPE_MAP |
              RACE_SETTING_SCOPE_RUNTIME,
    .persistent = true,
    .map_overridable = true,
    .runtime_mutable = true,
    .owner = RACE_SETTING_OWNER_FINISH_PRESENTATION,
    .ranking_impact = RACE_SETTING_RANKING_COMPATIBLE,
    .wire_impact = RACE_SETTING_WIRE_EXISTING_CHANNEL
  },
  {
    .id = RACE_SETTING_FINISH_CUE_GAIN,
    .key = "finish_cue_gain",
    .type = RACE_SETTING_INT,
    .default_value.integer = 100,
    .minimum = 1,
    .maximum = 100,
    .scopes = RACE_SETTING_SCOPE_GLOBAL | RACE_SETTING_SCOPE_MAP |
              RACE_SETTING_SCOPE_RUNTIME,
    .persistent = true,
    .map_overridable = true,
    .runtime_mutable = true,
    .owner = RACE_SETTING_OWNER_FINISH_PRESENTATION,
    .ranking_impact = RACE_SETTING_RANKING_COMPATIBLE,
    .wire_impact = RACE_SETTING_WIRE_EXISTING_CHANNEL
  },
  {
    .id = RACE_SETTING_CHECKPOINT_FEEDBACK,
    .key = "checkpoint_feedback",
    .type = RACE_SETTING_ENUM,
    .default_value.enumeration = "time",
    .enum_values = race_checkpoint_feedback_values,
    .enum_count = sizeof(race_checkpoint_feedback_values) /
                  sizeof(race_checkpoint_feedback_values[0]),
    .scopes = RACE_SETTING_SCOPE_GLOBAL | RACE_SETTING_SCOPE_MAP |
              RACE_SETTING_SCOPE_RUNTIME,
    .persistent = true,
    .map_overridable = true,
    .runtime_mutable = true,
    .owner = RACE_SETTING_OWNER_CHECKPOINT_PRESENTATION,
    .ranking_impact = RACE_SETTING_RANKING_COMPATIBLE,
    .wire_impact = RACE_SETTING_WIRE_EXISTING_CHANNEL
  },
  {
    .id = RACE_SETTING_VOTING_TIME,
    .key = "voting_time",
    .type = RACE_SETTING_INT,
    .default_value.integer = 30,
    .minimum = 0,
    .maximum = 300,
    .scopes = RACE_SETTING_SCOPE_GLOBAL | RACE_SETTING_SCOPE_MAP,
    .persistent = true,
    .map_overridable = true,
    .runtime_mutable = false,
    .owner = RACE_SETTING_OWNER_VOTE_POLICY,
    .ranking_impact = RACE_SETTING_RANKING_COMPATIBLE,
    .wire_impact = RACE_SETTING_WIRE_EXISTING_CHANNEL
  },
  {
    .id = RACE_SETTING_MAX_VOTES,
    .key = "max_votes",
    .type = RACE_SETTING_INT,
    .default_value.integer = 3,
    .minimum = 0,
    .maximum = 100,
    .scopes = RACE_SETTING_SCOPE_GLOBAL | RACE_SETTING_SCOPE_MAP,
    .persistent = true,
    .map_overridable = true,
    .runtime_mutable = false,
    .owner = RACE_SETTING_OWNER_VOTE_POLICY,
    .ranking_impact = RACE_SETTING_RANKING_COMPATIBLE,
    .wire_impact = RACE_SETTING_WIRE_EXISTING_CHANNEL
  },
  {
    .id = RACE_SETTING_VOTE_MENU_DURATION,
    .key = "vote_menu_duration",
    .type = RACE_SETTING_INT,
    .default_value.integer = 20,
    .minimum = 0,
    .maximum = 300,
    .scopes = RACE_SETTING_SCOPE_GLOBAL | RACE_SETTING_SCOPE_MAP,
    .persistent = true,
    .map_overridable = true,
    .runtime_mutable = false,
    .owner = RACE_SETTING_OWNER_VOTE_POLICY,
    .ranking_impact = RACE_SETTING_RANKING_COMPATIBLE,
    .wire_impact = RACE_SETTING_WIRE_EXISTING_CHANNEL
  },
  {
    .id = RACE_SETTING_VOTE_MENU_CHOICES,
    .key = "vote_menu_choices",
    .type = RACE_SETTING_INT,
    .default_value.integer = 3,
    .minimum = 0,
    .maximum = 8,
    .scopes = RACE_SETTING_SCOPE_GLOBAL | RACE_SETTING_SCOPE_MAP,
    .persistent = true,
    .map_overridable = true,
    .runtime_mutable = false,
    .owner = RACE_SETTING_OWNER_VOTE_POLICY,
    .ranking_impact = RACE_SETTING_RANKING_COMPATIBLE,
    .wire_impact = RACE_SETTING_WIRE_EXISTING_CHANNEL
  },
  {
    .id = RACE_SETTING_VOTE_ALLOW_SPECTATORS,
    .key = "vote_allow_spectators",
    .type = RACE_SETTING_BOOL,
    .default_value.boolean = false,
    .scopes = RACE_SETTING_SCOPE_GLOBAL | RACE_SETTING_SCOPE_MAP,
    .persistent = true,
    .map_overridable = true,
    .runtime_mutable = false,
    .owner = RACE_SETTING_OWNER_VOTE_POLICY,
    .ranking_impact = RACE_SETTING_RANKING_COMPATIBLE,
    .wire_impact = RACE_SETTING_WIRE_EXISTING_CHANNEL
  },
  {
    .id = RACE_SETTING_WEAPONS,
    .key = "weapons",
    .type = RACE_SETTING_BOOL,
    .default_value.boolean = true,
    .scopes = RACE_SETTING_SCOPE_GLOBAL | RACE_SETTING_SCOPE_MAP,
    .persistent = true,
    .map_overridable = true,
    .runtime_mutable = false,
    .next_map = true,
    .owner = RACE_SETTING_OWNER_WEAPON_POLICY,
    .ranking_impact = RACE_SETTING_RANKING_COMPATIBLE,
    .wire_impact = RACE_SETTING_WIRE_NONE
  }
};

static void Race_Settings_Error(char *error, size_t error_size,
                                const char *message) {
  if (error && error_size) {
    snprintf(error, error_size, "%s", message ? message : "Settings error");
  }
}

static bool Race_Settings_BoundedLength(const char *string, size_t maximum,
                                        size_t *length) {
  if (!string) {
    return false;
  }

  size_t len = 0;
  while (len <= maximum && string[len]) {
    len++;
  }

  if (len > maximum) {
    return false;
  }

  if (length) {
    *length = len;
  }
  return true;
}

static bool Race_Settings_KeyValid(const char *key) {
  size_t length;
  if (!Race_Settings_BoundedLength(key, RACE_SETTING_KEY_MAX, &length) ||
      !length || key[0] < 'a' || key[0] > 'z') {
    return false;
  }

  for (size_t i = 0; i < length; i++) {
    const char c = key[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

static bool Race_Settings_EnumValueValid(const char *value) {
  size_t length;
  if (!Race_Settings_BoundedLength(value, RACE_SETTING_ENUM_MAX, &length) ||
      !length) {
    return false;
  }

  for (size_t i = 0; i < length; i++) {
    const char c = value[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

static bool Race_Settings_EnumContains(const race_setting_descriptor_t *descriptor,
                                       const char *value) {
  if (!descriptor || !value) {
    return false;
  }

  for (size_t i = 0; i < descriptor->enum_count; i++) {
    if (descriptor->enum_values[i] && !strcmp(descriptor->enum_values[i], value)) {
      return true;
    }
  }
  return false;
}

static bool Race_Settings_ValueValid(const race_setting_descriptor_t *descriptor,
                                     const race_setting_value_t *value,
                                     char *error, size_t error_size) {
  if (!descriptor || !value) {
    Race_Settings_Error(error, error_size, "Missing setting descriptor or value");
    return false;
  }

  switch (descriptor->type) {
    case RACE_SETTING_BOOL:
      return true;

    case RACE_SETTING_INT:
      if (value->integer < descriptor->minimum ||
          value->integer > descriptor->maximum) {
        Race_Settings_Error(error, error_size, "Integer outside setting bounds");
        return false;
      }
      return true;

    case RACE_SETTING_ENUM:
      if (!Race_Settings_EnumContains(descriptor, value->enumeration)) {
        Race_Settings_Error(error, error_size, "Unknown enum value");
        return false;
      }
      return true;
  }

  Race_Settings_Error(error, error_size, "Unknown setting type");
  return false;
}

const race_setting_descriptor_t *Race_Settings_Catalog(size_t *count) {
  if (count) {
    *count = sizeof(race_settings_catalog) / sizeof(race_settings_catalog[0]);
  }
  return race_settings_catalog;
}

static const race_setting_descriptor_t *Race_Settings_FindDescriptor(
  const race_setting_descriptor_t *catalog, size_t count, const char *key) {
  if (!catalog || !key) {
    return NULL;
  }

  for (size_t i = 0; i < count; i++) {
    if (catalog[i].key && !strcmp(catalog[i].key, key)) {
      return catalog + i;
    }
  }
  return NULL;
}

const race_setting_descriptor_t *Race_Settings_DescriptorForKey(const char *key) {
  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  return Race_Settings_FindDescriptor(catalog, count, key);
}

bool Race_Settings_ValidateCatalog(const race_setting_descriptor_t *catalog,
                                   size_t count, char *error, size_t error_size) {
  const uint32_t valid_scopes = RACE_SETTING_SCOPE_GLOBAL |
                                RACE_SETTING_SCOPE_MAP |
                                RACE_SETTING_SCOPE_RUNTIME;

  if (!catalog || !count || count > RACE_SETTINGS_MAX) {
    Race_Settings_Error(error, error_size, "Invalid settings catalog size");
    return false;
  }

  for (size_t i = 0; i < count; i++) {
    const race_setting_descriptor_t *descriptor = catalog + i;
    if (descriptor->id >= count || !Race_Settings_KeyValid(descriptor->key) ||
        descriptor->type < RACE_SETTING_BOOL || descriptor->type > RACE_SETTING_ENUM ||
        !descriptor->scopes || (descriptor->scopes & ~valid_scopes) ||
        !(descriptor->scopes & RACE_SETTING_SCOPE_GLOBAL) ||
        descriptor->persistent != true ||
        descriptor->map_overridable !=
          !!(descriptor->scopes & RACE_SETTING_SCOPE_MAP) ||
        descriptor->runtime_mutable !=
          !!(descriptor->scopes & RACE_SETTING_SCOPE_RUNTIME) ||
        descriptor->owner < RACE_SETTING_OWNER_FINISH_PRESENTATION ||
        descriptor->owner > RACE_SETTING_OWNER_WEAPON_POLICY ||
        descriptor->ranking_impact < RACE_SETTING_RANKING_COMPATIBLE ||
        descriptor->ranking_impact > RACE_SETTING_RANKING_INVALIDATES_QUETOO_COMMON ||
        descriptor->wire_impact < RACE_SETTING_WIRE_NONE ||
        descriptor->wire_impact > RACE_SETTING_WIRE_EXISTING_CHANNEL) {
      Race_Settings_Error(error, error_size, "Invalid setting descriptor metadata");
      return false;
    }

    if (descriptor->type == RACE_SETTING_INT &&
        descriptor->minimum > descriptor->maximum) {
      Race_Settings_Error(error, error_size, "Invalid integer setting bounds");
      return false;
    }

    if (descriptor->type == RACE_SETTING_ENUM) {
      if (!descriptor->enum_values || !descriptor->enum_count) {
        Race_Settings_Error(error, error_size, "Enum setting has no values");
        return false;
      }
      for (size_t value = 0; value < descriptor->enum_count; value++) {
        if (!Race_Settings_EnumValueValid(descriptor->enum_values[value])) {
          Race_Settings_Error(error, error_size, "Invalid enum setting value");
          return false;
        }
        for (size_t prior = 0; prior < value; prior++) {
          if (!strcmp(descriptor->enum_values[prior], descriptor->enum_values[value])) {
            Race_Settings_Error(error, error_size, "Duplicate enum setting value");
            return false;
          }
        }
      }
    }

    if (!Race_Settings_ValueValid(descriptor, &descriptor->default_value,
                                  error, error_size)) {
      return false;
    }

    for (size_t prior = 0; prior < i; prior++) {
      if (catalog[prior].id == descriptor->id ||
          !strcmp(catalog[prior].key, descriptor->key)) {
        Race_Settings_Error(error, error_size, "Duplicate setting id or key");
        return false;
      }
    }
  }

  if (error && error_size) {
    error[0] = '\0';
  }
  return true;
}

bool Race_Settings_CatalogRankCompatible(const race_setting_descriptor_t *catalog,
                                         size_t count, const char *ruleset,
                                         char *error, size_t error_size) {
  if (!Race_Settings_ValidateCatalog(catalog, count, error, error_size)) {
    return false;
  }
  if (!Race_MapState_RulesetValid(ruleset)) {
    Race_Settings_Error(error, error_size, "Unsupported settings ranking ruleset");
    return false;
  }

  for (size_t i = 0; i < count; i++) {
    if (catalog[i].ranking_impact != RACE_SETTING_RANKING_COMPATIBLE ||
        catalog[i].affects_prediction) {
      Race_Settings_Error(error, error_size,
                          "Setting is incompatible with durable ranking");
      return false;
    }
  }

  if (error && error_size) {
    error[0] = '\0';
  }
  return true;
}

bool Race_Settings_ParseValue(const race_setting_descriptor_t *descriptor,
                              const char *text, race_setting_value_t *value,
                              char *error, size_t error_size) {
  if (!descriptor || !text || !*text || !value) {
    Race_Settings_Error(error, error_size, "Missing setting value");
    return false;
  }

  race_setting_value_t parsed = { 0 };
  switch (descriptor->type) {
    case RACE_SETTING_BOOL:
      if (!strcmp(text, "0")) {
        parsed.boolean = false;
      } else if (!strcmp(text, "1")) {
        parsed.boolean = true;
      } else {
        Race_Settings_Error(error, error_size, "Boolean must be 0 or 1");
        return false;
      }
      break;

    case RACE_SETTING_INT: {
      if ((text[0] == '0' && text[1]) ||
          (text[0] == '-' && text[1] == '0' && text[2]) || text[0] == '+') {
        Race_Settings_Error(error, error_size, "Integer is not canonical");
        return false;
      }

      errno = 0;
      char *end;
      const long integer = strtol(text, &end, 10);
      if (errno == ERANGE || !end || *end || integer < INT32_MIN ||
          integer > INT32_MAX) {
        Race_Settings_Error(error, error_size, "Invalid integer value");
        return false;
      }
      parsed.integer = (int32_t) integer;
      break;
    }

    case RACE_SETTING_ENUM:
      if (!Race_Settings_EnumValueValid(text)) {
        Race_Settings_Error(error, error_size, "Invalid enum value");
        return false;
      }
      memcpy(parsed.enumeration, text, strlen(text) + 1u);
      break;
  }

  if (!Race_Settings_ValueValid(descriptor, &parsed, error, error_size)) {
    return false;
  }

  *value = parsed;
  if (error && error_size) {
    error[0] = '\0';
  }
  return true;
}

bool Race_Settings_FormatValue(const race_setting_descriptor_t *descriptor,
                               const race_setting_value_t *value,
                               char *output, size_t output_size) {
  if (!descriptor || !value || !output || !output_size ||
      !Race_Settings_ValueValid(descriptor, value, NULL, 0)) {
    return false;
  }

  int32_t written = -1;
  switch (descriptor->type) {
    case RACE_SETTING_BOOL:
      written = snprintf(output, output_size, "%d", value->boolean ? 1 : 0);
      break;
    case RACE_SETTING_INT:
      written = snprintf(output, output_size, "%d", value->integer);
      break;
    case RACE_SETTING_ENUM:
      written = snprintf(output, output_size, "%s", value->enumeration);
      break;
  }

  return written >= 0 && (size_t) written < output_size;
}

static bool Race_Settings_ValueEquals(const race_setting_descriptor_t *descriptor,
                                      const race_setting_value_t *left,
                                      const race_setting_value_t *right) {
  if (!descriptor || !left || !right) {
    return false;
  }

  switch (descriptor->type) {
    case RACE_SETTING_BOOL:
      return left->boolean == right->boolean;
    case RACE_SETTING_INT:
      return left->integer == right->integer;
    case RACE_SETTING_ENUM:
      return !strcmp(left->enumeration, right->enumeration);
  }
  return false;
}

static uint32_t Race_Settings_SourceScope(race_setting_source_t source) {
  switch (source) {
    case RACE_SETTING_SOURCE_GLOBAL:
      return RACE_SETTING_SCOPE_GLOBAL;
    case RACE_SETTING_SOURCE_MAP:
      return RACE_SETTING_SCOPE_MAP;
    case RACE_SETTING_SOURCE_RUNTIME:
      return RACE_SETTING_SCOPE_RUNTIME;
    default:
      return 0;
  }
}

static bool Race_Settings_SourceValueEquals(
  const race_setting_descriptor_t *descriptor,
  const race_setting_source_value_t *left,
  const race_setting_source_value_t *right) {
  return left->present == right->present &&
         (!left->present || Race_Settings_ValueEquals(descriptor,
                                                      &left->value,
                                                      &right->value));
}

static bool Race_Settings_ResolveEntry(const race_setting_descriptor_t *descriptor,
                                       const race_setting_source_value_t sources[RACE_SETTING_SOURCE_TOTAL],
                                       race_setting_entry_t *entry,
                                       char *error, size_t error_size) {
  static const race_setting_source_t precedence[] = {
    RACE_SETTING_SOURCE_RUNTIME,
    RACE_SETTING_SOURCE_MAP,
    RACE_SETTING_SOURCE_GLOBAL
  };

  if (!descriptor || !sources || !entry) {
    Race_Settings_Error(error, error_size, "Missing settings resolution input");
    return false;
  }

  race_setting_entry_t resolved = {
    .effective = descriptor->default_value,
    .source = RACE_SETTING_SOURCE_DEFAULT
  };

  for (size_t i = 0; i < sizeof(precedence) / sizeof(precedence[0]); i++) {
    const race_setting_source_t source = precedence[i];
    if (!sources[source].present) {
      continue;
    }
    if (!(descriptor->scopes & Race_Settings_SourceScope(source)) ||
        !Race_Settings_ValueValid(descriptor, &sources[source].value,
                                  error, error_size)) {
      Race_Settings_Error(error, error_size, "Invalid value for setting source");
      return false;
    }
    resolved.effective = sources[source].value;
    resolved.source = source;
    break;
  }

  resolved.differs_from_default = !Race_Settings_ValueEquals(
    descriptor, &resolved.effective, &descriptor->default_value);
  *entry = resolved;
  return true;
}

bool Race_Settings_StateInit(race_settings_state_t *state) {
  if (!state) {
    return false;
  }

  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  char error[128];
  if (count != RACE_SETTING_TOTAL ||
      !Race_Settings_CatalogRankCompatible(catalog, count,
                                           "q2-v1",
                                           error, sizeof(error))) {
    return false;
  }

  memset(state, 0, sizeof(*state));
  state->revision = 1u;
  for (size_t i = 0; i < count; i++) {
    state->entries[i].effective = catalog[i].default_value;
    state->entries[i].source = RACE_SETTING_SOURCE_DEFAULT;
  }
  return true;
}

bool Race_Settings_StateValid(const race_settings_state_t *state) {
  if (!state || !state->revision) {
    return false;
  }

  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  for (size_t i = 0; i < count; i++) {
    if (state->sources[i][RACE_SETTING_SOURCE_DEFAULT].present) {
      return false;
    }
    race_setting_entry_t resolved;
    if (!Race_Settings_ResolveEntry(catalog + i, state->sources[i],
                                    &resolved, NULL, 0) ||
        resolved.source != state->entries[i].source ||
        resolved.differs_from_default != state->entries[i].differs_from_default ||
        !Race_Settings_ValueEquals(catalog + i, &resolved.effective,
                                   &state->entries[i].effective)) {
      return false;
    }
  }
  return true;
}

bool Race_Settings_StateResolve(const race_settings_state_t *current,
                                race_settings_state_t *candidate,
                                race_settings_change_t *change,
                                char *error, size_t error_size) {
  if (change) {
    memset(change, 0, sizeof(*change));
  }
  if (!Race_Settings_StateValid(current) || !candidate) {
    Race_Settings_Error(error, error_size, "Invalid settings state candidate");
    return false;
  }

  race_settings_state_t resolved = *candidate;
  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  bool source_changed = false;
  bool effective_changed = false;

  for (size_t i = 0; i < count; i++) {
    for (race_setting_source_t source = RACE_SETTING_SOURCE_DEFAULT;
         source < RACE_SETTING_SOURCE_TOTAL; source++) {
      if (!Race_Settings_SourceValueEquals(catalog + i,
                                            &current->sources[i][source],
                                            &resolved.sources[i][source])) {
        source_changed = true;
      }
    }

    if (resolved.sources[i][RACE_SETTING_SOURCE_DEFAULT].present ||
        !Race_Settings_ResolveEntry(catalog + i, resolved.sources[i],
                                    &resolved.entries[i], error, error_size)) {
      Race_Settings_Error(error, error_size, "Invalid settings source state");
      return false;
    }

    if (!Race_Settings_ValueEquals(catalog + i,
                                   &current->entries[i].effective,
                                   &resolved.entries[i].effective)) {
      effective_changed = true;
    }
  }

  if (effective_changed && current->revision == UINT64_MAX) {
    Race_Settings_Error(error, error_size, "Settings revision overflow");
    return false;
  }

  resolved.revision = current->revision + (effective_changed ? 1u : 0u);
  if (!Race_Settings_StateValid(&resolved)) {
    Race_Settings_Error(error, error_size, "Resolved settings state is invalid");
    return false;
  }

  *candidate = resolved;
  if (change) {
    change->source_changed = source_changed;
    change->effective_changed = effective_changed;
  }
  if (error && error_size) {
    error[0] = '\0';
  }
  return true;
}

bool Race_Settings_StateSet(const race_settings_state_t *current,
                            race_setting_source_t source,
                            const char *key, const char *text,
                            race_settings_state_t *candidate,
                            race_settings_change_t *change,
                            char *error, size_t error_size) {
  const race_setting_descriptor_t *descriptor = Race_Settings_DescriptorForKey(key);
  const uint32_t scope = Race_Settings_SourceScope(source);
  if (!Race_Settings_StateValid(current) || !descriptor || !text || !candidate ||
      !scope || !(descriptor->scopes & scope)) {
    Race_Settings_Error(error, error_size, "Invalid setting assignment");
    return false;
  }

  race_setting_value_t value;
  if (!Race_Settings_ParseValue(descriptor, text, &value, error, error_size)) {
    return false;
  }

  *candidate = *current;
  candidate->sources[descriptor->id][source] = (race_setting_source_value_t) {
    .present = true,
    .value = value
  };
  return Race_Settings_StateResolve(current, candidate, change,
                                    error, error_size);
}

bool Race_Settings_StateUnset(const race_settings_state_t *current,
                              race_setting_source_t source,
                              const char *key,
                              race_settings_state_t *candidate,
                              race_settings_change_t *change,
                              char *error, size_t error_size) {
  const race_setting_descriptor_t *descriptor = Race_Settings_DescriptorForKey(key);
  const uint32_t scope = Race_Settings_SourceScope(source);
  if (!Race_Settings_StateValid(current) || !descriptor || !candidate ||
      !scope || !(descriptor->scopes & scope)) {
    Race_Settings_Error(error, error_size, "Invalid setting reset");
    return false;
  }

  *candidate = *current;
  memset(&candidate->sources[descriptor->id][source], 0,
         sizeof(candidate->sources[descriptor->id][source]));
  return Race_Settings_StateResolve(current, candidate, change,
                                    error, error_size);
}

bool Race_Settings_DocumentInit(race_settings_document_t *document,
                                race_setting_scope_t scope,
                                const char *map, uint64_t generation) {
  if (!document || (scope != RACE_SETTING_SCOPE_GLOBAL &&
                    scope != RACE_SETTING_SCOPE_MAP)) {
    return false;
  }

  memset(document, 0, sizeof(*document));
  document->scope = scope;
  document->generation = generation;
  if (scope == RACE_SETTING_SCOPE_MAP) {
    if (!Race_MapState_CanonicalizeMap(map, document->map)) {
      memset(document, 0, sizeof(*document));
      return false;
    }
  } else if (map && *map) {
    memset(document, 0, sizeof(*document));
    return false;
  }
  return true;
}

bool Race_Settings_DocumentFromState(race_settings_document_t *document,
                                     race_setting_scope_t scope,
                                     const char *map, uint64_t generation,
                                     const race_settings_state_t *state) {
  if (!Race_Settings_StateValid(state) ||
      !Race_Settings_DocumentInit(document, scope, map, generation)) {
    return false;
  }

  const race_setting_source_t source = scope == RACE_SETTING_SCOPE_GLOBAL
    ? RACE_SETTING_SOURCE_GLOBAL
    : RACE_SETTING_SOURCE_MAP;
  for (size_t i = 0; i < RACE_SETTING_TOTAL; i++) {
    document->values[i] = state->sources[i][source];
  }
  return Race_Settings_DocumentValid(document, false);
}

bool Race_Settings_DocumentValid(const race_settings_document_t *document,
                                 bool require_generation) {
  if (!document || (document->scope != RACE_SETTING_SCOPE_GLOBAL &&
                    document->scope != RACE_SETTING_SCOPE_MAP) ||
      (require_generation && !document->generation)) {
    return false;
  }

  char canonical[RACE_MAP_IDENTITY_SIZE];
  if (document->scope == RACE_SETTING_SCOPE_MAP) {
    if (!Race_MapState_CanonicalizeMap(document->map, canonical) ||
        strcmp(canonical, document->map)) {
      return false;
    }
  } else if (*document->map) {
    return false;
  }

  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  const uint32_t scope = document->scope;
  for (size_t i = 0; i < count; i++) {
    if (!document->values[i].present) {
      continue;
    }
    if (!(catalog[i].scopes & scope) ||
        !Race_Settings_ValueValid(catalog + i, &document->values[i].value,
                                  NULL, 0)) {
      return false;
    }
  }
  return true;
}

bool Race_Settings_DocumentEquals(const race_settings_document_t *left,
                                  const race_settings_document_t *right) {
  if (!Race_Settings_DocumentValid(left, false) ||
      !Race_Settings_DocumentValid(right, false) ||
      left->scope != right->scope || strcmp(left->map, right->map) ||
      left->generation != right->generation) {
    return false;
  }

  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  for (size_t i = 0; i < count; i++) {
    if (!Race_Settings_SourceValueEquals(catalog + i,
                                         &left->values[i],
                                         &right->values[i])) {
      return false;
    }
  }
  return true;
}

bool Race_Settings_DocumentApply(const race_settings_document_t *document,
                                 race_settings_state_t *state) {
  if (!Race_Settings_DocumentValid(document, false) || !state) {
    return false;
  }

  const race_setting_source_t source = document->scope == RACE_SETTING_SCOPE_GLOBAL
    ? RACE_SETTING_SOURCE_GLOBAL
    : RACE_SETTING_SOURCE_MAP;
  for (size_t i = 0; i < RACE_SETTING_TOTAL; i++) {
    state->sources[i][source] = document->values[i];
  }
  return true;
}

bool Race_Settings_Paths(race_setting_scope_t scope, const char *map,
                         char *committed, size_t committed_size,
                         char *candidate, size_t candidate_size) {
  if (!committed || !committed_size || !candidate || !candidate_size) {
    return false;
  }

  int32_t committed_length;
  int32_t candidate_length;
  if (scope == RACE_SETTING_SCOPE_GLOBAL && (!map || !*map)) {
    committed_length = snprintf(committed, committed_size, "%s",
                                RACE_SETTINGS_GLOBAL_COMMITTED);
    candidate_length = snprintf(candidate, candidate_size, "%s",
                                RACE_SETTINGS_GLOBAL_CANDIDATE);
  } else if (scope == RACE_SETTING_SCOPE_MAP) {
    char encoded[RACE_MAP_IDENTITY_ENCODED_SIZE];
    if (!Race_MapState_EncodeMap(map, encoded)) {
      return false;
    }
    committed_length = snprintf(committed, committed_size,
                                RACE_SETTINGS_MAP_DIRECTORY "/%s.settings",
                                encoded);
    candidate_length = snprintf(candidate, candidate_size,
                                RACE_SETTINGS_MAP_DIRECTORY "/%s.candidate",
                                encoded);
  } else {
    return false;
  }

  return committed_length >= 0 && (size_t) committed_length < committed_size &&
         candidate_length >= 0 && (size_t) candidate_length < candidate_size;
}

static void Race_Settings_Write(race_settings_writer_t *writer,
                                const char *format, ...) {
  if (writer->failed) {
    return;
  }

  va_list args;
  va_start(args, format);
  const int32_t length = vsnprintf(writer->data + writer->length,
                                   writer->capacity - writer->length,
                                   format, args);
  va_end(args);
  if (length < 0 || (size_t) length >= writer->capacity - writer->length) {
    writer->failed = true;
    return;
  }
  writer->length += (size_t) length;
}

static uint32_t Race_Settings_Crc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; i++) {
    crc ^= bytes[i];
    for (size_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                          (uint32_t) -(int32_t) (crc & 1u));
    }
  }
  return ~crc;
}

const char *Race_Settings_TypeName(race_setting_type_t type) {
  switch (type) {
    case RACE_SETTING_BOOL:
      return "bool";
    case RACE_SETTING_INT:
      return "int";
    case RACE_SETTING_ENUM:
      return "enum";
  }
  return "unknown";
}

bool Race_Settings_Serialize(const race_settings_document_t *document,
                             char *output, size_t output_size,
                             size_t *output_length) {
  if (!Race_Settings_DocumentValid(document, true) || !output || !output_size) {
    return false;
  }

  const size_t capacity = output_size < RACE_SETTINGS_MAX_FILE_BYTES + 1u
    ? output_size
    : RACE_SETTINGS_MAX_FILE_BYTES + 1u;
  race_settings_writer_t writer = {
    .data = output,
    .capacity = capacity
  };

  char encoded_map[RACE_MAP_IDENTITY_ENCODED_SIZE] = "-";
  if (document->scope == RACE_SETTING_SCOPE_MAP &&
      !Race_MapState_EncodeMap(document->map, encoded_map)) {
    return false;
  }

  size_t value_count = 0;
  for (size_t i = 0; i < RACE_SETTING_TOTAL; i++) {
    value_count += document->values[i].present ? 1u : 0u;
  }

  Race_Settings_Write(&writer,
                      RACE_SETTINGS_MAGIC "\n"
                      "scope=%s\n"
                      "map=%s\n"
                      "generation=%llu\n"
                      "settings=%zu\n",
                      document->scope == RACE_SETTING_SCOPE_GLOBAL
                        ? "global" : "map",
                      encoded_map,
                      (unsigned long long) document->generation,
                      value_count);

  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  for (size_t i = 0; i < count && !writer.failed; i++) {
    if (!document->values[i].present) {
      continue;
    }
    char value[64];
    if (!Race_Settings_FormatValue(catalog + i,
                                   &document->values[i].value,
                                   value, sizeof(value))) {
      return false;
    }
    Race_Settings_Write(&writer, "setting=%s|%s|%s\n",
                        catalog[i].key,
                        Race_Settings_TypeName(catalog[i].type),
                        value);
  }

  if (writer.failed || writer.length > RACE_SETTINGS_MAX_FILE_BYTES) {
    return false;
  }
  const uint32_t checksum = Race_Settings_Crc32(output, writer.length);
  Race_Settings_Write(&writer, "crc=%08x\n", checksum);
  if (writer.failed || writer.length > RACE_SETTINGS_MAX_FILE_BYTES) {
    return false;
  }

  if (output_length) {
    *output_length = writer.length;
  }
  return true;
}

static bool Race_Settings_NextLine(race_settings_reader_t *reader,
                                   race_settings_span_t *line) {
  if (reader->position >= reader->length) {
    return false;
  }
  const size_t start = reader->position;
  while (reader->position < reader->length &&
         reader->data[reader->position] != '\n') {
    reader->position++;
  }
  if (reader->position == reader->length) {
    return false;
  }
  line->data = reader->data + start;
  line->length = reader->position - start;
  reader->position++;
  return true;
}

static bool Race_Settings_SpanEquals(race_settings_span_t span,
                                     const char *string) {
  const size_t length = strlen(string);
  return span.length == length && !memcmp(span.data, string, length);
}

static bool Race_Settings_SpanPrefix(race_settings_span_t span,
                                     const char *prefix,
                                     race_settings_span_t *value) {
  const size_t length = strlen(prefix);
  if (span.length < length || memcmp(span.data, prefix, length)) {
    return false;
  }
  value->data = span.data + length;
  value->length = span.length - length;
  return true;
}

static bool Race_Settings_CopySpan(race_settings_span_t span,
                                   char *output, size_t capacity) {
  if (!capacity || span.length >= capacity) {
    return false;
  }
  memcpy(output, span.data, span.length);
  output[span.length] = '\0';
  return true;
}

static bool Race_Settings_Decimal(race_settings_span_t span,
                                  uint64_t maximum, uint64_t *value) {
  if (!span.length || (span.length > 1 && span.data[0] == '0')) {
    return false;
  }
  uint64_t parsed = 0;
  for (size_t i = 0; i < span.length; i++) {
    if (span.data[i] < '0' || span.data[i] > '9') {
      return false;
    }
    const uint64_t digit = (uint64_t) (span.data[i] - '0');
    if (parsed > (maximum - digit) / 10u) {
      return false;
    }
    parsed = parsed * 10u + digit;
  }
  *value = parsed;
  return true;
}

static int32_t Race_Settings_HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

static bool Race_Settings_DecodeMap(race_settings_span_t span,
                                    char map[RACE_MAP_IDENTITY_SIZE]) {
  if (!span.length || (span.length & 1u) ||
      span.length / 2u > RACE_MAP_IDENTITY_MAX) {
    return false;
  }

  const size_t length = span.length / 2u;
  for (size_t i = 0; i < length; i++) {
    const int32_t high = Race_Settings_HexValue(span.data[i * 2]);
    const int32_t low = Race_Settings_HexValue(span.data[i * 2 + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    map[i] = (char) ((high << 4) | low);
    if (!map[i]) {
      return false;
    }
  }
  map[length] = '\0';

  char canonical[RACE_MAP_IDENTITY_SIZE];
  char encoded[RACE_MAP_IDENTITY_ENCODED_SIZE];
  return Race_MapState_CanonicalizeMap(map, canonical) &&
         !strcmp(map, canonical) &&
         Race_MapState_EncodeMap(map, encoded) &&
         strlen(encoded) == span.length && !memcmp(encoded, span.data, span.length);
}

static bool Race_Settings_SplitSetting(race_settings_span_t value,
                                       race_settings_span_t fields[3]) {
  size_t field = 0;
  size_t start = 0;
  for (size_t i = 0; i <= value.length; i++) {
    if (i == value.length || value.data[i] == '|') {
      if (field >= 3) {
        return false;
      }
      fields[field].data = value.data + start;
      fields[field].length = i - start;
      field++;
      start = i + 1;
    }
  }
  return field == 3;
}

static bool Race_Settings_ParseChecksum(race_settings_span_t span,
                                        uint32_t *checksum) {
  if (span.length != 8) {
    return false;
  }
  uint32_t parsed = 0;
  for (size_t i = 0; i < span.length; i++) {
    const int32_t digit = Race_Settings_HexValue(span.data[i]);
    if (digit < 0) {
      return false;
    }
    parsed = (parsed << 4) | (uint32_t) digit;
  }
  *checksum = parsed;
  return true;
}

race_settings_parse_result_t Race_Settings_Parse(
  const void *data, size_t length,
  race_setting_scope_t expected_scope, const char *expected_map,
  race_settings_document_t *document) {
  if (!data || !document ||
      (expected_scope != RACE_SETTING_SCOPE_GLOBAL &&
       expected_scope != RACE_SETTING_SCOPE_MAP)) {
    return RACE_SETTINGS_PARSE_MALFORMED;
  }
  if (length > RACE_SETTINGS_MAX_FILE_BYTES) {
    return RACE_SETTINGS_PARSE_TOO_LARGE;
  }
  if (!length || memchr(data, '\0', length) || memchr(data, '\r', length)) {
    return RACE_SETTINGS_PARSE_MALFORMED;
  }

  race_settings_reader_t reader = {
    .data = data,
    .length = length
  };
  race_settings_span_t line;
  if (!Race_Settings_NextLine(&reader, &line)) {
    return RACE_SETTINGS_PARSE_MALFORMED;
  }
  if (Race_Settings_SpanEquals(line, RACE_SETTINGS_LEGACY_HEADER)) {
    return RACE_SETTINGS_PARSE_LEGACY_UNSUPPORTED;
  }
  if (!Race_Settings_SpanEquals(line, RACE_SETTINGS_MAGIC)) {
    const size_t prefix_length = sizeof(RACE_SETTINGS_VERSION_PREFIX) - 1u;
    return line.length >= prefix_length &&
           !memcmp(line.data, RACE_SETTINGS_VERSION_PREFIX, prefix_length)
      ? RACE_SETTINGS_PARSE_UNKNOWN_VERSION
      : RACE_SETTINGS_PARSE_MALFORMED;
  }

  race_settings_span_t value;
  if (!Race_Settings_NextLine(&reader, &line) ||
      !Race_Settings_SpanPrefix(line, "scope=", &value)) {
    return RACE_SETTINGS_PARSE_MALFORMED;
  }
  const race_setting_scope_t scope = Race_Settings_SpanEquals(value, "global")
    ? RACE_SETTING_SCOPE_GLOBAL
    : Race_Settings_SpanEquals(value, "map")
      ? RACE_SETTING_SCOPE_MAP
      : 0;
  if (!scope || scope != expected_scope) {
    return RACE_SETTINGS_PARSE_UNSUPPORTED_SCOPE;
  }

  if (!Race_Settings_NextLine(&reader, &line) ||
      !Race_Settings_SpanPrefix(line, "map=", &value)) {
    return RACE_SETTINGS_PARSE_MALFORMED;
  }
  char map[RACE_MAP_IDENTITY_SIZE] = "";
  if (scope == RACE_SETTING_SCOPE_GLOBAL) {
    if (!Race_Settings_SpanEquals(value, "-") || (expected_map && *expected_map)) {
      return RACE_SETTINGS_PARSE_MALFORMED;
    }
  } else {
    char expected[RACE_MAP_IDENTITY_SIZE];
    if (!Race_Settings_DecodeMap(value, map) ||
        !Race_MapState_CanonicalizeMap(expected_map, expected) ||
        strcmp(map, expected)) {
      return RACE_SETTINGS_PARSE_MALFORMED;
    }
  }

  uint64_t generation;
  if (!Race_Settings_NextLine(&reader, &line) ||
      !Race_Settings_SpanPrefix(line, "generation=", &value) ||
      !Race_Settings_Decimal(value, UINT64_MAX, &generation) || !generation) {
    return RACE_SETTINGS_PARSE_MALFORMED;
  }

  uint64_t setting_count;
  if (!Race_Settings_NextLine(&reader, &line) ||
      !Race_Settings_SpanPrefix(line, "settings=", &value) ||
      !Race_Settings_Decimal(value, RACE_SETTING_TOTAL, &setting_count)) {
    return RACE_SETTINGS_PARSE_BOUNDS;
  }

  race_settings_document_t parsed;
  if (!Race_Settings_DocumentInit(&parsed, scope,
                                  scope == RACE_SETTING_SCOPE_MAP ? map : NULL,
                                  generation)) {
    return RACE_SETTINGS_PARSE_MALFORMED;
  }

  bool seen[RACE_SETTING_TOTAL] = { false };
  uint16_t previous_id = 0;
  for (size_t row = 0; row < (size_t) setting_count; row++) {
    if (!Race_Settings_NextLine(&reader, &line) ||
        !Race_Settings_SpanPrefix(line, "setting=", &value)) {
      return RACE_SETTINGS_PARSE_MALFORMED;
    }

    race_settings_span_t fields[3];
    char key[RACE_SETTING_KEY_SIZE];
    char type[16];
    char text[64];
    if (!Race_Settings_SplitSetting(value, fields) ||
        !Race_Settings_CopySpan(fields[0], key, sizeof(key)) ||
        !Race_Settings_CopySpan(fields[1], type, sizeof(type)) ||
        !Race_Settings_CopySpan(fields[2], text, sizeof(text))) {
      return RACE_SETTINGS_PARSE_MALFORMED;
    }

    const race_setting_descriptor_t *descriptor = Race_Settings_DescriptorForKey(key);
    if (!descriptor) {
      return RACE_SETTINGS_PARSE_UNKNOWN_KEY;
    }
    if (seen[descriptor->id]) {
      return RACE_SETTINGS_PARSE_DUPLICATE_KEY;
    }
    if (row && descriptor->id <= previous_id) {
      return RACE_SETTINGS_PARSE_MALFORMED;
    }
    if (strcmp(type, Race_Settings_TypeName(descriptor->type))) {
      return RACE_SETTINGS_PARSE_WRONG_TYPE;
    }
    if (!(descriptor->scopes & scope)) {
      return RACE_SETTINGS_PARSE_UNSUPPORTED_SCOPE;
    }

    race_setting_value_t parsed_value;
    char canonical[64];
    if (!Race_Settings_ParseValue(descriptor, text, &parsed_value, NULL, 0) ||
        !Race_Settings_FormatValue(descriptor, &parsed_value,
                                   canonical, sizeof(canonical)) ||
        strcmp(text, canonical)) {
      return RACE_SETTINGS_PARSE_INVALID_VALUE;
    }

    parsed.values[descriptor->id] = (race_setting_source_value_t) {
      .present = true,
      .value = parsed_value
    };
    seen[descriptor->id] = true;
    previous_id = descriptor->id;
  }

  const size_t crc_offset = reader.position;
  uint32_t expected_crc;
  if (!Race_Settings_NextLine(&reader, &line) ||
      !Race_Settings_SpanPrefix(line, "crc=", &value) ||
      !Race_Settings_ParseChecksum(value, &expected_crc) ||
      reader.position != reader.length) {
    return RACE_SETTINGS_PARSE_MALFORMED;
  }
  if (Race_Settings_Crc32(data, crc_offset) != expected_crc) {
    return RACE_SETTINGS_PARSE_CHECKSUM;
  }
  if (!Race_Settings_DocumentValid(&parsed, true)) {
    return RACE_SETTINGS_PARSE_MALFORMED;
  }

  *document = parsed;
  return RACE_SETTINGS_PARSE_OK;
}

const char *Race_Settings_ParseResultName(race_settings_parse_result_t result) {
  switch (result) {
    case RACE_SETTINGS_PARSE_OK:
      return "ok";
    case RACE_SETTINGS_PARSE_MALFORMED:
      return "malformed";
    case RACE_SETTINGS_PARSE_UNKNOWN_VERSION:
      return "unknown version";
    case RACE_SETTINGS_PARSE_LEGACY_UNSUPPORTED:
      return "legacy unsupported";
    case RACE_SETTINGS_PARSE_UNKNOWN_KEY:
      return "unknown key";
    case RACE_SETTINGS_PARSE_DUPLICATE_KEY:
      return "duplicate key";
    case RACE_SETTINGS_PARSE_WRONG_TYPE:
      return "wrong type";
    case RACE_SETTINGS_PARSE_INVALID_VALUE:
      return "invalid value";
    case RACE_SETTINGS_PARSE_UNSUPPORTED_SCOPE:
      return "unsupported scope";
    case RACE_SETTINGS_PARSE_CHECKSUM:
      return "checksum mismatch";
    case RACE_SETTINGS_PARSE_TOO_LARGE:
      return "too large";
    case RACE_SETTINGS_PARSE_BOUNDS:
      return "bounds exceeded";
  }
  return "unknown";
}

const char *Race_Settings_SourceName(race_setting_source_t source) {
  switch (source) {
    case RACE_SETTING_SOURCE_DEFAULT:
      return "default";
    case RACE_SETTING_SOURCE_GLOBAL:
      return "global";
    case RACE_SETTING_SOURCE_MAP:
      return "map";
    case RACE_SETTING_SOURCE_RUNTIME:
      return "runtime";
    default:
      return "unknown";
  }
}
