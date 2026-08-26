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
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quetoo.h"

static const char *const race_checkpoint_feedback_values[] = {
  "time", "silent"
};

static const char *const race_gameplay_values[] = {
  "default", "deathmatch", "team_deathmatch", "instagib",
  "team_instagib", "arena", "team_arena"
};

static const race_setting_descriptor_t race_settings_catalog[] = {
  {
    .id = RACE_SETTING_FINISH_CUE_ENABLED,
    .alias = "finish_cue_enabled",
    .cvar = "g_race_finish_cue_enabled",
    .map_key = "finish_cue_enabled",
    .type = RACE_SETTING_BOOL,
    .default_value = "1",
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_IMMEDIATE,
    .description = "Play the finish cue after a completed run."
  },
  {
    .id = RACE_SETTING_FINISH_CUE_GAIN,
    .alias = "finish_cue_gain",
    .cvar = "g_race_finish_cue_gain",
    .map_key = "finish_cue_gain",
    .type = RACE_SETTING_INT,
    .default_value = "100",
    .minimum = 1,
    .maximum = 100,
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_IMMEDIATE,
    .description = "Set finish cue gain from 1 through 100."
  },
  {
    .id = RACE_SETTING_CHECKPOINT_FEEDBACK,
    .alias = "checkpoint_feedback",
    .cvar = "g_race_checkpoint_feedback",
    .map_key = "checkpoint_feedback",
    .type = RACE_SETTING_ENUM,
    .default_value = "time",
    .enum_values = race_checkpoint_feedback_values,
    .enum_count = sizeof(race_checkpoint_feedback_values) /
                  sizeof(race_checkpoint_feedback_values[0]),
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_IMMEDIATE,
    .description = "Select checkpoint time or silent feedback."
  },
  {
    .id = RACE_SETTING_VOTING_TIME,
    .alias = "voting_time",
    .cvar = "g_race_voting_time",
    .map_key = "voting_time",
    .type = RACE_SETTING_INT,
    .default_value = "30",
    .minimum = 0,
    .maximum = 300,
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_IMMEDIATE,
    .description = "Set regular vote duration in seconds."
  },
  {
    .id = RACE_SETTING_MAX_VOTES,
    .alias = "max_votes",
    .cvar = "g_race_max_votes",
    .map_key = "max_votes",
    .type = RACE_SETTING_INT,
    .default_value = "3",
    .minimum = 0,
    .maximum = 100,
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_IMMEDIATE,
    .description = "Set the per-client vote limit."
  },
  {
    .id = RACE_SETTING_VOTE_MENU_DURATION,
    .alias = "vote_menu_duration",
    .cvar = "g_race_vote_menu_duration",
    .map_key = "vote_menu_duration",
    .type = RACE_SETTING_INT,
    .default_value = "20",
    .minimum = 0,
    .maximum = 300,
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_IMMEDIATE,
    .description = "Set end-menu vote duration in seconds."
  },
  {
    .id = RACE_SETTING_VOTE_MENU_CHOICES,
    .alias = "vote_menu_choices",
    .cvar = "g_race_vote_menu_choices",
    .map_key = "vote_menu_choices",
    .type = RACE_SETTING_INT,
    .default_value = "3",
    .minimum = 0,
    .maximum = 8,
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_IMMEDIATE,
    .description = "Set the number of end-menu map choices."
  },
  {
    .id = RACE_SETTING_VOTE_ALLOW_SPECTATORS,
    .alias = "vote_allow_spectators",
    .cvar = "g_race_vote_allow_spectators",
    .map_key = "vote_allow_spectators",
    .type = RACE_SETTING_BOOL,
    .default_value = "0",
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_IMMEDIATE,
    .description = "Allow spectators to vote."
  },
  {
    .id = RACE_SETTING_WEAPONS,
    .alias = "weapons",
    .cvar = "g_race_weapons",
    .map_key = "weapons",
    .type = RACE_SETTING_BOOL,
    .default_value = "1",
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_RESTART,
    .description = "Enable weapon inventory for Race players."
  },
  {
    .id = RACE_SETTING_GRAVITY,
    .alias = "gravity",
    .cvar = "g_gravity",
    .map_key = "gravity",
    .type = RACE_SETTING_INT,
    .default_value = "800",
    .minimum = 1,
    .maximum = 32767,
    .cvar_flags = CVAR_SERVER_INFO,
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_IMMEDIATE,
    .description = "Set world gravity."
  },
  {
    .id = RACE_SETTING_GAMEPLAY,
    .alias = "gameplay",
    .cvar = "g_gameplay",
    .map_key = "gameplay",
    .type = RACE_SETTING_ENUM,
    .default_value = "default",
    .enum_values = race_gameplay_values,
    .enum_count = sizeof(race_gameplay_values) /
                  sizeof(race_gameplay_values[0]),
    .cvar_flags = CVAR_SERVER_INFO,
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_RESTART,
    .description = "Select the gameplay mode or use the map default."
  },
  {
    .id = RACE_SETTING_MIN_CLIENTS,
    .alias = "min_clients",
    .cvar = "sv_min_clients",
    .map_key = "min_clients",
    .type = RACE_SETTING_INT,
    .default_value = "0",
    .minimum = 0,
    .maximum = MAX_CLIENTS,
    .cvar_flags = CVAR_SERVER_INFO,
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_IMMEDIATE,
    .description = "Set the minimum client population."
  },
  {
    .id = RACE_SETTING_FRAG_LIMIT,
    .alias = "frag_limit",
    .cvar = "g_frag_limit",
    .map_key = "frag_limit",
    .type = RACE_SETTING_INT,
    .default_value = "30",
    .minimum = 0,
    .maximum = INT32_MAX,
    .cvar_flags = CVAR_SERVER_INFO,
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_IMMEDIATE,
    .description = "Set the frag limit."
  },
  {
    .id = RACE_SETTING_TIME_LIMIT,
    .alias = "time_limit",
    .cvar = "g_time_limit",
    .map_key = "time_limit",
    .type = RACE_SETTING_FLOAT,
    .default_value = "30",
    .minimum = 0,
    .maximum = (double) INT32_MAX / 60000.0,
    .cvar_flags = CVAR_SERVER_INFO,
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_IMMEDIATE,
    .description = "Set the time limit in minutes."
  },
  {
    .id = RACE_SETTING_MUSIC,
    .alias = "music",
    .cvar = "g_music",
    .map_key = "music",
    .type = RACE_SETTING_STRING,
    .default_value = "",
    .map_overridable = true,
    .activation = RACE_SETTING_ACTIVATION_RESTART,
    .description = "Set music tracks or use the map default when empty."
  }
};

