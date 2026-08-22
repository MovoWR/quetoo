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
#include <stddef.h>
#include <stdint.h>

#include "quetoo.h"
#include "race_map_state.h"

#define RACE_VOTE_MENU_MAX_CHOICES 8u

typedef struct {
  char name[RACE_MAP_IDENTITY_SIZE];
  uint16_t votes;
} race_vote_menu_choice_t;

typedef struct {
  bool active;
  uint32_t deadline;
  bool allow_spectators;
  uint16_t max_clients;
  uint8_t num_choices;
  race_vote_menu_choice_t choices[RACE_VOTE_MENU_MAX_CHOICES];
  int8_t client_votes[MAX_CLIENTS];
} race_vote_menu_state_t;

typedef enum {
  RACE_VOTE_MENU_CAST_ACCEPTED,
  RACE_VOTE_MENU_CAST_CHANGED,
  RACE_VOTE_MENU_CAST_UNCHANGED,
  RACE_VOTE_MENU_CAST_INACTIVE,
  RACE_VOTE_MENU_CAST_SPECTATOR,
  RACE_VOTE_MENU_CAST_INVALID_CLIENT,
  RACE_VOTE_MENU_CAST_INVALID_CHOICE
} race_vote_menu_cast_result_t;

void Race_VoteMenu_Init(race_vote_menu_state_t *state);
bool Race_VoteMenu_Begin(race_vote_menu_state_t *state,
                         const char *const *choices, size_t num_choices,
                         uint16_t max_clients, uint32_t start_time,
                         uint32_t duration, bool allow_spectators);
bool Race_VoteMenu_CanCast(const race_vote_menu_state_t *state,
                           uint16_t slot, bool spectator);
race_vote_menu_cast_result_t Race_VoteMenu_Cast(
  race_vote_menu_state_t *state, uint16_t slot, bool spectator,
  uint8_t one_based_choice);
bool Race_VoteMenu_RemoveVoter(race_vote_menu_state_t *state, uint16_t slot);
uint32_t Race_VoteMenu_TimeRemaining(const race_vote_menu_state_t *state,
                                     uint32_t now);
bool Race_VoteMenu_Expired(const race_vote_menu_state_t *state, uint32_t now);
size_t Race_VoteMenu_TiedWinners(const race_vote_menu_state_t *state,
                                 uint8_t tied[RACE_VOTE_MENU_MAX_CHOICES],
                                 uint16_t *winning_votes);
bool Race_VoteMenu_Resolve(const race_vote_menu_state_t *state,
                           size_t tied_ordinal, uint8_t *winner,
                           uint16_t *winning_votes);
