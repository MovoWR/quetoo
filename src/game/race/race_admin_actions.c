/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "g_local.h"

#include <string.h>

#include "race_admin.h"
#include "race_admin_actions.h"
#include "race_admin_service.h"
#include "race_actions.h"
#include "race_map_state.h"
#include "race_settings_service.h"
#include "race_vote_service.h"

#define RACE_ADMIN_KICK_REASON "Removed by Race administrator"

static void Race_AdminActions_Help(g_client_t *cl) {
  gi.ClientPrint(cl, PRINT_HIGH,
                 "Race admin commands:\n"
                 "  set radmin_password <password>; radmin <account>\n"
                 "  radmin logout\n"
                 "  race admin status\n"
                 "  race admin help\n"
                 "  gset <alias-or-cvar> <value>\n"
                 "  gget [alias-or-cvar]\n"
                 "  gclear <alias-or-cvar>\n"
                 "  mset <alias-or-cvar> <value>\n"
                 "  mget [alias-or-cvar]\n"
                 "  mclear <alias-or-cvar>\n"
                 "  allowcvar <list|add|remove|reload> ...\n"
                 "  race admin map <map>\n"
                 "  race admin map validate <map>\n"
                 "  race admin kick <client-slot>\n"
                 "  race admin vote cancel\n"
                 "  race admin account <list|add|remove|role|enable|disable|password> ...\n"
                 "  race admin_logout\n"
                 "The server console bootstraps the first owner once.\n");
}

static void Race_AdminActions_Settings(g_client_t *cl) {
  Race_SettingsService_PrintMigrationHint(cl);
}

static void Race_AdminActions_Cvar(g_client_t *cl) {
  Race_SettingsService_PrintMigrationHint(cl);
}

/**
 * @brief `race admin map validate <map>` - reports whether a map is installed.
 * @details Ungated on purpose, at every role: this reads whether a .bsp is
 * present in the server's search path and grants no authority over anything.
 * It is the read half of the menu's Add map surface, and it exists so an
 * administrator can tell "the server does not have that map" apart from "the
 * name was rejected" before scheduling it.
 *
 * The name is normalized the same way a map change normalizes it, so
 * `maps/edge.bsp`, `edge.bsp` and `edge` all answer the same question.
 */
static void Race_AdminActions_MapValidate(g_client_t *cl) {

  char map[RACE_MAP_IDENTITY_SIZE];

  if (!Race_MapState_CanonicalizeMap(gi.Argv(4), map)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator map validate rejected: invalid bounded map name\n");
    return;
  }

  char path[MAX_QPATH + sizeof("maps/.bsp")];
  q_snprintf(path, sizeof(path), "maps/%s.bsp", map);

  if (gi.FileExists(path)) {
    gi.ClientPrint(cl, PRINT_HIGH, "%s - present\n", path);
  } else {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "%s - not installed on this server\n", path);
  }
}

static void Race_AdminActions_Map(g_client_t *cl) {
  if (gi.Argc() == 5 && !strcmp(gi.Argv(3), "validate")) {
    Race_AdminActions_MapValidate(cl);
    return;
  }

  if (gi.Argc() != 4) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Usage: race admin map <map> | race admin map validate <map>\n");
    return;
  }

  char map[RACE_MAP_IDENTITY_SIZE];
  if (!Race_Actions_ValidateMap(gi.Argv(3), map)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_MAP_CHANGE, NULL, "invalid-map");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator map rejected: invalid or unavailable bounded map name\n");
    return;
  }

  if (!Race_AdminService_AuthorizeClientAction(
        cl, RACE_ADMIN_ACTION_MAP_CHANGE)) {
    return;
  }
  if (!Race_Actions_ScheduleMap(map)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_MAP_CHANGE, map, "schedule-failed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator map rejected: server could not schedule the map\n");
    return;
  }

  Race_AdminService_AuditClientAction(
    cl, RACE_ADMIN_ACTION_MAP_CHANGE, map, "scheduled");
  gi.ClientPrint(cl, PRINT_HIGH, "Race administrator map scheduled: %s\n", map);
}

