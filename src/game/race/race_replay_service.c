/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_replay_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "race.h"
#include "race_map_state_service.h"
#include "race_modes.h"
#include "race_persistence.h"
#include "race_physics.h"
#include "race_physics_service.h"
#include "race_projectile_compat.h"
#include "race_publication.h"
#include "race_replay_format.h"
#include "race_replay_record.h"
#include "race_replay_store.h"

static race_replay_recording_t race_replay_recordings[MAX_CLIENTS];
static size_t race_replay_reserved_bytes;
static bool race_replay_map_active;
static char race_replay_directory[MAX_OS_PATH];
static g_projectile_observer_lifecycle_t race_replay_projectile_observer;

typedef struct {
  const g_entity_t *projectile;
  uint16_t id;
  race_replay_projectile_kind_t kind;
} race_replay_active_projectile_t;

static race_replay_active_projectile_t
  race_replay_active_projectiles[MAX_CLIENTS][RACE_REPLAY_MAX_ACTIVE_PROJECTILES];
static size_t race_replay_active_projectile_counts[MAX_CLIENTS];
static uint16_t race_replay_next_projectile_ids[MAX_CLIENTS];

typedef struct {
  const race_replay_t *replay;
  race_leaderboard_record_t candidate;
  race_leaderboard_evaluation_t evaluation;
  uint64_t replay_id;
  race_replay_store_result_t replay_result;
  race_replay_parse_result_t replay_parse_result;
  char committed[RACE_REPLAY_STORE_PATH_SIZE];
} race_replay_publication_t;

static bool Race_ReplayService_ClientIndex(const g_client_t *cl,
                                           size_t *index) {
  if (!cl || !index || cl->ps.client >= MAX_CLIENTS) {
    return false;
  }
  *index = (size_t) cl->ps.client;
  return true;
}

static race_replay_sample_t Race_ReplayService_Sample(const g_client_t *cl) {
  race_replay_sample_t sample = { 0 };
  if (!cl || !cl->entity) {
    return sample;
  }

  sample.pm_state = cl->ps.pm_state;
  sample.pm_state.origin = cl->entity->s.origin;
  sample.pm_state.velocity = cl->entity->velocity;
  memcpy(sample.stats, cl->ps.stats, sizeof(sample.stats));
  memcpy(sample.inventory, cl->ps.inventory, sizeof(sample.inventory));
  sample.stats[STAT_RACE_INPUT] = Race_InputFlags(&cl->cmd);
  sample.strafe_helper = cl->race_strafe_sample;
  return sample;
}

static void Race_ReplayService_DestroyIndex(size_t index) {
  if (index >= MAX_CLIENTS) {
    return;
  }
  const size_t allocation = Race_ReplayRecording_AllocationBytes(
    race_replay_recordings + index);
  Race_ReplayRecording_Destroy(race_replay_recordings + index);
  race_replay_reserved_bytes = race_replay_reserved_bytes >= allocation
    ? race_replay_reserved_bytes - allocation
    : 0u;
  race_replay_active_projectile_counts[index] = 0u;
  race_replay_next_projectile_ids[index] = 0u;
  memset(race_replay_active_projectiles[index], 0,
         sizeof(race_replay_active_projectiles[index]));
}

static size_t Race_ReplayService_AllocationLimit(
    const race_replay_recording_t *recording) {
  const size_t allocated = Race_ReplayRecording_AllocationBytes(recording);
  return race_replay_reserved_bytes < RACE_REPLAY_RECORDING_MEMORY_BYTES
    ? allocated + RACE_REPLAY_RECORDING_MEMORY_BYTES -
                  race_replay_reserved_bytes
    : allocated;
}

static void Race_ReplayService_AccountAllocation(
    const race_replay_recording_t *recording, const size_t previous) {
  const size_t current = Race_ReplayRecording_AllocationBytes(recording);
  if (current >= previous) {
    race_replay_reserved_bytes += current - previous;
  } else {
    race_replay_reserved_bytes = race_replay_reserved_bytes >= previous - current
      ? race_replay_reserved_bytes - (previous - current)
      : 0u;
  }
}