static void Race_Settings_Error(char *error, size_t error_size,
                                const char *format, ...) {
  if (!error || !error_size) {
    return;
  }
  va_list args;
  va_start(args, format);
  vsnprintf(error, error_size, format, args);
  va_end(args);
}

static bool Race_Settings_NameValid(const char *name) {
  if (!name || !*name || strlen(name) > RACE_SETTING_NAME_MAX) {
    return false;
  }
  for (const unsigned char *c = (const unsigned char *) name; *c; c++) {
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
          *c == '_')) {
      return false;
    }
  }
  return true;
}

static bool Race_Settings_StringValid(const char *text) {
  if (!text || strlen(text) > RACE_SETTING_VALUE_MAX) {
    return false;
  }
  for (const unsigned char *c = (const unsigned char *) text; *c; c++) {
    if (*c < 32u || *c == 127u) {
      return false;
    }
  }
  return true;
}

static bool Race_Settings_EnumContains(
  const race_setting_descriptor_t *descriptor, const char *text) {
  for (size_t i = 0; descriptor && i < descriptor->enum_count; i++) {
    if (!strcmp(descriptor->enum_values[i], text)) {
      return true;
    }
  }
  return false;
}

const race_setting_descriptor_t *Race_Settings_Catalog(size_t *count) {
  if (count) {
    *count = sizeof(race_settings_catalog) / sizeof(race_settings_catalog[0]);
  }
  return race_settings_catalog;
}

static const race_setting_descriptor_t *Race_Settings_Find(
  const char *name, size_t offset) {
  if (!name || !*name) {
    return NULL;
  }
  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  for (size_t i = 0; i < count; i++) {
    const char *candidate = *(const char *const *)
      ((const uint8_t *) (catalog + i) + offset);
    if (candidate && !strcmp(candidate, name)) {
      return catalog + i;
    }
  }
  return NULL;
}

const race_setting_descriptor_t *Race_Settings_DescriptorForCvar(
  const char *name) {
  return Race_Settings_Find(name, offsetof(race_setting_descriptor_t, cvar));
}

const race_setting_descriptor_t *Race_Settings_DescriptorForMapKey(
  const char *key) {
  return Race_Settings_Find(key, offsetof(race_setting_descriptor_t, map_key));
}

const race_setting_descriptor_t *Race_Settings_DescriptorForName(
  const char *name) {
  const race_setting_descriptor_t *descriptor = Race_Settings_Find(
    name, offsetof(race_setting_descriptor_t, alias));
  return descriptor ? descriptor : Race_Settings_DescriptorForCvar(name);
}

