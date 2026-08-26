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

#include <ObjectivelyMVC.h>

/**
 * @file
 * @brief The menu's speed horizon: a perspective grid scrolling at the
 * player's own speed.
 */

typedef struct SpeedGridView SpeedGridView;
typedef struct SpeedGridViewInterface SpeedGridViewInterface;

/**
 * @brief A vanishing-point grid, animated from the live player state.
 * @details The connected-session backdrop is a still gradient
 * (`MainView::createActiveBackgroundSurface`); this is the moving half of the
 * same plane. It draws a ground grid converging on a single vanishing point and
 * sweeps it towards the viewer at the speed the player was last travelling at,
 * so the menu carries the pace of the run it was opened out of rather than a
 * decorative loop.
 *
 * There is nothing to author and nothing to ship: the geometry is derived from
 * the view's own frame every frame, so it is correct at every window size and
 * aspect, and the only input is `pm_state.velocity`. Speed is smoothed and
 * clamped, so a standing player still gets a slow drift and a 1500ups player
 * does not get a strobe.
 *
 * The grid is drawn in `render` rather than assembled from subviews for the
 * same reason the backdrop is a texture: the ObjectivelyMVC stylesheet dialect
 * has one fill primitive and no animation properties at all, so neither the
 * convergence nor the motion has any CSS expression.
 * @extends View
 */
struct SpeedGridView {

  /**
   * @brief The superclass.
   * @private
   */
  View view;

  /**
   * @brief The interface.
   * @private
   */
  SpeedGridViewInterface *interface;

  /**
   * @brief Distance travelled through the grid, in world units.
   * @details Only its remainder over one cell is ever read, but it is kept
   * whole so the phase never accumulates the rounding a wrapped counter would.
   * @private
   */
  float travelled;

  /**
   * @brief The smoothed speed the grid is currently scrolling at, in units per
   * second.
   * @private
   */
  float speed;

  /**
   * @brief `SDL_GetTicks` at the last frame, or `0` before the first.
   * @private
   */
  uint64_t ticks;
};

/**
 * @brief The SpeedGridView interface.
 */
struct SpeedGridViewInterface {

  /**
   * @brief The superclass interface.
   */
  ViewInterface viewInterface;

  /**
   * @fn SpeedGridView *SpeedGridView::initWithFrame(SpeedGridView *self, const SDL_Rect *frame)
   * @brief Initializes this SpeedGridView.
   * @param self The SpeedGridView.
   * @param frame The frame, or `NULL`.
   * @return The initialized SpeedGridView, or `NULL` on error.
   * @memberof SpeedGridView
   */
  SpeedGridView *(*initWithFrame)(SpeedGridView *self, const SDL_Rect *frame);
};

/**
 * @brief Registers the grid's console variables.
 * @details Called from `Cg_Module_Init`, not from `initWithFrame`, so the
 * variables exist before the Settings route tries to bind a control to one.
 */
void SpeedGridView_Init(void);

/**
 * @brief False when the player has turned the backdrop off.
 * @details Read by MainView when it decides the view's visibility, so a
 * disabled grid costs nothing at all rather than rendering into a hidden view.
 */
bool SpeedGridView_Enabled(void);

/**
 * @fn Class *SpeedGridView::_SpeedGridView(void)
 * @brief The SpeedGridView archetype.
 * @return The SpeedGridView Class.
 * @memberof SpeedGridView
 */
CGAME_EXPORT Class *_SpeedGridView(void);
