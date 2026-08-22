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
 * @brief Credits ViewController.
 */

typedef struct CreditsViewController CreditsViewController;
typedef struct CreditsViewControllerInterface CreditsViewControllerInterface;

/**
 * @brief Race, Quetoo, Licenses.
 * @details Race extends Quetoo's credits rather than replacing them: the mode
 * gets a tab of its own, the engine's own roster is carried across unaltered on
 * the second, and the licenses the product ships under - which upstream has no
 * screen for at all - get the third.
 */
#define CREDITS_PAGE_COUNT 3

/**
 * @brief One card grid, four Quetoo sections, two license sections.
 */
#define CREDITS_SECTION_COUNT 7

/**
 * @brief The Race tab's two cards.
 */
#define CREDITS_CARD_COUNT 2

/**
 * @brief The Credits route.
 * @details One page strip over one flow of sections, the shape Settings and the
 * Controls roster already use - every section from every page lives in the same
 * flow and the C side shows or hides them. The route carries no filter: the
 * roster is a fixed 50-odd names and a filter over it was cut from the design.
 *
 * Sections land in one of two containers. Narrow ones flow into a ColumnsView;
 * wide ones (the Race tab's cards, Special thanks) sit under it at full width,
 * which is this dialect's answer to the mock's `grid-column: 1 / -1` - a
 * ColumnsView column cannot span its siblings.
 * @extends ViewController
 * @ingroup ViewControllers
 */
struct CreditsViewController {

  /**
   * @brief The superclass.
   * @private
   */
  ViewController viewController;

  /**
   * @brief The interface.
   * @private
   */
  CreditsViewControllerInterface *interface;

  /**
   * @brief The page strip, in strip order.
   * @private
   */
  Button *pageButtons[CREDITS_PAGE_COUNT];
  size_t selectedPage;

  /**
   * @brief The sections, in table order.
   * @details The two containers they live in are not held here: MainView
   * promotes a `columns` container to a ColumnsView after this route is
   * pushed, which replaces the container view, so a pointer taken during
   * loadView would be stale by the first page change. The sections themselves
   * are moved across the promotion intact.
   * @private
   */
  View *sectionViews[CREDITS_SECTION_COUNT];
};

/**
 * @brief The CreditsViewController interface.
 */
struct CreditsViewControllerInterface {

  /**
   * @brief The superclass interface.
   */
  ViewControllerInterface viewControllerInterface;
};

/**
 * @fn Class *CreditsViewController::_CreditsViewController(void)
 * @brief The CreditsViewController archetype.
 * @return The CreditsViewController Class.
 * @memberof CreditsViewController
 */
CGAME_EXPORT Class *_CreditsViewController(void);
