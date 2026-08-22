/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "g_local.h"

#include <stdlib.h>
#include <string.h>

#include "race_actions.h"
#include "race_map_browser_service.h"
#include "race_map_catalog.h"
#include "race_settings_service.h"
#include "race_vote.h"
#include "race_vote_menu.h"
#include "race_vote_menu_service.h"
#include "race_vote_service.h"

typedef struct {
  char map[RACE_MAP_IDENTITY_SIZE];
} race_vote_nomination_t;

static race_vote_menu_state_t race_vote_menu;
static race_vote_nomination_t race_vote_nominations[MAX_CLIENTS];
static uint32_t race_vote_menu_intermission;
static uint32_t race_vote_menu_next_publish;

static void Race_VoteMenuService_ClearWire(void) {
  gi.SetConfigString(CS_RACE_VOTE_MENU, "");
}

static void Race_VoteMenuService_Publish(void) {
  if (!race_vote_menu.active) {
    Race_VoteMenuService_ClearWire();
    return;
  }

  char wire[MAX_STRING_CHARS];
  const int32_t initial = q_snprintf(
    wire, sizeof(wire), "%u|",
    Race_Vote_TimeRemainingSeconds(g_level.time, race_vote_menu.deadline));
  if (initial < 0 || (size_t) initial >= sizeof(wire)) {
    Race_VoteMenuService_ClearWire();
    return;
  }
  size_t offset = (size_t) initial;

  for (uint8_t i = 0; i < race_vote_menu.num_choices; i++) {
    const int32_t written = q_snprintf(
      wire + offset, sizeof(wire) - offset, "%s%s\\%u",
      i ? "\\" : "", race_vote_menu.choices[i].name,
      race_vote_menu.choices[i].votes);
    if (written < 0 || (size_t) written >= sizeof(wire) - offset) {
      Race_VoteMenuService_ClearWire();
      return;
    }
    offset += (size_t) written;
  }
  gi.SetConfigString(CS_RACE_VOTE_MENU, wire);
  race_vote_menu_next_publish = g_level.time + 1000u;
}

static bool Race_VoteMenuService_HasChoice(
    const char *const *choices, const size_t count, const char *name) {
  for (size_t i = 0; i < count; i++) {
    if (!q_strcmp(choices[i], name)) {
      return true;
    }
  }
  return false;
}

static size_t Race_VoteMenuService_PickMaps(
    const race_map_catalog_t *catalog, const size_t maximum,
    const char *choices[RACE_VOTE_MENU_MAX_CHOICES]) {
  size_t count = 0u;
  for (uint16_t slot = 0; slot < MAX_CLIENTS && count < maximum; slot++) {
    const race_vote_nomination_t *nomination = race_vote_nominations + slot;
    if (*nomination->map &&
        Race_MapCatalog_Find(catalog, nomination->map) &&
        !Race_VoteMenuService_HasChoice(choices, count, nomination->map)) {
      choices[count++] = nomination->map;
    }
  }

  uint16_t indices[RACE_MAP_CATALOG_MAX_ENTRIES];
  for (size_t i = 0; i < catalog->count; i++) {
    indices[i] = (uint16_t) i;
  }
  for (size_t i = catalog->count; i > 1u; i--) {
    const size_t other = (size_t) RandomRangei(0, (int32_t) i);
    const uint16_t swap = indices[i - 1u];
    indices[i - 1u] = indices[other];
    indices[other] = swap;
  }
  for (size_t i = 0; i < catalog->count && count < maximum; i++) {
    const char *name = catalog->entries[indices[i]].name;
    if (!Race_VoteMenuService_HasChoice(choices, count, name)) {
      choices[count++] = name;
    }
  }
  return count;
}

