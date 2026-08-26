/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "g_local.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "race_admin_service.h"
#include "race_map_properties.h"
#include "race_persistence.h"
#include "race_physics_service.h"
#include "race_settings_service.h"
#include "race_settings_store.h"
#include "race_settings_wire.h"

#define RACE_SETTINGS_DIRECTORY "race"
#define RACE_SETTINGS_MAP_CANDIDATE_SUFFIX ".candidate"
#define RACE_SETTINGS_MAP_BUFFER_SIZE (RACE_MAP_PROPERTIES_MAX_FILE_BYTES + 1u)
#define RACE_SETTINGS_AUDIT_SUBJECT_SIZE 512u

static bool race_settings_catalog_valid;
static bool race_settings_ready;
static cvar_t *race_settings_cvars[RACE_SETTING_TOTAL];
static race_gset_document_t race_settings_gset;
static char race_settings_global_values[RACE_SETTING_TOTAL][RACE_SETTING_VALUE_SIZE];
static race_setting_value_t race_settings_effective[RACE_SETTING_TOTAL];
static bool race_settings_map_overrides[RACE_SETTING_TOTAL];
static bool race_settings_weapons_enabled = true;
static char race_settings_current_map[RACE_MAP_IDENTITY_SIZE];

static bool race_settings_fallback_valid;
static int32_t race_settings_fallback_gravity;
static g_gameplay_id_t race_settings_fallback_gameplay;
static bool race_settings_fallback_teams;
static int32_t race_settings_fallback_frag_limit;
static int32_t race_settings_fallback_time_limit;
static char race_settings_fallback_music[MAX_STRING_CHARS];
static char race_settings_fallback_tracks[MAX_MUSICS][MAX_QPATH];

static void Race_SettingsService_Reply(g_client_t *cl, const char *format, ...) {
  if (!cl || !cl->in_use || !format) {
    return;
  }

  char message[MAX_PRINT_MSG];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  gi.ClientPrint(cl, PRINT_HIGH, "%s", message);
}

static bool Race_SettingsService_IsSensitiveName(const char *name) {
  return name && (!strcmp(name, "rcon_password") ||
                  !strcmp(name, "g_password") ||
                  !strcmp(name, "g_admin_password"));
}

static bool Race_SettingsService_CurrentMap(char map[RACE_MAP_IDENTITY_SIZE]) {
  if (!map || !*g_level.name) {
    return false;
  }

  q_strlcpy(map, g_level.name, RACE_MAP_IDENTITY_SIZE);
  return true;
}

static bool Race_SettingsService_RealPath(const char *virtual_path,
                                          char *real_path,
                                          const size_t real_path_size) {
  if (!virtual_path || !*virtual_path || !real_path || !real_path_size) {
    return false;
  }

  return Race_Persistence_CopyRealPath(
    virtual_path, gi.RealPath(virtual_path), real_path, real_path_size);
}

static bool Race_SettingsService_GlobalPaths(char committed[MAX_OS_PATH],
                                             char candidate[MAX_OS_PATH]) {
  char committed_virtual[MAX_QPATH];
  char candidate_virtual[MAX_QPATH];
  const int32_t committed_written = q_snprintf(
    committed_virtual, sizeof(committed_virtual), "%s/%s",
    RACE_SETTINGS_DIRECTORY, RACE_SETTINGS_GSET_COMMITTED);
  const int32_t candidate_written = q_snprintf(
    candidate_virtual, sizeof(candidate_virtual), "%s/%s",
    RACE_SETTINGS_DIRECTORY, RACE_SETTINGS_GSET_CANDIDATE);
  if (committed_written < 0 ||
      (size_t) committed_written >= sizeof(committed_virtual) ||
      candidate_written < 0 ||
      (size_t) candidate_written >= sizeof(candidate_virtual)) {
    return false;
  }

  return Race_SettingsService_RealPath(
           committed_virtual, committed, MAX_OS_PATH) &&
         Race_SettingsService_RealPath(
           candidate_virtual, candidate, MAX_OS_PATH);
}

static bool Race_SettingsService_DescriptorValue(
  const race_setting_descriptor_t *descriptor, const char *text,
  char value[RACE_SETTING_VALUE_SIZE]) {
  return descriptor && text && value &&
         Race_Settings_CanonicalizeValue(
           descriptor, text, value, RACE_SETTING_VALUE_SIZE, NULL, 0);
}

static bool Race_SettingsService_SetEffective(
  const race_setting_descriptor_t *descriptor, const char *text) {
  if (!descriptor || descriptor->id >= RACE_SETTING_TOTAL || !text) {
    return false;
  }

  race_setting_value_t value;
  if (!Race_Settings_ParseValue(descriptor, text, &value, NULL, 0)) {
    return false;
  }

  race_settings_effective[descriptor->id] = value;
  return true;
}

static bool Race_SettingsService_HasGlobalOverride(
  const race_setting_descriptor_t *descriptor) {
  return descriptor &&
         Race_SettingsStore_Find(&race_settings_gset, descriptor->cvar) != NULL;
}

static const char *Race_SettingsService_Activation(
  const race_setting_descriptor_t *descriptor, const cvar_t *var) {
  if (descriptor) {
    return Race_Settings_ActivationName(descriptor->activation);
  }
  return var && (var->flags & CVAR_LATCH) ? "next map" : "active now";
}

static void Race_SettingsService_Audit(g_client_t *cl,
                                       const race_setting_descriptor_t *descriptor,
                                       const char *cvar, const char *scope,
                                       const char *map, const char *result) {
  char subject[RACE_SETTINGS_AUDIT_SUBJECT_SIZE];
  q_snprintf(subject, sizeof(subject),
             "scope=%s,cvar=%s,map=%s,activation=%s",
             scope ? scope : "unknown", cvar && *cvar ? cvar : "-",
             map && *map ? map : "-",
             Race_SettingsService_Activation(
               descriptor, cvar && *cvar ? gi.GetCvar(cvar) : NULL));
  Race_AdminService_AuditClientAction(
    cl, RACE_ADMIN_ACTION_SERVER_CVAR, subject, result);
}

static void Race_SettingsService_ReplyMutation(
  g_client_t *cl, const race_setting_descriptor_t *descriptor,
  const char *cvar, const char *scope, const char *map, const char *result) {
  Race_SettingsService_Reply(
    cl, "Race configuration: scope=%s cvar=%s map=%s activation=%s result=%s\n",
    scope, cvar, map && *map ? map : "-",
    Race_SettingsService_Activation(
      descriptor, cvar && *cvar ? gi.GetCvar(cvar) : NULL), result);
}

