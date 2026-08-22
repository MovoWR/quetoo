/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_replay_playback.h"

#include <string.h>

void Race_ReplayClock_Init(race_replay_clock_t *clock, const uint32_t now) {
  if (clock) {
    memset(clock, 0, sizeof(*clock));
    clock->last_update_time = now;
    clock->speed = RACE_REPLAY_SPEED_NORMAL;
  }
}

race_replay_advance_t Race_ReplayClock_Advance(
  race_replay_clock_t *clock, const uint32_t now,
  const uint32_t duration_ms) {
  if (!clock || !duration_ms) {
    return RACE_REPLAY_ADVANCE_NONE;
  }
  const uint32_t elapsed = now - clock->last_update_time;
  clock->last_update_time = now;
  if (clock->paused || clock->completed || !elapsed) {
    return RACE_REPLAY_ADVANCE_NONE;
  }
  uint32_t numerator, denominator;
  if (!Race_ReplaySpeed_Ratio(clock->speed, &numerator, &denominator)) {
    return RACE_REPLAY_ADVANCE_NONE;
  }
  if (clock->playhead_ms >= duration_ms) {
    clock->playhead_ms = duration_ms;
    clock->rate_remainder = 0u;
    clock->paused = true;
    clock->completed = true;
    return RACE_REPLAY_ADVANCE_COMPLETED;
  }
  const uint64_t scaled = (uint64_t) elapsed * numerator +
                          clock->rate_remainder;
  const uint64_t advance = scaled / denominator;
  clock->rate_remainder = scaled % denominator;
  const uint32_t remaining = duration_ms - clock->playhead_ms;
  if (advance >= remaining) {
    clock->playhead_ms = duration_ms;
    clock->rate_remainder = 0u;
    clock->paused = true;
    clock->completed = true;
    return RACE_REPLAY_ADVANCE_COMPLETED;
  }
  if (!advance) {
    return RACE_REPLAY_ADVANCE_NONE;
  }
  clock->playhead_ms += (uint32_t) advance;
  return RACE_REPLAY_ADVANCE_MOVED;
}

bool Race_ReplayClock_SetPaused(race_replay_clock_t *clock,
                                const bool paused, const uint32_t now) {
  if (!clock || clock->paused == paused || (clock->completed && !paused)) {
    return false;
  }
  clock->paused = paused;
  clock->last_update_time = now;
  clock->rate_remainder = 0u;
  return true;
}

bool Race_ReplayClock_Restart(race_replay_clock_t *clock,
                              const uint32_t now) {
  if (!clock) {
    return false;
  }
  const bool changed = clock->playhead_ms || clock->rate_remainder ||
                       clock->paused || clock->completed;
  const race_replay_speed_t speed = clock->speed;
  Race_ReplayClock_Init(clock, now);
  clock->speed = speed;
  return changed;
}

bool Race_ReplayClock_Seek(race_replay_clock_t *clock,
                           const uint32_t target_ms,
                           const uint32_t duration_ms,
                           const uint32_t now) {
  if (!clock || !duration_ms) {
    return false;
  }
  const uint32_t target = Mini(target_ms, duration_ms);
  const bool changed = target != clock->playhead_ms || !clock->paused ||
                       clock->completed || clock->rate_remainder;
  clock->playhead_ms = target;
  clock->last_update_time = now;
  clock->rate_remainder = 0u;
  clock->paused = true;
  clock->completed = target == duration_ms;
  return changed;
}

bool Race_ReplayClock_SetSpeed(race_replay_clock_t *clock,
                               const race_replay_speed_t speed,
                               const uint32_t now) {
  uint32_t numerator, denominator;
  if (!clock || !Race_ReplaySpeed_Ratio(speed, &numerator, &denominator) ||
      clock->speed == speed) {
    return false;
  }
  clock->speed = speed;
  clock->last_update_time = now;
  clock->rate_remainder = 0u;
  return true;
}

