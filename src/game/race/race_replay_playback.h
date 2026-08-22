/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "race_replay_format.h"
#include "race_replay_transport.h"

#define RACE_REPLAY_SEEK_MSEC 5000u
#define RACE_REPLAY_PAUSE_TIMEOUT_MSEC 300000u
#define RACE_REPLAY_CONTROL_COOLDOWN_MSEC 50u
#define RACE_REPLAY_LOAD_COOLDOWN_MSEC 500u

typedef struct {
  uint32_t playhead_ms;
  uint32_t last_update_time;
  uint64_t rate_remainder;
  race_replay_speed_t speed;
  bool paused;
  bool completed;
} race_replay_clock_t;

typedef enum {
  RACE_REPLAY_ADVANCE_NONE,
  RACE_REPLAY_ADVANCE_MOVED,
  RACE_REPLAY_ADVANCE_COMPLETED
} race_replay_advance_t;

void Race_ReplayClock_Init(race_replay_clock_t *clock, uint32_t now);
race_replay_advance_t Race_ReplayClock_Advance(
  race_replay_clock_t *clock, uint32_t now, uint32_t duration_ms);
bool Race_ReplayClock_SetPaused(race_replay_clock_t *clock, bool paused,
                                uint32_t now);
bool Race_ReplayClock_Restart(race_replay_clock_t *clock, uint32_t now);
bool Race_ReplayClock_Seek(race_replay_clock_t *clock, uint32_t target_ms,
                           uint32_t duration_ms, uint32_t now);
bool Race_ReplayClock_SetSpeed(race_replay_clock_t *clock,
                               race_replay_speed_t speed, uint32_t now);
bool Race_ReplayClock_ShiftSpeed(race_replay_clock_t *clock,
                                 int32_t direction, uint32_t now);
uint32_t Race_ReplayClock_OffsetTarget(uint32_t current_ms, int32_t delta_ms,
                                       uint32_t duration_ms);
bool Race_ReplayPlayback_LoadAllowed(uint32_t previous_time,
                                     bool initialized, uint32_t now);
bool Race_ReplayPlayback_AttackExit(bool *attack_released, bool attack_down);

bool Race_ReplaySelection_Select(
  const race_leaderboard_record_t *records, size_t count,
  race_replay_source_t source, const char *uid, uint64_t replay_id,
  race_leaderboard_record_t *record, size_t *rank);

size_t Race_ReplayPlayback_Window(const race_replay_t *replay,
                                  uint32_t playhead_ms,
                                  race_replay_pose_sample_t *output,
                                  size_t capacity, size_t *cursor);
bool Race_ReplayPlayback_Sample(const race_replay_t *replay,
                                uint32_t playhead_ms,
                                race_replay_sample_t *sample,
                                size_t *cursor);
void Race_ReplayPlayback_ApplyViewerState(
  player_state_t *viewer, const race_replay_sample_t *sample,
  size_t preserved_stat);
bool Race_ReplayPlayback_StepTarget(const race_replay_t *replay,
                                    uint32_t playhead_ms, int32_t direction,
                                    uint32_t *target_ms);

size_t Race_ReplayRaceline_PointCount(size_t sample_count);
bool Race_ReplayRaceline_Point(const race_replay_t *replay,
                               size_t point_count, size_t point,
                               race_raceline_point_t *output);
