/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race.h"
#include "race_modes.h"
#include "race_replay_playback_service.h"
#include "race_replay_service.h"
#include "race_weapon_tuning_service.h"

race_mode_t Race_Mode(const g_client_t *cl) {

  if (!cl || !cl->in_use || cl->persistent.spectator) {
    return RACE_MODE_SPECTATOR;
  }

  return cl->persistent.race_mode == RACE_MODE_PRACTICE
           ? RACE_MODE_PRACTICE
           : RACE_MODE_RACE;
}

const char *Race_ModeName(race_mode_t mode) {
  switch (mode) {
    case RACE_MODE_PRACTICE:
      return "Practice";
    case RACE_MODE_SPECTATOR:
      return "Spectator";
    default:
      return "Race";
  }
}

bool Race_AllowsHook(const g_client_t *cl) {
  return Race_Mode_AllowsHook(Race_Mode(cl));
}

void Race_SynchronizeMode(g_client_t *cl) {
  cl->persistent.race_mode = Race_Mode(cl);
}

void Race_ClearStoredSpawn(g_client_t *cl) {
  Race_StoredSpawn_Clear(&cl->persistent.race_stored_spawn);
}

void Race_StoreSpawn(g_client_t *cl) {

  if (!cl || !cl->in_use) {
    return;
  }

  if (Race_Mode(cl) != RACE_MODE_PRACTICE || cl->persistent.spectator) {
    gi.ClientPrint(cl, PRINT_HIGH, "Enter Practice mode before using store.\n");
    return;
  }

  const bool alive = cl->entity && cl->entity->in_use && !cl->entity->dead &&
                     cl->entity->health > 0;

  if (!alive) {
    gi.ClientPrint(cl, PRINT_HIGH, "Respawn before using store.\n");
    return;
  }

  if (Race_StoredSpawn_Capture(&cl->persistent.race_stored_spawn, Race_Mode(cl),
                               cl->persistent.spectator, alive,
                               cl->entity->s.origin, cl->angles)) {
    G_Debug("client=%s store origin=%s\n",
            cl->persistent.net_name, vtos(cl->entity->s.origin));
    gi.ClientPrint(cl, PRINT_HIGH, "Practice spawn stored. Use ^2kill^7 to return here.\n");
  }
}

void Race_PrepareClientSpawn(g_client_t *cl, g_client_spawn_t *spawn) {

  spawn->clip_mask = Race_MovementClipMask(spawn->clip_mask);
  spawn->kill_box = false;

  bool stage = false;
  bool stored = false;
  vec3_t origin, angles;
  if (cl->race_stage_restart_trigger &&
      cl->race_stage_restart_trigger->race_stage_valid &&
      cl->race_stage_restart_trigger->target_ent) {
    origin = cl->race_stage_restart_trigger->target_ent->s.origin;
    angles = cl->race_stage_restart_trigger->target_ent->s.angles;
    cl->race_stage_restart_trigger = NULL;
    stage = true;
  } else {
    stored = Race_StoredSpawn_Get(&cl->persistent.race_stored_spawn, Race_Mode(cl),
                                  cl->persistent.spectator, &origin, &angles);
  }
  if (stage || stored) {
    spawn->origin = origin;
    spawn->angles = angles;
  }

  G_Debug("client=%s spawn source=%s origin=%s clip_mask=%x kill_box=%d\n",
          cl->persistent.net_name, stage ? "stage" : stored ? "stored" : "default",
          vtos(spawn->origin),
          spawn->clip_mask, spawn->kill_box);
}

void Race_RestartStage(g_client_t *cl) {
  if (!cl || !cl->in_use || Race_Mode(cl) != RACE_MODE_PRACTICE ||
      cl->persistent.spectator) {
    if (cl) {
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Stage restart is available only in Practice mode.\n");
    }
    return;
  }
  if (Race_ReplayPlaybackService_ClientActive(cl)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Exit replay playback before restarting a stage.\n");
    return;
  }
  g_entity_t *stage = cl->race_stage_trigger;
  if (!stage || !stage->race_stage_valid || !stage->target_ent) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Touch a configured stage boundary before using restart_stage.\n");
    return;
  }

  Race_Reset(cl);
  cl->race_stage_restart_trigger = stage;
  G_ClientRespawn(cl, false);
  if (Race_Start(cl)) {
    cl->race_run.stage = (uint16_t) stage->count;
    cl->race_stage_trigger = stage;
    gi.ClientPrint(cl, PRINT_HIGH, "Practice stage %d restarted.\n",
                   stage->count);
  }
}