static bool Race_SettingsService_EnsureDescriptorCvars(void) {
  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  if (!race_settings_catalog_valid || count != RACE_SETTING_TOTAL) {
    return false;
  }

  for (size_t i = 0; i < count; i++) {
    const race_setting_descriptor_t *descriptor = catalog + i;
    cvar_t *var = gi.GetCvar(descriptor->cvar);
    if (!var) {
      var = gi.AddCvar(descriptor->cvar, descriptor->default_value,
                       descriptor->cvar_flags, descriptor->description);
    }
    if (!var) {
      G_Warn("Race settings cvar unavailable: %s\n", descriptor->cvar);
      return false;
    }
    race_settings_cvars[descriptor->id] = var;
  }
  return true;
}

static bool Race_SettingsService_ValidateGset(
  const race_gset_document_t *document) {
  if (!document) {
    return false;
  }

  for (size_t i = 0; i < document->count; i++) {
    const race_gset_assignment_t *assignment = document->assignments + i;
    cvar_t *var = gi.GetCvar(assignment->name);
    if (!var || (var->flags & CVAR_NO_SET)) {
      return false;
    }

    const race_setting_descriptor_t *descriptor =
      Race_Settings_DescriptorForCvar(var->name);
    if (descriptor) {
      char canonical[RACE_SETTING_VALUE_SIZE];
      if (!Race_SettingsService_DescriptorValue(
            descriptor, assignment->value, canonical) ||
          strcmp(canonical, assignment->value)) {
        return false;
      }
    }
  }
  return true;
}

static void Race_SettingsService_LoadGset(void) {
  Race_SettingsStore_DocumentInit(&race_settings_gset);

  char committed[MAX_OS_PATH];
  char candidate[MAX_OS_PATH];
  if (!Race_SettingsService_GlobalPaths(committed, candidate)) {
    G_Warn("Race global settings path resolution failed\n");
    return;
  }

  race_gset_document_t document;
  const race_settings_store_result_t loaded =
    Race_SettingsStore_Load(committed, &document);
  if (loaded == RACE_SETTINGS_STORE_MISSING) {
    return;
  }
  if (loaded != RACE_SETTINGS_STORE_OK) {
    G_Warn("Race global settings ignored: %s\n",
           Race_SettingsStore_ResultName(loaded));
    return;
  }
  if (!Race_SettingsService_ValidateGset(&document)) {
    G_Warn("Race global settings ignored: invalid cvar assignment\n");
    return;
  }

  for (size_t i = 0; i < document.count; i++) {
    const race_gset_assignment_t *assignment = document.assignments + i;
    if (!gi.SetCvarString(assignment->name, assignment->value)) {
      G_Warn("Race global settings ignored: cvar assignment failed\n");
      return;
    }
  }

  race_settings_gset = document;
  gi.Print("Race global cvars: source=committed assignments=%zu\n",
           document.count);
}

static void Race_SettingsService_CaptureGlobalValues(void) {
  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);

  for (size_t i = 0; i < count; i++) {
    const race_setting_descriptor_t *descriptor = catalog + i;
    cvar_t *var = race_settings_cvars[descriptor->id];
    char canonical[RACE_SETTING_VALUE_SIZE];
    if (!var || !Race_SettingsService_DescriptorValue(
                  descriptor, var->string, canonical)) {
      q_strlcpy(canonical, descriptor->default_value, sizeof(canonical));
      var = gi.SetCvarString(descriptor->cvar, canonical);
      race_settings_cvars[descriptor->id] = var;
      G_Warn("Race settings reset invalid cvar %s to its default\n",
             descriptor->cvar);
    }

    q_strlcpy(race_settings_global_values[descriptor->id], canonical,
              sizeof(race_settings_global_values[descriptor->id]));
    Race_SettingsService_SetEffective(descriptor, canonical);
  }
  race_settings_weapons_enabled =
    race_settings_effective[RACE_SETTING_WEAPONS].boolean;
}

static void Race_SettingsService_DetectLegacyMapSettings(const char *path,
                                                         void *data) {
  if (path && *path && data) {
    *(bool *) data = true;
  }
}

static void Race_SettingsService_CheckLegacyFiles(void) {
  bool legacy_map_settings = false;
  if (gi.EnumerateFiles) {
    gi.EnumerateFiles("settings/maps/*.settings",
                      Race_SettingsService_DetectLegacyMapSettings,
                      &legacy_map_settings);
  }

  const bool legacy = gi.FileExists("race/settings/global.settings") ||
                      gi.FileExists("settings/global.settings") ||
                      legacy_map_settings;
  if (legacy) {
    G_Warn("Race legacy settings files are inert; migrate to gset and mset\n");
  }
}

static void Race_SettingsService_CaptureMapFallback(void) {
  race_settings_fallback_gravity = g_level.gravity;
  race_settings_fallback_gameplay = g_level.gameplay;
  race_settings_fallback_teams = g_level.teams;
  race_settings_fallback_frag_limit = g_level.frag_limit;
  race_settings_fallback_time_limit = g_level.time_limit;
  q_strlcpy(race_settings_fallback_music, g_level.music,
            sizeof(race_settings_fallback_music));
  for (int32_t i = 0; i < MAX_MUSICS; i++) {
    const char *track = gi.GetConfigString(CS_MUSICS + i);
    q_strlcpy(race_settings_fallback_tracks[i], track ? track : "",
              sizeof(race_settings_fallback_tracks[i]));
  }
  race_settings_fallback_valid = true;
}

static void Race_SettingsService_ObserveGlobalValues(void) {
  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  for (size_t i = 0; i < count; i++) {
    const race_setting_descriptor_t *descriptor = catalog + i;
    if (race_settings_map_overrides[descriptor->id]) {
      continue;
    }

    cvar_t *var = race_settings_cvars[descriptor->id];
    char canonical[RACE_SETTING_VALUE_SIZE];
    if (var && Race_SettingsService_DescriptorValue(
                 descriptor, var->string, canonical)) {
      q_strlcpy(race_settings_global_values[descriptor->id], canonical,
                sizeof(race_settings_global_values[descriptor->id]));
    }
  }
}

static bool Race_SettingsService_SetDescriptor(
  const race_setting_descriptor_t *descriptor, const char *value) {
  if (!descriptor || !value) {
    return false;
  }

  cvar_t *var = gi.SetCvarString(descriptor->cvar, value);
  if (!var) {
    return false;
  }
  race_settings_cvars[descriptor->id] = var;
  return true;
}

static void Race_SettingsService_PublishMusic(const char *music) {
  for (int32_t i = 0; i < MAX_MUSICS; i++) {
    gi.SetConfigString(CS_MUSICS + i, "");
  }

  if (!music || !*music) {
    return;
  }

  char buffer[MAX_STRING_CHARS];
  q_strlcpy(buffer, music, sizeof(buffer));
  int32_t index = 0;
  for (char *track = strtok(buffer, ",");
       track && index < MAX_MUSICS;
       track = strtok(NULL, ",")) {
    while (isspace((unsigned char) *track)) {
      track++;
    }

    char *end = track + strlen(track);
    while (end > track && isspace((unsigned char) end[-1])) {
      *--end = '\0';
    }
    if (*track) {
      gi.SetConfigString(CS_MUSICS + index++, track);
    }
  }
}

