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
 * @brief Admin ViewController.
 */

typedef struct AdminViewController AdminViewController;
typedef struct AdminViewControllerInterface AdminViewControllerInterface;

#define ADMIN_SECTION_COUNT 6
#define ADMIN_ROW_COUNT 25
#define ADMIN_VALUE_SIZE 64

/**
 * @brief The Admin route.
 * @details Ported from "Menu v2 - Admin". The route is one flow of sections
 * over one descriptor table, the same grammar Settings uses - so a row cannot
 * appear here without also being searchable, capability-gated and reachable
 * from the footer hint.
 *
 * The one thing this route cannot reproduce from the design is a live value.
 * Race settings live on the server and reach a client only as console replies
 * to `race admin settings get`; there is no wire channel carrying settings
 * state. So a settings row opens at the value the shipped catalog defaults to
 * and is marked *unconfirmed* until this admin writes it, and the footer says
 * so rather than letting the control pose as authoritative. A value another
 * admin set, a `global.settings` file, or a map override will read as the
 * default here until it is written from this route.
 * @extends ViewController
 */
struct AdminViewController {

  /**
   * @brief The superclass.
   * @private
   */
  ViewController viewController;

  /**
   * @brief The interface.
   * @private
   */
  AdminViewControllerInterface *interface;

  /**
   * @brief The one filter slot, and the state it leaves behind.
   * @private
   */
  TextView *filter;
  Label *emptyState;

  /**
   * @brief Sections, and the per-section annotation ("applies on next map").
   * @private
   */
  View *sectionViews[ADMIN_SECTION_COUNT];
  Label *sectionMetrics[ADMIN_SECTION_COUNT];

  /**
   * @brief Rows, and the parts of a row the route drives per refresh.
   * @details `rowPainted` caches the last painted modified state so a refresh
   * only touches a view when it actually changed - the refresh runs on pointer
   * motion, and adding or removing a class name re-applies the stylesheet to
   * the whole subtree.
   * @private
   */
  View *rowViews[ADMIN_ROW_COUNT];
  View *rowRules[ADMIN_ROW_COUNT];
  View *rowDots[ADMIN_ROW_COUNT];
  Button *rowReverts[ADMIN_ROW_COUNT];
  View *rowControls[ADMIN_ROW_COUNT];
  bool rowPainted[ADMIN_ROW_COUNT];

  /**
   * @brief What this route believes each settings row holds, and whether that
   * belief came from this admin's own write rather than the shipped default.
   * @private
   */
  char rowValues[ADMIN_ROW_COUNT][ADMIN_VALUE_SIZE];
  bool rowConfirmed[ADMIN_ROW_COUNT];

  /**
   * @brief The last value this route actually sent for each row.
   * @details A Slider notifies its delegate on every motion, so a drag would
   * post one admin command per pixel. The delegate only moves `rowValues`; the
   * difference against this is flushed once the button or key comes back up.
   * @private
   */
  char rowSent[ADMIN_ROW_COUNT][ADMIN_VALUE_SIZE];

  /**
   * @brief The free-text fields, which are row controls like any other.
   * @private
   */
  TextView *mapName;
  TextView *playerSlot;
  TextView *settingsScope;
  TextView *settingsKey;
  TextView *settingsValue;

  /**
   * @brief The connected roster, which the Players section draws directly.
   * @private
   */
  View *rosterRows[MAX_CLIENTS];
  Label *rosterName[MAX_CLIENTS];
  Label *rosterSlot[MAX_CLIENTS];
  Label *rosterMode[MAX_CLIENTS];
  Label *rosterPing[MAX_CLIENTS];
  Label *rosterEmpty;

  /**
   * @brief How many roster lines the last refresh left standing.
   * @details The roster redraws on pointer motion, so the count is kept to spot
   * a join or a part - the section's own visibility is decided by the filter
   * pass, which has to run again when that number crosses zero.
   * @private
   */
  size_t rosterShown;

  /**
   * @brief The footer: the hovered row's command, and the provenance line.
   * @private
   */
  Label *hint;
  Label *provenance;

  /**
   * @brief The capability bitmask this client last saw in its player state.
   * @private
   */
  uint16_t capabilities;

  /**
   * @brief Callback synchronization guard.
   * @private
   */
  bool refreshing;
};

/**
 * @brief The AdminViewController interface.
 */
struct AdminViewControllerInterface {

  /**
   * @brief The superclass interface.
   */
  ViewControllerInterface viewControllerInterface;
};

/**
 * @fn Class *AdminViewController::_AdminViewController(void)
 * @brief The AdminViewController archetype.
 * @return The AdminViewController Class.
 * @memberof AdminViewController
 */
CGAME_EXPORT Class *_AdminViewController(void);