static bool Race_SetMode(g_client_t *cl, race_mode_t mode) {

  if (!cl || !cl->in_use || mode < RACE_MODE_RACE || mode >= RACE_MODE_TOTAL) {
    return false;
  }

  if (!Race_WeaponTuningService_Rankable() && mode == RACE_MODE_RACE) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Weapon tuning is active; Practice mode is required.\n");
    mode = RACE_MODE_PRACTICE;
  }

  if (Race_Mode(cl) == mode && cl->persistent.spectator == (mode == RACE_MODE_SPECTATOR)) {
    return false;
  }

  Race_ReplayPlaybackService_ClientRunStarted(cl);
  Race_ReplayService_Reset(cl);

  race_mode_t current = Race_Mode(cl);
  if (!Race_Mode_Transition(&current, &cl->race_run, mode)) {
    Race_Run_Reset(&cl->race_run);
    current = mode;
  }

  cl->persistent.race_mode = current;
  cl->persistent.spectator = mode == RACE_MODE_SPECTATOR;

  if (mode == RACE_MODE_SPECTATOR) {
    Race_ClearStoredSpawn(cl);
  }
  cl->race_start_trigger = NULL;
  cl->race_stage_trigger = NULL;
  cl->race_stage_restart_trigger = NULL;
  cl->race_oneway_latches = 0u;

  cl->ps.stats[STAT_RACE_MODE] = (int16_t) mode;
  G_Debug("client=%s mode=%s stored_spawn=%d\n", cl->persistent.net_name,
          Race_ModeName(mode), cl->persistent.race_stored_spawn.set);

  const char *mode_color;
  const char *mode_label;
  if (mode == RACE_MODE_RACE) {
    mode_color = "^2";
    mode_label = "Race Mode";
    gi.ClientPrint(cl, PRINT_HIGH,
                   "You entered ^2Race Mode^7. Valid finishes can set PBs and enter rankings.\n");
  } else if (mode == RACE_MODE_PRACTICE) {
    mode_color = "^3";
    mode_label = "Practice Mode";
    gi.ClientPrint(cl, PRINT_HIGH,
                   "You entered ^3Practice Mode^7. Training runs are never submitted as records.\n");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Use ^2store^7 to save your position, ^2kill^7 to return.\n");
  } else {
    mode_color = "^7";
    mode_label = "Spectator";
    gi.ClientPrint(cl, PRINT_HIGH, "You are now spectating.\n");
  }

  gi.BroadcastPrint(PRINT_HIGH, "%s^7 entered %s%s^7\n",
                    cl->persistent.net_name, mode_color, mode_label);

  G_ClientRespawn(cl, true);
  return true;
}

void Race_AssignClientMode(g_client_t *cl) {

  if (!cl || !cl->in_use) {
    return;
  }

  Race_ReplayPlaybackService_ClientRunStarted(cl);
  Race_Reset(cl);
  Race_ClearStoredSpawn(cl);
  cl->race_stage_trigger = NULL;
  cl->race_stage_restart_trigger = NULL;

  const race_mode_t mode = cl->persistent.spectator
    ? RACE_MODE_SPECTATOR
    : !Race_WeaponTuningService_Rankable()
      ? RACE_MODE_PRACTICE : RACE_MODE_RACE;
  cl->persistent.race_mode = mode;
  cl->ps.stats[STAT_RACE_MODE] = (int16_t) mode;

  if (mode == RACE_MODE_RACE) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "You joined ^2Race Mode^7. Completed valid runs can set records.\n");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Use ^2mode practice^7 for unrestricted training.\n");
  } else if (mode == RACE_MODE_PRACTICE) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "You joined ^3Practice Mode^7. Training runs are never submitted as records.\n");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Use ^2store^7 to save your position, ^2kill^7 to return.\n");
  } else {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "You are spectating. Use ^2join^7 or ^2mode race^7 to race.\n");
  }
}

