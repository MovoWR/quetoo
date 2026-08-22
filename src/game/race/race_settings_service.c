/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_settings_service.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "race_admin_service.h"
#include "race_persistence.h"
#include "race_settings.h"
#include "race_settings_store.h"

typedef enum {
  RACE_SETTINGS_SERVICE_UNAVAILABLE,
  RACE_SETTINGS_SERVICE_READY,
  RACE_SETTINGS_SERVICE_CORRUPT
} race_settings_service_status_t;

static race_settings_state_t race_settings_state;
static race_settings_document_t race_settings_global;
static race_settings_document_t race_settings_map;
static race_settings_service_status_t race_settings_global_status;
static race_settings_service_status_t race_settings_map_status;
static char race_settings_current_map[RACE_MAP_IDENTITY_SIZE];
static bool race_settings_weapons_enabled = true;

static void Race_SettingsService_Reply(g_client_t *cl, const char *format, ...) {
  char message[MAX_PRINT_MSG];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  if (cl) {
    gi.ClientPrint(cl, PRINT_HIGH, "%s", message);
  } else {
    gi.Print("%s", message);
  }
}

static const char *Race_SettingsService_StatusName(
  race_settings_service_status_t status) {
  switch (status) {
    case RACE_SETTINGS_SERVICE_READY:
      return "ready";
    case RACE_SETTINGS_SERVICE_CORRUPT:
      return "corrupt-quarantined";
    default:
      return "unavailable";
  }
}

static bool Race_SettingsService_RealPaths(
  race_setting_scope_t scope, const char *map,
  char committed[MAX_OS_PATH], char candidate[MAX_OS_PATH],
  char committed_virtual[MAX_OS_PATH], char candidate_virtual[MAX_OS_PATH]) {
  if (!Race_Settings_Paths(scope, map,
                           committed_virtual, MAX_OS_PATH,
                           candidate_virtual, MAX_OS_PATH)) {
    return false;
  }

  if (!Race_Persistence_CopyRealPath(committed_virtual,
                                     gi.RealPath(committed_virtual),
                                     committed, MAX_OS_PATH)) {
    return false;
  }

  if (!Race_Persistence_CopyRealPath(candidate_virtual,
                                     gi.RealPath(candidate_virtual),
                                     candidate, MAX_OS_PATH)) {
    return false;
  }
  return true;
}

static race_settings_service_status_t Race_SettingsService_LoadScope(
  race_setting_scope_t scope, const char *map,
  race_settings_document_t *document) {
  Race_Settings_DocumentInit(document, scope, map, 0);

  char committed[MAX_OS_PATH];
  char candidate[MAX_OS_PATH];
  char committed_virtual[MAX_OS_PATH];
  char candidate_virtual[MAX_OS_PATH];
  if (!Race_SettingsService_RealPaths(scope, map,
                                      committed, candidate,
                                      committed_virtual, candidate_virtual)) {
    G_Warn("Could not resolve Race settings paths for scope %s\n",
           scope == RACE_SETTING_SCOPE_GLOBAL ? "global" : "map");
    return RACE_SETTINGS_SERVICE_UNAVAILABLE;
  }

  race_settings_parse_result_t parse_result = RACE_SETTINGS_PARSE_OK;
  const race_settings_store_result_t load = Race_SettingsStore_Load(
    committed, scope, map, document, &parse_result);
  if (load == RACE_SETTINGS_STORE_OK || load == RACE_SETTINGS_STORE_MISSING) {
    gi.Print("Race settings source: scope=%s status=ready source=%s generation=%llu%s%s\n",
             scope == RACE_SETTING_SCOPE_GLOBAL ? "global" : "map",
             load == RACE_SETTINGS_STORE_OK ? "committed" : "empty",
             (unsigned long long) document->generation,
             scope == RACE_SETTING_SCOPE_MAP ? " map=" : "",
             scope == RACE_SETTING_SCOPE_MAP ? document->map : "");
    if (gi.FileExists(candidate_virtual)) {
      gi.Print("Race settings source: scope=%s stale candidate ignored%s%s\n",
               scope == RACE_SETTING_SCOPE_GLOBAL ? "global" : "map",
               scope == RACE_SETTING_SCOPE_MAP ? " map=" : "",
               scope == RACE_SETTING_SCOPE_MAP ? document->map : "");
    }
    return RACE_SETTINGS_SERVICE_READY;
  }

  if (load == RACE_SETTINGS_STORE_CORRUPT) {
    G_Warn("Race settings source quarantined: scope=%s%s%s result=%s parse=%s; committed data was left unchanged\n",
           scope == RACE_SETTING_SCOPE_GLOBAL ? "global" : "map",
           scope == RACE_SETTING_SCOPE_MAP ? " map=" : "",
           scope == RACE_SETTING_SCOPE_MAP ? map : "",
           Race_SettingsStore_ResultName(load),
           Race_Settings_ParseResultName(parse_result));
    return RACE_SETTINGS_SERVICE_CORRUPT;
  }

  G_Warn("Race settings source unavailable: scope=%s%s%s result=%s\n",
         scope == RACE_SETTING_SCOPE_GLOBAL ? "global" : "map",
         scope == RACE_SETTING_SCOPE_MAP ? " map=" : "",
         scope == RACE_SETTING_SCOPE_MAP ? map : "",
         Race_SettingsStore_ResultName(load));
  return RACE_SETTINGS_SERVICE_UNAVAILABLE;
}

