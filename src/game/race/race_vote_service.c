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

#include "race_actions.h"
#include "race_admin.h"
#include "race_admin_service.h"
#include "race_map_browser_service.h"
#include "race_map_catalog.h"
#include "race_modes.h"
#include "race_physics.h"
#include "race_settings_service.h"
#include "race_vote.h"
#include "race_vote_service.h"

#define RACE_VOTE_KICK_REASON "Removed by successful Race vote"
#define RACE_VOTE_IDLE_TIMEOUT 120000u
#define RACE_VOTE_EXECUTE_DELAY 2500u
#define RACE_VOTE_SOLE_EXECUTE_DELAY 1500u
#define RACE_VOTE_PHYSICS_CVAR "g_race_physics"
#define RACE_VOTE_PHYSICS_KEYS \
  RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY "|" \
  RACE_PHYSICS_PRESET_Q2_V1_KEY "|" \
  RACE_PHYSICS_PRESET_QUETOO_FIX_V1_KEY

static race_vote_state_t race_vote;
static uint64_t race_vote_next_connection_id;
static bool race_vote_pending_execute;
static uint32_t race_vote_execute_time;
static uint32_t race_vote_next_publish;
static char race_vote_initiator_name[32];
static char race_vote_target_name[64];
static uint64_t race_vote_activity_connections[MAX_CLIENTS];
static uint32_t race_vote_activity_times[MAX_CLIENTS];

static uint64_t Race_VoteService_NewConnectionId(void) {
  race_vote_next_connection_id++;
  if (!race_vote_next_connection_id) {
    race_vote_next_connection_id++;
  }
  return race_vote_next_connection_id;
}

static race_vote_identity_t Race_VoteService_Identity(g_client_t *cl) {
  if (cl && !cl->persistent.race_vote_connection_id) {
    cl->persistent.race_vote_connection_id =
      Race_VoteService_NewConnectionId();
    if (cl->ps.client < MAX_CLIENTS) {
      race_vote_activity_connections[cl->ps.client] =
        cl->persistent.race_vote_connection_id;
      race_vote_activity_times[cl->ps.client] = g_level.time;
    }
  }
  return (race_vote_identity_t) {
    .slot = cl ? cl->ps.client : -1,
    .connection_id = cl ? cl->persistent.race_vote_connection_id : 0u
  };
}

uint64_t Race_VoteService_ConnectionId(g_client_t *cl) {
  return Race_VoteService_Identity(cl).connection_id;
}

bool Race_VoteService_ClientCanCast(g_client_t *cl) {
  return cl && cl->in_use && !race_vote_pending_execute &&
         Race_Vote_CanCast(&race_vote, Race_VoteService_Identity(cl));
}

static g_client_t *Race_VoteService_Client(race_vote_identity_t identity) {
  if (!Race_Vote_IdentityValid(identity, (uint16_t) sv_max_clients->integer)) {
    return NULL;
  }
  g_client_t *cl = ge.clients[identity.slot];
  return cl && cl->in_use &&
         cl->persistent.race_vote_connection_id == identity.connection_id
    ? cl
    : NULL;
}

static bool Race_VoteService_IsIdle(g_client_t *cl) {
  const race_vote_identity_t identity = Race_VoteService_Identity(cl);
  return !Race_Vote_IdentityValid(identity,
                                  (uint16_t) sv_max_clients->integer) ||
         race_vote_activity_connections[identity.slot] !=
           identity.connection_id ||
         g_level.time - race_vote_activity_times[identity.slot] >
           RACE_VOTE_IDLE_TIMEOUT;
}

static bool Race_VoteService_Eligible(g_client_t *cl,
                                      const bool allow_spectators) {
  if (!cl || !cl->entity || !cl->entity->in_use ||
      Race_VoteService_IsIdle(cl)) {
    return false;
  }

  const bool alive = !cl->entity->dead && cl->entity->health > 0;
  const race_mode_t mode = Race_Mode(cl);
  return Race_Vote_IsEligible(cl->in_use, cl->ai,
                              cl->persistent.spectator, alive, mode) ||
         (allow_spectators && cl->in_use && !cl->ai &&
          cl->persistent.spectator && alive);
}