static void Race_SettingsService_RestoreFallbackMusic(void) {
  for (int32_t i = 0; i < MAX_MUSICS; i++) {
    gi.SetConfigString(CS_MUSICS + i, race_settings_fallback_tracks[i]);
  }
}

static void Race_SettingsService_ApplyEngineValues(void) {
  if (!race_settings_fallback_valid) {
    return;
  }

  const race_setting_descriptor_t *gravity =
    Race_Settings_DescriptorForCvar("g_gravity");
  const race_setting_descriptor_t *gameplay =
    Race_Settings_DescriptorForCvar("g_gameplay");
  const race_setting_descriptor_t *frag_limit =
    Race_Settings_DescriptorForCvar("g_frag_limit");
  const race_setting_descriptor_t *time_limit =
    Race_Settings_DescriptorForCvar("g_time_limit");
  const race_setting_descriptor_t *music =
    Race_Settings_DescriptorForCvar("g_music");

  if (gravity) {
    cvar_t *var = race_settings_cvars[gravity->id];
    if (var && (race_settings_map_overrides[gravity->id] ||
                Race_SettingsService_HasGlobalOverride(gravity))) {
      g_level.gravity = var->integer;
    } else {
      g_level.gravity = race_settings_fallback_gravity;
    }
    if (var) {
      var->modified = false;
    }
  }

  if (gameplay) {
    cvar_t *var = race_settings_cvars[gameplay->id];
    if (var && (race_settings_map_overrides[gameplay->id] ||
                Race_SettingsService_HasGlobalOverride(gameplay)) &&
        strcmp(var->string, "default")) {
      g_level.gameplay = G_GameplayByName(var->string)->id;
      g_level.teams = (g_level.gameplay & GAMEPLAY_TEAMS) != 0;
    } else {
      g_level.gameplay = race_settings_fallback_gameplay;
      g_level.teams = race_settings_fallback_teams;
    }
    gi.SetConfigString(CS_GAMEPLAY, va("%d", g_level.gameplay));
    G_InitNumTeams();
    if (var) {
      var->modified = false;
    }
  }

  if (frag_limit) {
    cvar_t *var = race_settings_cvars[frag_limit->id];
    if (var && (race_settings_map_overrides[frag_limit->id] ||
                Race_SettingsService_HasGlobalOverride(frag_limit))) {
      g_level.frag_limit = var->integer;
    } else {
      g_level.frag_limit = race_settings_fallback_frag_limit;
    }
    if (var) {
      var->modified = false;
    }
  }

  if (time_limit) {
    cvar_t *var = race_settings_cvars[time_limit->id];
    if (var && (race_settings_map_overrides[time_limit->id] ||
                Race_SettingsService_HasGlobalOverride(time_limit))) {
      const double milliseconds = (double) var->value * 60.0 * 1000.0;
      g_level.time_limit = milliseconds >= (double) INT32_MAX
        ? INT32_MAX
        : milliseconds > 0.0 ? (int32_t) milliseconds : 0;
    } else {
      g_level.time_limit = race_settings_fallback_time_limit;
    }
    if (var) {
      var->modified = false;
    }
  }

  if (music) {
    cvar_t *var = race_settings_cvars[music->id];
    if (var && (race_settings_map_overrides[music->id] ||
                Race_SettingsService_HasGlobalOverride(music)) &&
        *var->string) {
      q_strlcpy(g_level.music, var->string, sizeof(g_level.music));
      Race_SettingsService_PublishMusic(g_level.music);
    } else {
      q_strlcpy(g_level.music, race_settings_fallback_music,
                sizeof(g_level.music));
      Race_SettingsService_RestoreFallbackMusic();
    }
  }

  Race_PhysicsService_RefreshLevelParams();
}

static void Race_SettingsService_RefreshEffective(void) {
  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);

  for (size_t i = 0; i < count; i++) {
    const race_setting_descriptor_t *descriptor = catalog + i;
    cvar_t *var = race_settings_cvars[descriptor->id];
    char canonical[RACE_SETTING_VALUE_SIZE];
    if (!var || !Race_SettingsService_DescriptorValue(
                  descriptor, var->string, canonical)) {
      q_strlcpy(canonical, descriptor->default_value, sizeof(canonical));
      Race_SettingsService_SetDescriptor(descriptor, canonical);
    }
    Race_SettingsService_SetEffective(descriptor, canonical);
  }
  race_settings_weapons_enabled =
    race_settings_effective[RACE_SETTING_WEAPONS].boolean;
}

static bool Race_SettingsService_CatalogPath(
  char path[RACE_MAP_PROPERTIES_MAX_VIRTUAL_PATH + 1u],
  char *error, const size_t error_size) {
  cvar_t *map_list = gi.GetCvar("sv_map_list");
  if (!map_list || !*map_list->string) {
    q_snprintf(error, error_size, "sv_map_list is unavailable");
    return false;
  }

  if (Race_MapProperties_ValidateVirtualPath(
        map_list->string, error, error_size) != RACE_MAP_PROPERTIES_OK) {
    return false;
  }

  q_strlcpy(path, map_list->string,
            RACE_MAP_PROPERTIES_MAX_VIRTUAL_PATH + 1u);
  return true;
}

static bool Race_SettingsService_LoadCatalog(
  const char *path, char contents[RACE_SETTINGS_MAP_BUFFER_SIZE],
  size_t *length, char *error, const size_t error_size) {
  char committed[MAX_OS_PATH];
  if (!Race_SettingsService_RealPath(path, committed, sizeof(committed))) {
    q_snprintf(error, error_size, "could not resolve %s", path);
    return false;
  }

  const race_persistence_result_t writable = Race_Persistence_Read(
    committed, contents, RACE_SETTINGS_MAP_BUFFER_SIZE, length);
  if (writable == RACE_PERSISTENCE_OK) {
    if (*length > RACE_MAP_PROPERTIES_MAX_FILE_BYTES) {
      q_snprintf(error, error_size, "%s is too large", path);
      return false;
    }
    return true;
  }
  if (writable != RACE_PERSISTENCE_NOT_FOUND) {
    q_snprintf(error, error_size, "could not load writable %s: %s", path,
               Race_Persistence_ResultName(writable));
    return false;
  }

  void *packaged = NULL;
  const int64_t packaged_length = gi.LoadFile(path, &packaged);
  if (packaged_length < 0 ||
      (uint64_t) packaged_length > RACE_MAP_PROPERTIES_MAX_FILE_BYTES ||
      (!packaged && packaged_length)) {
    if (packaged) {
      gi.FreeFile(packaged);
    }
    q_snprintf(error, error_size, "could not load %s", path);
    return false;
  }

  *length = (size_t) packaged_length;
  if (*length) {
    memcpy(contents, packaged, *length);
  }
  if (packaged) {
    gi.FreeFile(packaged);
  }
  return true;
}

