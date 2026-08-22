/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_map_state_service.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "race_map_state.h"
#include "race_map_state_store.h"
#include "race_leaderboard_wire.h"
#include "race_modes.h"
#include "race_persistence.h"
#include "race_physics.h"
#include "race_replay_format.h"
#include "race_replay_playback.h"
#include "race_replay_store.h"

typedef enum {
  RACE_MAP_STATE_SERVICE_UNAVAILABLE,
  RACE_MAP_STATE_SERVICE_READY,
  RACE_MAP_STATE_SERVICE_VALIDATING,
  RACE_MAP_STATE_SERVICE_PENDING_V1,
  RACE_MAP_STATE_SERVICE_CORRUPT
} race_map_state_service_status_t;

static race_leaderboard_record_t race_map_state_records_a[RACE_MAP_STATE_MAX_RECORDS];
static race_leaderboard_record_t race_map_state_records_b[RACE_MAP_STATE_MAX_RECORDS];
static race_replay_sample_t race_map_state_validation_samples[RACE_REPLAY_MAX_SAMPLES];
static race_replay_projectile_event_t
  race_map_state_validation_projectile_events[RACE_REPLAY_MAX_PROJECTILE_EVENTS];
static race_map_state_t race_map_state;
static race_map_state_service_status_t race_map_state_status;
static size_t race_map_state_validation_index;
static char race_map_state_committed[MAX_OS_PATH];
static char race_map_state_candidate[MAX_OS_PATH];

static void Race_MapStateService_Publish(void) {
  race_leaderboard_wire_entry_t entries[RACE_LEADERBOARD_TOP_MAX];
  size_t count = 0;

  if (race_map_state_status == RACE_MAP_STATE_SERVICE_READY &&
      Race_MapState_ReplayBacked(&race_map_state)) {
    const race_leaderboard_record_t *top[RACE_LEADERBOARD_TOP_MAX];
    count = Race_Leaderboard_Top(race_map_state.records,
                                 race_map_state.record_count,
                                 top, lengthof(top));
    for (size_t i = 0; i < count; i++) {
      memset(entries + i, 0, sizeof(entries[i]));
      q_strlcpy(entries[i].name, top[i]->display_name,
                sizeof(entries[i].name));
      entries[i].time_ms = top[i]->elapsed_time;
      entries[i].date_unix_s = top[i]->date_unix_s;
    }
  }

  char wire[MAX_STRING_CHARS];
  if (!Race_LeaderboardWire_Encode(entries, count, wire, sizeof(wire))) {
    G_Warn("Could not publish the Race Home leaderboard snapshot\n");
    q_strlcpy(wire, RACE_LEADERBOARD_CONFIG_VERSION "\\0", sizeof(wire));
  }
  gi.SetConfigString(CS_RACE_LEADERBOARD, wire);
}

static bool Race_MapStateService_PhysicsRankable(void) {
  const race_physics_config_t *config = Race_Physics_Current();
  const char *ruleset = Race_Physics_ConfigRuleset(config);
  return Race_Physics_ConfigRankable(config) &&
         Race_MapState_RulesetValid(ruleset);
}

static const char *Race_MapStateService_StatusName(void) {
  if (!Race_MapStateService_PhysicsRankable()) {
    return "disabled-unranked-physics";
  }
  switch (race_map_state_status) {
    case RACE_MAP_STATE_SERVICE_READY:
      return "ready";
    case RACE_MAP_STATE_SERVICE_VALIDATING:
      return "validating-replays";
    case RACE_MAP_STATE_SERVICE_PENDING_V1:
      return "pending-v1-read-only";
    case RACE_MAP_STATE_SERVICE_CORRUPT:
      return "corrupt-disabled";
    default:
      return "unavailable";
  }
}

static const char *Race_MapStateService_PublicationName(void) {
  if (!Race_MapStateService_PhysicsRankable()) {
    return "disabled-unranked-physics";
  }
  return Race_MapState_ReplayBacked(&race_map_state)
    ? RACE_MAP_STATE_PUBLICATION_V2
    : RACE_MAP_STATE_PUBLICATION_V1;
}

