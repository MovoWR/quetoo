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
 * @brief The loading screen's arriving floor: the same perspective plane the
 * menu uses, drawn only as far in as load progress has reached.
 */

typedef struct LoadingGridView LoadingGridView;
typedef struct LoadingGridViewInterface LoadingGridViewInterface;

/**
 * @brief A perspective floor that extends toward the viewer as the map loads.
 * @details `SpeedGridView` scrolls a floor at the player's own speed. This is
 * the same room seen while it is still being built: the grid exists from the
 * far field in to the depth progress has reached, and a single bright edge
 * marks the boundary. At 0% there is only haze at the horizon; at 100% the
 * floor reaches the footer.
 *
 * @section clock Why there is no clock
 * This is the one thing that makes this view unlike `SpeedGridView`, and it is
 * not a stylistic choice. `Cl_LoadingProgress` drives the loading screen
 * synchronously - it calls `R_BeginFrame`, `Cl_UpdateScreen` and `R_EndFrame`
 * itself, once per asset touched. There is no frame loop. Frames arrive when
 * assets finish: in bursts, with hundreds of milliseconds of nothing across the
 * BSP load and the image-atlas compile.
 *
 * So nothing here is a function of time. The floor's near edge is a pure
 * function of `percent`, and every frame - whenever it happens to arrive -
 * lands exactly where that percentage says. An eased or interpolated edge would
 * freeze part-way through its ease for the whole of a slow asset, which reads
 * as a hang; a clean jump reads as progress.
 *
 * There is nothing to author and nothing to ship: the geometry is derived from
 * the view's own frame, and the only input is the percentage the screen is
 * already being given.
 * @extends View
 */
struct LoadingGridView {

  /**
   * @brief The superclass.
   * @private
   */
  View view;

  /**
   * @brief The interface.
   * @private
   */
  LoadingGridViewInterface *interface;

  /**
   * @brief Load progress, `0` to `100`. Set this from
   * `LoadingViewController::setProgress`.
   * @remarks `Cl_LoadingProgress` clamps relative increments to 99 and only
   * reaches 100 on "ready", so the fully-arrived floor is the last thing drawn
   * before the screen is torn down. That is the intent: arrival is completion.
   */
  int32_t percent;

  /**
   * @brief The footer band's height in pixels, taken out of the bottom of the
   * plane so the floor stops at the status bar rather than running under it.
   * @details Set from `layoutForBounds`, which already computes it. Zero is
   * valid and falls back to a fraction of the frame - the loading screen is the
   * one screen that has to survive a layout pass that never ran.
   */
  int32_t footer;

  /**
   * @brief The highest percentage seen since the last reset.
   * @details The floor never retreats. `Cl_LoadingProgress` takes absolute and
   * relative updates from several passes, and a single backward step would read
   * as the room being dismantled. Reset by `percent` reaching zero, which is
   * what the start of a load sends.
   * @private
   */
  int32_t reached;
};

/**
 * @brief The LoadingGridView interface.
 */
struct LoadingGridViewInterface {

  /**
   * @brief The superclass interface.
   */
  ViewInterface viewInterface;

  /**
   * @fn LoadingGridView *LoadingGridView::initWithFrame(LoadingGridView *self, const SDL_Rect *frame)
   * @brief Initializes this LoadingGridView.
   * @param self The LoadingGridView.
   * @param frame The frame, or `NULL`.
   * @return The initialized LoadingGridView, or `NULL` on error.
   * @memberof LoadingGridView
   */
  LoadingGridView *(*initWithFrame)(LoadingGridView *self, const SDL_Rect *frame);
};

/**
 * @fn Class *LoadingGridView::_LoadingGridView(void)
 * @brief The LoadingGridView archetype.
 * @return The LoadingGridView Class.
 * @memberof LoadingGridView
 */
CGAME_EXPORT Class *_LoadingGridView(void);