static bool Race_SettingsService_LoadMapProperties(
  const char *map, race_map_properties_t *properties,
  char *error, const size_t error_size) {
  char path[RACE_MAP_PROPERTIES_MAX_VIRTUAL_PATH + 1u];
  if (!Race_SettingsService_CatalogPath(path, error, error_size)) {
    return false;
  }

  char *contents = malloc(RACE_SETTINGS_MAP_BUFFER_SIZE);
  if (!contents) {
    q_snprintf(error, error_size, "out of memory");
    return false;
  }

  size_t length;
  if (!Race_SettingsService_LoadCatalog(
        path, contents, &length, error, error_size)) {
    free(contents);
    return false;
  }
  const race_map_properties_result_t result = Race_MapProperties_Parse(
    contents, length, map, properties,
    error, error_size);
  free(contents);
  return result == RACE_MAP_PROPERTIES_OK;
}

static void Race_SettingsService_PrepareMap(const char *map) {
  if (!race_settings_ready || !map || !*map) {
    return;
  }

  Race_SettingsService_ObserveGlobalValues();
  memset(race_settings_map_overrides, 0, sizeof(race_settings_map_overrides));

  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  for (size_t i = 0; i < count; i++) {
    const race_setting_descriptor_t *descriptor = catalog + i;
    Race_SettingsService_SetDescriptor(
      descriptor, race_settings_global_values[descriptor->id]);
  }

  race_map_properties_t properties;
  char error[256];
  if (Race_SettingsService_LoadMapProperties(
        map, &properties, error, sizeof(error))) {
    for (size_t i = 0; i < count; i++) {
      const race_setting_descriptor_t *descriptor = catalog + i;
      const race_map_property_t *property =
        properties.properties + descriptor->id;
      if (!property->present) {
        continue;
      }
      if (property->valid && !property->duplicate) {
        if (Race_SettingsService_SetDescriptor(
              descriptor, property->canonical)) {
          race_settings_map_overrides[descriptor->id] = true;
        } else {
          G_Warn("Race map setting ignored: map=%s cvar=%s\n",
                 map, descriptor->cvar);
        }
      } else {
        G_Warn("Race map setting ignored: map=%s cvar=%s invalid-property\n",
               map, descriptor->cvar);
      }
    }
  } else {
    G_Warn("Race map settings unavailable for %s: %s\n", map, error);
  }

  q_strlcpy(race_settings_current_map, map,
            sizeof(race_settings_current_map));
  Race_SettingsService_RefreshEffective();
}

static bool Race_SettingsService_ResolveCvar(
  const char *input, const race_setting_descriptor_t **descriptor_out,
  cvar_t **var_out) {
  if (!input || !*input || !descriptor_out || !var_out) {
    return false;
  }

  const race_setting_descriptor_t *descriptor =
    Race_Settings_DescriptorForName(input);
  const char *canonical = descriptor ? descriptor->cvar : input;
  cvar_t *var = descriptor ? race_settings_cvars[descriptor->id]
                           : gi.GetCvar(canonical);
  if (!var) {
    return false;
  }

  *descriptor_out = descriptor;
  *var_out = var;
  return true;
}

static bool Race_SettingsService_AuthorizeCvar(
  g_client_t *cl, const race_setting_descriptor_t *descriptor, cvar_t *var,
  const char *scope, const char *map) {
  if (!cl || !var) {
    return false;
  }

  if (!Race_AdminService_AuthorizeClientAction(
        cl, RACE_ADMIN_ACTION_SERVER_CVAR)) {
    Race_SettingsService_Audit(
      cl, descriptor, var->name, scope, map, "denied");
    return false;
  }
  if (!Race_AdminService_ClientCvarAllowed(cl, var->name)) {
    Race_SettingsService_Audit(
      cl, descriptor, var->name, scope, map, "not-allowlisted");
    Race_SettingsService_Reply(
      cl, "Race configuration rejected: cvar=%s is not delegated\n", var->name);
    return false;
  }
  if (var->flags & CVAR_NO_SET) {
    Race_SettingsService_Audit(
      cl, descriptor, var->name, scope, map, "write-protected");
    Race_SettingsService_Reply(
      cl, "Race configuration rejected: cvar=%s is write protected\n", var->name);
    return false;
  }
  return true;
}

static bool Race_SettingsService_CommitGlobal(
  const race_gset_document_t *candidate) {
  char committed[MAX_OS_PATH];
  char candidate_path[MAX_OS_PATH];
  if (!candidate || !gi.Mkdir(RACE_SETTINGS_DIRECTORY) ||
      !Race_SettingsService_GlobalPaths(committed, candidate_path)) {
    return false;
  }
  return Race_SettingsStore_Commit(committed, candidate_path, candidate) ==
         RACE_SETTINGS_STORE_OK;
}

static bool Race_SettingsService_SetNative(cvar_t *var, const char *value,
                                           bool *queued) {
  if (!var || !value || !queued) {
    return false;
  }

  cvar_t *updated = gi.SetCvarString(var->name, value);
  if (!updated) {
    return false;
  }
  *queued = updated->latched_string &&
            !strcmp(updated->latched_string, value);
  return *queued || !strcmp(updated->string, value);
}

