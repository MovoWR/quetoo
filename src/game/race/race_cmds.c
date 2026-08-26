/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race.h"
#include "race_admin_actions.h"
#include "race_admin_service.h"
#include "race_cmds.h"
#include "race_map_browser_service.h"
#include "race_map_state_service.h"
#include "race_modes.h"
#include "race_profiles.h"
#include "race_replay_playback_service.h"
#include "race_settings_service.h"
#include "race_vote_service.h"
#include "race_vote_menu_service.h"
#include "race_weapon_tuning_service.h"

bool Race_ClientCommand(g_client_t *cl, const char *cmd) {

  if (Race_Profiles_ClientCommand(cl, cmd)) {
    return true;
  }

  // Claim both forms before the generic `race <replay-selector>` fallback.
  if (Race_WeaponTuningService_ClientCommand(cl, cmd)) {
    return true;
  }

  if (q_strcmp(cmd, "radmin") == 0) {
    if (gi.Argc() == 2 && q_strcmp(gi.Argv(1), "logout") == 0) {
      Race_AdminService_ClientLogout(cl);
    } else if (gi.Argc() == 2) {
      Race_AdminService_ClientChallenge(cl, gi.Argv(1));
    } else if (gi.Argc() == 5 && q_strcmp(gi.Argv(1), "proof") == 0) {
      Race_AdminService_ClientProof(cl, gi.Argv(2), gi.Argv(3), gi.Argv(4));
    } else {
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Usage: set radmin_password <password>; radmin <account> | radmin logout\n");
    }
    return true;
  }

  if (Race_SettingsService_ClientCommand(cl, cmd)) {
    return true;
  }

  if (Race_VoteMenuService_ClientCommand(cl, cmd)) {
    return true;
  }

  if (Race_VoteService_LegacyCommand(cl, cmd)) {
    return true;
  }

  if (Race_MapBrowserService_ClientCommand(cl, cmd)) {
    return true;
  }

  if (q_strcmp(cmd, "store") == 0) {
    Race_StoreSpawn(cl);
    return true;
  }

  if (q_strcmp(cmd, "restart_stage") == 0) {
    Race_RestartStage(cl);
    return true;
  }

  if (q_strcmp(cmd, "mode") == 0) {
    Race_ModeCommand(cl);
    return true;
  }

  if (q_strcmp(cmd, "nominate") == 0) {
    Race_VoteMenuService_Nominate(cl, gi.Argc() > 1 ? gi.Argv(1) : NULL);
    return true;
  }

  if (q_strcmp(cmd, "chase_toggle") == 0) {
    if (Race_Mode(cl) != RACE_MODE_SPECTATOR) {
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Chase camera is available in Spectator mode.\n");
    } else if (cl->chase_target) {
      cl->chase_target = NULL;
      cl->old_chase_target = NULL;
      cl->ps.pm_state.type = PM_SPECTATOR;
    } else {
      G_ClientChaseTarget(cl);
      if (!cl->chase_target) {
        gi.ClientPrint(cl, PRINT_HIGH,
                       "No active runner is available to chase.\n");
      }
    }
    return true;
  }

  if (q_strcmp(cmd, "replay_control") == 0) {
    Race_ReplayPlaybackService_ControlCommand(cl);
    return true;
  }

  if (q_strcmp(cmd, "replay_cancel") == 0) {
    Race_ReplayPlaybackService_CancelCommand(cl);
    return true;
  }

  if (q_strcmp(cmd, "replay") == 0) {
    Race_ReplayPlaybackService_ClientCommand(cl);
    return true;
  }

  if (q_strcmp(cmd, "raceline") == 0) {
    Race_ReplayPlaybackService_RacelineCommand(cl);
    return true;
  }

  if (q_strcmp(cmd, "race") == 0) {
    if (gi.Argc() < 2) {
      Race_RequestStart(cl);
    } else if (q_strcmp(gi.Argv(1), "tune") == 0) {
      // Already claimed above. Retain a fail-closed guard if dispatch changes.
      gi.ClientPrint(cl, PRINT_HIGH, "Weapon tuning command unavailable\n");
    } else if (q_strcmp(gi.Argv(1), "admin") == 0) {
      Race_AdminActions_ClientCommand(cl);
    } else if (q_strcmp(gi.Argv(1), "vote") == 0) {
      Race_VoteService_ClientCommand(cl);
    } else if (q_strcmp(gi.Argv(1), "help") == 0) {
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Race commands: mode, store, restart_stage, replay, raceline, radmin, race [status|help|mapstate|replay|raceline|vote|admin|admin_logout]\n");
    } else if (q_strcmp(gi.Argv(1), "replay") == 0) {
      Race_ReplayPlaybackService_ClientCommand(cl);
    } else if (q_strcmp(gi.Argv(1), "raceline") == 0) {
      Race_ReplayPlaybackService_RacelineCommand(cl);
    } else if (q_strcmp(gi.Argv(1), "mapstate") == 0) {
      Race_MapStateService_PrintStatus(cl);
    } else if (q_strcmp(gi.Argv(1), "admin_logout") == 0) {
      if (gi.Argc() == 2) {
        Race_AdminService_ClientLogout(cl);
      } else {
        gi.ClientPrint(cl, PRINT_HIGH, "Usage: race admin_logout\n");
      }
    } else if (q_strcmp(gi.Argv(1), "status") == 0) {
      Race_PrintStatus(cl);
    } else if (q_strcmp(gi.Argv(1), "off") == 0) {
      Race_ReplayPlaybackService_ExitClient(cl);
      Race_ReplayPlaybackService_ClientRunStarted(cl);
      G_Debug("client=%s replay=off raceline=off command=race\n",
              cl->persistent.net_name);
      gi.ClientPrint(cl, PRINT_HIGH, "Replay and raceline disabled.\n");
    } else {
      Race_ReplayPlaybackService_RaceSelect(cl, gi.Argv(1));
    }
    return true;
  }

  return false;
}