bool Race_Settings_ParseValue(const race_setting_descriptor_t *descriptor,
                              const char *text, race_setting_value_t *value,
                              char *error, size_t error_size) {
  if (!descriptor || !text || !value) {
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
      if (!*text || text[0] == '+' || (text[0] == '0' && text[1]) ||
          (text[0] == '-' && text[1] == '0' && text[2])) {
        Race_Settings_Error(error, error_size, "Integer is not canonical");
        return false;
      }
      errno = 0;
      char *end;
      const long long integer = strtoll(text, &end, 10);
      if (errno == ERANGE || !end || *end || integer < INT32_MIN ||
          integer > INT32_MAX || integer < descriptor->minimum ||
          integer > descriptor->maximum) {
        Race_Settings_Error(error, error_size, "Integer outside setting bounds");
        return false;
      }
      parsed.integer = (int32_t) integer;
      break;
    }

    case RACE_SETTING_FLOAT: {
      if (!*text) {
        Race_Settings_Error(error, error_size, "Missing floating-point value");
        return false;
      }
      errno = 0;
      char *end;
      const double real = strtod(text, &end);
      if (errno == ERANGE || !end || *end || !isfinite(real) ||
          real < descriptor->minimum || real > descriptor->maximum) {
        Race_Settings_Error(error, error_size,
                            "Floating-point value outside setting bounds");
        return false;
      }
      parsed.real = real;
      break;
    }

    case RACE_SETTING_ENUM:
      if (!*text || !Race_Settings_EnumContains(descriptor, text)) {
        Race_Settings_Error(error, error_size, "Unknown setting value");
        return false;
      }
      memcpy(parsed.string, text, strlen(text) + 1u);
      break;

    case RACE_SETTING_STRING:
      if (!Race_Settings_StringValid(text)) {
        Race_Settings_Error(error, error_size, "Invalid setting string");
        return false;
      }
      memcpy(parsed.string, text, strlen(text) + 1u);
      break;
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
  if (!descriptor || !value || !output || !output_size) {
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
    case RACE_SETTING_FLOAT:
      written = snprintf(output, output_size, "%.9g", value->real);
      break;
    case RACE_SETTING_ENUM:
    case RACE_SETTING_STRING:
      written = snprintf(output, output_size, "%s", value->string);
      break;
  }
  return written >= 0 && (size_t) written < output_size;
}

bool Race_Settings_CanonicalizeValue(
  const race_setting_descriptor_t *descriptor, const char *text,
  char *output, size_t output_size, char *error, size_t error_size) {
  race_setting_value_t value;
  if (!Race_Settings_ParseValue(descriptor, text, &value, error, error_size) ||
      !Race_Settings_FormatValue(descriptor, &value, output, output_size)) {
    if (error && error_size && !*error) {
      Race_Settings_Error(error, error_size, "Could not format setting value");
    }
    return false;
  }
  return true;
}

bool Race_Settings_ValidateCatalog(const race_setting_descriptor_t *catalog,
                                   size_t count, char *error,
                                   size_t error_size) {
  if (!catalog || count != RACE_SETTING_TOTAL || count > RACE_SETTINGS_MAX) {
    Race_Settings_Error(error, error_size, "Invalid settings catalog size");
    return false;
  }

  for (size_t i = 0; i < count; i++) {
    const race_setting_descriptor_t *descriptor = catalog + i;
    if (descriptor->id != i || !Race_Settings_NameValid(descriptor->alias) ||
        !Race_Settings_NameValid(descriptor->cvar) ||
        !Race_Settings_NameValid(descriptor->map_key) ||
        !descriptor->default_value || !descriptor->description ||
        descriptor->type > RACE_SETTING_STRING ||
        descriptor->activation > RACE_SETTING_ACTIVATION_NEXT_MAP ||
        !descriptor->map_overridable) {
      Race_Settings_Error(error, error_size,
                          "Invalid settings descriptor metadata");
      return false;
    }
    if (descriptor->type == RACE_SETTING_ENUM &&
        (!descriptor->enum_values || !descriptor->enum_count)) {
      Race_Settings_Error(error, error_size, "Enum setting has no values");
      return false;
    }
    if ((descriptor->type == RACE_SETTING_INT ||
         descriptor->type == RACE_SETTING_FLOAT) &&
        descriptor->minimum > descriptor->maximum) {
      Race_Settings_Error(error, error_size, "Invalid setting bounds");
      return false;
    }

    race_setting_value_t parsed;
    char canonical[RACE_SETTING_VALUE_SIZE];
    if (!Race_Settings_ParseValue(descriptor, descriptor->default_value,
                                  &parsed, error, error_size) ||
        !Race_Settings_FormatValue(descriptor, &parsed,
                                   canonical, sizeof(canonical)) ||
        strcmp(canonical, descriptor->default_value)) {
      Race_Settings_Error(error, error_size,
                          "Setting default is not canonical");
      return false;
    }

    for (size_t prior = 0; prior < i; prior++) {
      if (!strcmp(catalog[prior].alias, descriptor->alias) ||
          !strcmp(catalog[prior].cvar, descriptor->cvar) ||
          !strcmp(catalog[prior].map_key, descriptor->map_key)) {
        Race_Settings_Error(error, error_size, "Duplicate setting identity");
        return false;
      }
    }
  }

  if (error && error_size) {
    error[0] = '\0';
  }
  return true;
}

const char *Race_Settings_TypeName(race_setting_type_t type) {
  switch (type) {
    case RACE_SETTING_BOOL: return "bool";
    case RACE_SETTING_INT: return "int";
    case RACE_SETTING_FLOAT: return "float";
    case RACE_SETTING_ENUM: return "enum";
    case RACE_SETTING_STRING: return "string";
  }
  return "unknown";
}

const char *Race_Settings_ActivationName(race_setting_activation_t activation) {
  switch (activation) {
    case RACE_SETTING_ACTIVATION_IMMEDIATE: return "active now";
    case RACE_SETTING_ACTIVATION_RESTART: return "requires restart";
    case RACE_SETTING_ACTIVATION_NEXT_MAP: return "next map";
  }
  return "unknown";
}
