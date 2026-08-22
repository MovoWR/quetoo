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

#include "BindTextView.h"

/**
 * @file
 * @brief A BindTextView showing one slot of a command's key list.
 */

typedef struct RaceBindTextView RaceBindTextView;
typedef struct RaceBindTextViewInterface RaceBindTextViewInterface;

/**
 * @brief A BindTextView showing one slot of a command's key list.
 * @details Common's BindTextView renders every key bound to its command in one
 * field, comma joined, and appends to that set on capture. The Race Controls
 * route gives each command two addressable slots instead - primary and
 * alternate - laid out inline in one shared pill as `Q, 6`, so the player can
 * retarget either without disturbing the other. This subclass narrows the field
 * to the key at `slot`, and makes capture replace that slot rather than grow
 * the set.
 * @extends BindTextView
 */
struct RaceBindTextView {

  /**
   * @brief The superclass.
   * @private
   */
  BindTextView bindTextView;

  /**
   * @brief The interface.
   * @private
   */
  RaceBindTextViewInterface *interface;

  /**
   * @brief The zero-based index into this command's key list.
   */
  int slot;
};

/**
 * @brief The RaceBindTextView interface.
 */
struct RaceBindTextViewInterface {

  /**
   * @brief The superclass interface.
   */
  BindTextViewInterface bindTextViewInterface;

  /**
   * @fn RaceBindTextView *RaceBindTextView::initWithBindAndSlot(RaceBindTextView *self, const char *bind, int slot)
   * @brief Initializes this RaceBindTextView with the given bind and slot.
   * @param bind The bind (e.g. `+forward`).
   * @param slot The zero-based index into the bind's key list.
   * @return The initialized RaceBindTextView, or `NULL` on error.
   * @memberof RaceBindTextView
   */
  RaceBindTextView *(*initWithBindAndSlot)(RaceBindTextView *self, const char *bind, int slot);
};

/**
 * @brief Returns the key occupying `slot` of `bind`, or `SDL_SCANCODE_UNKNOWN`.
 */
SDL_Scancode RaceBindTextView_KeyForSlot(const char *bind, int slot);

/**
 * @fn Class *RaceBindTextView::_RaceBindTextView(void)
 * @brief The RaceBindTextView archetype.
 * @return The RaceBindTextView Class.
 * @memberof RaceBindTextView
 */
CGAME_EXPORT Class *_RaceBindTextView(void);