static void Race_SettingsService_HandleGset(g_client_t *cl) {
  if (gi.Argc() != 3) {
    Race_SettingsService_Reply(cl, "Usage: gset <alias-or-cvar> <value>\n");
    return;
  }

  const race_setting_descriptor_t *descriptor;
  cvar_t *var;
  if (!Race_SettingsService_ResolveCvar(gi.Argv(1), &descriptor, &var)) {
    Race_SettingsService_Reply(cl, "Race configuration rejected: unknown cvar\n");
    return;
  }

  if (!Race_SettingsService_AuthorizeCvar(cl, descriptor, var, "global",
                                          race_settings_current_map)) {
    return;
  }

  char canonical[RACE_SETTING_VALUE_SIZE];
  const char *value = gi.Argv(2);
  if (descriptor) {
    if (!Race_SettingsService_DescriptorValue(descriptor, value, canonical)) {
      Race_SettingsService_Audit(
        cl, descriptor, var->name, "global", race_settings_current_map,
        "invalid-value");
      Race_SettingsService_Reply(
        cl, "Race configuration rejected: invalid value for cvar=%s\n",
        var->name);
      return;
    }
    value = canonical;
  }

  race_gset_document_t candidate;
  if (!Race_SettingsStore_Set(&race_settings_gset, var->name, value,
                              &candidate) ||
      !Race_SettingsService_CommitGlobal(&candidate)) {
    Race_SettingsService_Audit(
      cl, descriptor, var->name, "global", race_settings_current_map,
      "write-failed");
    Race_SettingsService_ReplyMutation(
      cl, descriptor, var->name, "global", race_settings_current_map,
      "write-failed");
    return;
  }

  const race_gset_document_t previous = race_settings_gset;
  race_settings_gset = candidate;

  bool queued = false;
  bool applied = true;
  if (!descriptor || !race_settings_map_overrides[descriptor->id]) {
    applied = Race_SettingsService_SetNative(var, value, &queued);
  }
  if (!applied) {
    Race_SettingsService_CommitGlobal(&previous);
    race_settings_gset = previous;
    Race_SettingsService_Audit(
      cl, descriptor, var->name, "global", race_settings_current_map,
      "native-rejected");
    Race_SettingsService_ReplyMutation(
      cl, descriptor, var->name, "global", race_settings_current_map,
      "native-rejected");
    return;
  }

  if (descriptor) {
    q_strlcpy(race_settings_global_values[descriptor->id], value,
              sizeof(race_settings_global_values[descriptor->id]));
    if (!race_settings_map_overrides[descriptor->id] &&
        descriptor->activation == RACE_SETTING_ACTIVATION_IMMEDIATE) {
      Race_SettingsService_SetEffective(descriptor, value);
      Race_SettingsService_ApplyEngineValues();
    }
    if (descriptor->activation != RACE_SETTING_ACTIVATION_IMMEDIATE) {
      var->modified = false;
    }
  }

  const char *result = queued ? "queued" :
    descriptor && race_settings_map_overrides[descriptor->id]
      ? "persisted-map-override-active" : "persisted";
  Race_SettingsService_Audit(
    cl, descriptor, var->name, "global", race_settings_current_map, result);
  Race_SettingsService_ReplyMutation(
    cl, descriptor, var->name, "global", race_settings_current_map, result);
}

static void Race_SettingsService_HandleGclear(g_client_t *cl) {
  if (gi.Argc() != 2) {
    Race_SettingsService_Reply(cl, "Usage: gclear <alias-or-cvar>\n");
    return;
  }

  const race_setting_descriptor_t *descriptor;
  cvar_t *var;
  if (!Race_SettingsService_ResolveCvar(gi.Argv(1), &descriptor, &var)) {
    Race_SettingsService_Reply(cl, "Race configuration rejected: unknown cvar\n");
    return;
  }
  if (!Race_SettingsService_AuthorizeCvar(cl, descriptor, var, "global",
                                          race_settings_current_map)) {
    return;
  }

  race_gset_document_t candidate;
  if (!Race_SettingsStore_Remove(&race_settings_gset, var->name, &candidate) ||
      !Race_SettingsService_CommitGlobal(&candidate)) {
    Race_SettingsService_Audit(
      cl, descriptor, var->name, "global", race_settings_current_map,
      "write-failed");
    Race_SettingsService_ReplyMutation(
      cl, descriptor, var->name, "global", race_settings_current_map,
      "write-failed");
    return;
  }

  const race_gset_document_t previous = race_settings_gset;
  race_settings_gset = candidate;
  const char *value = descriptor ? descriptor->default_value : var->default_string;
  bool queued = false;
  bool applied = true;
  if (!descriptor || !race_settings_map_overrides[descriptor->id]) {
    applied = Race_SettingsService_SetNative(var, value, &queued);
  }
  if (!applied) {
    Race_SettingsService_CommitGlobal(&previous);
    race_settings_gset = previous;
    Race_SettingsService_Audit(
      cl, descriptor, var->name, "global", race_settings_current_map,
      "native-rejected");
    Race_SettingsService_ReplyMutation(
      cl, descriptor, var->name, "global", race_settings_current_map,
      "native-rejected");
    return;
  }

  if (descriptor) {
    q_strlcpy(race_settings_global_values[descriptor->id], value,
              sizeof(race_settings_global_values[descriptor->id]));
    if (!race_settings_map_overrides[descriptor->id] &&
        descriptor->activation == RACE_SETTING_ACTIVATION_IMMEDIATE) {
      Race_SettingsService_SetEffective(descriptor, value);
      Race_SettingsService_ApplyEngineValues();
    }
    if (descriptor->activation != RACE_SETTING_ACTIVATION_IMMEDIATE) {
      var->modified = false;
    }
  }

  const char *result = queued ? "cleared-queued" :
    descriptor && race_settings_map_overrides[descriptor->id]
      ? "cleared-map-override-active" : "cleared";
  Race_SettingsService_Audit(
    cl, descriptor, var->name, "global", race_settings_current_map, result);
  Race_SettingsService_ReplyMutation(
    cl, descriptor, var->name, "global", race_settings_current_map, result);
}

static void Race_SettingsService_PrintGlobal(
  g_client_t *cl, const race_setting_descriptor_t *descriptor, cvar_t *var) {
  const char *value = var->string;
  if (descriptor) {
    value = race_settings_global_values[descriptor->id];
  }
  Race_SettingsService_Reply(
    cl, "Race global cvar: cvar=%s value=%s activation=%s map_override=%d\n",
    var->name, Race_SettingsService_IsSensitiveName(var->name) ? "<redacted>" : value,
    Race_SettingsService_Activation(descriptor, var),
    descriptor && race_settings_map_overrides[descriptor->id]);
}

static void Race_SettingsService_HandleGget(g_client_t *cl) {
  if (gi.Argc() > 2) {
    Race_SettingsService_Reply(cl, "Usage: gget [alias-or-cvar]\n");
    return;
  }
  if (!Race_AdminService_AuthorizeClientAction(
        cl, RACE_ADMIN_ACTION_SERVER_CVAR)) {
    return;
  }

  if (gi.Argc() == 2) {
    const race_setting_descriptor_t *descriptor;
    cvar_t *var;
    if (!Race_SettingsService_ResolveCvar(gi.Argv(1), &descriptor, &var) ||
        !Race_SettingsService_AuthorizeCvar(
          cl, descriptor, var, "global", race_settings_current_map)) {
      return;
    }
    Race_SettingsService_PrintGlobal(cl, descriptor, var);
    Race_SettingsService_Audit(
      cl, descriptor, var->name, "global", race_settings_current_map, "read");
    return;
  }

  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  Race_SettingsService_Reply(cl, "Race global cvars:\n");
  for (size_t i = 0; i < count; i++) {
    const race_setting_descriptor_t *descriptor = catalog + i;
    cvar_t *var = race_settings_cvars[descriptor->id];
    if (var && Race_AdminService_ClientCvarAllowed(cl, var->name)) {
      Race_SettingsService_PrintGlobal(cl, descriptor, var);
    }
  }
  Race_AdminService_AuditClientAction(
    cl, RACE_ADMIN_ACTION_SERVER_CVAR, "scope=global,cvar=registry", "listed");
}