static const char *Race_VoteService_TypeName(race_vote_type_t type) {
  return type == RACE_VOTE_TYPE_MAP ? "map" :
         type == RACE_VOTE_TYPE_KICK ? "kick" :
         type == RACE_VOTE_TYPE_PHYSICS ? "physics" : "invalid";
}

static const char *Race_VoteService_PhysicsDisplayName(const char *key) {
  const race_physics_preset_descriptor_t *preset =
    Race_Physics_PresetForKey(key);
  return preset ? preset->short_name : key;
}

static const char *Race_VoteService_Subject(const race_vote_state_t *vote) {
  if (vote->type == RACE_VOTE_TYPE_MAP) {
    return vote->target.map;
  }
  if (vote->type == RACE_VOTE_TYPE_PHYSICS) {
    return Race_VoteService_PhysicsDisplayName(vote->target.physics);
  }
  return *race_vote_target_name
    ? race_vote_target_name
    : va("slot %d", vote->target.kick.slot);
}

static void Race_VoteService_Sanitize(char *output, const size_t output_size,
                                      const char *input) {
  size_t offset = 0u;
  while (input && *input && offset + 1u < output_size) {
    char c = *input++;
    if (c == '|' || c == '\\' || c == '\r' || c == '\n') {
      c = '_';
    }
    output[offset++] = c;
  }
  output[offset] = '\0';
}

static void Race_VoteService_ClearPresentation(void) {
  gi.SetConfigString(CS_RACE_VOTE_INFO, "");
  race_vote_next_publish = 0u;
  race_vote_initiator_name[0] = '\0';
  race_vote_target_name[0] = '\0';
}

static void Race_VoteService_Publish(void) {
  if (!race_vote.active) {
    Race_VoteService_ClearPresentation();
    return;
  }
  char initiator[sizeof(race_vote_initiator_name)];
  char target[sizeof(race_vote_target_name)];
  Race_VoteService_Sanitize(initiator, sizeof(initiator),
                            race_vote_initiator_name);
  Race_VoteService_Sanitize(target, sizeof(target),
                            Race_VoteService_Subject(&race_vote));
  gi.SetConfigString(
    CS_RACE_VOTE_INFO,
    va("%s|%s|%s|%u|%u|%u|%u",
       Race_VoteService_TypeName(race_vote.type), initiator, target,
       race_vote.yes_count, race_vote.no_count,
       Race_Vote_RequiredYes(race_vote.eligible_count),
       Race_Vote_TimeRemainingSeconds(g_level.time, race_vote.deadline)));
  race_vote_next_publish = g_level.time + 1000u;
}

static void Race_VoteService_PrintUsage(g_client_t *cl) {
  gi.ClientPrint(cl, PRINT_HIGH,
                 "Race vote commands:\n"
                 "  race vote status\n"
                 "  race vote map <map>\n"
                 "  race vote kick <client-slot>\n"
                 "  race vote physics <" RACE_VOTE_PHYSICS_KEYS ">\n"
                 "  race vote yes\n"
                 "  race vote no\n");
}

static void Race_VoteService_PrintStatus(g_client_t *cl) {
  if (!race_vote.active) {
    gi.ClientPrint(cl, PRINT_HIGH, "Race vote: inactive\n");
    return;
  }

  gi.ClientPrint(
    cl, PRINT_HIGH,
    "Race vote #%llu: %s %s; yes=%u no=%u eligible=%u required=%u quorum=%u remaining=%ums\n",
    (unsigned long long) race_vote.generation,
    Race_VoteService_TypeName(race_vote.type),
    Race_VoteService_Subject(&race_vote), race_vote.yes_count,
    race_vote.no_count, race_vote.eligible_count,
    Race_Vote_RequiredYes(race_vote.eligible_count),
    Race_Vote_RequiredQuorum(race_vote.eligible_count),
    Race_Vote_TimeRemaining(g_level.time, race_vote.deadline));
}

