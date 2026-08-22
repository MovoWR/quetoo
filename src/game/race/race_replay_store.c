/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_replay_store.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "race_persistence.h"

static void Race_ReplayStore_RemoveCandidate(const char *candidate) {
  const race_persistence_result_t removed = Race_Persistence_Remove(candidate);
  (void) removed;
}

race_replay_store_result_t Race_ReplayStore_Load(
  const char *committed, const char *expected_map, uint64_t expected_id,
  race_replay_sample_t *samples, size_t capacity,
  race_replay_projectile_event_t *projectile_events,
  size_t projectile_event_capacity,
  race_replay_t *replay, race_replay_parse_result_t *parse_result) {
  if (parse_result) {
    *parse_result = RACE_REPLAY_PARSE_OK;
  }
  if (!committed || !*committed || !expected_map || !expected_id ||
      !replay || (!samples && capacity) || capacity > RACE_REPLAY_MAX_SAMPLES) {
    return RACE_REPLAY_STORE_INVALID_ARGUMENT;
  }
  if ((!projectile_events && projectile_event_capacity) ||
      projectile_event_capacity > RACE_REPLAY_MAX_PROJECTILE_EVENTS) {
    return RACE_REPLAY_STORE_INVALID_ARGUMENT;
  }

  uint8_t *serialized = malloc(RACE_REPLAY_MAX_FILE_BYTES);
  if (!serialized) {
    return RACE_REPLAY_STORE_MEMORY_ERROR;
  }
  size_t serialized_length;
  const race_persistence_result_t read = Race_Persistence_Read(
    committed, serialized, RACE_REPLAY_MAX_FILE_BYTES, &serialized_length);
  if (read != RACE_PERSISTENCE_OK) {
    free(serialized);
    if (read == RACE_PERSISTENCE_NOT_FOUND) {
      return RACE_REPLAY_STORE_NOT_FOUND;
    }
    if (read == RACE_PERSISTENCE_TOO_LARGE) {
      if (parse_result) {
        *parse_result = RACE_REPLAY_PARSE_TOO_LARGE;
      }
      return RACE_REPLAY_STORE_CORRUPT;
    }
    return RACE_REPLAY_STORE_IO_ERROR;
  }

  const race_replay_parse_result_t parsed = Race_Replay_Parse(
    serialized, serialized_length, samples, capacity,
    projectile_events, projectile_event_capacity, replay);
  free(serialized);
  if (parse_result) {
    *parse_result = parsed;
  }
  if (parsed != RACE_REPLAY_PARSE_OK) {
    return RACE_REPLAY_STORE_CORRUPT;
  }

  char canonical[RACE_MAP_IDENTITY_SIZE];
  if (!Race_MapState_CanonicalizeMap(expected_map, canonical) ||
      strcmp(canonical, replay->map) || replay->replay_id != expected_id) {
    if (parse_result) {
      *parse_result = RACE_REPLAY_PARSE_IDENTITY_MISMATCH;
    }
    return RACE_REPLAY_STORE_CORRUPT;
  }
  return RACE_REPLAY_STORE_OK;
}

static race_replay_store_result_t Race_ReplayStore_Verify(
  const char *path, const race_replay_t *expected,
  race_replay_parse_result_t *parse_result) {
  race_replay_sample_t *samples = calloc(RACE_REPLAY_MAX_SAMPLES,
                                          sizeof(*samples));
  race_replay_projectile_event_t *projectile_events = calloc(
    RACE_REPLAY_MAX_PROJECTILE_EVENTS, sizeof(*projectile_events));
  if (!samples || !projectile_events) {
    free(projectile_events);
    free(samples);
    return RACE_REPLAY_STORE_MEMORY_ERROR;
  }

  race_replay_t parsed;
  const race_replay_store_result_t loaded = Race_ReplayStore_Load(
    path, expected->map, expected->replay_id,
    samples, RACE_REPLAY_MAX_SAMPLES,
    projectile_events, RACE_REPLAY_MAX_PROJECTILE_EVENTS,
    &parsed, parse_result);
  const race_replay_store_result_t result =
    loaded == RACE_REPLAY_STORE_OK && !Race_Replay_Equals(expected, &parsed)
      ? RACE_REPLAY_STORE_COLLISION
      : loaded;
  free(samples);
  free(projectile_events);
  return result;
}

