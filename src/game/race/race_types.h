/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "shared/vector.h"

#define RACE_MAX_CHECKPOINTS 64

/**
 * @brief Authoritative Race participation modes.
 * @details These values are Race-owned and deliberately unrelated to team IDs.
 */
typedef enum {
  RACE_MODE_RACE,
  RACE_MODE_PRACTICE,
  RACE_MODE_SPECTATOR,

  RACE_MODE_TOTAL
} race_mode_t;

/**
 * @brief Authoritative states of a single Race course attempt.
 */
typedef enum {
  RACE_RUN_IDLE,
  RACE_RUN_ACTIVE,
  RACE_RUN_FINISHED,

  RACE_RUN_TOTAL
} race_run_state_t;

/**
 * @brief Mapper-selectable start-zone behavior.
 */
typedef enum {
  RACE_START_TOUCH,
  RACE_START_EXIT,
  RACE_START_JUMP
} race_start_mode_t;

/**
 * @brief Checkpoint-gate comparison policy.
 */
typedef enum {
  RACE_GATE_AT_LEAST,
  RACE_GATE_EXACT
} race_gate_mode_t;

typedef enum {
  RACE_BARRIER_NONE,
  RACE_BARRIER_CHECKPOINT_GATE,
  RACE_BARRIER_ONEWAY_WALL
} race_barrier_type_t;

static inline bool Race_CheckpointGateSatisfied(uint16_t reached, uint16_t required,
                                                race_gate_mode_t mode,
                                                bool invert) {
  bool satisfied = mode == RACE_GATE_EXACT
    ? reached == required
    : reached >= required;
  return invert ? !satisfied : satisfied;
}

static inline bool Race_OneWayDirectionAllowed(vec3_t movement,
                                               vec3_t permitted_direction,
                                               float epsilon) {
  movement.z = 0.f;
  permitted_direction.z = 0.f;
  return Vec3_Dot(movement, permitted_direction) > epsilon;
}

/**
 * @brief Reasons an otherwise completed Race-mode run cannot be ranked.
 * @details Values retain the legacy HUD bit assignments. Practice runs do not
 * use invalidation because they are excluded from durable submission outright.
 */
typedef enum {
  RACE_INVALID_NONE = 0,
  RACE_INVALID_NOCLIP = 1 << 4,
  RACE_INVALID_REPLAY_CAPACITY = 1 << 6
} race_invalid_flags_t;

/**
 * @brief Removes only player-entity contents from a movement clip mask.
 * @details Race players still collide with the world and player-clip brushes;
 * projectile and hitscan masks are independent of this movement-only policy.
 */
static inline int32_t Race_MovementClipMask(int32_t mask) {
  return mask & ~CONTENTS_MONSTER;
}