static bool Race_VoteService_PhysicsTarget(const char *input,
                                           char output[RACE_VOTE_PHYSICS_SIZE]) {
  if (!input) {
    return false;
  }

  const char *selector = input;
  if (!q_strcmp(input, "quetoo")) {
    selector = RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY;
  } else if (!q_strcmp(input, "quetoo_fix")) {
    selector = RACE_PHYSICS_PRESET_QUETOO_FIX_V1_KEY;
  } else if (!q_strcmp(input, "q2pro")) {
    selector = RACE_PHYSICS_PRESET_Q2_V1_KEY;
  }

  race_physics_config_t config;
  if (!Race_Physics_ConfigForSelector(selector, &config)) {
    return false;
  }
  const race_physics_preset_descriptor_t *preset =
    Race_Physics_Preset(config.preset);
  if (!preset || q_strlen(preset->key) >= RACE_VOTE_PHYSICS_SIZE) {
    return false;
  }
  q_strlcpy(output, preset->key, RACE_VOTE_PHYSICS_SIZE);
  return true;
}

static bool Race_VoteService_MapTarget(
    const char *input, char output[RACE_MAP_IDENTITY_SIZE]) {
  race_map_catalog_t catalog;
  return Race_Actions_ValidateMap(input, output) &&
         Race_MapBrowserService_LoadCatalog(&catalog) &&
         Race_MapCatalog_Find(&catalog, output) != NULL;
}

static bool Race_VoteService_ValidateAction(void) {
  if (race_vote.type == RACE_VOTE_TYPE_MAP) {
    char canonical[RACE_MAP_IDENTITY_SIZE];
    return Race_VoteService_MapTarget(race_vote.target.map, canonical) &&
           !strcmp(canonical, race_vote.target.map);
  }
  if (race_vote.type == RACE_VOTE_TYPE_KICK) {
    return Race_VoteService_Client(race_vote.target.kick) != NULL;
  }
  if (race_vote.type == RACE_VOTE_TYPE_PHYSICS) {
    char canonical[RACE_VOTE_PHYSICS_SIZE];
    return Race_VoteService_PhysicsTarget(race_vote.target.physics,
                                          canonical) &&
           !strcmp(canonical, race_vote.target.physics);
  }
  return false;
}

static bool Race_VoteService_Apply(const race_vote_state_t *completed) {
  if (completed->type == RACE_VOTE_TYPE_MAP) {
    char canonical[RACE_MAP_IDENTITY_SIZE];
    return Race_VoteService_MapTarget(completed->target.map, canonical) &&
           !strcmp(canonical, completed->target.map) &&
           Race_Actions_ScheduleMap(completed->target.map);
  }
  if (completed->type == RACE_VOTE_TYPE_KICK) {
    return Race_Actions_KickClient(
      Race_VoteService_Client(completed->target.kick),
      RACE_VOTE_KICK_REASON);
  }
  if (completed->type == RACE_VOTE_TYPE_PHYSICS) {
    const race_physics_config_t *current = Race_Physics_Current();
    const race_physics_preset_descriptor_t *previous = current
      ? Race_Physics_Preset(current->preset)
      : NULL;
    if (!previous) {
      return false;
    }
    if (!gi.SetCvarString(RACE_VOTE_PHYSICS_CVAR,
                          completed->target.physics)) {
      return false;
    }
    if (Race_Actions_ScheduleMap(g_level.name)) {
      return true;
    }
    if (!gi.SetCvarString(RACE_VOTE_PHYSICS_CVAR, previous->key)) {
      G_Warn("Race vote could not restore physics selector %s after reload scheduling failed\n",
             previous->key);
    }
    return false;
  }
  return false;
}

static void Race_VoteService_Execute(void) {
  race_vote_pending_execute = false;
  race_vote_state_t completed;
  if (!Race_Vote_Complete(&race_vote, RACE_VOTE_OUTCOME_PASSED,
                          &completed)) {
    return;
  }
  char subject[sizeof(race_vote_target_name)];
  q_strlcpy(subject, Race_VoteService_Subject(&completed), sizeof(subject));
  const bool applied = Race_VoteService_Apply(&completed);
  if (applied) {
    gi.BroadcastPrint(PRINT_HIGH, "Race vote #%llu passed: %s %s\n",
                      (unsigned long long) completed.generation,
                      Race_VoteService_TypeName(completed.type),
                      subject);
  } else {
    gi.BroadcastPrint(
      PRINT_HIGH,
      "Race vote #%llu passed but its validated action was unavailable; no action was applied\n",
      (unsigned long long) completed.generation);
  }
  Race_VoteService_ClearPresentation();
}

