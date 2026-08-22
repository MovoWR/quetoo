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

#include "race_profile.h"
#include "race_types.h"

#define RACE_LEADERBOARD_MAX_TIME_MS 600000u
#define RACE_LEADERBOARD_TOP_MAX 15u
#define RACE_LEADERBOARD_MAX_DATE_UNIX_S UINT64_C(253402300799)

/**
 * @brief One unpublished, replay-pending personal-best record.
 * @details Stable profile UID is identity. Display name is only a snapshot.
 */
typedef struct {
  char uid[RACE_PROFILE_UID_SIZE];
  char display_name[RACE_PROFILE_NAME_SIZE];
  uint32_t elapsed_time;
  /**
   * @brief UTC completion time in Unix seconds, or zero for imported records
   * from map-state formats that predate dated Home records.
   */
  uint64_t date_unix_s;
  uint16_t checkpoint_count;
  uint32_t checkpoint_times[RACE_MAX_CHECKPOINTS];
  /**
   * @brief Independent analytical split layout recorded by map-state V4.
   */
  uint16_t split_count;
  uint64_t split_layout;
  uint32_t split_times[RACE_MAX_CHECKPOINTS];
  /**
   * @brief Immutable replay content identity, or zero while publication is
   * pending in the legacy v1 map-state format.
   */
  uint64_t replay_id;
} race_leaderboard_record_t;

/**
 * @brief Pure classification of a record candidate against one map state.
 */
typedef struct {
  bool valid;
  bool would_accept;
  bool personal_best;
  bool world_record;
  bool first_completion;
  bool top;
  size_t top_rank;
} race_leaderboard_evaluation_t;

bool Race_Leaderboard_RecordInit(race_leaderboard_record_t *record,
                                 const char *uid, const char *display_name,
                                 uint32_t elapsed_time,
                                 const uint32_t *checkpoint_times,
                                 size_t checkpoint_count);
bool Race_Leaderboard_RecordSetDate(race_leaderboard_record_t *record,
                                    uint64_t date_unix_s);
bool Race_Leaderboard_RecordSetSplits(race_leaderboard_record_t *record,
                                      const uint32_t *split_times,
                                      size_t split_count,
                                      uint64_t split_layout);
bool Race_Leaderboard_RecordAttachReplay(race_leaderboard_record_t *record,
                                         uint64_t replay_id);
bool Race_Leaderboard_RecordsValid(const race_leaderboard_record_t *records,
                                   size_t count);

const race_leaderboard_record_t *Race_Leaderboard_Find(
  const race_leaderboard_record_t *records, size_t count, const char *uid);

bool Race_Leaderboard_Evaluate(const race_leaderboard_record_t *records,
                               size_t count, size_t capacity,
                               const race_leaderboard_record_t *candidate,
                               race_leaderboard_evaluation_t *evaluation);

bool Race_Leaderboard_Apply(race_leaderboard_record_t *records,
                            size_t *count, size_t capacity,
                            const race_leaderboard_record_t *candidate,
                            race_leaderboard_evaluation_t *evaluation);

size_t Race_Leaderboard_Top(const race_leaderboard_record_t *records,
                            size_t count,
                            const race_leaderboard_record_t **top,
                            size_t top_capacity);
