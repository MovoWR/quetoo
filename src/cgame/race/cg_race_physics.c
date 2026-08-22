/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"
#include "cg_race_physics.h"

#include <inttypes.h>

#include "race_physics.h"

static race_physics_parse_result_t cg_race_physics_result =
  RACE_PHYSICS_PARSE_MISSING;
static char cg_race_physics_wire[RACE_PHYSICS_CONFIG_STRING_SIZE];
static race_physics_parse_result_t cg_race_physics_reported_result =
  RACE_PHYSICS_PARSE_OK;

static race_physics_parse_result_t Cg_RacePhysics_Provide(
  race_physics_config_t *config) {
  const char *wire = cgi.ConfigString(CS_RACE_PHYSICS_CONFIG);
  q_strlcpy(cg_race_physics_wire, wire ? wire : "",
            sizeof(cg_race_physics_wire));
  cg_race_physics_result = Race_Physics_Decode(wire, config);
  return cg_race_physics_result;
}

bool Cg_RacePhysics_Synchronized(void) {
  const bool snapshot_valid = cgi.state && *cgi.state == CL_ACTIVE &&
    cgi.client && cgi.client->frame.valid;
  race_physics_config_t config;
  const race_physics_parse_result_t decoded =
    Cg_RacePhysics_Provide(&config);
  const pm_params_t *params = snapshot_valid
    ? &cgi.client->frame.ps.pm_state.params
    : NULL;
  const bool ready = Race_Physics_PredictionReady(
    decoded, snapshot_valid, &config, params);

  const race_physics_parse_result_t result = decoded != RACE_PHYSICS_PARSE_OK
    ? decoded
    : snapshot_valid && !ready
      ? RACE_PHYSICS_PARSE_PARAMETER_MISMATCH
      : RACE_PHYSICS_PARSE_OK;
  if (snapshot_valid && result != RACE_PHYSICS_PARSE_OK) {
    if (cg_race_physics_reported_result != result) {
      Cg_Warn("Race prediction paused while physics synchronizes: %s\n",
              Race_Physics_ParseResultName(result));
    }
    cg_race_physics_reported_result = result;
  } else if (ready) {
    cg_race_physics_reported_result = RACE_PHYSICS_PARSE_OK;
  }

  return ready;
}

static void Cg_RacePhysics_Command(void) {
  const race_physics_config_t *config = Race_Physics_Current();
  const bool synchronized = Cg_RacePhysics_Synchronized();
  const race_physics_family_descriptor_t *family =
    Race_Physics_Family(config->family);
  const race_physics_preset_descriptor_t *preset =
    Race_Physics_Preset(config->preset);
  const char *ruleset = Race_Physics_ConfigRuleset(config);
  const char *snap_mode = Race_Physics_Q2SnapModeKey(config->q2_snap_mode);
  cgi.Print("Race prediction physics: status=%s synchronized=%d version=%u family=%s preset=%s name=%s short=%s q2-snap=%s ruleset=%s rankable=%d wire=%s\n",
             Race_Physics_ParseResultName(cg_race_physics_result),
             synchronized, config->version,
             family ? family->key : "unavailable",
             preset ? preset->key : "unavailable",
             preset ? preset->name : "unavailable",
             preset ? preset->short_name : "unavailable",
             snap_mode ? snap_mode : "unavailable",
             ruleset ? ruleset : "unavailable",
             Race_Physics_ConfigRankable(config),
             *cg_race_physics_wire ? cg_race_physics_wire : "missing");
  if (!cgi.client || !cgi.client->frame.valid) {
    cgi.Print("Race prediction movement: awaiting authoritative snapshot\n");
    return;
  }
  const pm_params_t *params = &cgi.client->frame.ps.pm_state.params;
  cgi.Print("Race prediction movement: source=authoritative-snapshot params-hash=%016" PRIx64
            " accel_air=%g speed_water=%g speed_ladder=%g speed_spectator=%g\n",
            Race_Physics_ParamsHash(params), params->accel_air,
            params->speed_water, params->speed_ladder,
            params->speed_spectator);
}

void Cg_RacePhysics_Init(void) {
  Race_Physics_SetProvider(Cg_RacePhysics_Provide);
  Cg_RacePhysics_Clear();
  cgi.AddCmd("cg_race_physics", Cg_RacePhysics_Command, CMD_CGAME,
             "Inspect the synchronized Race prediction physics identity");
}

void Cg_RacePhysics_Shutdown(void) {
  Cg_RacePhysics_Clear();
  Race_Physics_SetProvider(NULL);
}

void Cg_RacePhysics_Clear(void) {
  cg_race_physics_result = RACE_PHYSICS_PARSE_MISSING;
  cg_race_physics_reported_result = RACE_PHYSICS_PARSE_OK;
  cg_race_physics_wire[0] = '\0';
  Race_Physics_Reset();
}