static void Race_VoteService_Resolve(race_vote_outcome_t outcome) {
  if (outcome == RACE_VOTE_OUTCOME_PASSED && race_vote.active) {
    if (!Race_VoteService_ValidateAction()) {
      outcome = race_vote.type == RACE_VOTE_TYPE_KICK
        ? RACE_VOTE_OUTCOME_CANCELLED_TARGET_GONE
        : RACE_VOTE_OUTCOME_CANCELLED_MAP_UNAVAILABLE;
    } else {
      if (!Race_Vote_MarkPassed(&race_vote, g_level.time)) {
        G_Warn("Race vote passed but could not enter its terminal state\n");
        return;
      }
      race_vote_pending_execute = true;
      race_vote_execute_time = g_level.time +
        (race_vote.eligible_count == 1u
          ? RACE_VOTE_SOLE_EXECUTE_DELAY
          : RACE_VOTE_EXECUTE_DELAY);
      gi.BroadcastPrint(PRINT_HIGH, "^2Vote passed!^7\n");
      Race_VoteService_Publish();
      return;
    }
  }

  race_vote_state_t completed;
  if (!Race_Vote_Complete(&race_vote, outcome, &completed)) {
    return;
  }

  const char *reason = "failed: required yes threshold was not reached";
  if (outcome == RACE_VOTE_OUTCOME_FAILED_QUORUM) {
    reason = "failed: quorum was not reached";
  } else if (outcome == RACE_VOTE_OUTCOME_FAILED_NO_ELIGIBLE) {
    reason = "failed: no eligible voters remain";
  } else if (outcome == RACE_VOTE_OUTCOME_CANCELLED_TARGET_GONE) {
    reason = "cancelled: kick target disconnected or changed";
  } else if (outcome == RACE_VOTE_OUTCOME_CANCELLED_MAP_UNAVAILABLE) {
    reason = "cancelled: map is no longer available";
  } else if (outcome == RACE_VOTE_OUTCOME_CANCELLED_INTERMISSION) {
    reason = "cancelled: the next-map intermission vote began";
  } else if (outcome == RACE_VOTE_OUTCOME_CANCELLED_ADMIN) {
    reason = "cancelled by a Race administrator";
  }
  gi.BroadcastPrint(PRINT_HIGH, "Race vote #%llu %s\n",
                    (unsigned long long) completed.generation, reason);
  race_vote_pending_execute = false;
  Race_VoteService_ClearPresentation();
}

static void Race_VoteService_Evaluate(void) {
  if (race_vote_pending_execute) {
    return;
  }
  const race_vote_outcome_t outcome = Race_Vote_Evaluate(&race_vote,
                                                          g_level.time);
  if (outcome != RACE_VOTE_OUTCOME_INACTIVE &&
      outcome != RACE_VOTE_OUTCOME_PENDING) {
    Race_VoteService_Resolve(outcome);
  }
}

static bool Race_VoteService_CheckInitiator(g_client_t *cl) {
  if (g_level.intermission_time) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race vote rejected: votes cannot start during intermission\n");
    return false;
  }
  if (Race_SettingsService_VotingTime() <= 0) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race vote rejected: voting is disabled on this server\n");
    return false;
  }
  if (cl->persistent.spectator) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "You cannot call a vote as spectator.\n");
    return false;
  }
  if (cl->ai) {
    gi.ClientPrint(cl, PRINT_HIGH, "Bots cannot call votes.\n");
    return false;
  }
  if (!cl->entity || cl->entity->dead || cl->entity->health <= 0) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "You cannot call a vote while dead.\n");
    return false;
  }
  if (Race_VoteService_IsIdle(cl)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "You are idle and cannot call a vote.\n");
    return false;
  }
  if (race_vote.active) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race vote rejected: another vote is active\n");
    return false;
  }

  const race_vote_start_availability_t availability =
    Race_Vote_StartAvailability(
      g_level.time, cl->persistent.race_vote_next_start_time,
      cl->persistent.race_vote_starts,
      (uint8_t) Race_SettingsService_MaxVotes());
  if (availability == RACE_VOTE_START_LIMIT) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race vote rejected: per-map start limit reached\n");
    return false;
  }
  if (availability == RACE_VOTE_START_COOLDOWN) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race vote rejected: start cooldown has %ums remaining\n",
                   Race_Vote_TimeRemaining(
                     g_level.time, cl->persistent.race_vote_next_start_time));
    return false;
  }
  return true;
}

