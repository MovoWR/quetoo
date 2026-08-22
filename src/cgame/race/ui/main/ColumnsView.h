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

#include <ObjectivelyMVC/StackView.h>

/**
 * @file
 * @brief The ColumnsView type.
 */

typedef struct ColumnsView ColumnsView;
typedef struct ColumnsViewInterface ColumnsViewInterface;

/**
 * @brief A StackView that flows its columns into as many vertical tracks as the
 * available width allows, each track stacking on its own.
 * @details Race routes are authored as a single row of columns. On a viewport
 * that cannot hold them all at `minColumnWidth`, this view reduces the number of
 * tracks rather than clipping the route or forcing it to scroll sideways. The
 * column order, hierarchy, and identifiers never change, so outlets, bindings,
 * and focus order survive every supported viewport.
 *
 * Columns are dealt into the tracks in order, and each track then stacks its own
 * without waiting for the track beside it - a short section does not hold a gap
 * open under itself until the long section opposite has finished. `equalRowHeights`
 * is what asks for the other behaviour, and it is for drawn cells only.
 * @extends StackView
 */
struct ColumnsView {

  /**
   * @brief The superclass.
   * @private
   */
  StackView stackView;

  /**
   * @brief The interface.
   * @private
   */
  ColumnsViewInterface *interface;

  /**
   * @brief The narrowest a column may become before this view drops to fewer
   * columns per row, in points.
   * @remarks Bound to the `min-column-width` style attribute.
   */
  int32_t minColumnWidth;

  /**
   * @brief The widest a column may become, in points; zero for unbounded.
   * @details A route whose flow is wider than its columns need is the normal
   * case, but a route reduced to a single column has nothing to divide the
   * viewport with and would hand that one column the whole of it. A row's label
   * pins left and its control pins right, so a column that wide reads as two
   * unrelated halves rather than as a setting and its value. Capping the slot
   * leaves the surplus as margin instead of pouring it into the column.
   * @remarks Bound to the `max-column-width` style attribute.
   */
  int32_t maxColumnWidth;

  /**
   * @brief Whether the flow squares its rows, giving every column in one the
   * same height and starting the next row below the tallest of them.
   * @details Off by default, and off is what an unframed section wants: a
   * section has no drawn edge to mismatch, and squaring makes it wait for the
   * section opposite before the next one in its own track may start, which is
   * dead canvas measured in hundreds of points. It matters where the column is
   * a *card*: a frame is drawn from the column's own height, so two cards side
   * by side end at whatever their own content happened to reach and the row
   * reads as two boxes that failed to line up rather than as a grid.
   * @remarks Bound to the `equal-row-heights` style attribute.
   */
  bool equalRowHeights;
};

/**
 * @brief The ColumnsView interface.
 */
struct ColumnsViewInterface {

  /**
   * @brief The superclass interface.
   */
  StackViewInterface stackViewInterface;

  /**
   * @fn ColumnsView *ColumnsView::initWithFrame(ColumnsView *self, const SDL_Rect *frame)
   * @brief Initializes this ColumnsView with the specified frame.
   * @param frame The frame.
   * @return The initialized ColumnsView, or `NULL` on error.
   * @memberof ColumnsView
   */
  ColumnsView *(*initWithFrame)(ColumnsView *self, const SDL_Rect *frame);
};

/**
 * @brief Returns the width the given column is flowed at, in points.
 * @param self The ColumnsView.
 * @param width The content width the flow would run at.
 * @param column A subview of `self`.
 * @return The column's width, or zero when it is not part of the flow.
 * @details The one authority on a column's width, so that the pass which
 * collapses a route's fixed desktop widths and the pass which places the
 * columns cannot disagree about where the split is. Everything inside a column
 * is measured against the answer to this; nothing inside one is measured
 * against the pane.
 * @memberof ColumnsView
 */
CGAME_EXPORT int32_t ColumnsView_WidthForColumn(ColumnsView *self, int32_t width,
                                                const View *column);

/**
 * @fn Class *ColumnsView::_ColumnsView(void)
 * @brief The ColumnsView archetype.
 * @return The ColumnsView Class.
 * @memberof ColumnsView
 */
CGAME_EXPORT Class *_ColumnsView(void);
