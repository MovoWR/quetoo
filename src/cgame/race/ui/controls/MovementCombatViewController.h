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

#include <ObjectivelyMVC/ViewController.h>

/**
 * @file
 * @brief The Controls route's binding roster.
 */

typedef struct MovementCombatViewController MovementCombatViewController;
typedef struct MovementCombatViewControllerInterface MovementCombatViewControllerInterface;

#define CONTROLS_PAGE_COUNT 7
#define CONTROLS_SECTION_COUNT 13
#define CONTROLS_ROW_COUNT 59
#define CONTROLS_SLOT_COUNT 2
#define CONTROLS_VALUE_SIZE 64

/**
 * @brief The Controls route's binding roster.
 * @details The roster is one page strip over one flow of sections. Sections
 * from every page live in the same flow so that a filter query can surface a
 * match from a page the player is not on, tagged with the page it came from;
 * switching pages is the degenerate case of that same show/hide pass.
 * @extends ViewController
 */
struct MovementCombatViewController {

  /**
   * @brief The superclass.
   */
  ViewController viewController;

  /**
   * @brief The interface.
   * @protected
   */
  MovementCombatViewControllerInterface *interface;

  /**
   * @brief The page strip, in descriptor-table order.
   * @private
   */
  Button *pageButtons[CONTROLS_PAGE_COUNT];
  size_t selectedPage;

  /**
   * @brief The one filter slot, and the state it leaves behind.
   * @private
   */
  TextView *filter;
  Label *emptyState;

  /**
   * @brief Sections, and the chip naming an off-page section's home.
   * @private
   */
  View *sectionViews[CONTROLS_SECTION_COUNT];
  Label *sectionTags[CONTROLS_SECTION_COUNT];

  /**
   * @brief Rows, and the parts of a row the route drives per refresh.
   * @private
   */
  View *rowViews[CONTROLS_ROW_COUNT];
  View *rowRules[CONTROLS_ROW_COUNT];
  View *rowDots[CONTROLS_ROW_COUNT];
  Button *rowReverts[CONTROLS_ROW_COUNT];
  View *rowFields[CONTROLS_ROW_COUNT][CONTROLS_SLOT_COUNT];

  /**
   * @brief The one pill a bind row's two slots share, and the comma between
   * them. `NULL` on rows that carry a cvar control instead of bindings.
   * @details The slots are two views inside one bordered box rather than two
   * boxes, so the box and the separator are row state the route drives - a
   * slot cannot style either from inside itself.
   * @private
   */
  View *rowPills[CONTROLS_ROW_COUNT];
  View *rowSeparators[CONTROLS_ROW_COUNT];

  /**
   * @brief Row state as of route entry, for the footer's commit pair.
   * @details Bindings are kept as scancodes rather than key names: reverting
   * has to bind a key back, and there is no name-to-scancode direction in the
   * module ABI. Cvar rows keep their string, which is what SetCvarString wants.
   * @private
   */
  SDL_Scancode openingKeys[CONTROLS_ROW_COUNT][CONTROLS_SLOT_COUNT];
  char openingValues[CONTROLS_ROW_COUNT][CONTROLS_VALUE_SIZE];

  /**
   * @brief Callback synchronization guard.
   * @private
   */
  bool refreshing;
};

/**
 * @brief The MovementCombatViewController interface.
 */
struct MovementCombatViewControllerInterface {

  /**
   * @brief The superclass interface.
   */
  ViewControllerInterface viewControllerInterface;
};

/**
 * @brief Restores every row in the route to its shipped default.
 */
void MovementCombatViewController_RestoreDefaults(ViewController *self);

/**
 * @brief Restores every row to the value it held when the route was entered.
 */
void MovementCombatViewController_RevertChanges(ViewController *self);

/**
 * @brief Accepts the current values as the new baseline for the commit pair.
 */
void MovementCombatViewController_ApplyChanges(ViewController *self);

/**
 * @fn Class *MovementCombatViewController::_MovementCombatViewController(void)
 * @brief The MovementCombatViewController archetype.
 * @return The MovementCombatViewController Class.
 * @memberof MovementCombatViewController
 */
CGAME_EXPORT Class *_MovementCombatViewController(void);
