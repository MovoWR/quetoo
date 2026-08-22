/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "quetoo.h"
#include "race_map_state.h"
#include "race_types.h"

typedef enum {
  RACE_VOTE_TYPE_NONE,
  RACE_VOTE_TYPE_MAP,
  RACE_VOTE_TYPE_KICK,
  RACE_VOTE_TYPE_PHYSICS
} race_vote_type_t;

#define RACE_VOTE_PHYSICS_SIZE 32u

typedef enum {
  RACE_VOTE_BALLOT_NONE,
  RACE_VOTE_BALLOT_NO,
  RACE_VOTE_BALLOT_YES
} race_vote_ballot_t;

typedef enum {
  RACE_VOTE_CAST_ACCEPTED,
  RACE_VOTE_CAST_CHANGED,
  RACE_VOTE_CAST_UNCHANGED,
  RACE_VOTE_CAST_INACTIVE,
  RACE_VOTE_CAST_INELIGIBLE,
  RACE_VOTE_CAST_INVALID
} race_vote_cast_result_t;

typedef enum {
  RACE_VOTE_OUTCOME_INACTIVE,
  RACE_VOTE_OUTCOME_PENDING,
  RACE_VOTE_OUTCOME_PASSED,
  RACE_VOTE_OUTCOME_FAILED_QUORUM,
  RACE_VOTE_OUTCOME_FAILED_THRESHOLD,
  RACE_VOTE_OUTCOME_FAILED_NO_ELIGIBLE,
  RACE_VOTE_OUTCOME_CANCELLED_TARGET_GONE,
  RACE_VOTE_OUTCOME_CANCELLED_MAP_UNAVAILABLE,
  RACE_VOTE_OUTCOME_CANCELLED_INTERMISSION,
  RACE_VOTE_OUTCOME_CANCELLED_ADMIN
} race_vote_outcome_t;

typedef enum {
  RACE_VOTE_START_AVAILABLE,
  RACE_VOTE_START_COOLDOWN,
  RACE_VOTE_START_LIMIT
} race_vote_start_availability_t;

typedef struct {
  int32_t slot;
  uint64_t connection_id;
} race_vote_identity_t;

typedef struct {
  race_vote_type_t type;
  race_vote_identity_t initiator;
  uint32_t start_time;
  uint32_t duration;
  uint16_t max_clients;
  uint64_t eligible_connection_ids[MAX_CLIENTS];
  union {
    char map[RACE_MAP_IDENTITY_SIZE];
    race_vote_identity_t kick;
    char physics[RACE_VOTE_PHYSICS_SIZE];
  } target;
} race_vote_request_t;

typedef struct {
  bool active;
  bool passed;
  uint64_t generation;
  race_vote_type_t type;
  uint32_t deadline;
  uint16_t max_clients;
  uint16_t eligible_count;
  uint16_t yes_count;
  uint16_t no_count;
  uint64_t eligible_connection_ids[MAX_CLIENTS];
  race_vote_ballot_t ballots[MAX_CLIENTS];
  union {
    char map[RACE_MAP_IDENTITY_SIZE];
    race_vote_identity_t kick;
    char physics[RACE_VOTE_PHYSICS_SIZE];
  } target;
} race_vote_state_t;

bool Race_Vote_IsEligible(bool connected, bool ai, bool spectator, bool alive,
                          race_mode_t mode);
bool Race_Vote_IdentityValid(race_vote_identity_t identity,
                             uint16_t max_clients);
bool Race_Vote_IdentityEqual(race_vote_identity_t first,
                             race_vote_identity_t second);
bool Race_Vote_TimeReached(uint32_t now, uint32_t deadline);
uint32_t Race_Vote_TimeRemaining(uint32_t now, uint32_t deadline);
uint32_t Race_Vote_TimeRemainingSeconds(uint32_t now, uint32_t deadline);
uint16_t Race_Vote_RequiredQuorum(uint16_t eligible_count);
uint16_t Race_Vote_RequiredYes(uint16_t eligible_count);
race_vote_start_availability_t Race_Vote_StartAvailability(
  uint32_t now, uint32_t next_start_time, uint8_t starts,
  uint8_t maximum_starts);
bool Race_Vote_Begin(race_vote_state_t *state,
                     const race_vote_request_t *request);
bool Race_Vote_CanCast(const race_vote_state_t *state,
                       race_vote_identity_t voter);
race_vote_cast_result_t Race_Vote_Cast(race_vote_state_t *state,
                                       race_vote_identity_t voter,
                                       race_vote_ballot_t ballot);
bool Race_Vote_RemoveVoter(race_vote_state_t *state,
                           race_vote_identity_t voter);
race_vote_outcome_t Race_Vote_Evaluate(const race_vote_state_t *state,
                                       uint32_t now);
bool Race_Vote_MarkPassed(race_vote_state_t *state, uint32_t now);
bool Race_Vote_Complete(race_vote_state_t *state,
                        race_vote_outcome_t outcome,
                        race_vote_state_t *completed);