static void Race_ReplayService_InvalidateProjectileCapture(
    const size_t index, g_client_t *cl) {
  if (cl) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Replay projectile recording limit reached; this run cannot publish a record.\n");
    Race_MarkInvalid(cl, RACE_INVALID_REPLAY_CAPACITY);
  }
  Race_ReplayService_DestroyIndex(index);
}

static void Race_ReplayService_ObserveProjectile(
    const g_projectile_observation_t *observation) {
  if (race_replay_projectile_observer.previous) {
    race_replay_projectile_observer.previous(observation);
  }
  if (!observation || !observation->owner ||
      !observation->owner->client || !observation->projectile) {
    return;
  }

  g_client_t *cl = observation->owner->client;
  size_t index;
  if (!Race_ReplayService_ClientIndex(cl, &index) ||
      cl->entity != observation->owner ||
      !race_replay_recordings[index].active ||
      cl->race_run.state != RACE_RUN_ACTIVE ||
      Race_Mode(cl) != RACE_MODE_RACE) {
    return;
  }

  race_replay_projectile_kind_t kind;
  if (observation->kind == G_PROJECTILE_ROCKET) {
    kind = RACE_REPLAY_PROJECTILE_ROCKET;
  } else if (observation->kind == G_PROJECTILE_HYPERBLASTER) {
    kind = RACE_REPLAY_PROJECTILE_HYPERBLASTER;
  } else {
    return;
  }

  race_replay_projectile_operation_t operation;
  if (observation->operation == G_PROJECTILE_SPAWN) {
    operation = RACE_REPLAY_PROJECTILE_SPAWN;
  } else if (observation->operation == G_PROJECTILE_IMPACT) {
    operation = RACE_REPLAY_PROJECTILE_IMPACT;
  } else if (observation->operation == G_PROJECTILE_SILENT_DESPAWN) {
    operation = RACE_REPLAY_PROJECTILE_SILENT_DESPAWN;
  } else {
    return;
  }

  race_replay_active_projectile_t *active =
    race_replay_active_projectiles[index];
  size_t active_count = race_replay_active_projectile_counts[index];
  size_t active_index = active_count;
  for (size_t i = 0u; i < active_count; i++) {
    if (active[i].projectile == observation->projectile) {
      active_index = i;
      break;
    }
  }

  uint16_t id;
  if (operation == RACE_REPLAY_PROJECTILE_SPAWN) {
    if (active_index != active_count ||
        active_count == RACE_REPLAY_MAX_ACTIVE_PROJECTILES) {
      Race_ReplayService_InvalidateProjectileCapture(index, cl);
      return;
    }
    id = ++race_replay_next_projectile_ids[index];
    if (!id) {
      Race_ReplayService_InvalidateProjectileCapture(index, cl);
      return;
    }
    active[active_count] = (race_replay_active_projectile_t) {
      .projectile = observation->projectile,
      .id = id,
      .kind = kind
    };
    race_replay_active_projectile_counts[index]++;
  } else {
    if (active_index == active_count || active[active_index].kind != kind) {
      Race_ReplayService_InvalidateProjectileCapture(index, cl);
      return;
    }
    id = active[active_index].id;
  }

  const race_replay_projectile_event_t event = {
    .id = id,
    .kind = kind,
    .operation = operation,
    .origin = observation->origin,
    .velocity = observation->velocity,
    .normal = observation->normal
  };
  race_replay_recording_t *recording = race_replay_recordings + index;
  const size_t previous = Race_ReplayRecording_AllocationBytes(recording);
  const bool captured = Race_ReplayRecording_CaptureProjectile(
    recording, g_level.time, &event,
    Race_ReplayService_AllocationLimit(recording));
  Race_ReplayService_AccountAllocation(recording, previous);
  if (!captured) {
    Race_ReplayService_InvalidateProjectileCapture(index, cl);
    return;
  }

  if (operation != RACE_REPLAY_PROJECTILE_SPAWN) {
    active_count = --race_replay_active_projectile_counts[index];
    active[active_index] = active[active_count];
    memset(active + active_count, 0, sizeof(*active));
  }
}