static void Race_AdminActions_Kick(g_client_t *cl) {
  if (gi.Argc() != 4) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Usage: race admin kick <client-slot>\n");
    return;
  }

  int32_t target_slot;
  if (!Race_Admin_ParseClientSlot(gi.Argv(3), sv_max_clients->integer,
                                  &target_slot)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_PLAYER_KICK, NULL, "invalid-slot");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator kick rejected: invalid client slot\n");
    return;
  }

  g_client_t *target = ge.clients[target_slot];
  switch (Race_Admin_ValidateKickTarget(cl->ps.client, target_slot,
                                        sv_max_clients->integer,
                                        target && target->in_use)) {
    case RACE_ADMIN_KICK_TARGET_SELF:
      Race_AdminService_AuditClientAction(
        cl, RACE_ADMIN_ACTION_PLAYER_KICK, gi.Argv(3), "self-denied");
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Race administrator kick rejected: self-kick is not allowed\n");
      return;
    case RACE_ADMIN_KICK_TARGET_UNAVAILABLE:
      Race_AdminService_AuditClientAction(
        cl, RACE_ADMIN_ACTION_PLAYER_KICK, gi.Argv(3), "target-unavailable");
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Race administrator kick rejected: target is not connected\n");
      return;
    case RACE_ADMIN_KICK_TARGET_INVALID:
      Race_AdminService_AuditClientAction(
        cl, RACE_ADMIN_ACTION_PLAYER_KICK, NULL, "invalid-target");
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Race administrator kick rejected: invalid target\n");
      return;
    case RACE_ADMIN_KICK_TARGET_OK:
      break;
  }

  if (!Race_AdminService_AuthorizeClientAction(
        cl, RACE_ADMIN_ACTION_PLAYER_KICK)) {
    return;
  }
  if (!Race_Actions_KickClient(target, RACE_ADMIN_KICK_REASON)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_PLAYER_KICK, gi.Argv(3), "kick-failed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator kick rejected: target changed or disconnected\n");
    return;
  }

  Race_AdminService_AuditClientAction(
    cl, RACE_ADMIN_ACTION_PLAYER_KICK, gi.Argv(3), "kicked");
  gi.ClientPrint(cl, PRINT_HIGH,
                 "Race administrator kick completed: slot=%d\n", target_slot);
}

static void Race_AdminActions_Vote(g_client_t *cl) {
  if (gi.Argc() == 4 && !strcmp(gi.Argv(3), "cancel")) {
    Race_VoteService_AdminCancel(cl);
    return;
  }
  gi.ClientPrint(cl, PRINT_HIGH,
                 "Usage: race admin vote cancel\n");
}

void Race_AdminActions_ClientCommand(g_client_t *cl) {
  if (!cl || !cl->in_use) {
    return;
  }

  if (gi.Argc() == 2 ||
      (gi.Argc() == 3 && !strcmp(gi.Argv(2), "status"))) {
    Race_AdminService_PrintClientStatus(cl);
  } else if (gi.Argc() == 3 && !strcmp(gi.Argv(2), "help")) {
    Race_AdminActions_Help(cl);
  } else if (gi.Argc() >= 3 && !strcmp(gi.Argv(2), "settings")) {
    Race_AdminActions_Settings(cl);
  } else if (gi.Argc() >= 3 && !strcmp(gi.Argv(2), "cvar")) {
    Race_AdminActions_Cvar(cl);
  } else if (gi.Argc() >= 3 && !strcmp(gi.Argv(2), "allowcvar")) {
    const int32_t argc = gi.Argc();
    Race_AdminService_ClientCvarAllowlistCommand(
      cl, argc >= 4 && argc <= 5 ? gi.Argv(3) : NULL,
      argc == 5 ? gi.Argv(4) : NULL);
  } else if (gi.Argc() >= 3 && !strcmp(gi.Argv(2), "map")) {
    Race_AdminActions_Map(cl);
  } else if (gi.Argc() >= 3 && !strcmp(gi.Argv(2), "kick")) {
    Race_AdminActions_Kick(cl);
  } else if (gi.Argc() >= 3 && !strcmp(gi.Argv(2), "vote")) {
    Race_AdminActions_Vote(cl);
  } else if (gi.Argc() >= 3 && !strcmp(gi.Argv(2), "account")) {
    Race_AdminService_ClientAccountCommand(cl);
  } else {
    Race_AdminActions_Help(cl);
  }
}
