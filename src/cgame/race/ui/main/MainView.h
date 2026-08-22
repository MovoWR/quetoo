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

#include <ObjectivelyMVC/ImageView.h>
#include <ObjectivelyMVC/Label.h>
#include <ObjectivelyMVC/ScrollView.h>
#include <ObjectivelyMVC/StackView.h>

/**
 * @file
 * @brief The MainViewController's View.
 */

typedef struct MainView MainView;
typedef struct MainViewInterface MainViewInterface;

/**
 * @brief The MainView type.
 * @extends View
 */
struct MainView {

  /**
   * @brief The superclass.
   */
  View view;

  /**
   * @brief The interface.
   * @protected
   */
  MainViewInterface *interface;

  /**
   * @brief The background image.
   */
  ImageView *background;

  /**
   * @brief The logo image.
   */
  ImageView *logo;

  /**
   * @brief The version string.
   */
  Label *version;

  /**
   * @brief Connected-session canvas, overlay and fixed menu regions.
   */
  View *activeBackground;
  View *footerHairline;
  View *overlayShade;
  View *topBar;
  StackView *brand;
  View *menuWindow;
  View *windowHeader;
  View *bottomBar;

  /**
   * @brief Route, connection, and live session labels.
   */
  Label *brandTitle;
  Label *menuHeading;
  Label *connectionLabel;
  Label *windowTitle;
  Label *topPlayers;
  Label *topTime;
  Label *topPhysics;
  Label *serverName;
  Label *serverMap;
  Label *serverPing;
  Label *contextHint;
  StackView *routeSummary;
  StackView *sessionMetrics;
  StackView *serverInfo;

  /**
   * @brief The navigation content viewport.
   */
  View *contentView;

  /**
   * @brief The current route's native content-driven size, captured before
   * overflow wrappers constrain it to the viewport.
   */
  View *sizedPage;
  SDL_Size pageSize;

  /**
   * @brief Shell-owned hosts for global contextual surfaces.
   */
  View *activeVoteHost;
  View *quickSettingsHost;

  /**
   * @brief The fixed route and global-action bars.
   */
  StackView *primaryMenu;
  StackView *topActions;
  StackView *secondaryMenu;
};

/**
 * @brief The MainView interface.
 */
struct MainViewInterface {

  /**
   * @brief The superclass interface.
   */
  ViewInterface viewInterface;

  /**
   * @fn MainView *MainView::init(MainView *self)
   * @brief Initializes this MainView.
   * @param self The MainView.
   * @param frame The frame, or `NULL`.
   * @return The initialized MainView, or `NULL` on error.
   * @memberof MainView
   */
  MainView *(*initWithFrame)(MainView *self, const SDL_Rect *frame);
};

/**
 * @fn Class *MainView::_MainView(void)
 * @brief The MainView archetype.
 * @return The MainView Class.
 * @memberof MainView
 */
CGAME_EXPORT Class *_MainView(void);

/**
 * @brief Collapses the width-flexible views of a subtree onto the width now
 * available to them.
 * @param view The root of the subtree; its own frame is left alone.
 * @param width The width available to `view`, padding included.
 * @details Exposed because a ColumnsView column is the one container whose
 * width is not known until the flow runs, and everything inside it has to be
 * measured against that width rather than against the pane.
 */
void MainView_CollapseWidths(View *view, int32_t width);

/**
 * @brief Promotes a page's authored column containers to ColumnsView.
 * @param page The page View to inspect.
 * @remarks Idempotent: a container that is already a ColumnsView is left alone.
 */
void MainView_PrepareColumnsViews(View *page);

/**
 * @brief Invalidates the cached native size when the active route changes.
 * @param self The MainView.
 */
void MainView_InvalidatePageSize(MainView *self);

/**
 * @brief Reveals a newly focused control within each containing ScrollView.
 * @param view The focused View.
 */
void MainView_RevealView(View *view);

/**
 * @brief Wraps overflowable content in a page-owned ScrollView.
 * @param content The content View.
 * @param className An optional theme class for the ScrollView.
 * @return The existing or newly created ScrollView.
 */
ScrollView *MainView_WrapScrollContent(View *content, const char *className);