static void Race_SettingsService_PrintEntry(
  g_client_t *cl, const race_setting_descriptor_t *descriptor) {
  if (!descriptor) {
    Race_SettingsService_Reply(cl, "Unknown Race setting\n");
    return;
  }

  const race_setting_entry_t *entry = &race_settings_state.entries[descriptor->id];
  char value[64];
  if (!Race_Settings_FormatValue(descriptor, &entry->effective,
                                 value, sizeof(value))) {
    q_strlcpy(value, "<invalid>", sizeof(value));
  }
  Race_SettingsService_Reply(
    cl, "  %s=%s type=%s source=%s default=%s ranking=quetoo-common-v1-compatible\n",
    descriptor->key, value, Race_Settings_TypeName(descriptor->type),
    Race_Settings_SourceName(entry->source),
    entry->differs_from_default ? "no" : "yes");
}

static void Race_SettingsService_PrintList(void) {
  gi.Print("Race settings: revision=%llu map=%s global_generation=%llu map_generation=%llu global_status=%s map_status=%s\n",
           (unsigned long long) race_settings_state.revision,
           *race_settings_current_map ? race_settings_current_map : "unavailable",
           (unsigned long long) race_settings_global.generation,
           (unsigned long long) race_settings_map.generation,
           Race_SettingsService_StatusName(race_settings_global_status),
           Race_SettingsService_StatusName(race_settings_map_status));

  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  for (size_t i = 0; i < count; i++) {
    Race_SettingsService_PrintEntry(NULL, catalog + i);
  }
}

static void Race_SettingsService_PrintSource(g_client_t *cl, const char *key) {
  const race_setting_descriptor_t *descriptor = Race_Settings_DescriptorForKey(key);
  if (!descriptor) {
    Race_SettingsService_Reply(cl, "Unknown Race setting: %s\n",
                               key ? key : "<null>");
    return;
  }

  Race_SettingsService_PrintEntry(cl, descriptor);
  for (race_setting_source_t source = RACE_SETTING_SOURCE_GLOBAL;
       source < RACE_SETTING_SOURCE_TOTAL; source++) {
    const race_setting_source_value_t *source_value =
      &race_settings_state.sources[descriptor->id][source];
    char value[64] = "<unset>";
    if (source_value->present) {
      Race_Settings_FormatValue(descriptor, &source_value->value,
                                value, sizeof(value));
    }
    Race_SettingsService_Reply(cl, "    %s=%s\n",
                               Race_Settings_SourceName(source), value);
  }
}

static bool Race_SettingsService_ParseSource(const char *name,
                                             race_setting_source_t *source) {
  if (!strcmp(name, "global")) {
    *source = RACE_SETTING_SOURCE_GLOBAL;
  } else if (!strcmp(name, "map")) {
    *source = RACE_SETTING_SOURCE_MAP;
  } else if (!strcmp(name, "runtime")) {
    *source = RACE_SETTING_SOURCE_RUNTIME;
  } else {
    return false;
  }
  return true;
}

static bool Race_SettingsService_PersistentReady(
  race_setting_source_t source,
  race_settings_document_t **document,
  race_settings_service_status_t **status) {
  if (source == RACE_SETTING_SOURCE_GLOBAL) {
    *document = &race_settings_global;
    *status = &race_settings_global_status;
  } else if (source == RACE_SETTING_SOURCE_MAP &&
             *race_settings_current_map) {
    *document = &race_settings_map;
    *status = &race_settings_map_status;
  } else {
    return false;
  }
  return **status == RACE_SETTINGS_SERVICE_READY;
}