static bool Race_VoteService_Begin(g_client_t *cl,
                                   race_vote_request_t *request) {
  request->initiator = Race_VoteService_Identity(cl);
  request->start_time = g_level.time;
  request->duration = (uint32_t) Race_SettingsService_VotingTime() * 1000u;
  request->max_clients = (uint16_t) sv_max_clients->integer;

  const bool allow_spectators =
    Race_SettingsService_VoteAllowSpectators();
  G_ForEachClient(voter, {
    if (Race_VoteService_Eligible(voter, allow_spectators)) {
      const race_vote_identity_t identity = Race_VoteService_Identity(voter);
      request->eligible_connection_ids[identity.slot] = identity.connection_id;
    }
  });

  if (!Race_Vote_Begin(&race_vote, request)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race vote rejected: authoritative vote state is invalid\n");
    return false;
  }

  cl->persistent.race_vote_starts++;
  cl->persistent.race_vote_next_start_time =
    g_level.time + request->duration * 5u;
  q_strlcpy(race_vote_initiator_name, cl->persistent.net_name,
            sizeof(race_vote_initiator_name));
  if (request->type == RACE_VOTE_TYPE_KICK) {
    g_client_t *target = Race_VoteService_Client(request->target.kick);
    q_strlcpy(race_vote_target_name,
              target ? target->persistent.net_name : "unavailable",
              sizeof(race_vote_target_name));
  } else {
    q_strlcpy(race_vote_target_name, Race_VoteService_Subject(&race_vote),
              sizeof(race_vote_target_name));
  }
  gi.BroadcastPrint(
    PRINT_HIGH,
    "%s started Race vote #%llu: %s %s (yes=%u required=%u eligible=%u, %us)\n",
    cl->persistent.net_name, (unsigned long long) race_vote.generation,
    Race_VoteService_TypeName(race_vote.type),
    Race_VoteService_Subject(&race_vote), race_vote.yes_count,
    Race_Vote_RequiredYes(race_vote.eligible_count), race_vote.eligible_count,
    request->duration / 1000u);
  Race_VoteService_Publish();
  Race_VoteService_Evaluate();
  return true;
}

static void Race_VoteService_StartMap(g_client_t *cl, const char *map,
                                      const char *usage) {
  if (!map || !*map) {
    gi.ClientPrint(cl, PRINT_HIGH, "%s\n", usage);
    return;
  }
  if (!Race_VoteService_CheckInitiator(cl)) {
    return;
  }

  race_vote_request_t request = { .type = RACE_VOTE_TYPE_MAP };
  if (!Race_VoteService_MapTarget(map, request.target.map)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race vote rejected: map is invalid, unavailable or not in the Race catalog\n");
    return;
  }
  Race_VoteService_Begin(cl, &request);
}

static g_client_t *Race_VoteService_FindKickTarget(const char *target,
                                                   bool *ambiguous) {
  if (ambiguous) {
    *ambiguous = false;
  }
  if (!target || !*target) {
    return NULL;
  }
  int32_t slot;
  if (Race_Admin_ParseClientSlot(target, sv_max_clients->integer, &slot)) {
    return ge.clients[slot] && ge.clients[slot]->in_use
      ? ge.clients[slot]
      : NULL;
  }
  g_client_t *match = NULL;
  G_ForEachClient(candidate, {
    if (!q_strcolorcmp(target, candidate->persistent.net_name)) {
      if (match) {
        if (ambiguous) {
          *ambiguous = true;
        }
      } else {
        match = candidate;
      }
    }
  });
  return ambiguous && *ambiguous ? NULL : match;
}

