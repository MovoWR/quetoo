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
#include <ObjectivelyMVC/Button.h>

/**
 * @file
 * @brief Controls ViewController.
 */

typedef struct ControlsViewController ControlsViewController;
typedef struct ControlsViewControllerInterface ControlsViewControllerInterface;

/**
 * @brief The ControlsViewController type.
 * @extends ViewController
 */
struct ControlsViewController {

  /**
   * @brief The superclass.
   * @private
   */
  ViewController viewController;

  /**
   * @brief The interface.
   * @private
   */
  ControlsViewControllerInterface *interface;

  /**
   * @brief The single flowed bindings page this route hosts.
   */
  ViewController *bindingsViewController;

  /**
   * @brief The footer commit pair, measured against route entry.
   */
  Label *dirtyStatus;
  Button *revertChanges;
  Button *apply;

  /**
   * @brief Restores the client-shipped defaults for bindings in this route.
   */
  Button *restoreDefaults;

  /**
   * @brief Opens the quit confirmation dialog.
   */
  Button *quit;
};

/**
 * @brief The ControlsViewController interface.
 */
struct ControlsViewControllerInterface {

  /**
   * @brief The superclass interface.
   */
  ViewControllerInterface viewControllerInterface;
};

/**
 * @fn Class *ControlsViewController::_ControlsViewController(void)
 * @brief The ControlsViewController archetype.
 * @return The ControlsViewController Class.
 * @memberof ControlsViewController
 */
CGAME_EXPORT Class *_ControlsViewController(void);

/**
 * @brief Refreshes every binding field in the containing Controls route.
 * @param view A Controls page or descendant View.
 */
void ControlsViewController_RefreshBindings(View *view);

/**
 * @brief Sets the footer hint, which names the command under the pointer.
 * @param view A Controls page or descendant View.
 * @param text The hint text; `NULL` clears it.
 */
void ControlsViewController_SetHint(View *view, const char *text);

/**
 * @brief Publishes the roster's dirty count to the route footer.
 * @param view A Controls page or descendant View.
 * @param dirtyCount The number of rows differing from their route-entry value.
 */
void ControlsViewController_UpdateStatus(View *view, size_t dirtyCount);
