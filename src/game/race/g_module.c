/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "g_local.h"
#include "race.h"
#include "race_actions.h"
#include "race_admin_service.h"
#include "race_cmds.h"
#include "race_map_browser_service.h"
#include "race_modes.h"
#include "race_map_state_service.h"
#include "race_physics_service.h"
#include "race_profiles.h"
#include "race_replay_service.h"
#include "race_replay_playback_service.h"
#include "race_settings_service.h"
#include "race_trigger.h"
#include "race_training_service.h"
#include "race_vote_service.h"
#include "race_vote_menu_service.h"
#include "race_weapon_tuning_service.h"

/**
 * @brief Module initialization and hook registration.
 */
void G_Module_Init(void) {
  G_Hook_SetClientAllowed(Race_AllowsHook);
  Race_Actions_Init();
  Race_Init();
  Race_PhysicsService_Init();
  Race_TrainingService_Init();
  Race_Profiles_Init();
  Race_MapBrowserService_Init();
  Race_MapStateService_Init();
  Race_ReplayService_Init();
  Race_ReplayPlaybackService_Init();
  Race_SettingsService_Init();
  Race_AdminService_Init();
  Race_WeaponTuningService_Init();
  Race_VoteService_Init();
  Race_VoteMenuService_Init();
}

/**
 * @brief Module shutdown.
 */
void G_Module_Shutdown(void) {
  G_Hook_SetClientAllowed(NULL);
  Race_Actions_Shutdown();
  Race_VoteMenuService_Shutdown();
  Race_VoteService_Shutdown();
  Race_WeaponTuningService_Shutdown();
  Race_ReplayPlaybackService_Shutdown();
  Race_ReplayService_Shutdown();
  Race_MapStateService_Shutdown();
  Race_TrainingService_Shutdown();
  Race_PhysicsService_Shutdown();
}

/**
 * @brief Advances bounded authoritative Race frame services.
 */
void G_Module_Frame(void) {
  Race_MapStateService_Frame();
  Race_ReplayService_Frame();
  Race_ReplayPlaybackService_Frame();
  // Intermission must cancel a regular vote before its delayed action can run.
  Race_VoteMenuService_Frame();
  Race_VoteService_Frame();
}

void G_Module_FinalizeClientFrames(void) {
  Race_ReplayPlaybackService_FinalizeClientFrames();
}

bool G_Module_IntermissionReady(void) {
  return Race_VoteMenuService_IntermissionReady();
}

bool G_Module_IntermissionClientCommand(g_client_t *cl, const char *cmd) {
  if (Race_WeaponTuningService_ClientCommand(cl, cmd)) {
    return true;
  }
  return Race_VoteMenuService_ClientCommand(cl, cmd);
}

void G_Module_ClientActivity(g_client_t *cl) {
  Race_VoteService_NoteActivity(cl);
}

/**
 * @brief Lets the Race module claim its brush trigger classes.
 */
bool G_Module_SpawnEntity(g_entity_t *ent) {
  return Race_SpawnEntity(ent);
}

/**
 * @brief Lets the Race module claim its per-client commands.
 */
bool G_Module_ClientCommand(g_client_t *cl, const char *cmd) {
  return Race_ClientCommand(cl, cmd);
}

void G_Module_ClientBegin(g_client_t *cl) {
  Race_ResetClientKillRate(cl);
  Race_AssignClientMode(cl);
  Race_WeaponTuningService_ClientBegin(cl);
}

void G_Module_ClientDisconnect(g_client_t *cl) {
  Race_ResetClientKillRate(cl);
  Race_VoteService_ClientDisconnect(cl);
  Race_VoteMenuService_ClientDisconnect(cl);
  Race_DisconnectClient(cl);
}

bool G_Module_ClientModeChange(g_client_t *cl, bool spectator) {
  return Race_HandleClientModeChange(cl, spectator);
}

bool G_Module_ClientNoClip(g_client_t *cl) {
  return Race_HandleClientNoClip(cl);
}

/**
 * @brief Resolves the sanitized client identity to a Race-owned profile.
 */
void G_Module_ClientUserInfoChanged(g_client_t *cl) {
  Race_VoteService_ClientUserInfoChanged(cl);
  Race_Profiles_ClientUserInfoChanged(cl);
}

/**
 * @brief Applies Race's per-spawn transform and non-blocking collision policy.
 */
void G_Module_ClientSpawn(g_client_t *cl, g_client_spawn_t *spawn) {
  Race_PhysicsService_SeedClient(cl);
  Race_PrepareClientSpawn(cl, spawn);
}

bool G_Module_ClientInput(g_client_t *cl, const pm_cmd_t *cmd) {
  Race_ClientInput(cl, cmd);
  return Race_ReplayPlaybackService_ClientInput(cl, cmd);
}

bool G_Module_ClientGameplay(const g_client_t *cl) {
  return !Race_ReplayPlaybackService_ClientActive(cl);
}

bool G_Module_ShouldClipMovementEntity(g_entity_t *mover,
                                       const g_entity_t *candidate,
                                       const vec3_t start, const vec3_t end,
                                       const box3_t bounds) {
  return Race_Trigger_ShouldClipMovementEntity(mover, candidate,
                                                start, end, bounds);
}