void Race_ReplayService_Init(void) {
  for (size_t i = 0; i < MAX_CLIENTS; i++) {
    Race_ReplayService_DestroyIndex(i);
  }
  race_replay_reserved_bytes = 0;
  race_replay_map_active = false;
  race_replay_directory[0] = '\0';
  G_InstallProjectileObserver(&race_replay_projectile_observer,
                              Race_ReplayService_ObserveProjectile);
}

void Race_ReplayService_Shutdown(void) {
  for (size_t i = 0; i < MAX_CLIENTS; i++) {
    Race_ReplayService_DestroyIndex(i);
  }
  race_replay_reserved_bytes = 0u;
  race_replay_map_active = false;
  race_replay_directory[0] = '\0';
  G_RestoreProjectileObserver(&race_replay_projectile_observer);
}

void Race_ReplayService_ConfigureLevel(const char *map) {
  Race_ReplayService_Init();

  const race_physics_config_t *physics = Race_Physics_Current();
  const char *ruleset = Race_Physics_ConfigRuleset(physics);
  if (!Race_PhysicsService_Rankable() ||
      !Race_MapState_RulesetValid(ruleset)) {
    // Keep bounded capture available for unranked validation, but do not open
    // or create another ruleset's durable replay namespace.
    race_replay_map_active = true;
    gi.Print("Race replay: mode=memory-only map=%s ruleset=%s\n",
             map ? map : "unavailable", ruleset ? ruleset : "unavailable");
    return;
  }

  char encoded_map[RACE_MAP_IDENTITY_ENCODED_SIZE];
  char virtual_directory[MAX_OS_PATH];
  const int32_t directory_length = Race_MapState_EncodeMap(map, encoded_map)
    ? snprintf(virtual_directory, sizeof(virtual_directory),
               RACE_REPLAY_ROOT_DIRECTORY "/%s/%s", ruleset, encoded_map)
    : -1;
  if (directory_length < 0 ||
      (size_t) directory_length >= sizeof(virtual_directory)) {
    G_Warn("Could not derive the Race replay directory for map %s\n",
           map ? map : "<null>");
    return;
  }
  char ruleset_directory[MAX_OS_PATH];
  const int32_t ruleset_directory_length = snprintf(
    ruleset_directory, sizeof(ruleset_directory),
    RACE_REPLAY_ROOT_DIRECTORY "/%s", ruleset);
  if (ruleset_directory_length < 0 ||
      (size_t) ruleset_directory_length >= sizeof(ruleset_directory) ||
      !gi.Mkdir(RACE_REPLAY_ROOT_DIRECTORY) ||
      !gi.Mkdir(ruleset_directory) ||
      !gi.Mkdir(virtual_directory)) {
    G_Warn("Could not prepare the Race replay directory for map %s\n",
           map ? map : "<null>");
    return;
  }

  if (!Race_Persistence_CopyRealPath(virtual_directory,
                                     gi.RealPath(virtual_directory),
                                     race_replay_directory,
                                     sizeof(race_replay_directory))) {
    G_Warn("Could not resolve the Race replay directory for map %s\n",
           map ? map : "<null>");
    return;
  }
  race_replay_map_active = true;
}

void Race_ReplayService_Reset(g_client_t *cl) {
  size_t index;
  if (Race_ReplayService_ClientIndex(cl, &index)) {
    Race_ReplayService_DestroyIndex(index);
  }
}