void Race_MapStateService_Init(void) {
  memset(&race_map_state, 0, sizeof(race_map_state));
  race_map_state_status = RACE_MAP_STATE_SERVICE_UNAVAILABLE;
}

void Race_MapStateService_Shutdown(void) {
  memset(&race_map_state, 0, sizeof(race_map_state));
  race_map_state_status = RACE_MAP_STATE_SERVICE_UNAVAILABLE;
  race_map_state_validation_index = 0;
  race_map_state_committed[0] = '\0';
  race_map_state_candidate[0] = '\0';
  Race_MapStateService_Publish();
}

void Race_MapStateService_Load(const char *map) {
  memset(&race_map_state, 0, sizeof(race_map_state));
  race_map_state_status = RACE_MAP_STATE_SERVICE_UNAVAILABLE;
  race_map_state_validation_index = 0;
  race_map_state_committed[0] = '\0';
  race_map_state_candidate[0] = '\0';
  Race_MapStateService_Publish();

  if (!Race_MapStateService_PhysicsRankable()) {
    const char *ruleset = Race_Physics_ConfigRuleset(Race_Physics_Current());
    gi.Print("Race map state: status=%s map=%s ruleset=%s publication=%s\n",
             Race_MapStateService_StatusName(), map ? map : "unavailable",
             ruleset ? ruleset : "unavailable",
             Race_MapStateService_PublicationName());
    return;
  }

  const char *ruleset = Race_Physics_ConfigRuleset(Race_Physics_Current());
  char ruleset_directory[MAX_OS_PATH];
  const int32_t ruleset_directory_length = snprintf(
    ruleset_directory, sizeof(ruleset_directory),
    RACE_MAP_STATE_ROOT_DIRECTORY "/%s", ruleset);
  if (ruleset_directory_length < 0 ||
      (size_t) ruleset_directory_length >= sizeof(ruleset_directory) ||
      !gi.Mkdir(RACE_MAP_STATE_ROOT_DIRECTORY) ||
      !gi.Mkdir(ruleset_directory)) {
    G_Warn("Could not prepare the Race map-state storage directory for ruleset %s\n",
           ruleset);
    return;
  }

  char canonical[RACE_MAP_IDENTITY_SIZE];
  char committed_virtual[MAX_OS_PATH];
  char candidate_virtual[MAX_OS_PATH];
  if (!Race_MapState_CanonicalizeMap(map, canonical) ||
      !Race_MapState_Paths(ruleset, canonical,
                           committed_virtual, sizeof(committed_virtual),
                           candidate_virtual, sizeof(candidate_virtual)) ||
      !Race_MapState_Init(&race_map_state,
                          race_map_state_records_a,
                          RACE_MAP_STATE_MAX_RECORDS,
                          canonical, ruleset)) {
    G_Warn("Could not derive a safe Race map-state identity for map %s\n",
           map ? map : "<null>");
    return;
  }

  if (!Race_Persistence_CopyRealPath(committed_virtual,
                                     gi.RealPath(committed_virtual),
                                     race_map_state_committed,
                                     sizeof(race_map_state_committed))) {
    G_Warn("Could not resolve the committed Race map-state path for %s\n", canonical);
    return;
  }

  if (!Race_Persistence_CopyRealPath(candidate_virtual,
                                     gi.RealPath(candidate_virtual),
                                     race_map_state_candidate,
                                     sizeof(race_map_state_candidate))) {
    G_Warn("Could not resolve the Race map-state candidate path for %s\n", canonical);
    race_map_state_committed[0] = '\0';
    return;
  }

  race_map_state_parse_result_t parse_result = RACE_MAP_STATE_PARSE_OK;
  const race_map_state_store_result_t load = Race_MapStateStore_Load(
    race_map_state_committed, canonical, ruleset,
    race_map_state_records_a, RACE_MAP_STATE_MAX_RECORDS,
    &race_map_state, &parse_result);

  if (load == RACE_MAP_STATE_STORE_OK || load == RACE_MAP_STATE_STORE_MISSING) {
    if (Race_MapState_ReplayBacked(&race_map_state) &&
        race_map_state.record_count) {
      race_map_state_status = RACE_MAP_STATE_SERVICE_VALIDATING;
    } else if (race_map_state.format == RACE_MAP_STATE_FORMAT_V1 &&
               race_map_state.record_count) {
      race_map_state_status = RACE_MAP_STATE_SERVICE_PENDING_V1;
    } else {
      race_map_state_status = RACE_MAP_STATE_SERVICE_READY;
    }
    gi.Print("Race map state: status=%s source=%s map=%s ruleset=%s generation=%llu records=%zu publication=%s\n",
             Race_MapStateService_StatusName(),
             load == RACE_MAP_STATE_STORE_OK ? "committed" : "empty",
             race_map_state.map, race_map_state.ruleset,
             (unsigned long long) race_map_state.generation,
             race_map_state.record_count,
             Race_MapStateService_PublicationName());

    if (gi.FileExists(candidate_virtual)) {
      gi.Print("Race map state: stale candidate ignored map=%s\n", canonical);
    }
    Race_MapStateService_Publish();
    return;
  }

  if (load == RACE_MAP_STATE_STORE_CORRUPT ||
      load == RACE_MAP_STATE_STORE_IDENTITY_MISMATCH) {
    race_map_state_status = RACE_MAP_STATE_SERVICE_CORRUPT;
    G_Warn("Race map state disabled for map %s: %s (%s); committed data was left unchanged\n",
           canonical, Race_MapStateStore_ResultName(load),
           Race_MapState_ParseResultName(parse_result));
    return;
  }

  G_Warn("Race map state unavailable for map %s: %s\n",
         canonical, Race_MapStateStore_ResultName(load));
}

