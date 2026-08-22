/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_map_state_store.h"

#include <stdlib.h>
#include <string.h>

#include "race_persistence.h"

static race_map_state_store_result_t Race_MapStateStore_Parse(
  const void *serialized, size_t serialized_length,
  const char *expected_map, const char *expected_ruleset,
  race_leaderboard_record_t *records, size_t capacity,
  race_map_state_t *state,
  race_map_state_parse_result_t *parse_result) {
  race_map_state_parse_result_t parsed = Race_MapState_Parse(serialized,
                                                              serialized_length,
                                                              records,
                                                              capacity,
                                                              state);
  if (parse_result) {
    *parse_result = parsed;
  }
  if (parsed != RACE_MAP_STATE_PARSE_OK) {
    return RACE_MAP_STATE_STORE_CORRUPT;
  }

  char canonical[RACE_MAP_IDENTITY_SIZE];
  if (!Race_MapState_CanonicalizeMap(expected_map, canonical) ||
      !Race_MapState_RulesetValid(expected_ruleset) ||
      strcmp(canonical, state->map) ||
      strcmp(expected_ruleset, state->ruleset)) {
    return RACE_MAP_STATE_STORE_IDENTITY_MISMATCH;
  }

  return RACE_MAP_STATE_STORE_OK;
}

race_map_state_store_result_t Race_MapStateStore_Load(
  const char *committed, const char *expected_map, const char *expected_ruleset,
  race_leaderboard_record_t *records, size_t capacity,
  race_map_state_t *state,
  race_map_state_parse_result_t *parse_result) {
  if (parse_result) {
    *parse_result = RACE_MAP_STATE_PARSE_OK;
  }

  char canonical[RACE_MAP_IDENTITY_SIZE];
  if (!committed || !*committed || !state || (!records && capacity) ||
      capacity > RACE_MAP_STATE_MAX_RECORDS ||
      !Race_MapState_CanonicalizeMap(expected_map, canonical) ||
      !Race_MapState_RulesetValid(expected_ruleset)) {
    return RACE_MAP_STATE_STORE_INVALID_ARGUMENT;
  }

  char *serialized = malloc(RACE_MAP_STATE_MAX_FILE_BYTES + 1u);
  race_leaderboard_record_t *parsed_records = capacity
    ? malloc(capacity * sizeof(*parsed_records))
    : NULL;
  if (!serialized || (capacity && !parsed_records)) {
    free(parsed_records);
    free(serialized);
    return RACE_MAP_STATE_STORE_MEMORY_ERROR;
  }

  size_t serialized_length;
  const race_persistence_result_t read = Race_Persistence_Read(
    committed, serialized, RACE_MAP_STATE_MAX_FILE_BYTES, &serialized_length);
  if (read == RACE_PERSISTENCE_NOT_FOUND) {
    free(parsed_records);
    free(serialized);
    return Race_MapState_Init(state, records, capacity, canonical,
                              expected_ruleset)
      ? RACE_MAP_STATE_STORE_MISSING
      : RACE_MAP_STATE_STORE_INVALID_ARGUMENT;
  }
  if (read != RACE_PERSISTENCE_OK) {
    if (read == RACE_PERSISTENCE_TOO_LARGE && parse_result) {
      *parse_result = RACE_MAP_STATE_PARSE_TOO_LARGE;
    }
    free(parsed_records);
    free(serialized);
    return read == RACE_PERSISTENCE_TOO_LARGE
      ? RACE_MAP_STATE_STORE_CORRUPT
      : RACE_MAP_STATE_STORE_IO_ERROR;
  }

  race_map_state_t parsed_state;
  const race_map_state_store_result_t result = Race_MapStateStore_Parse(
    serialized, serialized_length, canonical, expected_ruleset,
    parsed_records, capacity, &parsed_state, parse_result);

  if (result == RACE_MAP_STATE_STORE_OK) {
    Race_MapState_Init(state, records, capacity, parsed_state.map,
                       parsed_state.ruleset);
    state->format = parsed_state.format;
    state->generation = parsed_state.generation;
    state->record_count = parsed_state.record_count;
    if (state->record_count) {
      memcpy(state->records, parsed_state.records,
             state->record_count * sizeof(*state->records));
    }
  }

  free(parsed_records);
  free(serialized);
  return result;
}