race_replay_store_result_t Race_ReplayStore_Commit(
  const char *directory, const race_replay_t *replay,
  uint64_t *replay_id, bool *newly_created,
  race_replay_parse_result_t *parse_result) {
  if (replay_id) {
    *replay_id = 0;
  }
  if (newly_created) {
    *newly_created = false;
  }
  if (parse_result) {
    *parse_result = RACE_REPLAY_PARSE_OK;
  }
  if (!directory || !*directory || !Race_Replay_Valid(replay)) {
    return RACE_REPLAY_STORE_INVALID_ARGUMENT;
  }

  uint8_t *serialized = malloc(RACE_REPLAY_MAX_FILE_BYTES);
  if (!serialized) {
    return RACE_REPLAY_STORE_MEMORY_ERROR;
  }
  size_t serialized_length;
  uint64_t id;
  if (!Race_Replay_Serialize(replay, serialized, RACE_REPLAY_MAX_FILE_BYTES,
                             &serialized_length, &id)) {
    free(serialized);
    return RACE_REPLAY_STORE_INVALID_ARGUMENT;
  }

  char committed[RACE_REPLAY_STORE_PATH_SIZE];
  char candidate[RACE_REPLAY_STORE_PATH_SIZE];
  const int32_t committed_length = snprintf(
    committed, sizeof(committed), "%s/replay-%016" PRIx64 ".ghost",
    directory, id);
  const int32_t candidate_length = snprintf(
    candidate, sizeof(candidate), "%s/replay-%016" PRIx64 ".candidate",
    directory, id);
  if (committed_length < 0 || (size_t) committed_length >= sizeof(committed) ||
      candidate_length < 0 || (size_t) candidate_length >= sizeof(candidate) ||
      !strcmp(committed, candidate)) {
    free(serialized);
    return RACE_REPLAY_STORE_INVALID_ARGUMENT;
  }

  race_replay_t expected = *replay;
  expected.replay_id = id;
  race_replay_parse_result_t parsed = RACE_REPLAY_PARSE_OK;
  race_replay_store_result_t existing = Race_ReplayStore_Verify(
    committed, &expected, &parsed);
  if (existing == RACE_REPLAY_STORE_OK) {
    Race_ReplayStore_RemoveCandidate(candidate);
    free(serialized);
    if (replay_id) {
      *replay_id = id;
    }
    return RACE_REPLAY_STORE_OK;
  }
  if (existing != RACE_REPLAY_STORE_NOT_FOUND) {
    Race_ReplayStore_RemoveCandidate(candidate);
    free(serialized);
    if (parse_result) {
      *parse_result = parsed;
    }
    return existing == RACE_REPLAY_STORE_COLLISION
      ? existing
      : RACE_REPLAY_STORE_CORRUPT;
  }

  race_persistence_result_t persisted = Race_Persistence_WriteCandidate(
    candidate, serialized, serialized_length);
  free(serialized);
  if (persisted != RACE_PERSISTENCE_OK) {
    return RACE_REPLAY_STORE_IO_ERROR;
  }

  race_replay_store_result_t verified = Race_ReplayStore_Verify(
    candidate, &expected, &parsed);
  if (verified != RACE_REPLAY_STORE_OK) {
    Race_ReplayStore_RemoveCandidate(candidate);
    if (parse_result) {
      *parse_result = parsed;
    }
    return verified;
  }

  persisted = Race_Persistence_Promote(candidate, committed);
  if (persisted != RACE_PERSISTENCE_OK) {
    Race_ReplayStore_RemoveCandidate(candidate);
    return RACE_REPLAY_STORE_IO_ERROR;
  }

  verified = Race_ReplayStore_Verify(committed, &expected, &parsed);
  if (verified != RACE_REPLAY_STORE_OK) {
    Race_ReplayStore_Remove(committed);
    if (parse_result) {
      *parse_result = parsed;
    }
    return verified;
  }

  if (replay_id) {
    *replay_id = id;
  }
  if (newly_created) {
    *newly_created = true;
  }
  return RACE_REPLAY_STORE_OK;
}

race_replay_store_result_t Race_ReplayStore_Remove(const char *committed) {
  const race_persistence_result_t removed = Race_Persistence_Remove(committed);
  if (removed == RACE_PERSISTENCE_OK) {
    return RACE_REPLAY_STORE_OK;
  }
  if (removed == RACE_PERSISTENCE_NOT_FOUND) {
    return RACE_REPLAY_STORE_NOT_FOUND;
  }
  if (removed == RACE_PERSISTENCE_INVALID_ARGUMENT) {
    return RACE_REPLAY_STORE_INVALID_ARGUMENT;
  }
  return RACE_REPLAY_STORE_IO_ERROR;
}

const char *Race_ReplayStore_ResultName(race_replay_store_result_t result) {
  switch (result) {
    case RACE_REPLAY_STORE_OK:
      return "ok";
    case RACE_REPLAY_STORE_NOT_FOUND:
      return "not found";
    case RACE_REPLAY_STORE_CORRUPT:
      return "corrupt";
    case RACE_REPLAY_STORE_COLLISION:
      return "identity collision";
    case RACE_REPLAY_STORE_IO_ERROR:
      return "I/O error";
    case RACE_REPLAY_STORE_MEMORY_ERROR:
      return "memory error";
    case RACE_REPLAY_STORE_INVALID_ARGUMENT:
      return "invalid argument";
  }
  return "unknown";
}
