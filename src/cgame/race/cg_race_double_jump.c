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

#include "cg_local.h"
#include "cg_race_double_jump.h"

static button_t cg_race_double_jump_button;
static cg_race_double_jump_state_t cg_race_double_jump_state;
static const cvar_t *cg_race_double_jump_up_speed;

static void Cg_RaceDoubleJump_down_f(void) {
  const bool wasHeld = cg_race_double_jump_state.held;

  cgi.KeyDown(&cg_race_double_jump_button);

  if (!wasHeld &&
      (cg_race_double_jump_button.state & BUTTON_STATE_HELD)) {
    Cg_RaceDoubleJumpStateDown(&cg_race_double_jump_state);
  }
}

static void Cg_RaceDoubleJump_up_f(void) {
  cgi.KeyUp(&cg_race_double_jump_button);

  if (!(cg_race_double_jump_button.state & BUTTON_STATE_HELD) ||
      (!cg_race_double_jump_button.keys[0] &&
       !cg_race_double_jump_button.keys[1])) {
    memset(&cg_race_double_jump_button, 0,
           sizeof(cg_race_double_jump_button));
    Cg_RaceDoubleJumpStateUp(&cg_race_double_jump_state);
  }
}

static void Cg_RaceDoubleJump_Apply(pm_cmd_t *cmd, const bool commit) {
  if (!cg_race_double_jump_up_speed ||
      !Cg_RaceDoubleJumpStateMove(&cg_race_double_jump_state, commit)) {
    return;
  }

  const int16_t up = (int16_t) (cg_race_double_jump_up_speed->value *
                                cmd->msec);
  if (cmd->up < up) {
    cmd->up = up;
  }
}

void Cg_RaceDoubleJump_Init(void) {
  cg_race_double_jump_up_speed = cgi.GetCvar("cl_up_speed");

  cgi.AddCmd("+double_jump", Cg_RaceDoubleJump_down_f, CMD_CGAME,
             "Tap jump twice across consecutive movement commands");
  cgi.AddCmd("-double_jump", Cg_RaceDoubleJump_up_f, CMD_CGAME, NULL);

  Cg_RaceDoubleJump_Clear();
}

void Cg_RaceDoubleJump_Clear(void) {
  memset(&cg_race_double_jump_button, 0,
         sizeof(cg_race_double_jump_button));
  memset(&cg_race_double_jump_state, 0,
         sizeof(cg_race_double_jump_state));
}

/**
 * @brief Finalizes the macro phase exactly once, then preserves common CGAME
 * movement and button handling.
 */
void Cg_RaceDoubleJump_Move(pm_cmd_t *cmd) {
  Cg_RaceDoubleJump_Apply(cmd, true);
  Cg_Move(cmd);
}

/**
 * @brief Shapes the copied pending prediction command without consuming its
 * phase. Finalized commands already contain their committed macro input.
 */
void Cg_RaceDoubleJump_Preview(pm_cmd_t *cmd) {
  Cg_RaceDoubleJump_Apply(cmd, false);
}
