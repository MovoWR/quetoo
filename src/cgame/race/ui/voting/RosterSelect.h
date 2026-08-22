/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <ObjectivelyMVC/Select.h>

/**
 * @file
 * @brief A bounded Select for the Race voting roster.
 */

typedef struct RosterSelect RosterSelect;
typedef struct RosterSelectInterface RosterSelectInterface;

/**
 * @brief A Select that exposes a wheel-scrollable window over a large roster.
 * @details All Options remain in the Select, preserving their exact values and
 * keyboard order. Only a bounded window is rendered while the menu is open.
 * @extends Select
 */
struct RosterSelect {

  /**
   * @brief The superclass.
   * @private
   */
  Select select;

  /**
   * @brief The interface.
   * @private
   */
  RosterSelectInterface *interface;

  /**
   * @brief The first Option rendered in the open menu.
   */
  size_t firstVisibleOption;

  /**
   * @brief True while the Select menu is open.
   */
  bool menuOpen;
};

/**
 * @brief The RosterSelect interface.
 */
struct RosterSelectInterface {
  SelectInterface selectInterface;

  /**
   * @fn RosterSelect *RosterSelect::initWithFrame(RosterSelect *self, const SDL_Rect *frame)
   * @brief Initializes this RosterSelect with the specified frame.
   * @param frame The frame.
   * @return The initialized RosterSelect, or `NULL` on error.
   * @memberof RosterSelect
   */
  RosterSelect *(*initWithFrame)(RosterSelect *self, const SDL_Rect *frame);
};

/**
 * @fn Class *RosterSelect::_RosterSelect(void)
 * @brief The RosterSelect archetype.
 * @return The RosterSelect Class.
 * @memberof RosterSelect
 */
CGAME_EXPORT Class *_RosterSelect(void);