static void Race_VoteMenuService_Begin(void) {
  const int32_t configured_choices =
    Race_SettingsService_VoteMenuChoices();
  const int32_t configured_duration =
    Race_SettingsService_VoteMenuDuration();
  if (configured_choices < 1 || configured_duration < 1) {
    Race_VoteMenuService_ClearWire();
    return;
  }

  race_map_catalog_t catalog;
  if (!Race_MapBrowserService_LoadCatalog(&catalog) || !catalog.count) {
    G_Warn("Race next-map vote unavailable: map catalog is empty\n");
    Race_VoteMenuService_ClearWire();
    return;
  }

  const size_t maximum = configured_choices >
                           (int32_t) RACE_VOTE_MENU_MAX_CHOICES
    ? RACE_VOTE_MENU_MAX_CHOICES
    : (size_t) configured_choices;
  const int32_t maximum_clients = sv_max_clients->integer > MAX_CLIENTS
    ? MAX_CLIENTS
    : sv_max_clients->integer;
  const char *choices[RACE_VOTE_MENU_MAX_CHOICES] = { 0 };
  const size_t count = Race_VoteMenuService_PickMaps(&catalog, maximum,
                                                      choices);
  if (!Race_VoteMenu_Begin(
        &race_vote_menu, choices, count,
        (uint16_t) maximum_clients, g_level.time,
        (uint32_t) configured_duration * 1000u,
        Race_SettingsService_VoteAllowSpectators())) {
    Race_VoteMenuService_ClearWire();
    return;
  }

  Race_VoteService_NextMapVoteBegin();
  Race_VoteMenuService_Publish();
  gi.BroadcastPrint(PRINT_HIGH, "^2=== Vote for the next map! ===^7\n");
  for (uint8_t i = 0; i < race_vote_menu.num_choices; i++) {
    gi.BroadcastPrint(PRINT_HIGH, "  ^2%u^7: %s\n", i + 1u,
                      race_vote_menu.choices[i].name);
  }
  gi.BroadcastPrint(PRINT_HIGH,
                    "Type ^21^7-^2%u^7 or ^2vote_menu <choice>^7 to vote.\n",
                    race_vote_menu.num_choices);
}

static void Race_VoteMenuService_Resolve(void) {
  uint8_t tied[RACE_VOTE_MENU_MAX_CHOICES];
  uint16_t winning_votes;
  const size_t tied_count = Race_VoteMenu_TiedWinners(
    &race_vote_menu, tied, &winning_votes);

  char winner[RACE_MAP_IDENTITY_SIZE];
  q_strlcpy(winner, g_level.name, sizeof(winner));
  if (tied_count) {
    uint8_t winner_index;
    const size_t ordinal = tied_count > 1u
      ? (size_t) RandomRangei(0, (int32_t) tied_count)
      : 0u;
    if (Race_VoteMenu_Resolve(&race_vote_menu, ordinal, &winner_index,
                              &winning_votes)) {
      q_strlcpy(winner, race_vote_menu.choices[winner_index].name,
                sizeof(winner));
      gi.BroadcastPrint(PRINT_HIGH,
                        "^2Vote over!^7 Next map: ^2%s^7 (%u votes)\n",
                        winner, winning_votes);
    }
  } else {
    gi.BroadcastPrint(PRINT_HIGH,
                      "No votes cast - staying on current map.\n");
  }

  Race_VoteMenu_Init(&race_vote_menu);
  Race_VoteMenuService_ClearWire();
  if (!Race_Actions_ScheduleMap(winner)) {
    G_Warn("Race next-map vote could not schedule %s; server rotation will continue\n",
           winner);
  }
}

void Race_VoteMenuService_Init(void) {
  Race_VoteMenu_Init(&race_vote_menu);
  memset(race_vote_nominations, 0, sizeof(race_vote_nominations));
  race_vote_menu_intermission = 0u;
  race_vote_menu_next_publish = 0u;
  Race_VoteMenuService_ClearWire();
}

void Race_VoteMenuService_Shutdown(void) {
  Race_VoteMenuService_Init();
}

void Race_VoteMenuService_ConfigureLevel(void) {
  Race_VoteMenuService_Init();
}

void Race_VoteMenuService_Frame(void) {
  if (g_level.intermission_time &&
      race_vote_menu_intermission != g_level.intermission_time) {
    race_vote_menu_intermission = g_level.intermission_time;
    Race_VoteMenuService_Begin();
  }
  if (!race_vote_menu.active) {
    return;
  }
  if (Race_VoteMenu_Expired(&race_vote_menu, g_level.time)) {
    Race_VoteMenuService_Resolve();
  } else if ((int32_t) (g_level.time - race_vote_menu_next_publish) >= 0) {
    Race_VoteMenuService_Publish();
  }
}

void Race_VoteMenuService_ClientDisconnect(g_client_t *cl) {
  if (!cl || cl->ps.client >= MAX_CLIENTS) {
    return;
  }
  const uint16_t slot = (uint16_t) cl->ps.client;
  Race_VoteMenu_RemoveVoter(&race_vote_menu, slot);
  memset(race_vote_nominations + slot, 0,
         sizeof(race_vote_nominations[slot]));
  if (race_vote_menu.active) {
    Race_VoteMenuService_Publish();
  }
}

