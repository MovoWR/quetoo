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

#include "race_types.h"

/**
 * @brief The finish states the run report bar presents.
 *
 * The design names three (World record / Personal best / Run complete) and
 * models everything else as a variant of the last one. Race has two more
 * outcomes the design never had to draw -- a practice run, which is never
 * submitted, and an invalidated run, which was rejected -- so both get their
 * own state rather than being flattened into "Run complete", which would tell
 * the player their run counted when it did not.
 */
typedef enum {
  CG_RACE_FINISH_STATE_WORLD_RECORD,
  CG_RACE_FINISH_STATE_PERSONAL_BEST,
  CG_RACE_FINISH_STATE_COMPLETE,
  CG_RACE_FINISH_STATE_PRACTICE,
  CG_RACE_FINISH_STATE_INVALID
} cg_race_finish_state_t;

/**
 * @brief Picks the headline state for a decoded finish report.
 *
 * @remarks The precedence is deliberate and is the one decision in this file
 * worth arguing about. Disqualifications outrank achievements, because a run
 * that was rejected is not a record however fast it was. Practice outranks
 * both comparisons, because a practice run is never submitted and so cannot
 * be either. No achievement is shown unless GAME confirms that the replay and
 * map-state publication committed. A world record outranks a personal best,
 * because every world record is also a personal best and only the stronger
 * headline is worth the one line the bar has for it.
 *
 * @remarks `publication_committed` and `new_world_record` are authoritative
 * GAME results. Times are presentation values only: equality cannot
 * distinguish a newly published record from another runner tying it, and a
 * faster run is not a PB if durable publication failed.
 */
static inline cg_race_finish_state_t Cg_RaceFinishReport_Classify(
    const race_mode_t mode, const uint8_t invalid_flags,
    const bool publication_committed, const bool new_world_record) {

  if (invalid_flags) {
    return CG_RACE_FINISH_STATE_INVALID;
  }

  if (mode != RACE_MODE_RACE) {
    return CG_RACE_FINISH_STATE_PRACTICE;
  }

  if (!publication_committed) {
    return CG_RACE_FINISH_STATE_COMPLETE;
  }

  if (new_world_record) {
    return CG_RACE_FINISH_STATE_WORLD_RECORD;
  }

  return CG_RACE_FINISH_STATE_PERSONAL_BEST;
}

/**
 * @return The headline for `state`.
 */
static inline const char *Cg_RaceFinishReport_Headline(
    const cg_race_finish_state_t state) {

  switch (state) {
    case CG_RACE_FINISH_STATE_WORLD_RECORD:
      return "World record";
    case CG_RACE_FINISH_STATE_PERSONAL_BEST:
      return "Personal best";
    case CG_RACE_FINISH_STATE_PRACTICE:
      return "Practice run";
    case CG_RACE_FINISH_STATE_INVALID:
      return "Invalid run";
    default:
      return "Run complete";
  }
}

/**
 * @return True if the world record chip should read as a comparison rather
 * than as the run having become the record itself.
 * @remarks A record run compares to itself, so its delta is always `+0.000`.
 * Drawing that would read as having lost to the record it just set.
 */
static inline bool Cg_RaceFinishReport_ComparesToRecord(
    const cg_race_finish_state_t state, const uint32_t world_record) {
  return world_record && state != CG_RACE_FINISH_STATE_WORLD_RECORD;
}

/**
 * @return True if `delta` puts the run ahead of what it is compared against.
 * @remarks Zero counts as ahead: matching a record is not losing to it.
 */
static inline bool Cg_RaceFinishReport_Ahead(const int32_t delta) {
  return delta <= 0;
}
