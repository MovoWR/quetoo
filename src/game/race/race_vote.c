/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_vote.h"

#include <limits.h>
#include <string.h>

bool Race_Vote_IsEligible(bool connected, bool ai, bool spectator, bool alive,
                          race_mode_t mode) {
  return connected && !ai && !spectator && alive &&
         (mode == RACE_MODE_RACE || mode == RACE_MODE_PRACTICE);
}

bool Race_Vote_IdentityValid(race_vote_identity_t identity,
                             uint16_t max_clients) {
  return identity.slot >= 0 && identity.slot < max_clients &&
         max_clients <= MAX_CLIENTS && identity.connection_id != 0u;
}

bool Race_Vote_IdentityEqual(race_vote_identity_t first,
                             race_vote_identity_t second) {
  return first.slot == second.slot &&
         first.connection_id == second.connection_id;
}

bool Race_Vote_TimeReached(uint32_t now, uint32_t deadline) {
  return (int32_t) (now - deadline) >= 0;
}

uint32_t Race_Vote_TimeRemaining(uint32_t now, uint32_t deadline) {
  return Race_Vote_TimeReached(now, deadline) ? 0u : deadline - now;
}

uint32_t Race_Vote_TimeRemainingSeconds(const uint32_t now,
                                        const uint32_t deadline) {
  const uint32_t milliseconds = Race_Vote_TimeRemaining(now, deadline);
  return milliseconds / 1000u + (milliseconds % 1000u != 0u);
}

uint16_t Race_Vote_RequiredQuorum(uint16_t eligible_count) {
  return eligible_count ? (uint16_t) ((eligible_count + 1u) / 2u) : 0u;
}

uint16_t Race_Vote_RequiredYes(uint16_t eligible_count) {
  return eligible_count ? (uint16_t) (eligible_count / 2u + 1u) : 0u;
}

race_vote_start_availability_t Race_Vote_StartAvailability(
  uint32_t now, uint32_t next_start_time, uint8_t starts,
  uint8_t maximum_starts) {
  if (starts >= maximum_starts) {
    return RACE_VOTE_START_LIMIT;
  }
  if (next_start_time && !Race_Vote_TimeReached(now, next_start_time)) {
    return RACE_VOTE_START_COOLDOWN;
  }
  return RACE_VOTE_START_AVAILABLE;
}

static bool Race_Vote_RequestValid(const race_vote_request_t *request) {
  if (!request || !request->duration || request->duration > INT32_MAX ||
      !request->max_clients || request->max_clients > MAX_CLIENTS ||
      !Race_Vote_IdentityValid(request->initiator, request->max_clients) ||
      request->eligible_connection_ids[request->initiator.slot] !=
        request->initiator.connection_id) {
    return false;
  }

  if (request->type == RACE_VOTE_TYPE_MAP) {
    return request->target.map[0] &&
           strnlen(request->target.map, sizeof(request->target.map)) <
             sizeof(request->target.map);
  }
  if (request->type == RACE_VOTE_TYPE_KICK) {
    return Race_Vote_IdentityValid(request->target.kick,
                                   request->max_clients);
  }
  if (request->type == RACE_VOTE_TYPE_PHYSICS) {
    return request->target.physics[0] &&
           strnlen(request->target.physics,
                   sizeof(request->target.physics)) <
             sizeof(request->target.physics);
  }
  return false;
}

bool Race_Vote_Begin(race_vote_state_t *state,
                     const race_vote_request_t *request) {
  if (!state || state->active || !Race_Vote_RequestValid(request)) {
    return false;
  }

  uint16_t eligible_count = 0;
  for (uint16_t slot = 0; slot < request->max_clients; slot++) {
    if (request->eligible_connection_ids[slot]) {
      eligible_count++;
    }
  }
  if (!eligible_count) {
    return false;
  }

  uint64_t generation = state->generation + 1u;
  if (!generation) {
    generation = 1u;
  }

  race_vote_state_t next = {
    .active = true,
    .generation = generation,
    .type = request->type,
    .deadline = request->start_time + request->duration,
    .max_clients = request->max_clients,
    .eligible_count = eligible_count,
    .yes_count = 1u
  };
  memcpy(next.eligible_connection_ids, request->eligible_connection_ids,
         sizeof(next.eligible_connection_ids));
  next.ballots[request->initiator.slot] = RACE_VOTE_BALLOT_YES;
  if (request->type == RACE_VOTE_TYPE_MAP) {
    memcpy(next.target.map, request->target.map, sizeof(next.target.map));
  } else if (request->type == RACE_VOTE_TYPE_PHYSICS) {
    memcpy(next.target.physics, request->target.physics,
           sizeof(next.target.physics));
  } else {
    next.target.kick = request->target.kick;
  }

  *state = next;
  return true;
}