bool Race_VoteMenuService_ClientCanCast(const g_client_t *cl) {
  return cl && cl->in_use && cl->ps.client < MAX_CLIENTS &&
         Race_VoteMenu_CanCast(&race_vote_menu, (uint16_t) cl->ps.client,
                               cl->persistent.spectator);
}

bool Race_VoteMenuService_ClientCanNominate(const g_client_t *cl) {
  return cl && cl->in_use && cl->ps.client < MAX_CLIENTS &&
         !race_vote_menu.active &&
         (!cl->persistent.spectator ||
          Race_SettingsService_VoteAllowSpectators());
}

void Race_VoteMenuService_Nominate(g_client_t *cl, const char *map) {
  if (!cl || !cl->in_use) {
    return;
  }
  if (race_vote_menu.active) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "The next-map vote is already active.\n");
    return;
  }
  if (cl->persistent.spectator &&
      !Race_SettingsService_VoteAllowSpectators()) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Spectators cannot nominate maps.\n");
    return;
  }
  if (!map || !*map) {
    gi.ClientPrint(cl, PRINT_HIGH, "Usage: nominate <mapname>\n");
    return;
  }

  char canonical[RACE_MAP_IDENTITY_SIZE];
  race_map_catalog_t catalog;
  if (!Race_MapState_CanonicalizeMap(map, canonical) ||
      !Race_MapBrowserService_LoadCatalog(&catalog) ||
      !Race_MapCatalog_Find(&catalog, canonical)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Map ^1%s^7 is not in the maplist.\n", map);
    return;
  }
  if (cl->ps.client >= MAX_CLIENTS) {
    return;
  }
  race_vote_nomination_t *nomination =
    race_vote_nominations + cl->ps.client;
  if (!q_strcmp(nomination->map, canonical)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "You have already nominated ^2%s^7.\n", canonical);
    return;
  }
  q_strlcpy(nomination->map, canonical, sizeof(nomination->map));
  gi.BroadcastPrint(PRINT_HIGH,
                    "%s nominated ^2%s^7 for the next-map vote.\n",
                    cl->persistent.net_name, canonical);
}

static void Race_VoteMenuService_Cast(g_client_t *cl, const int32_t choice) {
  const race_vote_menu_cast_result_t result = Race_VoteMenu_Cast(
    &race_vote_menu, (uint16_t) cl->ps.client,
    cl->persistent.spectator, (uint8_t) choice);
  switch (result) {
    case RACE_VOTE_MENU_CAST_ACCEPTED:
    case RACE_VOTE_MENU_CAST_CHANGED:
      gi.ClientPrint(cl, PRINT_HIGH, "You voted for ^2%s^7.\n",
                     race_vote_menu.choices[choice - 1].name);
      Race_VoteMenuService_Publish();
      return;
    case RACE_VOTE_MENU_CAST_UNCHANGED:
      gi.ClientPrint(cl, PRINT_HIGH, "You already voted for ^2%s^7.\n",
                     race_vote_menu.choices[choice - 1].name);
      return;
    case RACE_VOTE_MENU_CAST_INACTIVE:
      gi.ClientPrint(cl, PRINT_HIGH,
                     "No vote menu is currently active.\n");
      return;
    case RACE_VOTE_MENU_CAST_SPECTATOR:
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Spectators cannot vote in the map vote menu.\n");
      return;
    case RACE_VOTE_MENU_CAST_INVALID_CHOICE:
      gi.ClientPrint(cl, PRINT_HIGH, "Invalid choice. Choose 1-%u.\n",
                     race_vote_menu.num_choices);
      return;
    case RACE_VOTE_MENU_CAST_INVALID_CLIENT:
      return;
  }
}

bool Race_VoteMenuService_ClientCommand(g_client_t *cl, const char *cmd) {
  if (!cl || !cl->in_use || !cmd) {
    return false;
  }
  int32_t choice = 0;
  if (cmd[0] >= '1' &&
      cmd[0] <= (char) ('0' + RACE_VOTE_MENU_MAX_CHOICES) &&
      !cmd[1]) {
    choice = cmd[0] - '0';
  } else if (!q_strcmp(cmd, "vote_menu")) {
    if (gi.Argc() > 1) {
      char *end;
      const long parsed = strtol(gi.Argv(1), &end, 10);
      if (end && !*end && parsed >= 1 && parsed <= INT32_MAX) {
        choice = (int32_t) parsed;
      }
    }
  } else {
    return false;
  }
  Race_VoteMenuService_Cast(cl, choice);
  return true;
}

bool Race_VoteMenuService_IntermissionReady(void) {
  return !race_vote_menu.active;
}
