/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_physics_service.h"

#include <inttypes.h>
#include <string.h>

#include "g_local.h"
#include "race_physics.h"

#define RACE_PHYSICS_SELECTOR_CVAR "g_race_physics"
#define RACE_Q2_SNAP_MODE_CVAR "g_q2_snap_mode"

static char race_physics_map[MAX_QPATH];
static pm_params_t race_physics_fixed_params;
static bool race_physics_fixed_params_valid;
static PrepareMove race_physics_previous_prepare_move;
static bool race_physics_prepare_move_installed;
static cvar_t *race_physics_selector;
static cvar_t *race_q2_snap_mode;
static const char *race_physics_source = "unconfigured";
static bool race_physics_ai_first_command_pending[MAX_CLIENTS];

static void Race_PhysicsService_PrintParams(const char *owner,
                                            const pm_params_t *params) {
  gi.Print("%s params-hash=%016" PRIx64
           " gravity=%d gravity_water=%g"
           " accel_ground=%g accel_ground_slick=%g accel_air=%g"
           " accel_water=%g accel_spectator=%g accel_ladder=%g"
           " friction_ground=%g friction_ground_slick=%g friction_air=%g"
           " friction_water=%g friction_spectator=%g friction_ladder=%g"
           " speed_ground=%g speed_air=%g speed_water=%g speed_ladder=%g"
           " speed_spectator=%g speed_stop=%g speed_jump=%g"
           " speed_ducked=%g speed_duck_stand=%g speed_water_jump=%g\n",
           owner, Race_Physics_ParamsHash(params), params->gravity,
           params->gravity_water, params->accel_ground,
           params->accel_ground_slick, params->accel_air,
           params->accel_water, params->accel_spectator,
           params->accel_ladder, params->friction_ground,
           params->friction_ground_slick, params->friction_air,
           params->friction_water, params->friction_spectator,
           params->friction_ladder, params->speed_ground,
           params->speed_air, params->speed_water, params->speed_ladder,
           params->speed_spectator, params->speed_stop, params->speed_jump,
           params->speed_ducked, params->speed_duck_stand,
           params->speed_water_jump);
}

/**
 * @brief Chains the existing preparation hook, then restores the complete
 * map-fixed named vector after common movement cvars have been hydrated.
 */
static void Race_PhysicsService_PrepareMove(g_client_t *cl, pm_move_t *pm) {
  race_physics_previous_prepare_move(cl, pm);

  if (race_physics_fixed_params_valid) {
    pm->s.params = race_physics_fixed_params;
    cl->ps.pm_state.params = race_physics_fixed_params;
  }
}

static void Race_PhysicsService_ResolveFixedParams(void) {
  const race_physics_config_t *config = Race_Physics_Current();
  race_physics_fixed_params_valid = Race_Physics_FixedParamsForPreset(
    config->preset, &race_physics_fixed_params);

  if (!race_physics_fixed_params_valid) {
    memset(&race_physics_fixed_params, 0, sizeof(race_physics_fixed_params));
    if (config->family == RACE_PHYSICS_FAMILY_Q2) {
      G_Error("Named Race physics preset has no exact parameter vector\n");
    }
  }
}

static void Race_PhysicsService_Select(
    const race_physics_config_t *config) {
  Race_Physics_Reset();
  if (!Race_Physics_SetActive(config)) {
    G_Error("Could not select the map-fixed Race physics configuration\n");
  }
  Race_PhysicsService_ResolveFixedParams();
}

static bool Race_PhysicsService_ParseSnapMode(
    const cvar_t *cvar, race_physics_q2_snap_mode_t *snap_mode) {
  if (!cvar || !cvar->string || !snap_mode) {
    return false;
  }
  if (!strcmp(cvar->string, "0")) {
    *snap_mode = RACE_PHYSICS_Q2_SNAP_OFF;
    return true;
  }
  if (!strcmp(cvar->string, "1")) {
    *snap_mode = RACE_PHYSICS_Q2_SNAP_NEAREST;
    return true;
  }
  if (!strcmp(cvar->string, "2")) {
    *snap_mode = RACE_PHYSICS_Q2_SNAP_TRUNCATE;
    return true;
  }
  return false;
}

static race_physics_config_t Race_PhysicsService_Configured(void) {
  race_physics_config_t config = { 0 };
  const char *key = race_physics_selector ? race_physics_selector->string : NULL;
  if (!Race_Physics_ConfigForSelector(key, &config)) {
    G_Error("Invalid %s value '%s'; expected q2, quake2, q2-v1, quetoo-fix-v1, or quetoo-common-v1\n",
            RACE_PHYSICS_SELECTOR_CVAR, key ? key : "<missing>");
  }

  race_physics_q2_snap_mode_t snap_mode;
  if (!Race_PhysicsService_ParseSnapMode(race_q2_snap_mode, &snap_mode)) {
    G_Error("Invalid %s value '%s'; expected 0, 1, or 2\n",
            RACE_Q2_SNAP_MODE_CVAR,
            race_q2_snap_mode && race_q2_snap_mode->string
              ? race_q2_snap_mode->string
              : "<missing>");
  }
  config.q2_snap_mode = config.family == RACE_PHYSICS_FAMILY_Q2
    ? snap_mode
    : RACE_PHYSICS_Q2_SNAP_OFF;
  return config;
}