static bool Race_MapStateService_ReplayMatches(
  const race_leaderboard_record_t *record, const race_replay_t *replay) {
  int32_t player_uid;
  return record && replay &&
         Race_Replay_ProfilePlayerUid(record->uid, &player_uid) &&
         record->replay_id == replay->replay_id &&
         record->elapsed_time == replay->elapsed_time &&
         player_uid == replay->player_uid;
}

void Race_MapStateService_Frame(void) {
  if (!Race_MapStateService_PhysicsRankable() ||
      race_map_state_status != RACE_MAP_STATE_SERVICE_VALIDATING) {
    return;
  }
  if (race_map_state_validation_index >= race_map_state.record_count) {
    race_map_state_status = RACE_MAP_STATE_SERVICE_READY;
    gi.Print("Race map state: replay validation complete map=%s records=%zu\n",
             race_map_state.map, race_map_state.record_count);
    Race_MapStateService_Publish();
    return;
  }

  const race_leaderboard_record_t *record =
    race_map_state.records + race_map_state_validation_index;
  char committed_virtual[MAX_OS_PATH];
  char candidate_virtual[MAX_OS_PATH];
  if (!Race_Replay_Paths(race_map_state.ruleset, race_map_state.map,
                         record->replay_id,
                         committed_virtual, sizeof(committed_virtual),
                         candidate_virtual, sizeof(candidate_virtual))) {
    race_map_state_status = RACE_MAP_STATE_SERVICE_CORRUPT;
    G_Warn("Race map state disabled: invalid replay reference at record %zu\n",
           race_map_state_validation_index);
    return;
  }

  char committed[MAX_OS_PATH];
  if (!Race_Persistence_CopyRealPath(committed_virtual,
                                     gi.RealPath(committed_virtual),
                                     committed, sizeof(committed))) {
    race_map_state_status = RACE_MAP_STATE_SERVICE_CORRUPT;
    G_Warn("Race map state disabled: replay path resolution failed at record %zu\n",
           race_map_state_validation_index);
    return;
  }

  race_replay_t replay;
  race_replay_parse_result_t parse_result = RACE_REPLAY_PARSE_OK;
  const race_replay_store_result_t loaded = Race_ReplayStore_Load(
    committed, race_map_state.map, record->replay_id,
    race_map_state_validation_samples, RACE_REPLAY_MAX_SAMPLES,
    race_map_state_validation_projectile_events,
    RACE_REPLAY_MAX_PROJECTILE_EVENTS,
    &replay, &parse_result);
  if (loaded != RACE_REPLAY_STORE_OK ||
      !Race_MapStateService_ReplayMatches(record, &replay)) {
    race_map_state_status = RACE_MAP_STATE_SERVICE_CORRUPT;
    G_Warn("Race map state disabled: referenced replay %016llx is %s (%s)\n",
           (unsigned long long) record->replay_id,
           Race_ReplayStore_ResultName(loaded),
           Race_Replay_ParseResultName(parse_result));
    return;
  }
  race_map_state_validation_index++;
}

