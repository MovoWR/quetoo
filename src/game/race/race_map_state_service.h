/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "g_local.h"
#include "race_leaderboard.h"

typedef struct {
  size_t count;
  race_leaderboard_record_t records[RACE_LEADERBOARD_TOP_MAX];
  /**
   * @brief Ranked completions across the whole map state, not just the top
   * `count` carried above. The Maps route states the run total, which is not
   * the same number as the rows it can show.
   */
  size_t total;
  /**
   * @brief The queried profile's own best in milliseconds, or zero. Resolved
   * against every record, so a profile outside the top still reports one.
   */
  uint32_t personal_best;
  /**
   * @brief One-based rank of the queried profile within `records`, or zero
   * when it holds none of them.
   */
  size_t personal_rank;
} race_map_state_summary_t;

void Race_MapStateService_Init(void);
void Race_MapStateService_Shutdown(void);
void Race_MapStateService_Load(const char *map);
void Race_MapStateService_Frame(void);
void Race_MapStateService_ClientTimes(const g_client_t *cl,
                                      uint32_t *personal_best,
                                      uint32_t *world_record);
void Race_MapStateService_ClientSplitTimes(const g_client_t *cl,
                                           uint16_t split,
                                           uint32_t *personal_best,
                                           uint32_t *world_record);
/**
 * @brief Loads one map's top records in a single pass.
 * @param uid The profile whose own best and rank to resolve, or NULL.
 */
bool Race_MapStateService_LoadSummary(const char *map, const char *uid,
                                      race_map_state_summary_t *summary);
bool Race_MapStateService_ReplayForRank(
  size_t rank, race_leaderboard_record_t *record, size_t *resolved_rank);
bool Race_MapStateService_ReplayForUid(
  const char *uid, race_leaderboard_record_t *record, size_t *resolved_rank);
bool Race_MapStateService_PrepareFinish(
  g_client_t *cl, race_leaderboard_record_t *candidate,
  race_leaderboard_evaluation_t *evaluation);
bool Race_MapStateService_CommitCandidate(
  const race_leaderboard_record_t *candidate,
  race_leaderboard_evaluation_t *evaluation);
void Race_MapStateService_PrintStatus(g_client_t *cl);
