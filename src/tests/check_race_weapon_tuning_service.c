/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "race_admin_service.h"
#include "race_weapon_tuning_service.h"
#include "race_weapon_tuning_wire.h"

g_import_t gi;
g_export_t ge;
g_level_t g_level;

#define WEAPON_TUNING_CVAR(name_, string_, value_, integer_) \
  static char name_ ## _string[] = string_; \
  static cvar_t name_ ## _storage = { \
    .name = #name_, \
    .default_string = string_, \
    .string = name_ ## _string, \
    .value = value_, \
    .integer = integer_ \
  }; \
  cvar_t *name_ = &name_ ## _storage

WEAPON_TUNING_CVAR(g_balance_hyperblaster_speed, "1775", 1775.f, 1775);
WEAPON_TUNING_CVAR(g_balance_hyperblaster_knockback, "4", 4.f, 4);
WEAPON_TUNING_CVAR(g_balance_hyperblaster_refire, "0.1", .1f, 0);
WEAPON_TUNING_CVAR(g_balance_hyperblaster_climb_knockback, "68", 68.f, 68);
WEAPON_TUNING_CVAR(g_balance_rocketlauncher_speed, "1000", 1000.f, 1000);
WEAPON_TUNING_CVAR(g_balance_rocketlauncher_knockback, "75", 75.f, 75);
WEAPON_TUNING_CVAR(g_balance_rocketlauncher_refire, "1", 1.f, 1);
WEAPON_TUNING_CVAR(g_balance_grenadelauncher_speed, "800", 800.f, 800);
WEAPON_TUNING_CVAR(g_balance_grenadelauncher_knockback, "120", 120.f, 120);
WEAPON_TUNING_CVAR(g_balance_grenadelauncher_timer, "2.5", 2.5f, 2);
WEAPON_TUNING_CVAR(g_balance_grenadelauncher_refire, "1", 1.f, 1);
WEAPON_TUNING_CVAR(g_balance_handgrenade_refire, "2", 2.f, 2);
WEAPON_TUNING_CVAR(g_self_knockback, "1", 1.f, 1);
WEAPON_TUNING_CVAR(g_player_projectile, "1", 1.f, 1);

#undef WEAPON_TUNING_CVAR

static cvar_t max_clients_storage = {
  .name = "sv_max_clients",
  .default_string = "2",
  .string = "2",
  .value = 2.f,
  .integer = 2
};
static cvar_t max_entities_storage = {
  .name = "sv_max_entities",
  .default_string = "4",
  .string = "4",
  .value = 4.f,
  .integer = 4
};

cvar_t *sv_max_clients = &max_clients_storage;
cvar_t *sv_max_entities = &max_entities_storage;

static const char *weapon_tuning_argv[40];
static int32_t weapon_tuning_argc;
static CmdExecuteFunc weapon_tuning_console_command;
static bool weapon_tuning_authorized;
static size_t weapon_tuning_assertions;
static size_t weapon_tuning_reset_count[MAX_CLIENTS];
static size_t weapon_tuning_replay_count[MAX_CLIENTS];
static size_t weapon_tuning_respawn_count[MAX_CLIENTS];
static size_t weapon_tuning_free_count;
static size_t weapon_tuning_audit_count;
static char weapon_tuning_last_audit[64];
static char weapon_tuning_last_print[MAX_STRING_CHARS];
static char weapon_tuning_status[RACE_WEAPON_TUNING_STATUS_SIZE];
static race_weapon_tuning_result_message_t weapon_tuning_result;
static bool weapon_tuning_result_valid;
static race_weapon_tuning_sync_begin_t weapon_tuning_sync_begin;
static race_weapon_tuning_sync_end_t weapon_tuning_sync_end;
static race_weapon_tuning_snapshot_t weapon_tuning_sync_baseline;
static race_weapon_tuning_snapshot_t weapon_tuning_sync_current;
static bool weapon_tuning_sync_begin_valid;
static bool weapon_tuning_sync_end_valid;
static bool weapon_tuning_sync_baseline_valid;
static bool weapon_tuning_sync_current_valid;
static size_t weapon_tuning_sync_snapshot_count;

#define WEAPON_TUNING_REQUIRE(expression_) \
  do { \
    weapon_tuning_assertions++; \
    if (!(expression_)) { \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
              #expression_); \
      *assertion_count = weapon_tuning_assertions; \
      return false; \
    } \
  } while (0)

