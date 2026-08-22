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
 * @brief Join server ViewController, Race module.
 * @details Shadows the common one (see the `vpath` in this module's
 * Makefile.am, and the `Remove` item group in Quetoo.vs15/cgame-race.vcxproj).
 * The list is the same list; what this adds is the two-pane split from the
 * design of record, the details pane it selects into, and ping colouring
 * against `cg_quick_join_max_ping`.
 */

typedef struct JoinServerViewController JoinServerViewController;
typedef struct JoinServerViewControllerInterface JoinServerViewControllerInterface;

/**
 * @brief The JoinServerViewController type.
 * @extends ViewController
 */
struct JoinServerViewController {

  /**
   * @brief The superclass.
   * @private
   */
  ViewController viewController;

  /**
   * @brief The interface.
   * @private
   */
  JoinServerViewControllerInterface *interface;

  /**
   * @brief A copy of the client's servers list, for sorting, filtering, etc.
   */
  List *servers;

  /**
   * @brief The servers TableView.
   */
  TableView *serversTableView;

  /**
   * @brief The roster TableView, listing the selected server's players.
   */
  TableView *rosterTableView;

  /**
   * @brief The list pane's count, sort order, empty state and ping legend.
   */
  Label *totalLabel, *sortLabel, *emptyLabel, *legendLabel;

  /**
   * @brief The details pane.
   */
  Label *hostnameLabel, *addressLabel, *hintLabel, *motdLabel, *sourceLabel;
  Label *mapLabel, *gameplayLabel, *physicsLabel, *playersLabel, *pingLabel;
  Label *rosterCountLabel, *rosterEmptyLabel;

  /**
   * @brief The parts of the details pane that only exist once a server is
   * selected, hidden together with the roster until then.
   */
  View *detailGrid, *rosterRule, *rosterHeader;

  /**
   * @brief The two pane bodies, which close up around whatever they are
   * currently showing rather than reserving room for a full table.
   */
  View *listBody, *detailsBody;

  /**
   * @brief The details pane's actions.
   */
  Button *connectButton, *quickJoinButton;

  /**
   * @brief The max ping slider, which doubles as the ping colour threshold.
   * @remarks Typed as its Slider superclass: the JSON declares a CvarSlider,
   * which writes cg_quick_join_max_ping itself, and CvarSlider.h is not part of
   * the umbrella ObjectivelyMVC header this one includes.
   */
  Slider *maxPingSlider;

  /**
   * @brief The selected server's hostname.
   * @details The selection is held by name rather than by row index, so that it
   * survives a re-sort and a refresh. Empty when nothing is selected.
   */
  char selectedHostname[48];
};

/**
 * @brief The JoinServerViewController interface.
 */
struct JoinServerViewControllerInterface {

  /**
   * @brief The superclass interface.
   */
  ViewControllerInterface viewControllerInterface;

  /**
   * @fn void JoinServerViewController::reloadServers(JoinServerViewController *self)
   * @brief Reloads the list of known servers.
   * @param self The JoinServerViewController.
   * @memberof JoinServerViewController
   */
  void (*reloadServers)(JoinServerViewController *self);
};

/**
 * @fn Class *JoinServerViewController::_JoinServerViewController(void)
 * @brief The JoinServerViewController archetype.
 * @return The JoinServerViewController Class.
 * @memberof JoinServerViewController
 */
CGAME_EXPORT Class *_JoinServerViewController(void);