bool Race_ReplayClock_ShiftSpeed(race_replay_clock_t *clock,
                                 const int32_t direction,
                                 const uint32_t now) {
  if (!clock || (direction != -1 && direction != 1)) {
    return false;
  }
  const int32_t speed = (int32_t) clock->speed + direction;
  if (speed < RACE_REPLAY_SPEED_QUARTER ||
      speed >= RACE_REPLAY_SPEED_TOTAL) {
    return false;
  }
  return Race_ReplayClock_SetSpeed(
    clock, (race_replay_speed_t) speed, now);
}

uint32_t Race_ReplayClock_OffsetTarget(const uint32_t current_ms,
                                       const int32_t delta_ms,
                                       const uint32_t duration_ms) {
  if (delta_ms < 0) {
    const uint32_t magnitude = (uint32_t) (-(int64_t) delta_ms);
    return magnitude >= current_ms ? 0u : current_ms - magnitude;
  }
  const uint32_t positive = (uint32_t) delta_ms;
  return positive >= duration_ms - Mini(current_ms, duration_ms)
           ? duration_ms
           : current_ms + positive;
}

bool Race_ReplayPlayback_LoadAllowed(const uint32_t previous_time,
                                     const bool initialized,
                                     const uint32_t now) {
  return !initialized ||
         now - previous_time >= RACE_REPLAY_LOAD_COOLDOWN_MSEC;
}

bool Race_ReplayPlayback_AttackExit(bool *attack_released,
                                    const bool attack_down) {
  if (!attack_released) {
    return false;
  }
  if (!attack_down) {
    *attack_released = true;
    return false;
  }
  return *attack_released;
}

bool Race_ReplaySelection_Select(
  const race_leaderboard_record_t *records, const size_t count,
  const race_replay_source_t source, const char *uid,
  const uint64_t replay_id, race_leaderboard_record_t *record,
  size_t *rank) {
  if (!record || !Race_Leaderboard_RecordsValid(records, count)) {
    return false;
  }

  const race_leaderboard_record_t *selected = NULL;
  if (source == RACE_REPLAY_SOURCE_PERSONAL_BEST) {
    selected = Race_Leaderboard_Find(records, count, uid);
  } else if (source == RACE_REPLAY_SOURCE_WORLD_RECORD) {
    const race_leaderboard_record_t *top[1];
    if (Race_Leaderboard_Top(records, count, top, lengthof(top))) {
      selected = top[0];
    }
  } else if (source == RACE_REPLAY_SOURCE_ID && replay_id) {
    for (size_t i = 0u; i < count; i++) {
      if (records[i].replay_id == replay_id) {
        selected = records + i;
        break;
      }
    }
  }
  if (!selected || !selected->replay_id) {
    return false;
  }

  *record = *selected;
  if (rank) {
    *rank = 0u;
    const race_leaderboard_record_t *top[RACE_LEADERBOARD_TOP_MAX];
    const size_t top_count = Race_Leaderboard_Top(
      records, count, top, lengthof(top));
    for (size_t i = 0u; i < top_count; i++) {
      if (top[i] == selected) {
        *rank = i + 1u;
        break;
      }
    }
  }
  return true;
}

static size_t Race_ReplayPlayback_Cursor(const race_replay_t *replay,
                                         const uint32_t playhead_ms) {
  size_t low = 0u;
  size_t high = replay->sample_count;
  while (low < high) {
    const size_t middle = low + (high - low) / 2u;
    if (replay->samples[middle].time_ms <= playhead_ms) {
      low = middle + 1u;
    } else {
      high = middle;
    }
  }
  return low ? low - 1u : 0u;
}