static size_t WeaponTuningService_ClientIndex(const g_client_t *cl) {
  for (size_t i = 0u; i < MAX_CLIENTS; i++) {
    if (ge.clients[i] == cl) {
      return i;
    }
  }
  return MAX_CLIENTS;
}

static cvar_t *WeaponTuningService_AddCvar(const char *name,
                                            const char *value,
                                            uint32_t flags,
                                            const char *description) {
  (void) name;
  (void) value;
  (void) flags;
  (void) description;
  return NULL;
}

static cvar_t *WeaponTuningService_GetCvar(const char *name) {
  (void) name;
  return NULL;
}

static cmd_t *WeaponTuningService_AddCmd(const char *name,
                                         CmdExecuteFunc function,
                                         uint32_t flags,
                                         const char *description) {
  (void) flags;
  (void) description;
  if (!strcmp(name, "race_tune")) {
    weapon_tuning_console_command = function;
  }
  return NULL;
}

static int32_t WeaponTuningService_Argc(void) {
  return weapon_tuning_argc;
}

static const char *WeaponTuningService_Argv(const int32_t arg) {
  return arg >= 0 && arg < weapon_tuning_argc ? weapon_tuning_argv[arg] : "";
}

static void WeaponTuningService_Print(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vsnprintf(weapon_tuning_last_print, sizeof(weapon_tuning_last_print),
            format, args);
  va_end(args);
}

static void WeaponTuningService_Warn(const char *func,
                                      const char *format, ...) {
  (void) func;
  va_list args;
  va_start(args, format);
  vsnprintf(weapon_tuning_last_print, sizeof(weapon_tuning_last_print),
            format, args);
  va_end(args);
}

static void WeaponTuningService_ClientPrint(const g_client_t *cl,
                                             const int32_t level,
                                             const char *format, ...) {
  (void) cl;
  (void) level;
  va_list args;
  va_start(args, format);
  vsnprintf(weapon_tuning_last_print, sizeof(weapon_tuning_last_print),
            format, args);
  va_end(args);
}

static void WeaponTuningService_SetConfigString(const int32_t index,
                                                 const char *string) {
  if (index == CS_RACE_WEAPON_TUNING_STATUS) {
    snprintf(weapon_tuning_status, sizeof(weapon_tuning_status), "%s",
             string ? string : "");
  }
}

static void WeaponTuningService_WriteByte(const int32_t value) {
  (void) value;
}

static void WeaponTuningService_WriteData(const void *data,
                                           const size_t length) {
  race_weapon_tuning_result_message_t result;
  if (Race_WeaponTuningWire_DecodeResult(data, length, &result)) {
    weapon_tuning_result = result;
    weapon_tuning_result_valid = true;
    return;
  }
  race_weapon_tuning_sync_begin_t begin;
  if (Race_WeaponTuningWire_DecodeSyncBegin(data, length, &begin)) {
    weapon_tuning_sync_begin = begin;
    weapon_tuning_sync_begin_valid = true;
    return;
  }
  race_weapon_tuning_snapshot_message_t snapshot;
  if (Race_WeaponTuningWire_DecodeSnapshot(data, length, &snapshot)) {
    weapon_tuning_sync_snapshot_count++;
    if (snapshot.kind == RACE_WEAPON_TUNING_SNAPSHOT_BASELINE) {
      weapon_tuning_sync_baseline = snapshot.snapshot;
      weapon_tuning_sync_baseline_valid = true;
    } else if (snapshot.kind == RACE_WEAPON_TUNING_SNAPSHOT_CURRENT) {
      weapon_tuning_sync_current = snapshot.snapshot;
      weapon_tuning_sync_current_valid = true;
    }
    return;
  }
  race_weapon_tuning_sync_end_t end;
  if (Race_WeaponTuningWire_DecodeSyncEnd(data, length, &end)) {
    weapon_tuning_sync_end = end;
    weapon_tuning_sync_end_valid = true;
  }
}

static void WeaponTuningService_Unicast(const g_client_t *cl,
                                         const bool reliable) {
  (void) cl;
  (void) reliable;
}