static bool Race_SettingsService_Commit(
  race_setting_source_t source,
  const race_settings_state_t *candidate_state,
  g_client_t *cl) {
  race_settings_document_t *current_document;
  race_settings_service_status_t *status;
  if (!Race_SettingsService_PersistentReady(source,
                                            &current_document, &status)) {
    Race_SettingsService_Reply(
      cl, "Race settings mutation rejected: source is unavailable or quarantined\n");
    return false;
  }
  if (current_document->generation == UINT64_MAX) {
    Race_SettingsService_Reply(
      cl, "Race settings mutation rejected: source generation overflow\n");
    return false;
  }

  const race_setting_scope_t scope = source == RACE_SETTING_SOURCE_GLOBAL
    ? RACE_SETTING_SCOPE_GLOBAL
    : RACE_SETTING_SCOPE_MAP;
  const char *map = scope == RACE_SETTING_SCOPE_MAP
    ? race_settings_current_map
    : NULL;
  race_settings_document_t candidate_document;
  if (!Race_Settings_DocumentFromState(&candidate_document, scope, map,
                                       current_document->generation + 1u,
                                       candidate_state)) {
    Race_SettingsService_Reply(
      cl, "Race settings mutation rejected: invalid persisted document\n");
    return false;
  }

  char committed[MAX_OS_PATH];
  char candidate[MAX_OS_PATH];
  char committed_virtual[MAX_OS_PATH];
  char candidate_virtual[MAX_OS_PATH];
  if (!Race_SettingsService_RealPaths(scope, map,
                                      committed, candidate,
                                      committed_virtual, candidate_virtual)) {
    Race_SettingsService_Reply(
      cl, "Race settings mutation rejected: could not resolve persistence paths\n");
    return false;
  }

  race_settings_parse_result_t parse_result = RACE_SETTINGS_PARSE_OK;
  const race_settings_store_result_t persisted = Race_SettingsStore_Commit(
    committed, candidate, &candidate_document, &parse_result);
  if (persisted != RACE_SETTINGS_STORE_OK) {
    Race_SettingsService_Reply(
      cl, "Race settings mutation rejected: persistence=%s parse=%s; effective state unchanged\n",
      Race_SettingsStore_ResultName(persisted),
      Race_Settings_ParseResultName(parse_result));
    return false;
  }

  *current_document = candidate_document;
  *status = RACE_SETTINGS_SERVICE_READY;
  return true;
}

static bool Race_SettingsService_Mutate(g_client_t *cl, bool reset,
                                        race_setting_source_t source,
                                        const char *key, const char *value) {
  race_settings_state_t candidate;
  race_settings_change_t change;
  char error[128];
  const bool valid = reset
    ? Race_Settings_StateUnset(&race_settings_state, source, key,
                               &candidate, &change, error, sizeof(error))
    : Race_Settings_StateSet(&race_settings_state, source, key, value,
                             &candidate, &change,
                             error, sizeof(error));
  if (!valid) {
    Race_SettingsService_Reply(cl, "Race settings mutation rejected: %s\n",
                               error);
    return false;
  }

  const race_setting_descriptor_t *descriptor =
    Race_Settings_DescriptorForKey(key);
  if (!change.source_changed) {
    Race_SettingsService_Reply(
      cl, "Race settings mutation: no-op revision=%llu\n",
      (unsigned long long) race_settings_state.revision);
    Race_SettingsService_PrintSource(cl, key);
    return true;
  }

  if (source != RACE_SETTING_SOURCE_RUNTIME &&
      !Race_SettingsService_Commit(source, &candidate, cl)) {
    return false;
  }

  race_settings_state = candidate;
  Race_SettingsService_Reply(
    cl, descriptor && descriptor->next_map
      ? "Race settings mutation: queued-next-map source=%s revision=%llu effective_changed=%d\n"
      : "Race settings mutation: applied source=%s revision=%llu effective_changed=%d\n",
    Race_Settings_SourceName(source),
    (unsigned long long) race_settings_state.revision,
    change.effective_changed);
  Race_SettingsService_PrintSource(cl, key);
  return true;
}

static void Race_SettingsService_CommandMutate(bool reset) {
  const int32_t required = reset ? 4 : 5;
  if (gi.Argc() != required) {
    gi.Print(reset
      ? "Usage: race_settings reset <global|map|runtime> <key>\n"
      : "Usage: race_settings set <global|map|runtime> <key> <value>\n");
    return;
  }

  race_setting_source_t source;
  if (!Race_SettingsService_ParseSource(gi.Argv(2), &source)) {
    gi.Print("Unknown Race settings source: %s\n", gi.Argv(2));
    return;
  }

  Race_SettingsService_Mutate(NULL, reset, source, gi.Argv(3),
                              reset ? NULL : gi.Argv(4));
}

