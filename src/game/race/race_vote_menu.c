/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_vote_menu.h"

#include <string.h>

#include "shared/shared.h"

void Race_VoteMenu_Init(race_vote_menu_state_t *state) {
  if (!state) {
    return;
  }
  memset(state, 0, sizeof(*state));
  memset(state->client_votes, -1, sizeof(state->client_votes));
}

bool Race_VoteMenu_Begin(race_vote_menu_state_t *state,
                         const char *const *choices, const size_t num_choices,
                         const uint16_t max_clients,
                         const uint32_t start_time, const uint32_t duration,
                         const bool allow_spectators) {
  if (!state || state->active || !choices || !num_choices ||
      num_choices > RACE_VOTE_MENU_MAX_CHOICES || !duration ||
      !max_clients || max_clients > MAX_CLIENTS) {
    return false;
  }

  race_vote_menu_state_t next;
  Race_VoteMenu_Init(&next);
  next.active = true;
  next.deadline = start_time + duration;
  next.allow_spectators = allow_spectators;
  next.max_clients = max_clients;
  next.num_choices = (uint8_t) num_choices;

  for (size_t i = 0; i < num_choices; i++) {
    char canonical[RACE_MAP_IDENTITY_SIZE];
    if (!choices[i] || !Race_MapState_CanonicalizeMap(choices[i], canonical)) {
      return false;
    }
    for (size_t j = 0; j < i; j++) {
      if (!strcmp(next.choices[j].name, canonical)) {
        return false;
      }
    }
    q_strlcpy(next.choices[i].name, canonical,
              sizeof(next.choices[i].name));
  }

  *state = next;
  return true;
}

bool Race_VoteMenu_CanCast(const race_vote_menu_state_t *state,
                           const uint16_t slot, const bool spectator) {
  return state && state->active && slot < state->max_clients &&
         (!spectator || state->allow_spectators);
}

race_vote_menu_cast_result_t Race_VoteMenu_Cast(
    race_vote_menu_state_t *state, const uint16_t slot,
    const bool spectator, const uint8_t one_based_choice) {
  if (!state || !state->active) {
    return RACE_VOTE_MENU_CAST_INACTIVE;
  }
  if (slot >= state->max_clients) {
    return RACE_VOTE_MENU_CAST_INVALID_CLIENT;
  }
  if (spectator && !state->allow_spectators) {
    return RACE_VOTE_MENU_CAST_SPECTATOR;
  }
  if (!one_based_choice || one_based_choice > state->num_choices) {
    return RACE_VOTE_MENU_CAST_INVALID_CHOICE;
  }

  const int8_t next = (int8_t) (one_based_choice - 1u);
  const int8_t previous = state->client_votes[slot];
  if (previous == next) {
    return RACE_VOTE_MENU_CAST_UNCHANGED;
  }
  if (previous >= 0 && previous < state->num_choices) {
    state->choices[previous].votes--;
  }
  state->client_votes[slot] = next;
  state->choices[next].votes++;
  return previous < 0
    ? RACE_VOTE_MENU_CAST_ACCEPTED
    : RACE_VOTE_MENU_CAST_CHANGED;
}

bool Race_VoteMenu_RemoveVoter(race_vote_menu_state_t *state,
                               const uint16_t slot) {
  if (!state || !state->active || slot >= state->max_clients) {
    return false;
  }
  const int8_t previous = state->client_votes[slot];
  if (previous < 0 || previous >= state->num_choices) {
    return false;
  }
  state->choices[previous].votes--;
  state->client_votes[slot] = -1;
  return true;
}

uint32_t Race_VoteMenu_TimeRemaining(const race_vote_menu_state_t *state,
                                     const uint32_t now) {
  if (!state || !state->active || (int32_t) (now - state->deadline) >= 0) {
    return 0u;
  }
  return state->deadline - now;
}

bool Race_VoteMenu_Expired(const race_vote_menu_state_t *state,
                           const uint32_t now) {
  return state && state->active &&
         (int32_t) (now - state->deadline) >= 0;
}

size_t Race_VoteMenu_TiedWinners(
    const race_vote_menu_state_t *state,
    uint8_t tied[RACE_VOTE_MENU_MAX_CHOICES], uint16_t *winning_votes) {
  if (!state || !state->active || !tied) {
    return 0u;
  }

  uint16_t best = 0u;
  size_t count = 0u;
  for (uint8_t i = 0; i < state->num_choices; i++) {
    const uint16_t votes = state->choices[i].votes;
    if (votes > best) {
      best = votes;
      count = 0u;
      tied[count++] = i;
    } else if (votes && votes == best) {
      tied[count++] = i;
    }
  }
  if (winning_votes) {
    *winning_votes = best;
  }
  return count;
}

bool Race_VoteMenu_Resolve(const race_vote_menu_state_t *state,
                           const size_t tied_ordinal, uint8_t *winner,
                           uint16_t *winning_votes) {
  uint8_t tied[RACE_VOTE_MENU_MAX_CHOICES];
  uint16_t votes;
  const size_t count = Race_VoteMenu_TiedWinners(state, tied, &votes);
  if (!count || tied_ordinal >= count || !winner) {
    return false;
  }
  *winner = tied[tied_ordinal];
  if (winning_votes) {
    *winning_votes = votes;
  }
  return true;
}