void Race_MapStateService_ClientTimes(const g_client_t *cl,
                                      uint32_t *personal_best,
                                      uint32_t *world_record) {
  if (personal_best) {
    *personal_best = 0;
  }
  if (world_record) {
    *world_record = 0;
  }
  if (!cl || race_map_state_status != RACE_MAP_STATE_SERVICE_READY ||
      !Race_MapState_ReplayBacked(&race_map_state)) {
    return;
  }

  if (world_record) {
    const race_leaderboard_record_t *top[1];
    if (Race_Leaderboard_Top(race_map_state.records,
                             race_map_state.record_count,
                             top, lengthof(top))) {
      *world_record = top[0]->elapsed_time;
    }
  }

  if (personal_best && cl->persistent.race_profile.ready) {
    const race_leaderboard_record_t *record = Race_Leaderboard_Find(
      race_map_state.records, race_map_state.record_count,
      cl->persistent.race_profile.uid);
    if (record) {
      *personal_best = record->elapsed_time;
    }
  }
}

void Race_MapStateService_ClientSplitTimes(const g_client_t *cl,
                                           const uint16_t split,
                                           uint32_t *personal_best,
                                           uint32_t *world_record) {
  if (personal_best) {
    *personal_best = 0u;
  }
  if (world_record) {
    *world_record = 0u;
  }
  if (!cl || !split || !g_level.race_course.splits_valid ||
      split > g_level.race_course.split_count ||
      race_map_state_status != RACE_MAP_STATE_SERVICE_READY ||
      !Race_MapState_ReplayBacked(&race_map_state)) {
    return;
  }

  if (world_record) {
    const race_leaderboard_record_t *top[1];
    if (Race_Leaderboard_Top(race_map_state.records,
                             race_map_state.record_count,
                             top, lengthof(top)) &&
        top[0]->split_count == g_level.race_course.split_count &&
        top[0]->split_layout == g_level.race_course.split_layout) {
      *world_record = top[0]->split_times[split - 1u];
    }
  }

  if (personal_best && cl->persistent.race_profile.ready) {
    const race_leaderboard_record_t *record = Race_Leaderboard_Find(
      race_map_state.records, race_map_state.record_count,
      cl->persistent.race_profile.uid);
    if (record && record->split_count == g_level.race_course.split_count &&
        record->split_layout == g_level.race_course.split_layout) {
      *personal_best = record->split_times[split - 1u];
    }
  }
}