uint32_t Race_AdminService_ClientCapabilities(const g_client_t *cl) {
  return cl && weapon_tuning_authorized
    ? RACE_ADMIN_CAP_SETTINGS_MUTATE : 0u;
}

bool Race_AdminService_AuthorizeClientAction(g_client_t *cl,
                                              race_admin_action_t action) {
  return cl && action == RACE_ADMIN_ACTION_SETTINGS_MUTATE &&
         weapon_tuning_authorized;
}

void Race_AdminService_AuditClientAction(const g_client_t *cl,
                                          race_admin_action_t action,
                                          const char *subject,
                                          const char *result) {
  (void) cl;
  (void) action;
  (void) result;
  weapon_tuning_audit_count++;
  snprintf(weapon_tuning_last_audit, sizeof(weapon_tuning_last_audit), "%s",
           subject ? subject : "");
}

static const race_physics_config_t weapon_tuning_physics = {
  .version = RACE_PHYSICS_CONFIG_VERSION,
  .family = RACE_PHYSICS_FAMILY_Q2,
  .preset = RACE_PHYSICS_PRESET_Q2,
  .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_OFF
};
static const race_physics_preset_descriptor_t weapon_tuning_presets[] = {
  {
    .id = RACE_PHYSICS_PRESET_QUETOO_COMMON_V1,
    .family = RACE_PHYSICS_FAMILY_QUETOO,
    .key = RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY,
    .name = "Current common Quetoo",
    .short_name = "Quetoo",
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
    .hyperblaster_climb_range = 32.f,
    .available = true,
    .rankable = true
  }
};

const race_physics_config_t *Race_Physics_Current(void) {
  return &weapon_tuning_physics;
}

const race_physics_preset_descriptor_t *Race_Physics_Preset(
    const race_physics_preset_id_t id) {
  for (size_t i = 0u; i < lengthof(weapon_tuning_presets); i++) {
    if (weapon_tuning_presets[i].id == id) {
      return weapon_tuning_presets + i;
    }
  }
  return NULL;
}

const race_physics_preset_descriptor_t *Race_Physics_Presets(size_t *count) {
  if (count) {
    *count = lengthof(weapon_tuning_presets);
  }
  return weapon_tuning_presets;
}

const race_physics_preset_descriptor_t *Race_Physics_PresetForKey(
    const char *key) {
  for (size_t i = 0u; key && i < lengthof(weapon_tuning_presets); i++) {
    if (!strcmp(weapon_tuning_presets[i].key, key)) {
      return weapon_tuning_presets + i;
    }
  }
  return NULL;
}

const race_physics_config_t *Race_Physics_Default(void) {
  return &weapon_tuning_physics;
}

void Race_ReplayPlaybackService_ClientRunStarted(g_client_t *cl) {
  const size_t index = WeaponTuningService_ClientIndex(cl);
  if (index < MAX_CLIENTS) {
    weapon_tuning_replay_count[index]++;
  }
}

void Race_Reset(g_client_t *cl) {
  const size_t index = WeaponTuningService_ClientIndex(cl);
  if (index < MAX_CLIENTS) {
    weapon_tuning_reset_count[index]++;
  }
}

void G_ClientRespawn(g_client_t *cl, bool voluntary) {
  (void) voluntary;
  const size_t index = WeaponTuningService_ClientIndex(cl);
  if (index < MAX_CLIENTS) {
    weapon_tuning_respawn_count[index]++;
  }
}

void G_FreeEntity(g_entity_t *ent) {
  if (ent && ent->in_use) {
    ent->in_use = false;
    weapon_tuning_free_count++;
  }
}

static void WeaponTuningService_SetCommand(const size_t argc,
                                            const char *const *argv) {
  weapon_tuning_argc = (int32_t) argc;
  for (size_t i = 0u; i < argc; i++) {
    weapon_tuning_argv[i] = argv[i];
  }
  weapon_tuning_result_valid = false;
  *weapon_tuning_last_print = '\0';
}

static void WeaponTuningService_ClearSync(void) {
  memset(&weapon_tuning_sync_begin, 0, sizeof(weapon_tuning_sync_begin));
  memset(&weapon_tuning_sync_end, 0, sizeof(weapon_tuning_sync_end));
  memset(&weapon_tuning_sync_baseline, 0,
         sizeof(weapon_tuning_sync_baseline));
  memset(&weapon_tuning_sync_current, 0,
         sizeof(weapon_tuning_sync_current));
  weapon_tuning_sync_begin_valid = false;
  weapon_tuning_sync_end_valid = false;
  weapon_tuning_sync_baseline_valid = false;
  weapon_tuning_sync_current_valid = false;
  weapon_tuning_sync_snapshot_count = 0u;
}

