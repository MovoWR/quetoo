/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "game/race/race_training.h"

typedef enum {
  CG_INPUT_VIEWER_LOCAL,
  CG_INPUT_VIEWER_CHASE,
  CG_INPUT_VIEWER_REPLAY
} cg_input_viewer_source_t;

typedef struct {
  int16_t flags;
  cg_input_viewer_source_t source;
  bool valid;
} cg_input_viewer_state_t;

static inline bool Cg_InputViewer_ShouldCapturePredictionCommand(
    const size_t index, const size_t count) {
  return count > 0 && index < count - 1;
}

static inline cg_input_viewer_state_t Cg_InputViewer_Select(
    const int16_t local_flags, const int16_t chase_flags,
    const int16_t replay_flags, const bool replay_active,
    const bool chasing) {
  cg_input_viewer_state_t state;

  if (replay_active) {
    state = (cg_input_viewer_state_t) {
      .flags = replay_flags,
      .source = CG_INPUT_VIEWER_REPLAY
    };
  } else if (chasing) {
    state = (cg_input_viewer_state_t) {
      .flags = chase_flags,
      .source = CG_INPUT_VIEWER_CHASE
    };
  } else {
    state = (cg_input_viewer_state_t) {
      .flags = local_flags,
      .source = CG_INPUT_VIEWER_LOCAL
    };
  }

  state.valid = Race_InputFlagsValid(state.flags);
  return state;
}

static inline bool Cg_InputViewer_Visible(
    const bool enabled, const bool key_game, const bool dead,
    const bool free_spectator, const bool scores) {
  return enabled && key_game && !dead && !free_spectator && !scores;
}

/**
 * @return The height of the chevron cluster: the up chevron centered above
 * the left, down and right row.
 */
static inline int32_t Cg_InputViewer_ChevronsHeight(
    const int32_t chevron, const int32_t gap) {
  return chevron * 2 + gap;
}

/**
 * @return The width of the chevron cluster.
 */
static inline int32_t Cg_InputViewer_ChevronsWidth(
    const int32_t chevron, const int32_t gap) {
  return chevron * 3 + gap * 2;
}

/**
 * @return The width of the whole cluster: the ATTACK/DUCK column, the
 * chevrons and JUMP, each separated by `group_gap`.
 * @remarks The viewer is anchored to the bottom-left corner and no longer
 * negotiates width against a centered label, so this is a pure sum.
 */
static inline int32_t Cg_InputViewer_ClusterWidth(
    const int32_t chevron, const int32_t gap, const int32_t group_gap,
    const int32_t actions_width, const int32_t jump_width) {
  return actions_width + group_gap +
         Cg_InputViewer_ChevronsWidth(chevron, gap) + group_gap + jump_width;
}