static bool Race_SettingsService_EnsureCatalogDirectory(const char *path) {
  char directory[RACE_MAP_PROPERTIES_MAX_VIRTUAL_PATH + 1u];
  q_strlcpy(directory, path, sizeof(directory));
  char *separator = strrchr(directory, '/');
  if (!separator) {
    return true;
  }
  *separator = '\0';
  return !*directory || gi.Mkdir(directory);
}

static bool Race_SettingsService_CommitMapEdit(
  const char *map, const race_setting_descriptor_t *descriptor,
  const char *value, char *error, const size_t error_size) {
  char catalog_path[RACE_MAP_PROPERTIES_MAX_VIRTUAL_PATH + 1u];
  if (!Race_SettingsService_CatalogPath(
        catalog_path, error, error_size)) {
    return false;
  }

  char *candidate_data = malloc(RACE_SETTINGS_MAP_BUFFER_SIZE);
  char *verified_data = malloc(RACE_SETTINGS_MAP_BUFFER_SIZE);
  if (!candidate_data || !verified_data) {
    free(candidate_data);
    free(verified_data);
    q_snprintf(error, error_size, "out of memory");
    return false;
  }

  size_t loaded;
  if (!Race_SettingsService_LoadCatalog(
        catalog_path, verified_data, &loaded, error, error_size)) {
    free(candidate_data);
    free(verified_data);
    return false;
  }

  race_map_properties_edit_t edit;
  const race_map_properties_result_t edited = Race_MapProperties_Edit(
    verified_data, loaded, map, descriptor, value,
    candidate_data, RACE_SETTINGS_MAP_BUFFER_SIZE, &edit, error, error_size);
  if (edited != RACE_MAP_PROPERTIES_OK) {
    free(candidate_data);
    free(verified_data);
    return false;
  }

  char candidate_virtual[RACE_MAP_PROPERTIES_MAX_VIRTUAL_PATH +
                         sizeof(RACE_SETTINGS_MAP_CANDIDATE_SUFFIX)];
  const int32_t candidate_written = q_snprintf(
    candidate_virtual, sizeof(candidate_virtual), "%s%s",
    catalog_path, RACE_SETTINGS_MAP_CANDIDATE_SUFFIX);
  char committed[MAX_OS_PATH];
  char candidate[MAX_OS_PATH];
  if (candidate_written < 0 ||
      (size_t) candidate_written >= sizeof(candidate_virtual) ||
      !Race_SettingsService_EnsureCatalogDirectory(catalog_path) ||
      !Race_SettingsService_RealPath(catalog_path, committed, sizeof(committed)) ||
      !Race_SettingsService_RealPath(candidate_virtual, candidate,
                                     sizeof(candidate)) ||
      Race_Persistence_WriteCandidateOwnerOnly(
        candidate, candidate_data, edit.length) != RACE_PERSISTENCE_OK) {
    free(candidate_data);
    free(verified_data);
    q_snprintf(error, error_size, "candidate write failed");
    return false;
  }

  size_t verified_length;
  if (Race_Persistence_Read(candidate, verified_data,
                            RACE_SETTINGS_MAP_BUFFER_SIZE,
                            &verified_length) != RACE_PERSISTENCE_OK ||
      verified_length != edit.length ||
      memcmp(verified_data, candidate_data, edit.length) ||
      Race_MapProperties_ValidateCandidate(
        verified_data, verified_length, map, descriptor, value,
        NULL, error, error_size) != RACE_MAP_PROPERTIES_OK ||
      Race_Persistence_Promote(candidate, committed) != RACE_PERSISTENCE_OK) {
    Race_Persistence_Remove(candidate);
    free(candidate_data);
    free(verified_data);
    if (!*error) {
      q_snprintf(error, error_size, "candidate validation failed");
    }
    return false;
  }

  free(candidate_data);
  free(verified_data);
  return true;
}

static bool Race_SettingsService_ResolveMapDescriptor(
  g_client_t *cl, const char *input,
  const race_setting_descriptor_t **descriptor_out, cvar_t **var_out,
  const char *scope, const char *map) {
  const race_setting_descriptor_t *descriptor;
  cvar_t *var;
  if (!Race_SettingsService_ResolveCvar(input, &descriptor, &var)) {
    Race_SettingsService_Reply(cl, "Race map configuration rejected: unknown cvar\n");
    return false;
  }
  if (!descriptor || !descriptor->map_overridable) {
    Race_SettingsService_Reply(
      cl, "Race map configuration rejected: cvar=%s is not map-overridable\n",
      var->name);
    return false;
  }
  if (!Race_SettingsService_AuthorizeCvar(cl, descriptor, var, scope, map)) {
    return false;
  }
  *descriptor_out = descriptor;
  *var_out = var;
  return true;
}

static void Race_SettingsService_HandleMset(g_client_t *cl) {
  if (gi.Argc() != 3) {
    Race_SettingsService_Reply(cl, "Usage: mset <alias-or-cvar> <value>\n");
    return;
  }

  char map[RACE_MAP_IDENTITY_SIZE];
  if (!Race_SettingsService_CurrentMap(map)) {
    Race_SettingsService_Reply(cl, "Race map configuration rejected: no active map\n");
    return;
  }

  const race_setting_descriptor_t *descriptor;
  cvar_t *var;
  if (!Race_SettingsService_ResolveMapDescriptor(
        cl, gi.Argv(1), &descriptor, &var, "map", map)) {
    return;
  }

  char canonical[RACE_SETTING_VALUE_SIZE];
  if (!Race_SettingsService_DescriptorValue(
        descriptor, gi.Argv(2), canonical)) {
    Race_SettingsService_Audit(
      cl, descriptor, var->name, "map", map, "invalid-value");
    Race_SettingsService_Reply(
      cl, "Race map configuration rejected: invalid value for cvar=%s\n",
      var->name);
    return;
  }

  char error[256] = "";
  if (!Race_SettingsService_CommitMapEdit(
        map, descriptor, canonical, error, sizeof(error))) {
    Race_SettingsService_Audit(
      cl, descriptor, var->name, "map", map, "write-failed");
    Race_SettingsService_ReplyMutation(
      cl, descriptor, var->name, "map", map, "write-failed");
    return;
  }

  const char *result = "persisted-next-map";
  Race_SettingsService_Audit(cl, descriptor, var->name, "map", map, result);
  Race_SettingsService_ReplyMutation(
    cl, descriptor, var->name, "map", map, result);
}

