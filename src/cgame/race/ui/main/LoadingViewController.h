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

#include "LoadingGridView.h"

#include <ObjectivelyMVC.h>

/**
 * @file
 *
 * @brief The LoadingViewController.
 */

typedef struct LoadingViewController LoadingViewController;
typedef struct LoadingViewControllerInterface LoadingViewControllerInterface;

/**
 * @brief The LoadingViewController type.
 * @extends ViewController
 * @ingroup ViewControllers
 *
 * @remarks Race's whole-file override of the stock loading screen, carrying
 * the "Menu v2 - Loading" design. `src/cgame/common/cg_ui.c` is stock and is
 * compiled against the stock header, so the first five members below and the
 * whole of LoadingViewControllerInterface are a binding ABI prefix: cg_ui.c
 * allocates this class, dispatches `init` and `setProgress` through the
 * interface, and would read the wrong offsets if either shape moved. Race
 * state is appended, never inserted, and no interface method is added.
 */
struct LoadingViewController {

  /**
   * @brief The superclass.
   * @private
   */
  ViewController viewController;

  /**
   * @brief The interface.
   * @private
   */
  LoadingViewControllerInterface *interface;

  /**
   * @brief The map shot, the full-bleed plate the whole screen is built on.
   */
  ImageView *mapShot;

  /**
   * @brief The Race lockup, centred in the stage above the status bar.
   */
  ImageView *logo;

  /**
   * @brief The progress rail beneath the status bar.
   */
  ProgressBar *progressBar;

  /* Race additions below this line. See the type remarks. */

  /**
   * @brief The radial scrim laid over the map shot.
   */
  ImageView *scrim;

  /**
   * @brief The region the lockup is centred in, inset by the footer's height.
   */
  View *stage;

  /**
   * @brief The status bar, rail and key hint, stacked against the bottom edge.
   */
  StackView *footer;

  /**
   * @brief The status bar. An ImageView because the design fills it with a
   * vertical alpha ramp, which the stylesheet dialect cannot state.
   */
  ImageView *statusBar;

  /**
   * @brief The status bar's two columns.
   */
  StackView *statusLeft;
  StackView *statusRight;

  /**
   * @brief The eyebrow, the map's title, its currently loading asset, the
   * percentage, and the server the client is loading into.
   */
  Label *eyebrowLabel;
  Label *mapTitle;
  Label *mapPath;
  Label *percentLabel;
  Label *hostLabel;

  /**
   * @brief The key hint band along the bottom edge.
   */
  View *keysRule;
  View *keysBar;
  Label *keysHint;

  /**
   * @brief The arriving floor, between the scrim and the stage.
   * @details Last in the struct on purpose. The leading fields and the whole
   * interface are a binding ABI prefix that stock `cg_ui.c` casts to the common
   * type and reads through, so anything new goes after the Race additions.
   */
  LoadingGridView *grid;
};

/**
 * @brief The LoadingViewController interface.
 */
struct LoadingViewControllerInterface {

  /**
   * @brief The superclass interface.
   */
  ViewControllerInterface viewControllerInterface;

  /**
   * @fn LoadingViewController *LoadingViewController::init(LoadingViewController *self)
   * @brief Initializes this ViewController.
   * @return The initialized LoadingViewController, or `NULL` on error.
   * @memberof LoadingViewController
   */
  LoadingViewController *(*init)(LoadingViewController *self);

  /**
   * @fn void LoadingViewController::setProgress(LoadingViewController *self, const cl_loading_t loading)
   * @brief Sets the visual progress of the loading screen.
   * @param percent The percent loaded.
   * @param status The currently loading media item.
   * @memberof LoadingViewController
   */
  void (*setProgress)(LoadingViewController *self, const cl_loading_t loading);
};

/**
 * @fn Class *LoadingViewController::_LoadingViewController(void)
 * @brief The LoadingViewController archetype.
 * @return The LoadingViewController Class.
 * @memberof LoadingViewController
 */
CGAME_EXPORT Class *_LoadingViewController(void);