static void Race_SettingsService_Command(void) {
  if (gi.Argc() == 1 || (gi.Argc() == 2 && !strcmp(gi.Argv(1), "list"))) {
    Race_SettingsService_PrintList();
    return;
  }
  if (gi.Argc() == 3 && !strcmp(gi.Argv(1), "get")) {
    const race_setting_descriptor_t *descriptor =
      Race_Settings_DescriptorForKey(gi.Argv(2));
    if (!descriptor) {
      gi.Print("Unknown Race setting: %s\n", gi.Argv(2));
      return;
    }
    Race_SettingsService_PrintEntry(NULL, descriptor);
    return;
  }
  if (gi.Argc() == 3 && !strcmp(gi.Argv(1), "source")) {
    Race_SettingsService_PrintSource(NULL, gi.Argv(2));
    return;
  }
  if (!strcmp(gi.Argv(1), "set")) {
    Race_SettingsService_CommandMutate(false);
    return;
  }
  if (!strcmp(gi.Argv(1), "reset")) {
    Race_SettingsService_CommandMutate(true);
    return;
  }
  if (gi.Argc() == 2 && !strcmp(gi.Argv(1), "reload")) {
    if (*race_settings_current_map) {
      const bool weapons_enabled = race_settings_weapons_enabled;
      Race_SettingsService_Load(race_settings_current_map);
      race_settings_weapons_enabled = weapons_enabled;
    } else {
      gi.Print("Race settings reload rejected: no current map\n");
    }
    return;
  }

  gi.Print("Usage: race_settings <list|get|source|set|reset|reload> ...\n");
}

void Race_SettingsService_Init(void) {
  size_t count;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  char error[128];
  if (!Race_Settings_CatalogRankCompatible(catalog, count,
                                            "q2-v1",
                                            error, sizeof(error)) ||
      !Race_Settings_StateInit(&race_settings_state) ||
      !Race_Settings_DocumentInit(&race_settings_global,
                                  RACE_SETTING_SCOPE_GLOBAL, NULL, 0)) {
    G_Error("Could not initialize Race settings: %s\n", error);
  }

  Race_Settings_DocumentInit(&race_settings_map,
                             RACE_SETTING_SCOPE_MAP, "unavailable", 0);
  race_settings_global_status = RACE_SETTINGS_SERVICE_UNAVAILABLE;
  race_settings_map_status = RACE_SETTINGS_SERVICE_UNAVAILABLE;
  race_settings_current_map[0] = '\0';
  race_settings_weapons_enabled = true;

  if (!gi.Mkdir(RACE_SETTINGS_DIRECTORY) ||
      !gi.Mkdir(RACE_SETTINGS_MAP_DIRECTORY)) {
    G_Warn("Could not prepare the Race settings storage directories\n");
  }

  gi.AddCmd("race_settings", Race_SettingsService_Command, CMD_GAME,
            "Inspect or mutate authoritative Race settings");
}

void Race_SettingsService_Load(const char *map) {
  char canonical[RACE_MAP_IDENTITY_SIZE];
  if (!Race_MapState_CanonicalizeMap(map, canonical)) {
    G_Warn("Could not derive a safe Race settings identity for map %s\n",
           map ? map : "<null>");
    return;
  }

  race_settings_document_t global;
  race_settings_document_t map_document;
  const race_settings_service_status_t global_status =
    Race_SettingsService_LoadScope(RACE_SETTING_SCOPE_GLOBAL, NULL, &global);
  const race_settings_service_status_t map_status =
    Race_SettingsService_LoadScope(RACE_SETTING_SCOPE_MAP, canonical,
                                   &map_document);

  race_settings_state_t candidate;
  if (!Race_Settings_StateInit(&candidate)) {
    G_Warn("Could not initialize Race settings resolution for map %s\n", canonical);
    return;
  }
  if (global_status == RACE_SETTINGS_SERVICE_READY &&
      !Race_Settings_DocumentApply(&global, &candidate)) {
    G_Warn("Could not apply global Race settings for map %s\n", canonical);
    return;
  }
  if (map_status == RACE_SETTINGS_SERVICE_READY &&
      !Race_Settings_DocumentApply(&map_document, &candidate)) {
    G_Warn("Could not apply map Race settings for map %s\n", canonical);
    return;
  }

  race_settings_change_t change;
  char error[128];
  if (!Race_Settings_StateResolve(&race_settings_state, &candidate,
                                  &change, error, sizeof(error))) {
    G_Warn("Could not resolve Race settings for map %s: %s\n",
           canonical, error);
    return;
  }

  race_settings_state = candidate;
  race_settings_weapons_enabled =
    candidate.entries[RACE_SETTING_WEAPONS].effective.boolean;
  race_settings_global = global;
  race_settings_map = map_document;
  race_settings_global_status = global_status;
  race_settings_map_status = map_status;
  q_strlcpy(race_settings_current_map, canonical,
            sizeof(race_settings_current_map));

  gi.Print("Race settings resolved: map=%s revision=%llu effective_changed=%d runtime=cleared precedence=runtime>map>global>default\n",
           canonical, (unsigned long long) race_settings_state.revision,
           change.effective_changed);
  Race_SettingsService_PrintList();
}