static void Race_PhysicsService_PrintStatus(void) {
  const race_physics_config_t *config = Race_Physics_Current();
  const race_physics_family_descriptor_t *family =
    Race_Physics_Family(config->family);
  const race_physics_preset_descriptor_t *preset =
    Race_Physics_Preset(config->preset);
  const char *ruleset = Race_Physics_ConfigRuleset(config);
  const char *snap_mode = Race_Physics_Q2SnapModeKey(config->q2_snap_mode);
  char wire[RACE_PHYSICS_CONFIG_STRING_SIZE];
  if (!family || !preset || !ruleset || !snap_mode ||
      !Race_Physics_Encode(config, wire)) {
    gi.Print("Race physics: unavailable\n");
    return;
  }
  gi.Print("Race physics: version=%u family=%s preset=%s name=%s short=%s q2-snap=%s ruleset=%s rankable=%d source=%s map=%s wire=%s pending-preset=%s pending-q2-snap=%s\n",
           config->version, family->key, preset->key, preset->name,
           preset->short_name, snap_mode, ruleset,
           Race_Physics_ConfigRankable(config), race_physics_source,
           *race_physics_map ? race_physics_map : "unavailable", wire,
           race_physics_selector && race_physics_selector->latched_string
             ? race_physics_selector->latched_string
             : "none",
           race_q2_snap_mode && race_q2_snap_mode->latched_string
             ? race_q2_snap_mode->latched_string
             : "none");
  if (race_physics_fixed_params_valid) {
    Race_PhysicsService_PrintParams("Race authoritative movement",
                                    &race_physics_fixed_params);
  } else {
    gi.Print("Race authoritative movement: owner=common-dynamic\n");
  }
}

static void Race_PhysicsService_PrintCatalog(void) {
  size_t family_count;
  const race_physics_family_descriptor_t *families =
    Race_Physics_Families(&family_count);
  for (size_t i = 0; i < family_count; i++) {
    gi.Print("Race physics family: id=%d key=%s name=%s available=%d\n",
             families[i].id, families[i].key, families[i].name,
             families[i].available);
  }

  size_t preset_count;
  const race_physics_preset_descriptor_t *presets =
    Race_Physics_Presets(&preset_count);
  for (size_t i = 0; i < preset_count; i++) {
    gi.Print("Race physics preset: id=%d key=%s name=%s short=%s family=%s ruleset=%s available=%d rankable=%d\n",
             presets[i].id, presets[i].key,
             presets[i].name, presets[i].short_name,
             Race_Physics_Family(presets[i].family)->key,
             presets[i].ruleset, presets[i].available,
             presets[i].rankable);
  }
}

static void Race_PhysicsService_Command(void) {
  if (gi.Argc() == 1 ||
      (gi.Argc() == 2 && !strcmp(gi.Argv(1), "status"))) {
    Race_PhysicsService_PrintStatus();
    return;
  }
  if (gi.Argc() == 2 && !strcmp(gi.Argv(1), "families")) {
    Race_PhysicsService_PrintCatalog();
    return;
  }
  gi.Print("Usage: race_physics [status|families]\n");
}

void Race_PhysicsService_Init(void) {
  race_physics_map[0] = '\0';
  race_physics_fixed_params_valid = false;
  memset(&race_physics_fixed_params, 0, sizeof(race_physics_fixed_params));
  memset(race_physics_ai_first_command_pending, 0,
         sizeof(race_physics_ai_first_command_pending));
  Race_Physics_SetProvider(NULL);
  // CVAR_SERVER_INFO so that the server browser can report the ruleset a
  // server runs before the player commits to connecting to it
  race_physics_selector = gi.AddCvar(
    RACE_PHYSICS_SELECTOR_CVAR, RACE_PHYSICS_SELECTOR_Q2_KEY, CVAR_LATCH | CVAR_SERVER_INFO,
    "Canonical Race physics preset. Changes apply at the next map/session load.");
  if (!race_physics_selector) {
    G_Error("Could not register the Race physics selector\n");
  }
  race_q2_snap_mode = gi.AddCvar(
    RACE_Q2_SNAP_MODE_CVAR, "1", CVAR_LATCH,
    "Q2 state snapping: 0 off, 1 nearest 1/8 unit, 2 toward-zero 1/8 unit. Changes apply at the next map/session load.");
  if (!race_q2_snap_mode) {
    G_Error("Could not register the Race Q2 snap-mode selector\n");
  }
  race_physics_source = RACE_PHYSICS_SELECTOR_CVAR;
  const race_physics_config_t configured = Race_PhysicsService_Configured();
  Race_PhysicsService_Select(&configured);
  if (!race_physics_prepare_move_installed) {
    race_physics_prepare_move_installed = true;
    race_physics_previous_prepare_move = G_PrepareMove;
    G_PrepareMove = Race_PhysicsService_PrepareMove;
  }
  gi.AddCmd("race_physics", Race_PhysicsService_Command, CMD_GAME,
            "Inspect the authoritative map-fixed Race physics identity");
}