static bool G_Module_SkipMovementEntity(const g_entity_t *mover,
                                        const g_entity_t *candidate) {
  if (!candidate || candidate == mover || candidate->owner == mover) {
    return true;
  }
  if (mover && mover->owner &&
      (candidate == mover->owner || candidate->owner == mover->owner)) {
    return true;
  }
  return mover && mover->solid == SOLID_TRIGGER &&
         candidate->solid != SOLID_BSP;
}

cm_trace_t G_Module_TraceMovement(g_entity_t *mover, const vec3_t start,
                                 const vec3_t end, const box3_t bounds,
                                 const int32_t contents) {
  cm_trace_t trace = {
    .fraction = 1.f,
    .end = end
  };
  const box3_t absBounds = Box3_Expand(
    Box3(Vec3_Add(Vec3_Minf(start, end), bounds.mins),
         Vec3_Add(Vec3_Maxf(start, end), bounds.maxs)),
    BOX_EPSILON);
  g_entity_t *entities[MAX_ENTITIES];
  const size_t count = gi.BoxEntities(absBounds, entities,
                                      lengthof(entities), BOX_COLLIDE);
  for (size_t i = 0; i < count; i++) {
    g_entity_t *candidate = entities[i];
    if (G_Module_SkipMovementEntity(mover, candidate) ||
        !G_Module_ShouldClipMovementEntity(mover, candidate,
                                           start, end, bounds)) {
      continue;
    }

    const cm_trace_t clipped = gi.Clip(start, end, bounds, candidate,
                                       contents);
    if (clipped.all_solid || clipped.fraction < trace.fraction) {
      trace = clipped;
    }
  }
  return trace;
}

/**
 * @brief Observes the accepted client command at the post-move phase required
 * for Race auto-start.
 */
void G_Module_ClientThink(g_client_t *cl, const pm_cmd_t *cmd) {
  Race_PhysicsService_ClientThink(cl, cmd);
  if (cmd && (cmd->forward || cmd->right || cmd->up || cmd->buttons)) {
    Race_VoteService_NoteActivity(cl);
  }
  Race_ClientThink(cl, cmd);
}

/**
 * @brief Publishes the authoritative per-client Race snapshot fields.
 */
void G_Module_ClientStats(g_client_t *cl) {
  Race_ClientStats(cl);
}

/**
 * @brief Publishes authoritative per-client Race roster state.
 */
void G_Module_ClientScore(const g_client_t *cl, g_score_t *score) {
  Race_ClientScore(cl, score);
}

/**
 * @brief Publishes the two things a runner wants to know about a server before
 * joining it: who is on it, and what each of them has run here.
 * @details `best` is that runner's own record on the map the server is on now,
 * in milliseconds, and 0 when they have none. `status` is the Race mode they
 * are in, spelled the way the roster reads it. Both come out of state the level
 * already holds - Race_MapStateService_ClientTimes resolves against the records
 * loaded for the current map, not off disk - so this stays cheap enough to run
 * per client on every status packet.
 */
void G_Module_ClientStatusInfo(const g_client_t *cl, char *info, size_t len) {

  if (!cl || !info || !len) {
    return;
  }

  uint32_t personal_best = 0;
  Race_MapStateService_ClientTimes(cl, &personal_best, NULL);

  const char *status;
  if (cl->ai) {
    status = "bot";
  } else {
    switch (Race_Mode(cl)) {
      case RACE_MODE_PRACTICE:
        status = "practising";
        break;
      case RACE_MODE_SPECTATOR:
        status = "spectating";
        break;
      default:
        status = "racing";
        break;
    }
  }

  q_snprintf(info, len, "\\best\\%u\\status\\%s", personal_best, status);
}

bool G_Module_WeaponsEnabled(void) {
  return Race_SettingsService_WeaponsEnabled();
}

bool G_Module_UnlimitedAmmo(void) {
  return g_level.gameplay == GAME_DEATHMATCH;
}

void G_Module_ClientInventory(g_client_t *cl) {
  if (!Race_SettingsService_WeaponsEnabled()) {
    G_Debug("client=%s weapons=disabled inventory=empty\n",
            cl->persistent.net_name);
    return;
  }

  if (g_level.gameplay != GAME_DEATHMATCH || g_level.items == ITEMS_QUAKE) {
    return;
  }

  const g_item_t *weapon = &g_items[WEAPON_MACHINEGUN];

  // Common selects the Blaster before this module policy runs.
  cl->inventory[WEAPON_BLASTER] = 0;
  cl->weapon = NULL;
  cl->prev_weapon = NULL;
  cl->next_weapon = NULL;

  cl->inventory[weapon->def.tag] = 1;
  if (weapon->def.ammo) {
    cl->inventory[weapon->def.ammo] = 200;
  }

  G_UseWeapon(cl, weapon);

  G_Debug("client=%s weapons=enabled unlimited=%d machinegun=%d "
          "bullets=%d blaster=%d\n",
          cl->persistent.net_name, G_Module_UnlimitedAmmo(),
          cl->inventory[weapon->def.tag],
          weapon->def.ammo ? cl->inventory[weapon->def.ammo] : 0,
          cl->inventory[WEAPON_BLASTER]);
}

bool G_Module_FilterDamage(g_entity_t *target, g_entity_t *attacker,
                           int32_t *damage, int32_t *knockback) {
  return Race_DamagePolicy(target && target->client,
                           attacker && attacker->client,
                           target == attacker,
                           damage, knockback);
}