static void Race_VoteService_StartKick(g_client_t *cl, const char *name,
                                       const char *usage) {
  if (!name || !*name) {
    gi.ClientPrint(cl, PRINT_HIGH, "%s\n", usage);
    return;
  }
  if (!Race_VoteService_CheckInitiator(cl)) {
    return;
  }

  bool ambiguous = false;
  g_client_t *target = Race_VoteService_FindKickTarget(name, &ambiguous);
  if (!target) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   ambiguous
                     ? "Player name ^1%s^7 is ambiguous; use the client slot.\n"
                     : "Player ^1%s^7 not found.\n",
                   name);
    return;
  }
  const int32_t slot = target->ps.client;
  switch (Race_Admin_ValidateKickTarget(
            cl->ps.client, slot, sv_max_clients->integer,
            target && target->in_use)) {
    case RACE_ADMIN_KICK_TARGET_SELF:
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Race vote rejected: self-kick is not allowed\n");
      return;
    case RACE_ADMIN_KICK_TARGET_UNAVAILABLE:
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Race vote rejected: target is not connected\n");
      return;
    case RACE_ADMIN_KICK_TARGET_INVALID:
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Race vote rejected: invalid kick target\n");
      return;
    case RACE_ADMIN_KICK_TARGET_OK:
      break;
  }

  race_vote_request_t request = { .type = RACE_VOTE_TYPE_KICK };
  request.target.kick = Race_VoteService_Identity(target);
  Race_VoteService_Begin(cl, &request);
}

static void Race_VoteService_StartPhysics(g_client_t *cl,
                                          const char *physics,
                                          const char *usage) {
  if (!physics || !*physics) {
    gi.ClientPrint(cl, PRINT_HIGH, "%s\n", usage);
    return;
  }
  if (!Race_VoteService_CheckInitiator(cl)) {
    return;
  }
  race_vote_request_t request = { .type = RACE_VOTE_TYPE_PHYSICS };
  if (!Race_VoteService_PhysicsTarget(physics, request.target.physics)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Unknown physics mode: %s. Use "
                   RACE_VOTE_PHYSICS_KEYS ".\n",
                   physics);
    return;
  }
  const race_physics_preset_descriptor_t *current =
    Race_Physics_Preset(Race_Physics_Current()->preset);
  if (current && !q_strcmp(current->key, request.target.physics)) {
    gi.ClientPrint(cl, PRINT_HIGH, "Physics mode is already %s.\n",
                   Race_VoteService_PhysicsDisplayName(
                     request.target.physics));
    return;
  }
  Race_VoteService_Begin(cl, &request);
}

static void Race_VoteService_Cast(g_client_t *cl,
                                  race_vote_ballot_t ballot) {
  if (race_vote_pending_execute) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race vote already passed; its action is pending.\n");
    return;
  }
  const race_vote_cast_result_t result = Race_Vote_Cast(
    &race_vote, Race_VoteService_Identity(cl), ballot);
  switch (result) {
    case RACE_VOTE_CAST_ACCEPTED:
    case RACE_VOTE_CAST_CHANGED:
      break;
    case RACE_VOTE_CAST_UNCHANGED:
      gi.ClientPrint(cl, PRINT_HIGH, "Your Race vote is unchanged.\n");
      return;
    case RACE_VOTE_CAST_INACTIVE:
      gi.ClientPrint(cl, PRINT_HIGH, "Race vote rejected: no vote is active\n");
      return;
    case RACE_VOTE_CAST_INELIGIBLE:
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Race vote rejected: this connection was not eligible when the vote started\n");
      return;
    case RACE_VOTE_CAST_INVALID:
      gi.ClientPrint(cl, PRINT_HIGH, "Race vote rejected: invalid ballot\n");
      return;
  }
  Race_VoteService_NoteActivity(cl);
  Race_VoteService_Publish();
  Race_VoteService_Evaluate();
}

void Race_VoteService_Init(void) {
  memset(&race_vote, 0, sizeof(race_vote));
  memset(race_vote_activity_connections, 0,
         sizeof(race_vote_activity_connections));
  memset(race_vote_activity_times, 0, sizeof(race_vote_activity_times));
  race_vote_next_connection_id = 0u;
  race_vote_pending_execute = false;
  race_vote_execute_time = 0u;
  Race_VoteService_ClearPresentation();
}

