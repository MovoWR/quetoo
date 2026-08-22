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

#include "race_vote_menu.h"

typedef struct {
  char type[16];
  char initiator[32];
  char target[64];
  int32_t yes_votes;
  int32_t no_votes;
  int32_t needed;
  int32_t remaining;
} cg_race_vote_info_t;

typedef struct {
  int32_t remaining;
  uint8_t num_choices;
  race_vote_menu_choice_t choices[RACE_VOTE_MENU_MAX_CHOICES];
} cg_race_vote_menu_t;

typedef struct {
  bool authoritative;
  bool can_cast;
  bool can_cast_menu;
  bool can_nominate;
} cg_race_vote_client_state_t;

bool Cg_RaceVote_ParseInfo(const char *wire, cg_race_vote_info_t *info);
bool Cg_RaceVote_ParseMenu(const char *wire, cg_race_vote_menu_t *menu);
cg_race_vote_client_state_t Cg_RaceVote_ClientState(
  const player_state_t *ps);
void Cg_RaceVote_Draw(void);
