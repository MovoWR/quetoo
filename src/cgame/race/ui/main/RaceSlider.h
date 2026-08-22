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

#include "CvarSlider.h"

#include <ObjectivelyMVC.h>

/**
 * @file
 * @brief A CvarSlider that draws a filled track.
 */

typedef struct RaceSlider RaceSlider;
typedef struct RaceSliderInterface RaceSliderInterface;

/**
 * @brief A CvarSlider whose track is filled up to the handle.
 * @details Slider renders a single hardcoded white line across the whole bar
 * and has no notion of a filled portion. This adds a `.fill` View inside the
 * bar, ahead of the handle so the handle still draws on top, and suppresses the
 * line. Everything else — the cvar binding, snapping, the readout — is
 * inherited unchanged.
 * @extends CvarSlider
 */
struct RaceSlider {

  /**
   * @brief The superclass.
   * @private
   */
  CvarSlider cvarSlider;

  /**
   * @brief The interface.
   * @private
   */
  RaceSliderInterface *interface;

  /**
   * @brief The filled portion of the track.
   */
  View *fill;

  /**
   * @brief True once a route has set a readout format of its own.
   * @details CvarSlider::updateBindings forces `"%g"` on any slider whose step
   * is 1 or more, and it runs on every pass - so a unit set once in `loadView`
   * is gone before the control first draws. That is not a default being
   * applied, it is an authored format being overwritten, and this is what tells
   * the two apart: the format Slider::initWithFrame installs does not count,
   * anything set afterwards does.
   * @remarks A format authored through the JSON `labelFormat` inlet is written
   * to the field directly rather than through Slider::setLabelFormat, so it
   * does not set this. No Race resource authors one; a route that wants a unit
   * asks for it in C.
   * @private
   */
  bool labelFormatAuthored;
};

/**
 * @brief The RaceSlider interface.
 */
struct RaceSliderInterface {

  /**
   * @brief The superclass interface.
   */
  CvarSliderInterface cvarSliderInterface;
};

/**
 * @fn Class *RaceSlider::_RaceSlider(void)
 * @brief The RaceSlider archetype.
 * @return The RaceSlider Class.
 * @memberof RaceSlider
 */
CGAME_EXPORT Class *_RaceSlider(void);