void Race_PhysicsService_Shutdown(void) {
  race_physics_map[0] = '\0';
  race_physics_fixed_params_valid = false;
  memset(&race_physics_fixed_params, 0, sizeof(race_physics_fixed_params));
  memset(race_physics_ai_first_command_pending, 0,
         sizeof(race_physics_ai_first_command_pending));
  race_physics_selector = NULL;
  race_q2_snap_mode = NULL;
  race_physics_source = "unconfigured";
  Race_Physics_SetProvider(NULL);
}

static void Race_PhysicsService_Configure(const char *map,
                                          const race_physics_config_t *config,
                                          const char *source) {
  Race_PhysicsService_Select(config);
  race_physics_source = source;

  q_strlcpy(race_physics_map, map ? map : "unavailable",
            sizeof(race_physics_map));
  char wire[RACE_PHYSICS_CONFIG_STRING_SIZE];
  const bool encoded = Race_Physics_Encode(Race_Physics_Current(), wire);
  if (!encoded) {
    G_Error("Could not encode the map-fixed Race physics configuration\n");
  }
  gi.SetConfigString(CS_RACE_PHYSICS_CONFIG, wire);
  // G_RestartGame respawns clients before its ConfigureLevel tail. Preserve
  // SeedClient's pending bot check here so that restart's first AI command is
  // still verified; module init and each subsequent spawn reset slot state.
  Race_PhysicsService_PrintStatus();
}

void Race_PhysicsService_ConfigureLevel(const char *map) {
  const race_physics_config_t config = Race_PhysicsService_Configured();
  Race_PhysicsService_Configure(map, &config, RACE_PHYSICS_SELECTOR_CVAR);
}

void Race_PhysicsService_SeedClient(g_client_t *cl) {
  if (!cl) {
    return;
  }

  if (cl->ps.client < MAX_CLIENTS) {
    race_physics_ai_first_command_pending[cl->ps.client] = false;
  }
  if (!race_physics_fixed_params_valid) {
    return;
  }

  cl->ps.pm_state.params = race_physics_fixed_params;

  if (cl->ai && cl->ps.client < MAX_CLIENTS) {
    race_physics_ai_first_command_pending[cl->ps.client] = true;
    G_Debug("AI first-move seed client=%u preset=%s params-hash=%016" PRIx64
            " phase=before-direct-ai-move\n",
            (unsigned int) cl->ps.client,
            Race_Physics_Preset(Race_Physics_Current()->preset)->key,
            Race_Physics_ParamsHash(&cl->ps.pm_state.params));
  }
}

void Race_PhysicsService_ClientThink(g_client_t *cl, const pm_cmd_t *cmd) {
  if (!cl || !cmd || !cmd->msec || !cl->ai || cl->ps.client >= MAX_CLIENTS ||
      !race_physics_ai_first_command_pending[cl->ps.client]) {
    return;
  }

  if (!Race_Physics_ParamsEqual(&cl->ps.pm_state.params,
                                &race_physics_fixed_params)) {
    G_Error("First AI command completed with non-matching named parameters\n");
  }
  race_physics_ai_first_command_pending[cl->ps.client] = false;
  G_Debug("AI first-move verification client=%u preset=%s params-hash=%016" PRIx64
          " phase=first-ai-command-complete\n",
          (unsigned int) cl->ps.client,
          Race_Physics_Preset(Race_Physics_Current()->preset)->key,
          Race_Physics_ParamsHash(&cl->ps.pm_state.params));
}

#if defined(RACE_PHYSICS_TEST)
static bool Race_PhysicsService_ConfigureTestLevelWithSnapMode(
  const char *map, race_physics_preset_id_t preset,
  race_physics_q2_snap_mode_t snap_mode);

bool Race_PhysicsService_ConfigureTestLevel(
    const char *map, const race_physics_preset_id_t preset) {
  return Race_PhysicsService_ConfigureTestLevelWithSnapMode(
    map, preset, preset == RACE_PHYSICS_PRESET_QUETOO_COMMON_V1
      ? RACE_PHYSICS_Q2_SNAP_OFF
      : RACE_PHYSICS_Q2_SNAP_NEAREST);
}

static bool Race_PhysicsService_ConfigureTestLevelWithSnapMode(
    const char *map, const race_physics_preset_id_t preset,
    const race_physics_q2_snap_mode_t snap_mode) {
  const race_physics_preset_descriptor_t *descriptor =
    Race_Physics_Preset(preset);
  if (!descriptor || !descriptor->available) {
    return false;
  }

  race_physics_config_t config;
  if (!Race_Physics_ConfigForPresetKey(descriptor->key, &config)) {
    return false;
  }
  config.q2_snap_mode = snap_mode;
  if (!Race_Physics_ConfigValid(&config)) {
    return false;
  }
  Race_PhysicsService_Configure(map, &config, "test-only-wrapper");
  return true;
}
#endif