void Race_DisconnectClient(g_client_t *cl) {

  if (!cl) {
    return;
  }

  Race_ReplayPlaybackService_ClientRunStarted(cl);
  Race_ReplayService_Reset(cl);
  Race_ClearStoredSpawn(cl);
  cl->race_start_trigger = NULL;
  cl->race_stage_trigger = NULL;
  cl->race_stage_restart_trigger = NULL;
  cl->race_oneway_latches = 0u;
}

bool Race_HandleClientModeChange(g_client_t *cl, bool spectator) {

  if (!cl || !cl->in_use) {
    return true;
  }

  const race_mode_t mode = spectator ? RACE_MODE_SPECTATOR : RACE_MODE_RACE;
  if (spectator && Race_Mode(cl) != RACE_MODE_SPECTATOR && cl->entity) {
    G_TossInventory(cl);
    gi.WriteByte(SV_CMD_MUZZLE_FLASH);
    gi.WriteShort(cl->entity->s.number);
    gi.WriteByte(MZ_LOGOUT);
    gi.Multicast(cl->entity->s.origin, MULTICAST_PHS);
  }
  if (!Race_SetMode(cl, mode)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   spectator ? "You are already spectating\n" : "You have already joined\n");
  }
  return true;
}

bool Race_HandleClientNoClip(g_client_t *cl) {

  if (!cl || !cl->in_use || !cl->entity) {
    return true;
  }

  const bool practice = Race_Mode(cl) == RACE_MODE_PRACTICE;
  const bool cheats_allowed = sv_max_clients->integer <= 1 || g_cheats->value;
  if (!practice && !cheats_allowed) {
    G_Debug("client=%s noclip=denied mode=%s\n", cl->persistent.net_name,
            Race_ModeName(Race_Mode(cl)));
    gi.ClientPrint(cl, PRINT_HIGH, "Cheats are disabled\n");
  } else if (cl->entity->move_type == MOVE_TYPE_NO_CLIP) {
    cl->entity->move_type = MOVE_TYPE_WALK;
    G_Debug("client=%s noclip=0 mode=%s invalid=0x%02x\n",
            cl->persistent.net_name, Race_ModeName(Race_Mode(cl)),
            (unsigned) cl->race_run.invalid_flags);
    gi.ClientPrint(cl, PRINT_HIGH, "noclip disabled\n");
  } else {
    cl->entity->move_type = MOVE_TYPE_NO_CLIP;
    Race_MarkInvalid(cl, RACE_INVALID_NOCLIP);
    G_Debug("client=%s noclip=1 mode=%s invalid=0x%02x\n",
            cl->persistent.net_name, Race_ModeName(Race_Mode(cl)),
            (unsigned) cl->race_run.invalid_flags);
    gi.ClientPrint(cl, PRINT_HIGH, "noclip enabled\n");
  }

  return true;
}

void Race_ModeCommand(g_client_t *cl) {

  if (gi.Argc() < 2) {
    gi.ClientPrint(cl, PRINT_HIGH, "Current mode: %s\n", Race_ModeName(Race_Mode(cl)));
    gi.ClientPrint(cl, PRINT_HIGH, "Usage: mode race | mode practice | mode spectator\n");
    return;
  }

  const char *argument = gi.Argv(1);
  race_mode_t mode;

  if (q_strcmp(argument, "race") == 0) {
    mode = RACE_MODE_RACE;
  } else if (q_strcmp(argument, "practice") == 0) {
    mode = RACE_MODE_PRACTICE;
  } else if (q_strcmp(argument, "spectator") == 0 || q_strcmp(argument, "spectate") == 0) {
    mode = RACE_MODE_SPECTATOR;
  } else {
    gi.ClientPrint(cl, PRINT_HIGH, "Unknown mode: %s. Use race, practice, or spectator.\n", argument);
    return;
  }

  if (!Race_SetMode(cl, mode)) {
    gi.ClientPrint(cl, PRINT_HIGH, "You are already in that mode.\n");
    return;
  }
}
