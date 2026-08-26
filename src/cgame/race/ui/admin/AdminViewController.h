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

#define ADMIN_SECTION_COUNT 7
#define ADMIN_ROW_COUNT 28
#define ADMIN_VALUE_SIZE 64
#define ADMIN_CAPABILITY_COUNT 8
#define ADMIN_TAB_COUNT 6

/**
 * @brief The Admin route.
 * @details Ported from "Menu v2 - Admin". The route is one flow of sections
 * over one descriptor table, the same grammar Settings uses - so a row cannot
 * appear here without also being searchable, capability-gated and reachable
 * from the footer hint.
 *
 * The one thing this route cannot reproduce from the design is a live value.
 * Registered cvars and map properties live on the server and reach a client
 * only as console replies to `gget` and `mget`; there is no wire channel carrying
 * configuration state. A setting row therefore opens at its shared registry
 * default and is marked *unconfirmed* until this admin writes it. The footer
 * says so rather than letting the control pose as authoritative.
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
   * @brief Sections, and their optional annotations or roster metric.
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
   * @brief Registry defaults, displayed values, and whether each displayed
   * value came from this admin's own write rather than initialization.
   * @private
   */
  char rowDefaults[ADMIN_ROW_COUNT][ADMIN_VALUE_SIZE];
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

  /**
   * @brief The Add map section's name field, and the answer it last got.
   * @private
   */
  TextView *newMapName;
  Label *newMapResult;
  TextView *playerSlot;
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
   * @brief The design's "Admin route hidden" dismissal, and the document it
   * replaces when this connection publishes no capabilities at all.
   * @details The menu is deliberately not a login form. The console supplies
   * the local one-use `radmin_password`, and `radmin <account>` starts the
   * challenge without putting the password in a client command.
   * @private
   */
  View *signedOutPanel;
  View *document;

  /**
   * @brief The subtab strip, and which tab is open.
   * @details Same grammar as the Settings route's pages: five tabs group the
   * seven sections and a sixth hosts the weapon lab, a tab with nothing visible
   * at this capability mask is not offered at all, and a query reaches every
   * tab rather than only the open one - which is why a filtered section names
   * the tab it came from.
   * @private
   */
  Button *tabs[ADMIN_TAB_COUNT];
  size_t openTab;

  /**
   * @brief The Weapons tab's panel, and the tab a confirmation is waiting on.
   * @details Weapons is a tab *kind* rather than a section group: its rows are
   * one authoritative GAME snapshot, not settings rows, so it is a hosted
   * ViewController with its own catalog and commit footer instead of another
   * entry in adminRows. Leaving it with a staged draft asks first, and
   * `pendingTab` is where the answer waits.
   * @private
   */
  ViewController *weaponLab;
  size_t pendingTab;

  /**
   * @brief The eight capability chips, and the hex mask they summarize.
   * @details One chip per bit in STAT_RACE_ADMIN_CAPABILITIES, lit when this
   * session holds it. The whole set is shown rather than only the held bits,
   * because the useful question an administrator asks here is which authority
   * they are *missing*.
   * @private
   */
  Label *capabilityChips[ADMIN_CAPABILITY_COUNT];
  Label *capabilityValue;

  /**
   * @brief The response log: the last command this route sent, and what can be
   * said about its outcome.
   * @details The design prints the command *and* the server's audit line. Only
   * the first half is available to a client: the audit line is a GAME-side
   * console print with no wire channel behind it, so the reply half says where
   * to read it rather than inventing one.
   * @private
   */
  View *responseView;
  Label *responseCommand;
  Label *responseReply;

  /**
   * @brief Row accounting for the legend, recomputed by every filter pass.
   * @details `visibleRows` is what this capability mask and this query left
   * standing; `totalRows` is every row the route declares.
   * @private
   */
  size_t visibleRows;
  size_t totalRows;

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