static void Race_SettingsService_HandleMclear(g_client_t *cl) {
  if (gi.Argc() != 2) {
    Race_SettingsService_Reply(cl, "Usage: mclear <alias-or-cvar>\n");
    return;
  }

  char map[RACE_MAP_IDENTITY_SIZE];
  if (!Race_SettingsService_CurrentMap(map)) {
    Race_SettingsService_Reply(cl, "Race map configuration rejected: no active map\n");
    return;
  }

  const race_setting_descriptor_t *descriptor;
  cvar_t *var;
  if (!Race_SettingsService_ResolveMapDescriptor(
        cl, gi.Argv(1), &descriptor, &var, "map", map)) {
    return;
  }

  char error[256] = "";
  if (!Race_SettingsService_CommitMapEdit(
        map, descriptor, NULL, error, sizeof(error))) {
    Race_SettingsService_Audit(
      cl, descriptor, var->name, "map", map, "write-failed");
    Race_SettingsService_ReplyMutation(
      cl, descriptor, var->name, "map", map, "write-failed");
    return;
  }

  const char *result = "cleared-next-map";
  Race_SettingsService_Audit(cl, descriptor, var->name, "map", map, result);
  Race_SettingsService_ReplyMutation(
    cl, descriptor, var->name, "map", map, result);
}

static void Race_SettingsService_PrintMapProperty(
  g_client_t *cl, const char *map, const race_setting_descriptor_t *descriptor,
  const race_map_properties_t *properties) {
  const race_map_property_t *property =
    properties->properties + descriptor->id;
  const char *state = !property->present ? "inherited" :
    property->valid && !property->duplicate ? "override" : "invalid";
  const char *value = property->valid && !property->duplicate
    ? property->canonical : "<none>";
  Race_SettingsService_Reply(
    cl, "Race map cvar: map=%s cvar=%s state=%s value=%s activation=%s\n",
    map, descriptor->cvar, state, value,
    Race_Settings_ActivationName(descriptor->activation));
}

static void Race_SettingsService_HandleMget(g_client_t *cl) {
  if (gi.Argc() > 2) {
    Race_SettingsService_Reply(cl, "Usage: mget [alias-or-cvar]\n");
    return;
  }

  char map[RACE_MAP_IDENTITY_SIZE];
  if (!Race_SettingsService_CurrentMap(map)) {
    Race_SettingsService_Reply(cl, "Race map configuration rejected: no active map\n");
    return;
  }
  if (!Race_AdminService_AuthorizeClientAction(
        cl, RACE_ADMIN_ACTION_SERVER_CVAR)) {
    return;
  }

  race_map_properties_t properties;
  char error[256];
  if (!Race_SettingsService_LoadMapProperties(
        map, &properties, error, sizeof(error))) {
    Race_SettingsService_Reply(
      cl, "Race map configuration unavailable: map=%s\n", map);
    return;
  }

  if (gi.Argc() == 2) {
    const race_setting_descriptor_t *descriptor;
    cvar_t *var;
    if (!Race_SettingsService_ResolveMapDescriptor(
          cl, gi.Argv(1), &descriptor, &var, "map", map)) {
      return;
    }
    Race_SettingsService_PrintMapProperty(cl, map, descriptor, &properties);
    Race_SettingsService_Audit(cl, descriptor, var->name, "map", map, "read");
    return;
  }

  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  Race_SettingsService_Reply(cl, "Race map cvars: map=%s\n", map);
  for (size_t i = 0; i < count; i++) {
    const race_setting_descriptor_t *descriptor = catalog + i;
    cvar_t *var = race_settings_cvars[descriptor->id];
    if (var && Race_AdminService_ClientCvarAllowed(cl, var->name)) {
      Race_SettingsService_PrintMapProperty(cl, map, descriptor, &properties);
    }
  }
  Race_AdminService_AuditClientAction(
    cl, RACE_ADMIN_ACTION_SERVER_CVAR, "scope=map,cvar=registry", "listed");
}

void Race_SettingsService_Init(void) {
  memset(race_settings_cvars, 0, sizeof(race_settings_cvars));
  memset(race_settings_global_values, 0, sizeof(race_settings_global_values));
  memset(race_settings_effective, 0, sizeof(race_settings_effective));
  memset(race_settings_map_overrides, 0, sizeof(race_settings_map_overrides));
  memset(race_settings_current_map, 0, sizeof(race_settings_current_map));
  memset(race_settings_fallback_music, 0, sizeof(race_settings_fallback_music));
  memset(race_settings_fallback_tracks, 0,
         sizeof(race_settings_fallback_tracks));
  race_settings_ready = false;
  race_settings_fallback_valid = false;
  Race_SettingsStore_DocumentInit(&race_settings_gset);

  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  char error[256];
  race_settings_catalog_valid = Race_Settings_ValidateCatalog(
    catalog, count, error, sizeof(error));
  if (!race_settings_catalog_valid) {
    G_Warn("Race settings catalog rejected: %s\n", error);
  }
}

void Race_SettingsService_PostInit(void) {
  if (!race_settings_catalog_valid ||
      !Race_SettingsService_EnsureDescriptorCvars()) {
    return;
  }

  Race_SettingsService_LoadGset();
  Race_SettingsService_CaptureGlobalValues();
  Race_SettingsService_CheckLegacyFiles();
  race_settings_ready = true;
}

/**
 * @brief Publishes both registry stores into CS_RACE_SETTINGS_STATUS.
 * @details The cgame compiles `race_settings.c`, so it already holds the whole
 * catalog. What it cannot reach is state: `cgi.Print` runs module-to-console
 * and nothing carries a `gget` reply back, so without this the menu would have
 * to show catalog defaults and call them current. Publishing the two masks and
 * the values behind them is the read path, and it costs nothing for the rows
 * nobody has assigned.
 */