size_t Race_ReplayPlayback_Window(const race_replay_t *replay,
                                  const uint32_t playhead_ms,
                                  race_replay_pose_sample_t *output,
                                  const size_t capacity, size_t *cursor) {
  if (!Race_Replay_Valid(replay) || !output || !capacity ||
      playhead_ms > replay->elapsed_time) {
    return 0u;
  }
  const size_t first = Race_ReplayPlayback_Cursor(replay, playhead_ms);
  const size_t available = replay->sample_count - first;
  const size_t count = capacity < available ? capacity : available;
  for (size_t i = 0u; i < count; i++) {
    const race_replay_sample_t *sample = replay->samples + first + i;
    output[i] = (race_replay_pose_sample_t) {
      .time_ms = sample->time_ms,
      .origin = sample->pm_state.origin,
      .view_angles = sample->pm_state.view_angles
    };
  }
  if (cursor) {
    *cursor = first;
  }
  return count;
}

bool Race_ReplayPlayback_Sample(const race_replay_t *replay,
                                const uint32_t playhead_ms,
                                race_replay_sample_t *sample,
                                size_t *cursor) {
  if (!Race_Replay_Valid(replay) || !sample ||
      playhead_ms > replay->elapsed_time) {
    return false;
  }
  const size_t index = Race_ReplayPlayback_Cursor(replay, playhead_ms);
  *sample = replay->samples[index];
  if (cursor) {
    *cursor = index;
  }
  return true;
}

void Race_ReplayPlayback_ApplyViewerState(
  player_state_t *viewer, const race_replay_sample_t *sample,
  const size_t preserved_stat) {
  if (!viewer || !sample) {
    return;
  }

  const uint8_t viewer_client = viewer->client;
  const int16_t viewer_entity = viewer->entity;
  const int16_t preserved_value = preserved_stat < MAX_STATS
    ? viewer->stats[preserved_stat] : 0;

  viewer->pm_state = sample->pm_state;
  memcpy(viewer->stats, sample->stats, sizeof(viewer->stats));
  memcpy(viewer->inventory, sample->inventory, sizeof(viewer->inventory));

  viewer->client = viewer_client;
  viewer->entity = viewer_entity;
  if (preserved_stat < MAX_STATS) {
    viewer->stats[preserved_stat] = preserved_value;
  }
}

bool Race_ReplayPlayback_StepTarget(const race_replay_t *replay,
                                    const uint32_t playhead_ms,
                                    const int32_t direction,
                                    uint32_t *target_ms) {
  if (!Race_Replay_Valid(replay) || !target_ms ||
      (direction != -1 && direction != 1) ||
      playhead_ms > replay->elapsed_time) {
    return false;
  }
  size_t cursor = Race_ReplayPlayback_Cursor(replay, playhead_ms);
  if (direction < 0) {
    if (replay->samples[cursor].time_ms >= playhead_ms && cursor) {
      cursor--;
    }
    if (!cursor && replay->samples[cursor].time_ms >= playhead_ms) {
      return false;
    }
  } else {
    while (cursor < replay->sample_count &&
           replay->samples[cursor].time_ms <= playhead_ms) {
      cursor++;
    }
    if (cursor >= replay->sample_count) {
      return false;
    }
  }
  *target_ms = replay->samples[cursor].time_ms;
  return true;
}

size_t Race_ReplayRaceline_PointCount(const size_t sample_count) {
  return sample_count < RACE_REPLAY_RACELINE_MAX_POINTS
    ? sample_count
    : RACE_REPLAY_RACELINE_MAX_POINTS;
}

bool Race_ReplayRaceline_Point(const race_replay_t *replay,
                               const size_t point_count, const size_t point,
                               race_raceline_point_t *output) {
  if (!Race_Replay_Valid(replay) || !output || point_count < 2u ||
      point_count != Race_ReplayRaceline_PointCount(replay->sample_count) ||
      point >= point_count) {
    return false;
  }
  const size_t source = point * (replay->sample_count - 1u) /
                        (point_count - 1u);
  const race_replay_sample_t *sample = replay->samples + source;
  *output = (race_raceline_point_t) {
    .time_ms = sample->time_ms,
    .origin = sample->pm_state.origin
  };
  return true;
}