void Race_VoteService_Shutdown(void) {
  memset(&race_vote, 0, sizeof(race_vote));
  race_vote_pending_execute = false;
  Race_VoteService_ClearPresentation();
}

void Race_VoteService_ConfigureLevel(void) {
  memset(&race_vote, 0, sizeof(race_vote));
  race_vote_pending_execute = false;
  memset(race_vote_activity_connections, 0,
         sizeof(race_vote_activity_connections));
  memset(race_vote_activity_times, 0, sizeof(race_vote_activity_times));
  Race_VoteService_ClearPresentation();
  G_ForEachClient(cl, {
    cl->persistent.race_vote_next_start_time = 0u;
    cl->persistent.race_vote_starts = 0u;
    Race_VoteService_NoteActivity(cl);
  });
}

void Race_VoteService_ClientUserInfoChanged(g_client_t *cl) {
  (void) Race_VoteService_Identity(cl);
}

void Race_VoteService_ClientDisconnect(g_client_t *cl) {
  if (!cl) {
    return;
  }
  const race_vote_identity_t identity = Race_VoteService_Identity(cl);
  if (!race_vote_pending_execute) {
    if (race_vote.active && race_vote.type == RACE_VOTE_TYPE_KICK &&
        Race_Vote_IdentityEqual(identity, race_vote.target.kick)) {
      Race_VoteService_Resolve(RACE_VOTE_OUTCOME_CANCELLED_TARGET_GONE);
    } else if (Race_Vote_RemoveVoter(&race_vote, identity)) {
      Race_VoteService_Publish();
      Race_VoteService_Evaluate();
    }
  }
  if (Race_Vote_IdentityValid(identity,
                              (uint16_t) sv_max_clients->integer)) {
    race_vote_activity_connections[identity.slot] = 0u;
    race_vote_activity_times[identity.slot] = 0u;
  }
}

void Race_VoteService_NoteActivity(g_client_t *cl) {
  const race_vote_identity_t identity = Race_VoteService_Identity(cl);
  if (!Race_Vote_IdentityValid(identity,
                               (uint16_t) sv_max_clients->integer)) {
    return;
  }
  race_vote_activity_connections[identity.slot] = identity.connection_id;
  race_vote_activity_times[identity.slot] = g_level.time;
}

void Race_VoteService_Frame(void) {
  if (!race_vote.active) {
    return;
  }

  if (race_vote_pending_execute) {
    if (Race_Vote_TimeReached(g_level.time, race_vote_execute_time)) {
      Race_VoteService_Execute();
    }
    return;
  }

  if (race_vote.type == RACE_VOTE_TYPE_KICK &&
      !Race_VoteService_Client(race_vote.target.kick)) {
    Race_VoteService_Resolve(RACE_VOTE_OUTCOME_CANCELLED_TARGET_GONE);
    return;
  }

  for (uint16_t slot = 0; slot < race_vote.max_clients; slot++) {
    const uint64_t connection_id = race_vote.eligible_connection_ids[slot];
    if (!connection_id) {
      continue;
    }
    const race_vote_identity_t identity = {
      .slot = slot,
      .connection_id = connection_id
    };
    if (!Race_VoteService_Client(identity)) {
      Race_Vote_RemoveVoter(&race_vote, identity);
    }
  }
  if (Race_Vote_TimeReached(g_level.time, race_vote_next_publish)) {
    Race_VoteService_Publish();
  }
  Race_VoteService_Evaluate();
}

void Race_VoteService_NextMapVoteBegin(void) {
  if (race_vote.active) {
    Race_VoteService_Resolve(RACE_VOTE_OUTCOME_CANCELLED_INTERMISSION);
  }
}