bool Race_MapStateService_LoadSummary(const char *map, const char *uid,
                                      race_map_state_summary_t *summary) {
  if (!summary) {
    return false;
  }
  memset(summary, 0, sizeof(*summary));

  char canonical[RACE_MAP_IDENTITY_SIZE];
  char committed_virtual[MAX_OS_PATH];
  char candidate_virtual[MAX_OS_PATH];
  const char *ruleset = Race_Physics_ConfigRuleset(Race_Physics_Current());
  if (!Race_MapState_CanonicalizeMap(map, canonical) ||
      !Race_MapState_RulesetValid(ruleset) ||
      !Race_MapState_Paths(ruleset, canonical,
                           committed_virtual, sizeof(committed_virtual),
                           candidate_virtual, sizeof(candidate_virtual))) {
    return false;
  }

  char committed[MAX_OS_PATH];
  if (!Race_Persistence_CopyRealPath(committed_virtual,
                                     gi.RealPath(committed_virtual),
                                     committed, sizeof(committed))) {
    return false;
  }

  race_leaderboard_record_t *records = malloc(
    RACE_MAP_STATE_MAX_RECORDS * sizeof(*records));
  if (!records) {
    return false;
  }

  race_map_state_t state;
  race_map_state_parse_result_t parse_result;
  const race_map_state_store_result_t loaded = Race_MapStateStore_Load(
    committed, canonical, ruleset, records, RACE_MAP_STATE_MAX_RECORDS,
    &state, &parse_result);
  if (loaded == RACE_MAP_STATE_STORE_MISSING) {
    free(records);
    return true;
  }
  if (loaded != RACE_MAP_STATE_STORE_OK) {
    free(records);
    return false;
  }

  if (Race_MapState_ReplayBacked(&state)) {
    const race_leaderboard_record_t *top[RACE_LEADERBOARD_TOP_MAX];
    summary->count = Race_Leaderboard_Top(state.records, state.record_count,
                                          top, lengthof(top));
    summary->total = state.record_count;
    for (size_t i = 0; i < summary->count; i++) {
      summary->records[i] = *top[i];
    }
    if (uid && *uid) {
      const race_leaderboard_record_t *own = Race_Leaderboard_Find(
        state.records, state.record_count, uid);
      if (own) {
        summary->personal_best = own->elapsed_time;
        for (size_t i = 0; i < summary->count; i++) {
          if (!q_strcmp(summary->records[i].uid, uid)) {
            summary->personal_rank = i + 1u;
            break;
          }
        }
      }
    }
  }
  free(records);
  return true;
}

static bool Race_MapStateService_ReplayReady(void) {
  return Race_MapStateService_PhysicsRankable() &&
         race_map_state_status == RACE_MAP_STATE_SERVICE_READY &&
         Race_MapState_ReplayBacked(&race_map_state) &&
         race_map_state.record_count;
}

bool Race_MapStateService_ReplayForRank(
  const size_t rank, race_leaderboard_record_t *record,
  size_t *resolved_rank) {
  if (!Race_MapStateService_ReplayReady() || !rank ||
      rank > RACE_LEADERBOARD_TOP_MAX) {
    return false;
  }
  const race_leaderboard_record_t *top[RACE_LEADERBOARD_TOP_MAX];
  const size_t count = Race_Leaderboard_Top(
    race_map_state.records, race_map_state.record_count,
    top, lengthof(top));
  return rank <= count && Race_ReplaySelection_Select(
    race_map_state.records, race_map_state.record_count,
    RACE_REPLAY_SOURCE_ID, NULL, top[rank - 1u]->replay_id,
    record, resolved_rank);
}

bool Race_MapStateService_ReplayForUid(
  const char *uid, race_leaderboard_record_t *record,
  size_t *resolved_rank) {
  if (!Race_MapStateService_ReplayReady()) {
    return false;
  }
  return Race_ReplaySelection_Select(
    race_map_state.records, race_map_state.record_count,
    RACE_REPLAY_SOURCE_PERSONAL_BEST, uid, 0u,
    record, resolved_rank);
}