bool Race_SettingsService_FinishCueEnabled(void) {
  return race_settings_state.entries[RACE_SETTING_FINISH_CUE_ENABLED]
    .effective.boolean;
}

float Race_SettingsService_FinishCueGain(void) {
  return race_settings_state.entries[RACE_SETTING_FINISH_CUE_GAIN]
    .effective.integer / 100.f;
}

bool Race_SettingsService_CheckpointTimeEnabled(void) {
  return !strcmp(
    race_settings_state.entries[RACE_SETTING_CHECKPOINT_FEEDBACK]
      .effective.enumeration,
    "time");
}

int32_t Race_SettingsService_VotingTime(void) {
  return race_settings_state.entries[RACE_SETTING_VOTING_TIME]
    .effective.integer;
}

int32_t Race_SettingsService_MaxVotes(void) {
  return race_settings_state.entries[RACE_SETTING_MAX_VOTES]
    .effective.integer;
}

int32_t Race_SettingsService_VoteMenuDuration(void) {
  return race_settings_state.entries[RACE_SETTING_VOTE_MENU_DURATION]
    .effective.integer;
}

int32_t Race_SettingsService_VoteMenuChoices(void) {
  return race_settings_state.entries[RACE_SETTING_VOTE_MENU_CHOICES]
    .effective.integer;
}

bool Race_SettingsService_VoteAllowSpectators(void) {
  return race_settings_state.entries[RACE_SETTING_VOTE_ALLOW_SPECTATORS]
    .effective.boolean;
}

bool Race_SettingsService_WeaponsEnabled(void) {
  return race_settings_weapons_enabled;
}

bool Race_SettingsService_ClientInspect(g_client_t *cl, const char *key,
                                        bool include_sources) {
  if (!Race_AdminService_AuthorizeClientAction(
        cl, RACE_ADMIN_ACTION_SETTINGS_MUTATE)) {
    return false;
  }

  const race_setting_descriptor_t *descriptor =
    Race_Settings_DescriptorForKey(key);
  if (!descriptor) {
    gi.ClientPrint(cl, PRINT_HIGH, "Unknown Race setting\n");
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_SETTINGS_MUTATE, "invalid", "inspect-rejected");
    return false;
  }

  if (include_sources) {
    Race_SettingsService_PrintSource(cl, descriptor->key);
  } else {
    Race_SettingsService_PrintEntry(cl, descriptor);
  }
  Race_AdminService_AuditClientAction(
    cl, RACE_ADMIN_ACTION_SETTINGS_MUTATE, descriptor->key,
    include_sources ? "source-inspected" : "inspected");
  return true;
}

bool Race_SettingsService_ClientMutate(g_client_t *cl, bool reset,
                                       const char *source_name,
                                       const char *key, const char *value) {
  race_setting_source_t source;
  if (!source_name || !Race_SettingsService_ParseSource(source_name, &source)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_SETTINGS_MUTATE, NULL, "invalid-source");
    gi.ClientPrint(cl, PRINT_HIGH, "Unknown Race settings source\n");
    return false;
  }

  if (!Race_AdminService_AuthorizeClientAction(
        cl, RACE_ADMIN_ACTION_SETTINGS_MUTATE)) {
    return false;
  }

  const bool success = Race_SettingsService_Mutate(
    cl, reset, source, key, value);
  const race_setting_descriptor_t *descriptor =
    Race_Settings_DescriptorForKey(key);
  Race_AdminService_AuditClientAction(
    cl, RACE_ADMIN_ACTION_SETTINGS_MUTATE,
    descriptor ? descriptor->key : "invalid",
    success ? (reset ? "reset" : "set") : "mutation-rejected");
  return success;
}
