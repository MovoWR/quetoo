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


#include "cg_local.h"
#include "cg_module_compat.h"
#include "cg_race_hud.h"
#include "cg_race_finish_report.h"
#include "cg_race_barriers.h"
#include "cg_race_double_jump.h"
#include "cg_race_map_browser.h"
#include "cg_race_markers.h"
#include "cg_race_physics.h"
#include "cg_race_practice_markers.h"
#include "cg_race_replay.h"
#include "cg_race_training.h"
#include "cg_race_weapon_tuning.h"
#include "ui/home/HomeViewController.h"
#include "ui/main/MainViewController.h"
#include "ui/voting/VotingViewController.h"

static cvar_t *cg_show_jumpers;

static void Cg_RaceJumpers_f(void) {
  cgi.ToggleCvar(cg_show_jumpers->name);
  cgi.Print("Other jumpers are %s.\n",
            cg_show_jumpers->integer ? "visible" : "hidden");
}

/**
 * @brief Installs the module-owned Race HUD on the existing additive HUD chain.
 */
void Cg_Module_Init(void) {
  Cg_RaceDoubleJump_Init();
  Cg_RaceWeaponTuning_Init();
  Cg_RacePhysics_Init();
  Cg_RaceFinishReport_Init();
  Cg_RaceHud_Init();
  Cg_RaceMarkers_Init();
  Cg_RacePracticeMarkers_Init();
  Cg_RaceReplay_Init();
  Cg_RaceTraining_Init();
  cg_show_jumpers = cgi.AddCvar(
    "cg_show_jumpers", "1", CVAR_ARCHIVE,
    "Show other players and their attributed sounds, projectiles, and effects.");
  cgi.AddCmd("jumpers", Cg_RaceJumpers_f, CMD_CGAME,
             "Toggle presentation of other Race players and their effects.");
}

/**
 * @brief Releases all Race CGAME subsystem state.
 */
void Cg_Module_Shutdown(void) {
  Cg_RaceDoubleJump_Clear();
  Cg_RaceWeaponTuning_Clear();
  Cg_RaceBarriers_Clear();
  Cg_RaceReplay_Clear();
  Cg_RaceFinishReport_Clear();
  Cg_RacePracticeMarkers_Shutdown();
  Cg_RaceTraining_Clear();
  Cg_RacePhysics_Shutdown();
}

bool Cg_Module_ParseMessage(const int32_t command) {
  return Cg_RaceWeaponTuning_ParseMessage(command) ||
         Cg_RaceHud_ParseMessage(command) ||
         Cg_RaceFinishReport_ParseMessage(command) ||
         Cg_RaceMapBrowser_ParseMessage(command) ||
         Cg_RaceReplay_ParseMessage(command);
}

void Cg_Module_ClearState(void) {
  Cg_RaceDoubleJump_Clear();
  Cg_RaceWeaponTuning_Clear();
  MainViewController_ClearState();
  Cg_RaceMapBrowser_Clear();
  Cg_RaceFinishReport_Clear();
  Cg_RaceHud_Clear();
  Cg_RaceReplay_Clear();
  Cg_RacePracticeMarkers_Clear();
  Cg_RacePhysics_Clear();
  Cg_RaceTraining_Clear();
  Cg_RaceBarriers_Clear();
}

bool Cg_Module_DisablePrediction(void) {
  return Cg_ReplayActive() || !Cg_HookPullSpeedValid() ||
         !Cg_RacePhysics_Synchronized();
}

/**
 * @brief Mirrors Race GAME's movement-only player collision policy.
 */
int32_t Cg_Module_PredictionClipMask(void) {
  return Race_MovementClipMask(CONTENTS_MASK_CLIP_PLAYER);
}

cm_trace_t Cg_Module_TracePrediction(const vec3_t start, const vec3_t end,
                                     const box3_t bounds) {
  return Cg_RaceBarriers_TracePrediction(start, end, bounds);
}

void Cg_Module_PreparePredictionCommand(pm_move_t *pm,
                                        const size_t index,
                                        const size_t count) {
  Cg_RaceBarriers_PreparePredictionCommand(index);
  if (index + 1u == count) {
    Cg_RaceDoubleJump_Preview(&pm->cmd);
  }
  Cg_RaceTraining_PreparePredictionCommand(pm, index, count);
}

void Cg_Module_CompletePredictionCommand(const pm_move_t *pm,
                                         const size_t index,
                                         const size_t count) {
  Cg_RaceTraining_CompletePredictionCommand(pm, index, count);
}

void Cg_Module_CompletePrediction(const pm_move_t *pm) {
  Cg_RaceTraining_CompletePrediction(pm);
}

void Cg_Module_LoadMedia(void) {
  Cg_RaceBarriers_Load();
  Cg_RacePracticeMarkers_Load();
  Cg_RaceReplay_LoadMedia();
}

static bool Cg_Module_ShouldHideClient(const int32_t client) {
  if (!cg_show_jumpers || cg_show_jumpers->integer || !cgi.client ||
      client < 0 || client >= MAX_CLIENTS) {
    return false;
  }
  return client != Cg_Self()->current.client;
}

bool Cg_Module_ShouldHideEntity(const cl_entity_t *entity) {
  if (!entity || !cg_show_jumpers || cg_show_jumpers->integer ||
      !cgi.client || entity == Cg_Self()) {
    return false;
  }

  const entity_state_t *state = &entity->current;
  if (state->effects & EF_CLIENT) {
    return true;
  }
  if (!Cg_Module_ShouldHideClient(state->client)) {
    return false;
  }
  if (state->solid == SOLID_PROJECTILE) {
    return true;
  }

  switch (state->trail) {
    case TRAIL_BLASTER:
    case TRAIL_GRENADE:
    case TRAIL_QUAKE_GRENADE:
    case TRAIL_ROCKET:
    case TRAIL_HYPERBLASTER:
    case TRAIL_LIGHTNING:
    case TRAIL_HOOK:
    case TRAIL_BFG:
    case TRAIL_GIB:
    case TRAIL_QUAKE_NAIL:
      return true;
    default:
      return false;
  }
}

/**
 * @brief Adds presentation-only course markers after common scene population.
 */
void Cg_Module_PopulateScene(void) {
  Cg_RaceBarriers_Draw();
  Cg_RaceMarkers_Draw();
  Cg_RacePracticeMarkers_Draw();
  Cg_RaceReplay_PopulateScene();
}

void Cg_Module_Update(void) {
  Cg_RaceWeaponTuning_Update();
}

void Cg_Module_UpdateUi(const player_state_t *ps) {
  HomeViewController_RefreshPlayerActions(ps);
  MainViewController_RefreshEscState();
  MainViewController_RefreshAdmin(ps);
  MainViewController_RefreshVote();
  VotingViewController_Refresh(ps);
}