bool Race_MapStateService_PrepareFinish(
  g_client_t *cl, race_leaderboard_record_t *candidate,
  race_leaderboard_evaluation_t *evaluation) {
  if (evaluation) {
    memset(evaluation, 0, sizeof(*evaluation));
  }
  if (!cl || !candidate || !evaluation || Race_Mode(cl) != RACE_MODE_RACE) {
    if (cl) {
      G_Debug("client=%s ranking=unranked reason=mode\n",
              cl->persistent.net_name);
    }
    return false;
  }
  if (!cl->persistent.race_profile.ready) {
    G_Debug("client=%s ranking=unranked reason=unregistered\n",
            cl->persistent.net_name);
    return false;
  }
  if (!Race_MapStateService_PhysicsRankable()) {
    G_Debug("client=%s ranking=unranked reason=physics\n",
            cl->persistent.net_name);
    return false;
  }
  if (race_map_state_status != RACE_MAP_STATE_SERVICE_READY ||
      !Race_MapState_CanPublishReplay(&race_map_state)) {
    G_Debug("client=%s ranking=unranked reason=map-state-%s\n",
            cl->persistent.net_name, Race_MapStateService_StatusName());
    return false;
  }
  const size_t split_count = g_level.race_course.splits_valid &&
                             cl->race_run.split_count ==
                               g_level.race_course.split_count
    ? cl->race_run.split_count : 0u;
  if (!Race_Leaderboard_RecordInit(candidate,
                                   cl->persistent.race_profile.uid,
                                   cl->persistent.net_name,
                                   cl->race_run.elapsed_time,
                                   cl->race_run.checkpoint_times,
                                   cl->race_run.checkpoint_count) ||
      !Race_Leaderboard_RecordSetSplits(candidate,
                                        cl->race_run.split_times,
                                        split_count,
                                        split_count
                                          ? g_level.race_course.split_layout
                                          : 0u) ||
      !Race_Leaderboard_RecordSetDate(candidate, (uint64_t) time(NULL))) {
    G_Debug("client=%s ranking=unranked reason=invalid-record\n",
            cl->persistent.net_name);
    return false;
  }
  if (!Race_MapState_EvaluateCandidate(&race_map_state, candidate, evaluation)) {
    G_Debug("client=%s ranking=unranked reason=evaluation-failed\n",
            cl->persistent.net_name);
    return false;
  }

  G_Debug("client=%s ranking=candidate pb=%d wr=%d first=%d top=%d rank=%zu accepted=%d publication=replay-required\n",
          cl->persistent.net_name, evaluation->personal_best,
          evaluation->world_record, evaluation->first_completion,
          evaluation->top, evaluation->top_rank, evaluation->would_accept);
  return evaluation->would_accept;
}

bool Race_MapStateService_CommitCandidate(
  const race_leaderboard_record_t *candidate,
  race_leaderboard_evaluation_t *evaluation) {
  if (!candidate || !candidate->replay_id || !evaluation ||
      !Race_MapStateService_PhysicsRankable() ||
      race_map_state_status != RACE_MAP_STATE_SERVICE_READY ||
      !*race_map_state_committed || !*race_map_state_candidate) {
    return false;
  }

  race_leaderboard_record_t *next_records =
    race_map_state.records == race_map_state_records_a
      ? race_map_state_records_b
      : race_map_state_records_a;
  race_map_state_t next = {
    .records = next_records,
    .record_capacity = RACE_MAP_STATE_MAX_RECORDS
  };
  race_leaderboard_evaluation_t applied;
  if (!Race_MapState_ApplyCandidate(&race_map_state, candidate, &next,
                                    &applied)) {
    return false;
  }

  race_map_state_parse_result_t parse_result = RACE_MAP_STATE_PARSE_OK;
  const race_map_state_store_result_t committed = Race_MapStateStore_Commit(
    race_map_state_committed, race_map_state_candidate, &next, &parse_result);
  if (committed != RACE_MAP_STATE_STORE_OK) {
    G_Warn("Race map-state publication failed for replay %016llx: %s (%s)\n",
           (unsigned long long) candidate->replay_id,
           Race_MapStateStore_ResultName(committed),
           Race_MapState_ParseResultName(parse_result));
    return false;
  }

  race_map_state = next;
  race_map_state_validation_index = race_map_state.record_count;
  *evaluation = applied;
  Race_MapStateService_Publish();
  return true;
}

void Race_MapStateService_PrintStatus(g_client_t *cl) {
  if (!cl) {
    return;
  }

  const char *active_ruleset = Race_Physics_ConfigRuleset(
    Race_Physics_Current());
  const char *reported_map = *race_map_state.map
    ? race_map_state.map
    : *g_level.name ? g_level.name : "unavailable";
  const char *reported_ruleset = *race_map_state.ruleset
    ? race_map_state.ruleset
    : active_ruleset ? active_ruleset : "unavailable";
  gi.ClientPrint(cl, PRINT_HIGH,
                 "Race map state: status=%s map=%s ruleset=%s generation=%llu records=%zu publication=%s validation=%zu/%zu\n",
                 Race_MapStateService_StatusName(),
                 reported_map, reported_ruleset,
                 (unsigned long long) race_map_state.generation,
                 race_map_state.record_count,
                 Race_MapStateService_PublicationName(),
                 race_map_state_validation_index,
                 race_map_state.record_count);
}
