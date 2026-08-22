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

#include "cg_strafe_helper.h"
#include "cg_types.h"

#include <ObjectivelyMVC.h>

/**
 * @file
 * @brief The Strafe helper page's live preview.
 */

typedef struct StrafeHelperPreview StrafeHelperPreview;
typedef struct StrafeHelperPreviewInterface StrafeHelperPreviewInterface;

/**
 * @brief The three readouts the helper can stack above the bar.
 */
#define STRAFE_PREVIEW_READOUT_COUNT 3

/**
 * @brief A stand-in for the helper as `Cg_DrawStrafeHelper` draws it.
 * @details Every `cg_race_strafe_helper_*` cvar the page owns is a screen-space quantity, and the
 * page is not the screen: the preview is the only place a player can see what
 * `cg_race_strafe_helper_y` of -200 or a 0.4 alpha actually looks like without closing the menu,
 * strafing, and opening it again. The bar geometry is a stand-in - its real
 * width comes from the acceleration angles of the frame being drawn, which the
 * menu has no frame to ask - but the colours, styles, offsets, widths, scales,
 * shadows and formats are all read from the same cvars and resolved by the same
 * rules the helper itself uses.
 * @extends View
 */
struct StrafeHelperPreview {

  /**
   * @brief The superclass.
   * @private
   */
  View view;

  /**
   * @brief The interface.
   * @private
   */
  StrafeHelperPreviewInterface *interface;

  /**
   * @brief The readouts, in stacking order: speed, max speed, velocity angle.
   * @private
   */
  Label *readouts[STRAFE_PREVIEW_READOUT_COUNT];

  /**
   * @brief The point size each readout's font was last built at.
   * @details Font::cachedFont is a cache lookup rather than a load, but
   * Text::setFont re-renders the string into a texture - and updateBindings
   * runs on every refresh, which the route drives from pointer motion.
   * @private
   */
  int32_t readoutPointSize[STRAFE_PREVIEW_READOUT_COUNT];

  /**
   * @brief The label naming the dashed rule as screen centre.
   * @private
   */
  Label *centerLabel;

  /**
   * @brief The element the Colours sub-tab is editing, or `SH_COLOR_NONE`.
   * @details Everything else draws at a fraction of its computed alpha, so the
   * edited element is unambiguous without an outline - the markers are two
   * pixels wide and an outline would double them. Set by the Settings route on
   * chip selection, and cleared when the sub-tab changes away from Colours.
   */
  sh_color_element_t isolated;
};

/**
 * @brief The StrafeHelperPreview interface.
 */
struct StrafeHelperPreviewInterface {

  /**
   * @brief The superclass interface.
   */
  ViewInterface viewInterface;

  /**
   * @fn StrafeHelperPreview *StrafeHelperPreview::initWithFrame(StrafeHelperPreview *self, const SDL_Rect *frame)
   * @brief Initializes this StrafeHelperPreview.
   * @param self The StrafeHelperPreview.
   * @param frame The frame, or `NULL`.
   * @return The initialized StrafeHelperPreview, or `NULL` on error.
   * @memberof StrafeHelperPreview
   */
  StrafeHelperPreview *(*initWithFrame)(StrafeHelperPreview *self, const SDL_Rect *frame);
};

/**
 * @fn Class *StrafeHelperPreview::_StrafeHelperPreview(void)
 * @brief The StrafeHelperPreview archetype.
 * @return The StrafeHelperPreview Class.
 * @memberof StrafeHelperPreview
 */
CGAME_EXPORT Class *_StrafeHelperPreview(void);
