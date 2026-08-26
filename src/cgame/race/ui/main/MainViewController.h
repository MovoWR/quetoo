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

#include <ObjectivelyMVC/Button.h>
#include <ObjectivelyMVC/NavigationViewController.h>
#include <ObjectivelyMVC/ViewController.h>

#include "MainView.h"
#include "ActiveVoteViewController.h"
#include "QuickSettingsViewController.h"

/**
 * @file
 *
 * @brief The MainViewController.
 */

/**
 * @brief What a staged route hands the shell so the footer can commit for it.
 * @details The design has one footer, carrying the commit pair for whichever
 * route is staged. A route registers on appear and the shell drops the
 * registration on every navigation, so the pair can never outlive the
 * controller it points at.
 */
typedef struct {
  ident self;
  void (*didApply)(ident self);
  void (*didRevert)(ident self);
} RaceCommitDelegate;

/**
 * @brief Registers, or with NULL clears, the footer's commit pair.
 */
typedef struct MainViewController MainViewController;
typedef struct MainViewControllerInterface MainViewControllerInterface;

enum {
  MAIN_VIEW_CONTROLLER_MAX_ROUTES = 8
};

/**
 * @brief The MainViewController type.
 * @extends ViewController
 * @ingroup ViewControllers
 */
struct MainViewController {

  /**
   * @brief The superclass.
   * @private
   */
  ViewController viewController;

  /**
   * @brief The interface.
   * @private
   */
  MainViewControllerInterface *interface;

  /**
   * @brief The MainView.
   */
  MainView *mainView;

  /**
   * @brief The NavigationViewController.
   */
  NavigationViewController *navigationViewController;

  /**
   * @brief The admin menu button, visible only after admin login.
   */
  Button *adminButton;

  /**
   * @brief Global ESC-menu contextual surfaces.
   */
  ActiveVoteViewController *activeVoteViewController;
  QuickSettingsViewController *quickSettingsViewController;
  Button *quickSettingsButton;

  /**
   * @brief The footer's commit pair, and the route it currently acts for.
   */
  Button *applyButton;
  Button *revertButton;
  RaceCommitDelegate commitDelegate;

  /**
   * @brief Active-game actions whose state follows the connection lifecycle.
   */
  Button *resumeButton;
  Button *homeButton;
  Button *disconnectButton;

  /**
   * @brief Ordered route controls and their ViewController classes.
   */
  Button *routeButtons[MAIN_VIEW_CONTROLLER_MAX_ROUTES];
  Class *routeClasses[MAIN_VIEW_CONTROLLER_MAX_ROUTES];
  size_t numRouteButtons;

  /**
   * @brief True while the in-game ESC surface is the active input destination.
   */
  bool menuActive;

  /**
   * @brief Identifier of the most recently focused menu control.
   */
  char focusedIdentifier[128];

  /**
   * @brief Set when an update is available, to present the stock update dialog
   * on the next appearance.
   */
  bool updateAvailable;
};

/**
 * @brief The MainViewController interface.
 */
struct MainViewControllerInterface {

  /**
   * @brief The superclass interface.
   */
  ViewControllerInterface viewControllerInterface;

  /**
   * @fn MainViewController *MainViewController::init(MainViewController *self)
   * @brief Initializes this ViewController.
   * @return The initialized MainViewController, or `NULL` on error.
   * @memberof MainViewController
   */
  MainViewController *(*init)(MainViewController *self);

  /**
   * @fn void MainViewController::navigateToViewController(MainViewController *self, Class *clazz)
   * @brief Navigates to an instance of the ViewController `clazz`.
   * @param self The MainViewController.
   * @param clazz The ViewController Class.
   * @memberof MainViewController
   */
  void (*navigateToViewController)(MainViewController *self, Class *clazz);

  /**
   * @fn void MainViewController::primaryButton(const MainViewController *self, const char *title, const ButtonDelegate *delegate)
   * @brief Adds a Button to the primary menu.
   * @param self The MainViewController.
   * @param title The title text.
   * @param delegate The ButtonDelegate.
   * @memberof MainViewController
   */
  void (*primaryButton)(MainViewController *self, const char *title, const ButtonDelegate *delegate);

  /**
   * @fn void MainViewController::secondaryButton(const MainViewController *self, const char *title, const ButtonDelegate *delegate)
   * @brief Adds a Button to the secondary menu.
   * @param self The MainViewController.
   * @param title The title text.
   * @param delegate The ButtonDelegate.
   * @memberof MainViewController
   */
  void (*secondaryButton)(MainViewController *self, const char *title, const ButtonDelegate *delegate);

  /**
   * @fn void MainViewController::refreshAdminButton(MainViewController *self)
   * @brief Updates the Admin button visibility and dismisses the admin panel if the player is no longer an admin.
   * @param self The MainViewController.
   * @memberof MainViewController
   */
  void (*refreshAdminButton)(MainViewController *self);

  /**
   * @brief Refreshes player-dependent quick actions while the ESC menu is open.
   */
  void (*refreshEscState)(MainViewController *self);

  /**
   * @fn void MainViewController::refreshRoster(MainViewController *self)
   * @brief Refreshes live menu bindings and the visible roster.
   * @param self The MainViewController.
   * @memberof MainViewController
   */
  void (*refreshRoster)(MainViewController *self);

  /**
   * @brief Refreshes the global active-vote banner.
   */
  void (*refreshVote)(MainViewController *self);

  /**
   * @brief Opens or closes the shell-owned quick-settings drawer.
   */
  void (*setQuickSettingsVisible)(MainViewController *self, bool visible);
};

  /**
   * @fn Class *MainViewController::_MainViewController(void)
   * @brief The MainViewController archetype.
   * @return The MainViewController Class.
   * @memberof MainViewController
   */
  CGAME_EXPORT Class *_MainViewController(void);

void MainViewController_RefreshAdmin(const player_state_t *ps);
void MainViewController_ClearState(void);
void MainViewController_RefreshEscState(void);
void MainViewController_CloseQuickSettings(void);
void MainViewController_RefreshVote(void);

void MainViewController_SetCommitDelegate(const RaceCommitDelegate *delegate);

/**
 * @brief Paints the footer's dirty count and enables the commit pair.
 * @param status The count, or NULL/"" for a route holding nothing.
 * @param warn True when a staged change needs a restart, which the design
 * turns gold.
 */
void MainViewController_SetCommitStatus(const char *status, bool warn);

/**
 * @brief Overrides the shell eyebrow for the route currently on top.
 * @param text The eyebrow, or NULL to restore the session default.
 * @param offline True to paint it in brand red, which the design reserves for
 * "No administrator session".
 */
void MainViewController_SetRouteEyebrow(const char *text, bool offline);
