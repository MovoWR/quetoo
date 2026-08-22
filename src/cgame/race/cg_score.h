/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#pragma once

#include "cg_types.h"
#include "race_leaderboard_wire.h"

typedef enum {
  CG_ROSTER_RACE_MODE,
  CG_ROSTER_PRACTICE_MODE,
  CG_ROSTER_SPECTATOR
} cg_roster_group_t;

typedef enum {
  CG_CONNECTION_GOOD,
  CG_CONNECTION_FAIR,
  CG_CONNECTION_POOR,
  CG_CONNECTION_UNAVAILABLE
} cg_connection_quality_t;

typedef struct {
  uint16_t client;
  int16_t ping;
  int16_t ping_average;
  int16_t ping_min;
  int16_t ping_max;
  int16_t ping_variation;
  uint8_t ping_samples;
  uint8_t flags;
  cg_roster_group_t group;
  cg_connection_quality_t quality;
  int16_t spectator_target;
  char name[32];
} cg_roster_entry_t;

typedef struct {
  char name[RACE_LEADERBOARD_MAX_NAME_BYTES + 1u];
  uint32_t time_ms;
  uint64_t date_unix_s;
  bool local_pb;
} cg_leaderboard_snapshot_entry_t;

#if defined(__CG_LOCAL_H__)
size_t Cg_LeaderboardSnapshot(cg_leaderboard_snapshot_entry_t *entries, size_t capacity);
void Cg_ParseScores(void);
void Cg_DrawScores(const player_state_t *ps);
int16_t Cg_LocalPing(void);
size_t Cg_RosterSnapshot(cg_roster_entry_t *entries, size_t capacity);
#endif /* __CG_LOCAL_H__ */