bool Race_ReplayService_Start(g_client_t *cl) {
  size_t index;
  if (!Race_ReplayService_ClientIndex(cl, &index)) {
    return false;
  }
  Race_ReplayService_DestroyIndex(index);
  if (!race_replay_map_active || !cl->entity ||
      Race_Mode(cl) != RACE_MODE_RACE ||
      !cl->persistent.race_profile.ready) {
    return false;
  }

  int32_t player_uid;
  const race_physics_config_t *physics = Race_Physics_Current();
  const uint8_t physics_mode = physics &&
                               physics->family == RACE_PHYSICS_FAMILY_Q2
    ? 1u : 0u;
  if (race_replay_reserved_bytes > RACE_REPLAY_RECORDING_MEMORY_BYTES ||
      !physics ||
      (physics->family != RACE_PHYSICS_FAMILY_QUETOO &&
       physics->family != RACE_PHYSICS_FAMILY_Q2) ||
      !Race_Replay_ProfilePlayerUid(cl->persistent.race_profile.uid,
                                    &player_uid) ||
      !Race_ReplayRecording_Start(
        race_replay_recordings + index, g_level.name,
        cl->persistent.race_profile.uid, cl->persistent.net_name, player_uid,
        physics_mode, g_level.time,
        RACE_REPLAY_RECORDING_MEMORY_BYTES - race_replay_reserved_bytes)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Replay recording unavailable; this run cannot publish a record.\n");
    return false;
  }
  race_replay_recording_t *recording = race_replay_recordings + index;
  Race_ReplayService_AccountAllocation(recording, 0u);

  const race_replay_sample_t sample = Race_ReplayService_Sample(cl);
  const size_t previous = Race_ReplayRecording_AllocationBytes(recording);
  const bool captured = Race_ReplayRecording_Capture(
    recording, g_level.time, &sample,
    Race_ReplayService_AllocationLimit(recording));
  Race_ReplayService_AccountAllocation(recording, previous);
  if (!captured) {
    Race_ReplayService_DestroyIndex(index);
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Replay recording failed at start; this run cannot publish a record.\n");
    return false;
  }

  return true;
}

void Race_ReplayService_Frame(void) {
  for (size_t index = 0; index < MAX_CLIENTS; index++) {
    race_replay_recording_t *recording = race_replay_recordings + index;
    if (!recording->active) {
      continue;
    }

    g_client_t *cl = ge.clients[index];
    if (!cl || !cl->in_use || !cl->entity ||
        cl->race_run.state != RACE_RUN_ACTIVE ||
        Race_Mode(cl) != RACE_MODE_RACE ||
        !cl->persistent.race_profile.ready ||
        strcmp(cl->persistent.race_profile.uid,
               recording->replay.profile_uid)) {
      Race_ReplayService_DestroyIndex(index);
      continue;
    }

    const race_replay_sample_t sample = Race_ReplayService_Sample(cl);
    const size_t previous = Race_ReplayRecording_AllocationBytes(recording);
    const bool captured = Race_ReplayRecording_Capture(
      recording, g_level.time, &sample,
      Race_ReplayService_AllocationLimit(recording));
    Race_ReplayService_AccountAllocation(recording, previous);
    if (!captured) {
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Replay recording limit reached; this run cannot publish a record.\n");
      Race_MarkInvalid(cl, RACE_INVALID_REPLAY_CAPACITY);
      Race_ReplayService_DestroyIndex(index);
    }
  }
}

static bool Race_ReplayService_CommitReplay(void *data,
                                            bool *newly_created) {
  race_replay_publication_t *publication = data;
  publication->replay_result = Race_ReplayStore_Commit(
    race_replay_directory, publication->replay,
    &publication->replay_id, newly_created,
    &publication->replay_parse_result);
  if (publication->replay_result != RACE_REPLAY_STORE_OK) {
    return false;
  }

  const int32_t length = snprintf(
    publication->committed, sizeof(publication->committed),
    "%s/replay-%016" PRIx64 ".ghost", race_replay_directory,
    publication->replay_id);
  if (length < 0 || (size_t) length >= sizeof(publication->committed)) {
    if (*newly_created) {
      char fallback[RACE_REPLAY_STORE_PATH_SIZE];
      const int32_t fallback_length = snprintf(
        fallback, sizeof(fallback), "%s/replay-%016" PRIx64 ".ghost",
        race_replay_directory, publication->replay_id);
      if (fallback_length >= 0 &&
          (size_t) fallback_length < sizeof(fallback)) {
        Race_ReplayStore_Remove(fallback);
      }
    }
    return false;
  }
  if (!Race_Leaderboard_RecordAttachReplay(&publication->candidate,
                                           publication->replay_id)) {
    if (*newly_created) {
      Race_ReplayStore_Remove(publication->committed);
    }
    return false;
  }
  return true;
}