race_map_state_store_result_t Race_MapStateStore_Commit(
  const char *committed, const char *candidate,
  const race_map_state_t *state,
  race_map_state_parse_result_t *parse_result) {
  if (parse_result) {
    *parse_result = RACE_MAP_STATE_PARSE_OK;
  }

  if (!committed || !*committed || !candidate || !*candidate ||
      !strcmp(committed, candidate) || !Race_MapState_Valid(state) ||
      !state->generation) {
    return RACE_MAP_STATE_STORE_INVALID_ARGUMENT;
  }

  char *serialized = malloc(RACE_MAP_STATE_MAX_FILE_BYTES + 1u);
  race_leaderboard_record_t *verified_records =
    malloc(RACE_MAP_STATE_MAX_RECORDS * sizeof(*verified_records));
  if (!serialized || !verified_records) {
    free(verified_records);
    free(serialized);
    return RACE_MAP_STATE_STORE_MEMORY_ERROR;
  }

  size_t serialized_length;
  if (!Race_MapState_Serialize(state, serialized,
                               RACE_MAP_STATE_MAX_FILE_BYTES + 1u,
                               &serialized_length)) {
    free(verified_records);
    free(serialized);
    return RACE_MAP_STATE_STORE_INVALID_ARGUMENT;
  }

  race_persistence_result_t persisted = Race_Persistence_WriteCandidate(
    candidate, serialized, serialized_length);
  if (persisted != RACE_PERSISTENCE_OK) {
    free(verified_records);
    free(serialized);
    return RACE_MAP_STATE_STORE_IO_ERROR;
  }

  size_t verified_length;
  persisted = Race_Persistence_Read(candidate, serialized,
                                    RACE_MAP_STATE_MAX_FILE_BYTES,
                                    &verified_length);
  if (persisted != RACE_PERSISTENCE_OK) {
    free(verified_records);
    free(serialized);
    return RACE_MAP_STATE_STORE_IO_ERROR;
  }

  race_map_state_t verified_state;
  race_map_state_store_result_t result = Race_MapStateStore_Parse(
    serialized, verified_length, state->map, state->ruleset,
    verified_records, RACE_MAP_STATE_MAX_RECORDS,
    &verified_state, parse_result);
  if (result != RACE_MAP_STATE_STORE_OK ||
      !Race_MapState_Equals(state, &verified_state)) {
    free(verified_records);
    free(serialized);
    return result == RACE_MAP_STATE_STORE_OK
      ? RACE_MAP_STATE_STORE_CORRUPT
      : result;
  }

  persisted = Race_Persistence_Promote(candidate, committed);
  free(verified_records);
  free(serialized);
  return persisted == RACE_PERSISTENCE_OK
    ? RACE_MAP_STATE_STORE_OK
    : RACE_MAP_STATE_STORE_IO_ERROR;
}

const char *Race_MapStateStore_ResultName(race_map_state_store_result_t result) {
  switch (result) {
    case RACE_MAP_STATE_STORE_OK:
      return "ok";
    case RACE_MAP_STATE_STORE_MISSING:
      return "missing";
    case RACE_MAP_STATE_STORE_CORRUPT:
      return "corrupt";
    case RACE_MAP_STATE_STORE_IDENTITY_MISMATCH:
      return "identity mismatch";
    case RACE_MAP_STATE_STORE_IO_ERROR:
      return "I/O error";
    case RACE_MAP_STATE_STORE_MEMORY_ERROR:
      return "memory error";
    case RACE_MAP_STATE_STORE_INVALID_ARGUMENT:
      return "invalid argument";
  }

  return "unknown";
}