static void WeaponTuningService_ClientCommand(g_client_t *cl,
                                               const size_t argc,
                                               const char *const *argv) {
  WeaponTuningService_SetCommand(argc, argv);
  Race_WeaponTuningService_ClientCommand(cl, argv[0]);
}

static void WeaponTuningService_ConsoleCommand(
    const size_t argc, const char *const *argv) {
  WeaponTuningService_SetCommand(argc, argv);
  if (weapon_tuning_console_command) {
    weapon_tuning_console_command();
  }
}

bool WeaponTuningService_Lifecycle(size_t *assertion_count) {
  weapon_tuning_assertions = 0u;
  memset(&gi, 0, sizeof(gi));
  memset(&ge, 0, sizeof(ge));
  memset(&g_level, 0, sizeof(g_level));
  memset(weapon_tuning_reset_count, 0, sizeof(weapon_tuning_reset_count));
  memset(weapon_tuning_replay_count, 0, sizeof(weapon_tuning_replay_count));
  memset(weapon_tuning_respawn_count, 0, sizeof(weapon_tuning_respawn_count));
  weapon_tuning_free_count = 0u;
  weapon_tuning_audit_count = 0u;
  *weapon_tuning_last_audit = '\0';
  *weapon_tuning_last_print = '\0';
  *weapon_tuning_status = '\0';
  weapon_tuning_console_command = NULL;
  weapon_tuning_authorized = false;
  weapon_tuning_result_valid = false;
  WeaponTuningService_ClearSync();

  gi.AddCvar = WeaponTuningService_AddCvar;
  gi.GetCvar = WeaponTuningService_GetCvar;
  gi.AddCmd = WeaponTuningService_AddCmd;
  gi.Argc = WeaponTuningService_Argc;
  gi.Argv = WeaponTuningService_Argv;
  gi.Print = WeaponTuningService_Print;
  gi.Warn = WeaponTuningService_Warn;
  gi.ClientPrint = WeaponTuningService_ClientPrint;
  gi.SetConfigString = WeaponTuningService_SetConfigString;
  gi.WriteByte = WeaponTuningService_WriteByte;
  gi.WriteData = WeaponTuningService_WriteData;
  gi.Unicast = WeaponTuningService_Unicast;

  g_client_t clients[2] = { };
  g_entity_t entities[4] = { };
  for (size_t i = 0u; i < 2u; i++) {
    clients[i].in_use = true;
    clients[i].ps.client = (uint16_t) i;
    clients[i].entity = entities + i;
    entities[i].in_use = true;
    entities[i].client = clients + i;
    ge.clients[i] = clients + i;
    ge.entities[i] = entities + i;
  }
  clients[0].persistent.race_mode = RACE_MODE_RACE;
  clients[0].inventory[POWERUP_QUAD] = 1;
  clients[0].quad_damage_time = 1000u;
  clients[0].quad_countdown_time = 900u;
  clients[0].quad_attack_time = 800u;
  entities[0].s.effects |= EF_QUAD;
  clients[1].persistent.spectator = true;
  clients[1].persistent.race_mode = RACE_MODE_SPECTATOR;

  entities[2].in_use = true;
  entities[2].classname = "G_PullGrenadePin";
  entities[2].owner = entities;
  ge.entities[2] = entities + 2;
  clients[0].held_grenade = entities + 2;
  clients[0].grenade_hold_time = 700u;
  clients[0].grenade_hold_frame = 9u;

  entities[3].in_use = true;
  entities[3].classname = "G_RocketProjectile";
  entities[3].owner = entities;
  ge.entities[3] = entities + 3;

  Race_WeaponTuningService_Init();
  Race_WeaponTuningService_PostInit();
  WEAPON_TUNING_REQUIRE(weapon_tuning_console_command != NULL);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Rankable());
  WEAPON_TUNING_REQUIRE(!Race_WeaponTuningService_Active());
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Generation() == 0u);

  // The server clears its ConfigString table for a map transition without
  // reloading GAME. The persistent tuning service must publish its cached
  // identity into the new level before clients fetch ConfigStrings.
  *weapon_tuning_status = '\0';
  Race_WeaponTuningService_ConfigureLevel();
  race_weapon_tuning_status_t status;
  WEAPON_TUNING_REQUIRE(Race_WeaponTuning_StatusDecode(weapon_tuning_status,
                                                       &status));
  WEAPON_TUNING_REQUIRE(status.state == RACE_WEAPON_TUNING_STATE_INACTIVE);
  WEAPON_TUNING_REQUIRE(status.generation == 0u);
  WEAPON_TUNING_REQUIRE(status.hash != 0u);
  WEAPON_TUNING_REQUIRE(!strcmp(status.identity, "baseline"));
  WEAPON_TUNING_REQUIRE(status.hyper_climb_range == 32.f);
  const uint64_t baseline_hash = status.hash;

  g_level.time = 1u;
  WeaponTuningService_ClearSync();
  const char *sync[] = { "race", "tune", "sync", "req=1" };
  WeaponTuningService_ClientCommand(clients, lengthof(sync), sync);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_begin_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_end_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_snapshot_count == 2u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_baseline_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_current_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_begin.request_id == 1u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_begin.generation == 0u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_begin.state ==
                        RACE_WEAPON_TUNING_STATE_INACTIVE);
  WEAPON_TUNING_REQUIRE((weapon_tuning_sync_begin.flags &
    (RACE_WEAPON_TUNING_SYNC_HAS_BASELINE |
     RACE_WEAPON_TUNING_SYNC_HAS_CURRENT)) ==
    (RACE_WEAPON_TUNING_SYNC_HAS_BASELINE |
     RACE_WEAPON_TUNING_SYNC_HAS_CURRENT));
  WEAPON_TUNING_REQUIRE((weapon_tuning_sync_begin.flags &
                         RACE_WEAPON_TUNING_SYNC_CAN_MUTATE) == 0u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_begin.baseline_hash ==
                        baseline_hash);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_begin.current_hash ==
                        baseline_hash);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_end.request_id == 1u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_end.descriptor_count ==
                        RACE_WEAPON_TUNING_VALUE_COUNT);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuning_SnapshotEqual(
    &weapon_tuning_sync_baseline, &weapon_tuning_sync_current));
  race_weapon_tuning_snapshot_t descriptor_defaults;
  Race_WeaponTuning_DefaultSnapshot(&descriptor_defaults);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuning_Int(
    &descriptor_defaults, RACE_WEAPON_TUNING_HYPER_SPEED) == 1800);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuning_Int(
    &weapon_tuning_sync_baseline, RACE_WEAPON_TUNING_HYPER_SPEED) == 1775);

  const char *denied[] = {
    "race", "tune", "apply", "req=2", "0", "hyper.speed=1825"
  };
  WeaponTuningService_ClientCommand(clients, lengthof(denied), denied);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result.operation ==
                        RACE_WEAPON_TUNING_OPERATION_APPLY);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result.result ==
                        RACE_WEAPON_TUNING_RESULT_DENIED);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Generation() == 0u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_respawn_count[0] == 0u);

  weapon_tuning_authorized = true;
  const char *stale[] = {
    "race", "tune", "apply", "req=3", "1", "hyper.speed=1825"
  };
  WeaponTuningService_ClientCommand(clients, lengthof(stale), stale);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result.operation ==
                        RACE_WEAPON_TUNING_OPERATION_APPLY);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result.result ==
                        RACE_WEAPON_TUNING_RESULT_STALE);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Generation() == 0u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_respawn_count[0] == 0u);

  const char *apply[] = {
    "race", "tune", "apply", "req=4", "0", "hyper.speed=1825",
    "global.self_knockback=1.5"
  };
  WeaponTuningService_ClientCommand(clients, lengthof(apply), apply);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result.result ==
                        RACE_WEAPON_TUNING_RESULT_OK);
  WEAPON_TUNING_REQUIRE(!strcmp(weapon_tuning_result.text,
                                "batch applied; runs reset"));
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Generation() == 1u);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Active());
  WEAPON_TUNING_REQUIRE(!Race_WeaponTuningService_Rankable());
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Current() != NULL);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Int(
                          RACE_WEAPON_TUNING_HYPER_SPEED, -1) == 1825);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Float(
                          RACE_WEAPON_TUNING_GLOBAL_SELF_KNOCKBACK, -1.f) ==
                        1.5f);
  WEAPON_TUNING_REQUIRE(clients[0].persistent.race_mode == RACE_MODE_PRACTICE);
  WEAPON_TUNING_REQUIRE(clients[0].ps.stats[STAT_RACE_MODE] ==
                        RACE_MODE_PRACTICE);
  WEAPON_TUNING_REQUIRE(clients[1].persistent.race_mode ==
                        RACE_MODE_SPECTATOR);
  WEAPON_TUNING_REQUIRE(weapon_tuning_reset_count[0] == 1u &&
                        weapon_tuning_reset_count[1] == 1u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_replay_count[0] == 1u &&
                        weapon_tuning_replay_count[1] == 1u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_respawn_count[0] == 1u &&
                        weapon_tuning_respawn_count[1] == 0u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_free_count == 2u);
  WEAPON_TUNING_REQUIRE(clients[0].held_grenade == NULL);
  WEAPON_TUNING_REQUIRE(clients[0].grenade_hold_time == 0u &&
                        clients[0].grenade_hold_frame == 0u);
  WEAPON_TUNING_REQUIRE(clients[0].inventory[POWERUP_QUAD] == 0);
  WEAPON_TUNING_REQUIRE(clients[0].quad_damage_time == 0u &&
                        clients[0].quad_countdown_time == 0u &&
                        clients[0].quad_attack_time == 0u);
  WEAPON_TUNING_REQUIRE((entities[0].s.effects & EF_QUAD) == 0u);
  WEAPON_TUNING_REQUIRE(!strcmp(weapon_tuning_last_audit,
                                "batch applied; runs reset"));
  WEAPON_TUNING_REQUIRE(weapon_tuning_audit_count == 1u);

  WEAPON_TUNING_REQUIRE(Race_WeaponTuning_StatusDecode(weapon_tuning_status,
                                                       &status));
  WEAPON_TUNING_REQUIRE(status.state == RACE_WEAPON_TUNING_STATE_ACTIVE);
  WEAPON_TUNING_REQUIRE(status.generation == 1u);
  WEAPON_TUNING_REQUIRE(status.hash != baseline_hash);
  WEAPON_TUNING_REQUIRE(!strcmp(status.identity, "custom"));

  const char *no_op_apply[] = {
    "race", "tune", "apply", "req=5", "1", "hyper.speed=1825",
    "global.self_knockback=1.5"
  };
  WeaponTuningService_ClientCommand(clients, lengthof(no_op_apply),
                                    no_op_apply);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result.result ==
                        RACE_WEAPON_TUNING_RESULT_NOOP);
  WEAPON_TUNING_REQUIRE(!strcmp(weapon_tuning_result.text,
                                "no effective change"));
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Generation() == 1u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_respawn_count[0] == 1u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_audit_count == 1u);

  g_client_t joining = { .in_use = true };
  joining.ps.client = 2u;
  joining.persistent.race_mode = RACE_MODE_RACE;
  Race_WeaponTuningService_ClientBegin(&joining);
  WEAPON_TUNING_REQUIRE(joining.persistent.race_mode == RACE_MODE_PRACTICE);
  WEAPON_TUNING_REQUIRE(joining.ps.stats[STAT_RACE_MODE] ==
                        RACE_MODE_PRACTICE);

  weapon_tuning_authorized = false;
  const char *reset_denied[] = {
    "race", "tune", "reset", "req=6", "1", "all"
  };
  WeaponTuningService_ClientCommand(clients, lengthof(reset_denied),
                                    reset_denied);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result.operation ==
                        RACE_WEAPON_TUNING_OPERATION_RESET_ALL);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result.result ==
                        RACE_WEAPON_TUNING_RESULT_DENIED);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Generation() == 1u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_respawn_count[0] == 1u);

  weapon_tuning_authorized = true;
  const char *invalid_reset[] = {
    "race", "tune", "reset", "req=7", "1", "hyper.speed"
  };
  WeaponTuningService_ClientCommand(clients, lengthof(invalid_reset),
                                    invalid_reset);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result.result ==
                        RACE_WEAPON_TUNING_RESULT_INVALID);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Generation() == 1u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_respawn_count[0] == 1u);

  const char *reset[] = {
    "race", "tune", "reset", "req=8", "1", "all"
  };
  WeaponTuningService_ClientCommand(clients, lengthof(reset), reset);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result.operation ==
                        RACE_WEAPON_TUNING_OPERATION_RESET_ALL);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result.result ==
                        RACE_WEAPON_TUNING_RESULT_OK);
  WEAPON_TUNING_REQUIRE(!strcmp(weapon_tuning_result.text,
                                "baseline values restored"));
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Generation() == 2u);
  WEAPON_TUNING_REQUIRE(!Race_WeaponTuningService_Active());
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Rankable());
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Current() == NULL);
  WEAPON_TUNING_REQUIRE(weapon_tuning_respawn_count[0] == 2u);
  WEAPON_TUNING_REQUIRE(!strcmp(weapon_tuning_last_audit,
                                "baseline values restored"));
  WEAPON_TUNING_REQUIRE(weapon_tuning_audit_count == 2u);

  WEAPON_TUNING_REQUIRE(Race_WeaponTuning_StatusDecode(weapon_tuning_status,
                                                       &status));
  WEAPON_TUNING_REQUIRE(status.state == RACE_WEAPON_TUNING_STATE_INACTIVE);
  WEAPON_TUNING_REQUIRE(status.generation == 2u);
  WEAPON_TUNING_REQUIRE(status.hash == baseline_hash);
  WEAPON_TUNING_REQUIRE(!strcmp(status.identity, "baseline"));

  g_level.time = 300u;
  WeaponTuningService_ClearSync();
  const char *resync[] = { "race", "tune", "sync", "req=9" };
  WeaponTuningService_ClientCommand(clients, lengthof(resync), resync);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_begin_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_end_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_snapshot_count == 2u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_baseline_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_current_valid);
  WEAPON_TUNING_REQUIRE((weapon_tuning_sync_begin.flags &
                         RACE_WEAPON_TUNING_SYNC_CAN_MUTATE) != 0u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_begin.generation == 2u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_begin.baseline_hash ==
                        baseline_hash);
  WEAPON_TUNING_REQUIRE(weapon_tuning_sync_begin.current_hash ==
                        baseline_hash);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuning_Int(
    &weapon_tuning_sync_current, RACE_WEAPON_TUNING_HYPER_SPEED) == 1775);

  const char *no_op_reset[] = {
    "race", "tune", "reset", "req=10", "2", "all"
  };
  WeaponTuningService_ClientCommand(clients, lengthof(no_op_reset),
                                    no_op_reset);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result_valid);
  WEAPON_TUNING_REQUIRE(weapon_tuning_result.result ==
                        RACE_WEAPON_TUNING_RESULT_NOOP);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Generation() == 2u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_respawn_count[0] == 2u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_audit_count == 2u);

  const char *console_apply[] = {
    "race_tune", "apply", "2", "hyper.speed=1850"
  };
  WeaponTuningService_ConsoleCommand(lengthof(console_apply), console_apply);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Active());
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Generation() == 3u);
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Int(
                          RACE_WEAPON_TUNING_HYPER_SPEED, -1) == 1850);
  WEAPON_TUNING_REQUIRE(weapon_tuning_respawn_count[0] == 3u);

  const char *console_reset[] = {
    "race_tune", "reset", "3", "all"
  };
  WeaponTuningService_ConsoleCommand(lengthof(console_reset), console_reset);
  WEAPON_TUNING_REQUIRE(!Race_WeaponTuningService_Active());
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Rankable());
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Generation() == 4u);
  WEAPON_TUNING_REQUIRE(weapon_tuning_respawn_count[0] == 4u);
  WEAPON_TUNING_REQUIRE(!strcmp(weapon_tuning_last_audit,
                                "baseline values restored"));
  WEAPON_TUNING_REQUIRE(weapon_tuning_audit_count == 4u);

  Race_WeaponTuningService_Shutdown();
  WEAPON_TUNING_REQUIRE(!Race_WeaponTuningService_Active());
  WEAPON_TUNING_REQUIRE(Race_WeaponTuningService_Rankable());

  *assertion_count = weapon_tuning_assertions;
  return true;
}
