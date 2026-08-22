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
#include "MapListCollectionView.h"

#include <ObjectivelyMVC.h>

/**
 * @file
 * @brief Create Server ViewController, Race module.
 * @details Shadows the common one. The differences are all in what a race
 * server is allowed to be: a physics select the common page has no concept of,
 * and no frag limit, spawn-farthest or bot count - deathmatch inheritance that
 * means nothing on a ranked race server.
 */

typedef struct CreateServerViewController CreateServerViewController;
typedef struct CreateServerViewControllerInterface CreateServerViewControllerInterface;

/**
 * @brief The CreateServerViewController type.
 * @extends ViewController
 */
struct CreateServerViewController {

  /**
   * @brief The superclass.
   * @private
   */
  ViewController viewController;

  /**
   * @brief The interface.
   * @private
   */
  CreateServerViewControllerInterface *interface;

  /**
   * @brief The create Button.
   */
  Button *create;

  /**
   * @brief The player slot and time limit Selects.
   * @details Both are numeric cvars the design states as a fixed roster rather
   * than a free-text field. Each roster carries the cvar's current value as an
   * extra option when it names something the roster does not - `sv_max_clients`
   * ships at MAX_CLIENTS, which is not one of the design's five.
   */
  Select *maxClients;
  Select *timeLimit;

  /**
   * @brief The captions beside those two labels, shown only when the cvar holds
   * a value the roster does not name.
   * @details The odd value is a value like any other, so it reads as one: the
   * option says `64`, and where that 64 came from is said beside the setting's
   * name rather than folded into the option's own title.
   */
  Label *maxClientsCaption;
  Label *timeLimitCaption;

  /**
   * @brief The gameplay Select.
   */
  Select *gameplay;

  /**
   * @brief The physics preset Select, bound to `g_race_physics`.
   */
  Select *physics;

  /**
   * @brief The SelectDelegate CvarSelect installed on `physics` before this
   * route took the slot, so a pick still writes the cvar.
   */
  SelectDelegate physicsCvarDelegate;

  /**
   * @brief The MapListCollectionView.
   */
  MapListCollectionView *mapList;

  /**
   * @brief The teamsplay Select.
   */
  Select *teams;

  /**
   * @brief The Race rules section's own status, stating the ruleset the
   * records of a server started from this page would be kept against.
   */
  Label *rulesMeta;

  /**
   * @brief The Map rotation section's own status: how much of the rotation is
   * selected, stated beside the section head the way every other Race pane
   * states its count.
   */
  Label *mapsSelection;

  /**
   * @brief The rotation's guidance line, which doubles as where a rotation of
   * nothing says so - the page used to answer an empty selection by printing
   * to the console, where a player in the menu never sees it.
   */
  Label *mapsHint;
};

/**
 * @brief The CreateServerViewController interface.
 */
struct CreateServerViewControllerInterface {

  /**
   * @brief The superclass interface.
   */
  ViewControllerInterface viewControllerInterface;
};

/**
 * @fn Class *CreateServerViewController::_CreateServerViewController(void)
 * @brief The CreateServerViewController archetype.
 * @return The CreateServerViewController Class.
 * @memberof CreateServerViewController
 */
CGAME_EXPORT Class *_CreateServerViewController(void);