bool Race_Vote_CanCast(const race_vote_state_t *state,
                       const race_vote_identity_t voter) {
  return state && state->active && !state->passed &&
         Race_Vote_IdentityValid(voter, state->max_clients) &&
         state->eligible_connection_ids[voter.slot] == voter.connection_id;
}

race_vote_cast_result_t Race_Vote_Cast(race_vote_state_t *state,
                                       race_vote_identity_t voter,
                                       race_vote_ballot_t ballot) {
  if (!state || !state->active || state->passed) {
    return RACE_VOTE_CAST_INACTIVE;
  }
  if (ballot != RACE_VOTE_BALLOT_YES && ballot != RACE_VOTE_BALLOT_NO) {
    return RACE_VOTE_CAST_INVALID;
  }
  if (!Race_Vote_IdentityValid(voter, state->max_clients) ||
      state->eligible_connection_ids[voter.slot] != voter.connection_id) {
    return RACE_VOTE_CAST_INELIGIBLE;
  }

  const race_vote_ballot_t previous = state->ballots[voter.slot];
  if (previous == ballot) {
    return RACE_VOTE_CAST_UNCHANGED;
  }
  if (previous == RACE_VOTE_BALLOT_YES) {
    state->yes_count--;
  } else if (previous == RACE_VOTE_BALLOT_NO) {
    state->no_count--;
  }

  state->ballots[voter.slot] = ballot;
  if (ballot == RACE_VOTE_BALLOT_YES) {
    state->yes_count++;
  } else {
    state->no_count++;
  }
  return previous == RACE_VOTE_BALLOT_NONE
    ? RACE_VOTE_CAST_ACCEPTED
    : RACE_VOTE_CAST_CHANGED;
}

bool Race_Vote_RemoveVoter(race_vote_state_t *state,
                           race_vote_identity_t voter) {
  if (!state || !state->active || state->passed ||
      !Race_Vote_IdentityValid(voter, state->max_clients) ||
      state->eligible_connection_ids[voter.slot] != voter.connection_id) {
    return false;
  }

  if (state->ballots[voter.slot] == RACE_VOTE_BALLOT_YES) {
    state->yes_count--;
  } else if (state->ballots[voter.slot] == RACE_VOTE_BALLOT_NO) {
    state->no_count--;
  }
  state->ballots[voter.slot] = RACE_VOTE_BALLOT_NONE;
  state->eligible_connection_ids[voter.slot] = 0u;
  state->eligible_count--;
  return true;
}

race_vote_outcome_t Race_Vote_Evaluate(const race_vote_state_t *state,
                                       uint32_t now) {
  if (!state || !state->active) {
    return RACE_VOTE_OUTCOME_INACTIVE;
  }
  if (state->passed) {
    return RACE_VOTE_OUTCOME_PASSED;
  }
  if (!state->eligible_count) {
    return RACE_VOTE_OUTCOME_FAILED_NO_ELIGIBLE;
  }

  const uint16_t decided = state->yes_count + state->no_count;
  if (decided < state->eligible_count &&
      !Race_Vote_TimeReached(now, state->deadline)) {
    return RACE_VOTE_OUTCOME_PENDING;
  }
  if (decided < Race_Vote_RequiredQuorum(state->eligible_count)) {
    return RACE_VOTE_OUTCOME_FAILED_QUORUM;
  }
  return state->yes_count >= Race_Vote_RequiredYes(state->eligible_count)
    ? RACE_VOTE_OUTCOME_PASSED
    : RACE_VOTE_OUTCOME_FAILED_THRESHOLD;
}

bool Race_Vote_MarkPassed(race_vote_state_t *state, const uint32_t now) {
  if (!state || !state->active || state->passed ||
      Race_Vote_Evaluate(state, now) != RACE_VOTE_OUTCOME_PASSED) {
    return false;
  }
  state->passed = true;
  state->deadline = now;
  return true;
}

bool Race_Vote_Complete(race_vote_state_t *state,
                        race_vote_outcome_t outcome,
                        race_vote_state_t *completed) {
  if (!state || !state->active || outcome == RACE_VOTE_OUTCOME_INACTIVE ||
      outcome == RACE_VOTE_OUTCOME_PENDING) {
    return false;
  }

  if (completed) {
    *completed = *state;
  }
  const uint64_t generation = state->generation;
  memset(state, 0, sizeof(*state));
  state->generation = generation;
  return true;
}