static void Race_SettingsService_PublishStatus(void) {

  if (!race_settings_ready) {
    return;
  }

  race_settings_status_t status = { 0 };
  q_strlcpy(status.map, race_settings_current_map, sizeof(status.map));

  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  for (size_t i = 0; i < count; i++) {
    const race_setting_descriptor_t *descriptor = catalog + i;
    if (descriptor->id >= RACE_SETTING_TOTAL) {
      continue;
    }
    const uint16_t id = descriptor->id;

    const race_gset_assignment_t *assignment =
      Race_SettingsStore_Find(&race_settings_gset, descriptor->cvar);
    if (assignment) {
      status.global_mask |= 1u << id;
      if (strlen(assignment->value) <= RACE_SETTINGS_STATUS_VALUE_MAX) {
        q_strlcpy(status.global[id], assignment->value,
                  sizeof(status.global[id]));
      } else {
        status.truncated = true;
      }
    }
  }

  // `mset` edits `sv_map_list` and deliberately leaves the running level alone,
  // so `race_settings_map_overrides` - which is what actually applied at level
  // load - goes stale the moment an operator writes one. The menu's MSET tab
  // reports the store, so the store is what gets re-read here.
  race_map_properties_t properties;
  char error[256];
  if (status.map[0] && Race_SettingsService_LoadMapProperties(
        status.map, &properties, error, sizeof(error))) {
    for (size_t i = 0; i < count; i++) {
      const race_setting_descriptor_t *descriptor = catalog + i;
      if (descriptor->id >= RACE_SETTING_TOTAL) {
        continue;
      }
      const uint16_t id = descriptor->id;
      const race_map_property_t *property = properties.properties + id;
      if (!property->present || !property->valid || property->duplicate) {
        continue;
      }
      status.map_mask |= 1u << id;
      if (strlen(property->canonical) <= RACE_SETTINGS_STATUS_VALUE_MAX) {
        q_strlcpy(status.overrides[id], property->canonical,
                  sizeof(status.overrides[id]));
      } else {
        status.truncated = true;
      }
    }
  }

  // Shed the longest value at a time until the payload fits. The row stays
  // assigned in its mask; only the value goes, and `truncated` is what tells the
  // menu to say the value is unknown instead of drawing a default the server is
  // not running.
  char wire[RACE_SETTINGS_STATUS_SIZE];
  while (!Race_Settings_StatusEncode(&status, wire, sizeof(wire))) {

    char *longest = NULL;
    size_t longest_length = 0u;
    for (uint16_t id = 0u; id < RACE_SETTING_TOTAL; id++) {
      char *const candidates[] = { status.global[id], status.overrides[id] };
      for (size_t c = 0; c < lengthof(candidates); c++) {
        const size_t length = strlen(candidates[c]);
        if (length > longest_length) {
          longest = candidates[c];
          longest_length = length;
        }
      }
    }

    if (!longest) {
      // Unreachable with a bounded map name and fifteen descriptors, but a
      // stale mirror would be worse than an empty one.
      memset(&status, 0, sizeof(status));
      status.truncated = true;
      if (!Race_Settings_StatusEncode(&status, wire, sizeof(wire))) {
        return;
      }
      break;
    }

    *longest = '\0';
    status.truncated = true;
  }

  gi.SetConfigString(CS_RACE_SETTINGS_STATUS, wire);
}

void Race_SettingsService_PrepareLevel(const char *map) {
  Race_SettingsService_PrepareMap(map);
}

void Race_SettingsService_FinalizeLevel(const char *map) {
  if (!race_settings_ready || !map || !*map ||
      strcmp(race_settings_current_map, map)) {
    return;
  }

  Race_SettingsService_CaptureMapFallback();
  Race_SettingsService_ApplyEngineValues();
  Race_SettingsService_PublishStatus();
}

void Race_SettingsService_PrintMigrationHint(g_client_t *cl) {
  Race_SettingsService_Reply(
    cl, "Race settings migration: use gset/gget/gclear or mset/mget/mclear; "
    "use allowcvar for Operator delegation.\n");
}

bool Race_SettingsService_ClientCommand(g_client_t *cl, const char *command) {
  if (!cl || !cl->in_use || !command) {
    return false;
  }

  if (!strcmp(command, "gset")) {
    Race_SettingsService_HandleGset(cl);
    Race_SettingsService_PublishStatus();
    return true;
  }
  if (!strcmp(command, "gget")) {
    Race_SettingsService_HandleGget(cl);
    return true;
  }
  if (!strcmp(command, "gclear")) {
    Race_SettingsService_HandleGclear(cl);
    Race_SettingsService_PublishStatus();
    return true;
  }
  if (!strcmp(command, "mset")) {
    Race_SettingsService_HandleMset(cl);
    Race_SettingsService_PublishStatus();
    return true;
  }
  if (!strcmp(command, "mget")) {
    Race_SettingsService_HandleMget(cl);
    return true;
  }
  if (!strcmp(command, "mclear")) {
    Race_SettingsService_HandleMclear(cl);
    Race_SettingsService_PublishStatus();
    return true;
  }
  if (!strcmp(command, "allowcvar")) {
    const int32_t argc = gi.Argc();
    Race_AdminService_ClientCvarAllowlistCommand(
      cl, argc >= 2 && argc <= 3 ? gi.Argv(1) : NULL,
      argc == 3 ? gi.Argv(2) : NULL);
    return true;
  }

  if (!strcmp(command, "race_settings") ||
      (!strcmp(command, "race") && gi.Argc() >= 3 &&
       !strcmp(gi.Argv(1), "admin") &&
       (!strcmp(gi.Argv(2), "settings") || !strcmp(gi.Argv(2), "cvar")))) {
    Race_SettingsService_PrintMigrationHint(cl);
    return true;
  }

  return false;
}

bool Race_SettingsService_EffectiveValue(const race_setting_id_t id,
                                         race_setting_value_t *value) {
  if (!race_settings_ready || id >= RACE_SETTING_TOTAL || !value) {
    return false;
  }
  *value = race_settings_effective[id];
  return true;
}

bool Race_SettingsService_HasMapOverride(const race_setting_id_t id) {
  return race_settings_ready && id < RACE_SETTING_TOTAL &&
         race_settings_map_overrides[id];
}

bool Race_SettingsService_FinishCueEnabled(void) {
  return race_settings_ready &&
         race_settings_effective[RACE_SETTING_FINISH_CUE_ENABLED].boolean;
}

float Race_SettingsService_FinishCueGain(void) {
  return race_settings_ready
    ? (float) race_settings_effective[RACE_SETTING_FINISH_CUE_GAIN].integer / 100.f
    : 1.f;
}

bool Race_SettingsService_CheckpointTimeEnabled(void) {
  return race_settings_ready &&
         !strcmp(race_settings_effective[
                   RACE_SETTING_CHECKPOINT_FEEDBACK].string, "time");
}

int32_t Race_SettingsService_VotingTime(void) {
  return race_settings_ready
    ? race_settings_effective[RACE_SETTING_VOTING_TIME].integer : 30;
}

int32_t Race_SettingsService_MaxVotes(void) {
  return race_settings_ready
    ? race_settings_effective[RACE_SETTING_MAX_VOTES].integer : 3;
}

int32_t Race_SettingsService_VoteMenuDuration(void) {
  return race_settings_ready
    ? race_settings_effective[RACE_SETTING_VOTE_MENU_DURATION].integer : 20;
}

int32_t Race_SettingsService_VoteMenuChoices(void) {
  return race_settings_ready
    ? race_settings_effective[RACE_SETTING_VOTE_MENU_CHOICES].integer : 3;
}

bool Race_SettingsService_VoteAllowSpectators(void) {
  return race_settings_ready &&
         race_settings_effective[RACE_SETTING_VOTE_ALLOW_SPECTATORS].boolean;
}

bool Race_SettingsService_WeaponsEnabled(void) {
  return race_settings_ready ? race_settings_weapons_enabled : true;
}
