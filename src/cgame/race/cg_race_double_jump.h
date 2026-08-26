/*
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
 */

#pragma once

#include "cg_types.h"

typedef enum {
  CG_RACE_DOUBLE_JUMP_IDLE,
  CG_RACE_DOUBLE_JUMP_FIRST_PRESS,
  CG_RACE_DOUBLE_JUMP_RELEASE,
  CG_RACE_DOUBLE_JUMP_SECOND_PRESS
} cg_race_double_jump_phase_t;

typedef struct {
  cg_race_double_jump_phase_t phase;
  bool held;
} cg_race_double_jump_state_t;

static inline void Cg_RaceDoubleJumpStateDown(
    cg_race_double_jump_state_t *state) {
  if (!state->held) {
    state->held = true;
    state->phase = CG_RACE_DOUBLE_JUMP_FIRST_PRESS;
  }
}

static inline void Cg_RaceDoubleJumpStateUp(
    cg_race_double_jump_state_t *state) {
  state->held = false;
  state->phase = CG_RACE_DOUBLE_JUMP_IDLE;
}

/**
 * @return True when the macro should add jump to this movement command.
 * @param commit Advance the state for a finalized outgoing command. Prediction
 * previews use false so repeated renders cannot consume an input phase.
 */
static inline bool Cg_RaceDoubleJumpStateMove(
    cg_race_double_jump_state_t *state, const bool commit) {
  if (!state->held) {
    return false;
  }

  switch (state->phase) {
    case CG_RACE_DOUBLE_JUMP_FIRST_PRESS:
      if (commit) {
        state->phase = CG_RACE_DOUBLE_JUMP_RELEASE;
      }
      return true;

    case CG_RACE_DOUBLE_JUMP_RELEASE:
      if (commit) {
        state->phase = CG_RACE_DOUBLE_JUMP_SECOND_PRESS;
      }
      return false;

    case CG_RACE_DOUBLE_JUMP_SECOND_PRESS:
      return true;

    default:
      return false;
  }
}

void Cg_RaceDoubleJump_Init(void);
void Cg_RaceDoubleJump_Clear(void);
void Cg_RaceDoubleJump_Move(pm_cmd_t *cmd);
void Cg_RaceDoubleJump_Preview(pm_cmd_t *cmd);
