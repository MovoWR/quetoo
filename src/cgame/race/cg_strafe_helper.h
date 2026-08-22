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
#include "game/race/race_training.h"

/**
 * @brief The three elements the helper draws in a colour of their own.
 * @details Declared here rather than in the implementation because the Settings
 * route's Colours editor and its live preview both name these three, and a
 * private index in each of them is three rosters that can disagree. The order
 * is the strip order the editor draws, so a target's index is its element.
 */
typedef enum {
  SH_COLOR_ACCELERATING,
  SH_COLOR_OPTIMAL,
  SH_COLOR_CENTER_MARKER,

  /**
   * @brief Not an element: nothing is being edited, so nothing is isolated.
   */
  SH_COLOR_NONE
} sh_color_element_t;

#if defined(__CG_LOCAL_H__)

void Cg_DrawStrafeHelper(const player_state_t *ps);

/**
 * @return True when the helper's own speed readout is enabled, and the Race
 * HUD must therefore stand down from its crosshair readout.
 * @remarks The two are the same number in different colours 85 pixels apart,
 * so exactly one of them may draw. The helper wins because its readout is the
 * configurable one and the player opted into it; see the Race HUD spec, §6.
 * @remarks Gated on the readout alone rather than on the helper bar, because
 * `cg_race_strafe_helper_ups` draws independently of
 * `cg_race_strafe_helper_draw` -- a player with the bar off and the readout on
 * would otherwise still get two.
 */
bool Cg_StrafeHelper_OwnsSpeedReadout(void);
void Cg_InitStrafeHelper(void);
void Cg_ClearStrafeHelper(void);
void Cg_SetStrafeHelperVelocity(const vec3_t velocity);
void Cg_UpdateStrafeHelper(const race_strafe_sample_t *update);

#endif /* __CG_LOCAL_H__ */