static bool Race_ReplayService_CommitMapState(void *data) {
  race_replay_publication_t *publication = data;
  return Race_MapStateService_CommitCandidate(&publication->candidate,
                                               &publication->evaluation);
}

static bool Race_ReplayService_RemoveReplay(void *data) {
  const race_replay_publication_t *publication = data;
  return Race_ReplayStore_Remove(publication->committed) ==
         RACE_REPLAY_STORE_OK;
}

bool Race_ReplayService_Finish(g_client_t *cl,
                               race_leaderboard_evaluation_t *evaluation,
                               uint64_t *replay_id) {
  if (evaluation) {
    memset(evaluation, 0, sizeof(*evaluation));
  }
  if (replay_id) {
    *replay_id = 0;
  }

  race_leaderboard_record_t candidate;
  race_leaderboard_evaluation_t ranked;
  if (!Race_MapStateService_PrepareFinish(cl, &candidate, &ranked)) {
    Race_ReplayService_Reset(cl);
    return false;
  }

  size_t index;
  if (!Race_ReplayService_ClientIndex(cl, &index) ||
      !race_replay_recordings[index].active ||
      strcmp(candidate.uid,
             race_replay_recordings[index].replay.profile_uid)) {
    Race_ReplayService_Reset(cl);
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Record not published: no complete authoritative replay was recorded.\n");
    return false;
  }

  race_replay_recording_t *recording = race_replay_recordings + index;
  const race_replay_sample_t sample = Race_ReplayService_Sample(cl);
  const size_t previous = Race_ReplayRecording_AllocationBytes(recording);
  const bool finished = Race_ReplayRecording_Finish(
    recording, g_level.time, cl->race_run.elapsed_time, &sample,
    Race_ReplayService_AllocationLimit(recording));
  Race_ReplayService_AccountAllocation(recording, previous);
  if (!finished) {
    Race_ReplayService_DestroyIndex(index);
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Record not published: replay finalization failed.\n");
    return false;
  }

  race_replay_publication_t publication = {
    .replay = &recording->replay,
    .candidate = candidate,
    .evaluation = ranked,
    .replay_result = RACE_REPLAY_STORE_INVALID_ARGUMENT,
    .replay_parse_result = RACE_REPLAY_PARSE_OK
  };
  const race_publication_ops_t ops = {
    .commit_replay = Race_ReplayService_CommitReplay,
    .commit_map_state = Race_ReplayService_CommitMapState,
    .remove_replay = Race_ReplayService_RemoveReplay,
    .context = &publication
  };
  const race_publication_result_t published = Race_Publication_Commit(&ops);
  Race_ReplayService_DestroyIndex(index);
  if (published != RACE_PUBLICATION_OK) {
    G_Warn("Race publication failed for client %s: %s; replay=%s (%s)\n",
           cl->persistent.net_name, Race_Publication_ResultName(published),
           Race_ReplayStore_ResultName(publication.replay_result),
           Race_Replay_ParseResultName(publication.replay_parse_result));
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Record not published: durable replay/map-state commit failed.\n");
    return false;
  }

  if (evaluation) {
    *evaluation = publication.evaluation;
  }
  if (replay_id) {
    *replay_id = publication.replay_id;
  }
  gi.ClientPrint(cl, PRINT_HIGH, "Replay and record published.\n");
  return true;
}