void Race_VoteService_ClientCommand(g_client_t *cl) {
  if (!cl || !cl->in_use) {
    return;
  }
  if (gi.Argc() == 2 ||
      (gi.Argc() == 3 && !strcmp(gi.Argv(2), "status"))) {
    Race_VoteService_PrintStatus(cl);
  } else if (gi.Argc() == 4 && !strcmp(gi.Argv(2), "map")) {
    Race_VoteService_StartMap(cl, gi.Argv(3),
                              "Usage: race vote map <map>");
  } else if (gi.Argc() == 4 && !strcmp(gi.Argv(2), "kick")) {
    Race_VoteService_StartKick(cl, gi.Argv(3),
                               "Usage: race vote kick <client-slot|name>");
  } else if (gi.Argc() == 4 && !strcmp(gi.Argv(2), "physics")) {
    Race_VoteService_StartPhysics(
      cl, gi.Argv(3),
      "Usage: race vote physics <" RACE_VOTE_PHYSICS_KEYS ">");
  } else if (gi.Argc() == 3 && !strcmp(gi.Argv(2), "yes")) {
    Race_VoteService_Cast(cl, RACE_VOTE_BALLOT_YES);
  } else if (gi.Argc() == 3 && !strcmp(gi.Argv(2), "no")) {
    Race_VoteService_Cast(cl, RACE_VOTE_BALLOT_NO);
  } else {
    Race_VoteService_PrintUsage(cl);
  }
}

bool Race_VoteService_LegacyCommand(g_client_t *cl, const char *cmd) {
  if (!cl || !cl->in_use || !cmd) {
    return false;
  }
  if (!q_strcmp(cmd, "yes")) {
    Race_VoteService_Cast(cl, RACE_VOTE_BALLOT_YES);
    return true;
  }
  if (!q_strcmp(cmd, "no")) {
    Race_VoteService_Cast(cl, RACE_VOTE_BALLOT_NO);
    return true;
  }
  if (!q_strcmp(cmd, "votemap") || !q_strcmp(cmd, "mapvote") ||
      !q_strcmp(cmd, "cv")) {
    Race_VoteService_StartMap(
      cl, gi.Argc() > 1 ? gi.Argv(1) : NULL,
      "Usage: votemap <mapname>");
    return true;
  }
  if (!q_strcmp(cmd, "votekick")) {
    Race_VoteService_StartKick(
      cl, gi.Argc() > 1 ? gi.Argv(1) : NULL,
      "Usage: votekick <client-slot|name>");
    return true;
  }
  if (!q_strcmp(cmd, "vote")) {
    if (gi.Argc() >= 2 && !q_strcmp(gi.Argv(1), "yes")) {
      Race_VoteService_Cast(cl, RACE_VOTE_BALLOT_YES);
    } else if (gi.Argc() >= 2 && !q_strcmp(gi.Argv(1), "no")) {
      Race_VoteService_Cast(cl, RACE_VOTE_BALLOT_NO);
    } else if (gi.Argc() >= 3 && !q_strcmp(gi.Argv(1), "map")) {
      Race_VoteService_StartMap(cl, gi.Argv(2),
                                "Usage: vote map <mapname>");
    } else if (gi.Argc() >= 3 && !q_strcmp(gi.Argv(1), "kick")) {
      Race_VoteService_StartKick(cl, gi.Argv(2),
                                 "Usage: vote kick <client-slot|name>");
    } else if (gi.Argc() >= 3 && !q_strcmp(gi.Argv(1), "physics")) {
      Race_VoteService_StartPhysics(
        cl, gi.Argv(2),
        "Usage: vote physics <" RACE_VOTE_PHYSICS_KEYS ">");
    } else {
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Usage: vote <map|kick|physics|yes|no> [...]\n");
    }
    return true;
  }
  return false;
}

void Race_VoteService_AdminCancel(g_client_t *cl) {
  if (!race_vote.active) {
    gi.ClientPrint(cl, PRINT_HIGH, "Race vote cancel rejected: no vote is active\n");
    return;
  }
  if (!Race_AdminService_AuthorizeClientAction(
        cl, RACE_ADMIN_ACTION_VOTE_CANCEL)) {
    return;
  }

  const uint64_t generation = race_vote.generation;
  Race_VoteService_Resolve(RACE_VOTE_OUTCOME_CANCELLED_ADMIN);
  Race_AdminService_AuditClientAction(
    cl, RACE_ADMIN_ACTION_VOTE_CANCEL, va("vote-%llu",
                                         (unsigned long long) generation),
    "cancelled");
}
