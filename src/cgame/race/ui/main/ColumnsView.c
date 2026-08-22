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

#include "cg_local.h"

#include "ColumnsView.h"
#include "MainView.h"

#define _Class _ColumnsView

/**
 * @brief The narrowest a column may become before the flow drops a slot.
 */
#define COLUMNS_VIEW_MIN_COLUMN_WIDTH 440

/**
 * @brief The most tracks a flow will run.
 * @details A ceiling on the stack arrays the flow keeps a running height in,
 * not a design limit: the narrowest column any route authors is 300, so 16
 * tracks is a viewport several times wider than anything this menu is composed
 * for.
 */
#define COLUMNS_VIEW_MAX_TRACKS 16

/**
 * @brief What one pass over the flow is for.
 * @details The flow is the only place that knows a column's width, so measuring
 * it, placing it, and answering `ColumnsView_WidthForColumn` all have to be the
 * same walk. Anything that derives a column's width a second way is free to
 * derive a different one, which is the whole of the bug this type closes.
 */
typedef struct {

  /**
   * @brief Invoked for each column; `NULL` to only measure.
   */
  void (*place)(View *column, int32_t x, int32_t y, int32_t w);

  /**
   * @brief Optional; the column whose flowed width the caller wants.
   */
  const View *query;

  /**
   * @brief The width `query` was flowed at, or zero when it was not flowed.
   * @remarks A flow run for a query answers the width and nothing else: column
   * widths fall out of the packing alone, so it skips the per-column height
   * measurement that a real measuring pass exists to do. `collapsePageWidths`
   * asks once per column per pass, and `sizeThatContains` on a whole route
   * column is not something to do a second time for an answer it cannot give.
   */
  int32_t queryWidth;

} columns_flow_t;

/**
 * @brief Returns the width the given column is composed at, or zero for none.
 * @details A two-column route states its split as exact widths on the columns
 * themselves - the design's 901 / 701 within 1642 - so the split is authored
 * data, not something the flow may invent. An auto-fit route (Settings,
 * Controls, Admin) authors `min-width: 0` and no maximum instead, which is how
 * a column says it has no width of its own and wants an equal share.
 * @remarks The maximum is the field to read, not the minimum:
 * `MainView_CollapseWidths` releases the minimum of a column it has had to
 * narrow, so only the maximum survives a pass unchanged.
 */
static int32_t composedWidth(const View *column) {

  if (column->maxSize.w > 0 && column->maxSize.w < INT32_MAX) {
    return column->maxSize.w;
  }

  return 0;
}

/**
 * @brief Returns the number of slots the given content width can hold.
 */
static int32_t slotCapacity(const ColumnsView *self, int32_t width) {

  const int32_t spacing = self->stackView.spacing;
  const int32_t minWidth = max(1, self->minColumnWidth);

  return max(1, (width + spacing) / (minWidth + spacing));
}

/**
 * @brief Returns how many tracks the flow runs, given how many columns it has.
 */
static size_t trackCount(const ColumnsView *self, int32_t width, size_t count) {

  if (count == 0) {
    return 1;
  }

  const int32_t capacity = slotCapacity(self, width);

  return (size_t) clamp(capacity, 1, (int32_t) min(count, (size_t) COLUMNS_VIEW_MAX_TRACKS));
}

/**
 * @brief Returns the width one track gets when the flow divides evenly.
 * @details Divided by the number of tracks the *viewport* could hold, floored
 * at two, rather than by the number of columns the page happens to have. A page
 * of one section would otherwise be handed the whole viewport and then capped
 * at `maxColumnWidth`, giving it a different column width from the page beside
 * it with two - the 799 against 780 that made Settings' tabs disagree about
 * where their left column ends. Floored at two because a lone column measured
 * against a viewport that could hold five would come out narrower than any real
 * column on the route.
 */
static int32_t evenTrackWidth(const ColumnsView *self, int32_t width, size_t count) {

  const int32_t spacing = self->stackView.spacing;
  const int32_t capacity = slotCapacity(self, width);
  const int32_t slots = clamp(capacity, 1, (int32_t) max(count, (size_t) 2));

  int32_t trackWidth = (width - spacing * (slots - 1)) / slots;
  if (self->maxColumnWidth > 0) {
    trackWidth = min(trackWidth, self->maxColumnWidth);
  }

  return max(1, trackWidth);
}

/**
 * @brief Gives every column of one finished row that row's height.
 * @details Assigned to the frame after the column has laid itself out, not
 * before, so nothing inside the column re-measures against the taller box - the
 * contents stay exactly where their own layout put them and only the box that
 * is drawn around them grows. Re-derived from a freshly measured height on
 * every flow rather than accumulated, so a row that gets shorter does shrink;
 * assigning through `sizeToContain` instead would be a high-water mark and
 * could only ever grow.
 */
static void equalizeRow(const Array *columns, size_t first, size_t last,
                        int32_t rowHeight) {

  for (size_t i = first; i < last; i++) {
    View *column = columns->elements[i];
    column->frame.h = rowHeight;
  }
}

/**
 * @brief The total width the flow's tracks are composed at, or zero when the
 * flow divides evenly instead.
 * @details Three things disqualify a flow. One track has nothing to split with -
 * a stacked pane takes the whole width, which is what wrapping is for. A column
 * that declares no width of its own is an auto-fit column, and one of them makes
 * the whole flow auto-fit. And a set of columns that leaves slack at this width
 * is not a split either: Join's filter band composes 768 + 460 inside 1642
 * because it is a group pinned left and a hint pinned right, and handing those
 * two their composed widths would pull the hint in off the edge it belongs on.
 * Only columns with no slack - the routes composed as 901 / 701 across the whole
 * 1642 - are stating a split, and only there is the split the flow's to keep.
 */
static int32_t composedFlowWidth(const Array *columns, size_t tracks,
                                 int32_t width, int32_t spacing) {

  if (tracks < 2 || columns->count < tracks) {
    return 0;
  }

  int32_t total = 0;
  for (size_t i = 0; i < tracks; i++) {
    const int32_t composed = composedWidth(columns->elements[i]);
    if (composed <= 0) {
      return 0;
    }
    total += composed;
  }

  if (total + spacing * (int32_t) (tracks - 1) < width) {
    return 0;
  }

  return total;
}

/**
 * @brief Lays out the flow's tracks: how wide each one is, and where it starts.
 * @details Capped at the width a column is composed at, never stretched past it -
 * the same rule `maxColumnWidth` states for the auto-fit flows. A viewport wider
 * than the composition leaves the surplus as margin rather than pouring it into
 * a column, so every row, rule and field inside keeps the width it was authored
 * at. Narrower, and the columns give up width in proportion, which is what keeps
 * the split reading as the same split at every size.
 */
static void measureTracks(const ColumnsView *self, const Array *columns,
                          int32_t width, size_t tracks,
                          int32_t *trackWidth, int32_t *trackX) {

  const int32_t spacing = self->stackView.spacing;
  const int32_t composedTotal = composedFlowWidth(columns, tracks, width, spacing);
  const int32_t evenWidth = evenTrackWidth(self, width, columns->count);
  const int32_t content = width - spacing * (int32_t) (tracks - 1);

  int32_t total = 0;
  for (size_t i = 0; i < tracks; i++) {

    if (composedTotal > 0) {
      const int32_t composed = composedWidth(columns->elements[i]);
      int32_t share = (int32_t) ((int64_t) content * composed / composedTotal);
      share = min(share, composed);
      if (self->maxColumnWidth > 0) {
        share = min(share, self->maxColumnWidth);
      }
      trackWidth[i] = max(1, share);
    } else {
      trackWidth[i] = evenWidth;
    }

    total += trackWidth[i];
  }

  total += spacing * (int32_t) (tracks - 1);

  // An evenly divided flow that does not fill its width is centred in it. A
  // capped lone column otherwise sits hard left with the rest of the pane blank
  // beside it, which reads as a route whose right half failed to load rather
  // than as a deliberately narrow one. A composed split is left where its
  // composition puts it; that origin is authored, not left over.
  int32_t x = composedTotal > 0 ? 0 : max(0, (width - total) / 2);

  for (size_t i = 0; i < tracks; i++) {
    trackX[i] = x;
    x += trackWidth[i] + spacing;
  }
}

/**
 * @brief Flows the columns, invoking `flow->place` for each one.
 * @return The total height the flow occupies.
 */
static int32_t flowColumns(ColumnsView *self, int32_t width, columns_flow_t *flow) {

  Array *columns = $((View *) self, visibleSubviews);
  if (columns->count == 0) {
    release(columns);
    return 0;
  }

  const int32_t spacing = self->stackView.spacing;
  const size_t tracks = trackCount(self, width, columns->count);

  int32_t trackWidth[COLUMNS_VIEW_MAX_TRACKS], trackX[COLUMNS_VIEW_MAX_TRACKS];
  measureTracks(self, columns, width, tracks, trackWidth, trackX);

  // Two ways to stack the same columns in the same tracks; the only difference
  // is where the next one starts. A card grid squares its rows, because a card's
  // frame is its column's own height and two of them ending at different heights
  // read as two boxes that failed to line up. Everything else lets each track
  // run on its own, because an unframed section has no such edge to mismatch -
  // and paying for the alignment there means a section waits for the one
  // opposite it: Display's one-row "Quality preset" left 270 points of empty
  // canvas under itself before the next section could start, because the section
  // beside it ran that much longer.
  int32_t trackY[COLUMNS_VIEW_MAX_TRACKS] = { 0 };
  int32_t rowY = 0, rowHeight = 0;
  size_t rowStart = 0;

  for (size_t i = 0; i < columns->count; i++) {

    View *column = columns->elements[i];
    const size_t track = i % tracks;

    if (flow->query == column) {
      flow->queryWidth = trackWidth[track];
    }

    if (self->equalRowHeights && track == 0 && i) {
      if (flow->place) {
        equalizeRow(columns, rowStart, i, rowHeight);
      }
      rowY += rowHeight + spacing;
      rowStart = i;
      rowHeight = 0;
    }

    const int32_t y = self->equalRowHeights ? rowY : trackY[track];
    int32_t columnHeight;

    if (flow->place) {
      flow->place(column, trackX[track], y, trackWidth[track]);
      columnHeight = column->frame.h;
    } else {
      columnHeight = $(column, sizeThatContains).h;
    }

    rowHeight = max(rowHeight, columnHeight);
    trackY[track] += columnHeight + spacing;
  }

  int32_t height;

  if (self->equalRowHeights) {
    if (flow->place) {
      equalizeRow(columns, rowStart, columns->count, rowHeight);
    }
    height = rowY + rowHeight + spacing;
  } else {
    height = 0;
    for (size_t j = 0; j < tracks; j++) {
      height = max(height, trackY[j]);
    }
  }

  release(columns);
  return max(0, height - spacing);
}

/**
 * @brief ViewEnumerator warning for any descendant wider than its column.
 * @details This failure never shows up as a wrong number anywhere - it is one
 * view measured against the wrong container, and it draws as a caption over the
 * column beside it, a field sliced off at the gutter, or a grid clipped by the
 * window. Naming the view and both widths is what turns those unrelated-looking
 * symptoms back into the one geometry error they are.
 */
static void warnColumnOverflow_enumerate(View *view, ident data) {

  const int32_t columnWidth = (int32_t) (intptr_t) data;

  if (view->hidden) {
    return;
  }

  if (view->frame.w > columnWidth) {
    $(view, warn, WarningTypeLayout, "Width %d exceeds its column's %d",
      view->frame.w, columnWidth);
  }

  // A container that clips, and a ScrollView's document, are both allowed to be
  // wider than the box they show through - that is what they are for. Only what
  // draws straight onto the column is measured against it.
  if (view->clipsSubviews || $((Object *) view, isKindOfClass, _ScrollView())) {
    return;
  }

  $(view, enumerateSubviews, warnColumnOverflow_enumerate, data);
}

/**
 * @brief ViewEnumerator for marking a placed column's whole subtree.
 */
static void setNeedsLayout_enumerate(View *view, ident data) {

  view->needsLayout = true;
  $(view, enumerateSubviews, setNeedsLayout_enumerate, data);
}

/**
 * @brief Sizes a column to its slot and pins it there for its own layout pass.
 * @details The slot width is only known here, but View::layoutIfNeeded lays a
 * view's subviews out before the view itself - so by the time the flow runs,
 * everything inside the column has already resolved against the width the
 * column happened to be carrying, and cleared its needsLayout on the way. The
 * pass below would then find nothing left to do and the stale geometry would
 * stand. Re-marking the subtree is what makes the column's own layout pass
 * authoritative, and it is what lets a column that was hidden during an earlier
 * pass - a Controls page the player had not opened yet - resolve at all.
 */
static void placeColumn(View *column, int32_t x, int32_t y, int32_t w) {

  // The pin is restored afterwards because it must not outlive this pass. A
  // flow that runs before the document has been clamped to the viewport sees a
  // nonsense width - millions of points, from an unsized ScrollView - and a
  // surviving `minSize.w == maxSize.w` at that width is unrecoverable:
  // MainView::collapsePageWidths clamps against exactly those two fields, so no
  // later pass can ever bring the column back down. Scoping the pin means a
  // garbage pass costs a wasted layout and nothing more.
  const int32_t minWidth = column->minSize.w, maxWidth = column->maxSize.w;

  column->minSize.w = column->maxSize.w = w;

  const SDL_Size size = MakeSize(w, column->frame.h);
  $(column, resize, &size);

  // Pinning the column is not enough on its own. A two-column route states the
  // design's own column width on every row, rule, field and grid inside the
  // column as well - 901 in the 901 pane, 701 in the 701 one - and a fixed
  // width answers to nothing the flow does. Collapsing the subtree against the
  // width this column actually got is what makes the column, and not the pane,
  // the thing its contents are measured from.
  MainView_CollapseWidths(column, w);

  setNeedsLayout_enumerate(column, NULL);
  $(column, layoutIfNeeded);

  const SDL_Rect bounds = $(column, bounds);
  $(column, enumerateSubviews, warnColumnOverflow_enumerate,
    (ident) (intptr_t) bounds.w);

  column->minSize.w = minWidth;
  column->maxSize.w = maxWidth;

  column->frame.x = x;
  column->frame.y = y;
}

#pragma mark - View

/**
 * @see View::applyStyle(View *, const Style *)
 */
static void applyStyle(View *self, const Style *style) {

  super(View, self, applyStyle, style);

  ColumnsView *this = (ColumnsView *) self;

  const Inlet inlets[] = MakeInlets(
    MakeInlet("equal-row-heights", InletTypeBool, &this->equalRowHeights, NULL),
    MakeInlet("max-column-width", InletTypeInteger, &this->maxColumnWidth, NULL),
    MakeInlet("min-column-width", InletTypeInteger, &this->minColumnWidth, NULL)
  );

  $(self, bind, inlets, (Dictionary *) style->attributes);
}

/**
 * @see View::init(View *)
 */
static View *init(View *self) {
  return (View *) $((ColumnsView *) self, initWithFrame, NULL);
}

/**
 * @see View::layoutSubviews(View *)
 */
static void layoutSubviews(View *self) {

  // The width always comes from the superview; only the wrapped height is ours.
  const SDL_Rect bounds = $(self, bounds);

  columns_flow_t flow = { .place = placeColumn };
  const int32_t height = flowColumns((ColumnsView *) self, bounds.w, &flow);

  const SDL_Size size = MakeSize(self->frame.w,
                                 height + self->padding.top + self->padding.bottom);
  $(self, resize, &size);
}

/**
 * @see View::sizeThatFits(const View *)
 */
static SDL_Size sizeThatFits(const View *self) {

  const SDL_Rect bounds = $(self, bounds);

  columns_flow_t flow = { .place = NULL };

  SDL_Size size = MakeSize(bounds.w + self->padding.left + self->padding.right,
                           flowColumns((ColumnsView *) self, bounds.w, &flow) +
                             self->padding.top + self->padding.bottom);

  // Report no intrinsic width: a route that has to wrap must never widen its
  // document past the viewport the shell handed it.
  if (self->autoresizingMask & ViewAutoresizingWidth) {
    size.w = 0;
  }

  size.w = clamp(size.w, self->minSize.w, self->maxSize.w);
  size.h = clamp(size.h, self->minSize.h, self->maxSize.h);

  return size;
}

/**
 * @see View::sizeThatContains(const View *)
 */
static SDL_Size sizeThatContains(const View *self) {

  const SDL_Size size = $(self, size);
  const SDL_Size fits = $(self, sizeThatFits);

  // Unlike View, report neither axis' current frame: the flow shrinks with the
  // viewport horizontally, and vertically whenever a wider viewport fits the
  // same sections into fewer rows. View's max() would make the reported size a
  // high-water mark, pinning the document open at the largest it has ever
  // been. `flowColumns` measures both axes exactly, so its answer is the whole
  // truth.
  return MakeSize(self->autoresizingMask & ViewAutoresizingWidth
                    ? fits.w : max(size.w, fits.w),
                  fits.h);
}

#pragma mark - ColumnsView

/**
 * @fn ColumnsView *ColumnsView::initWithFrame(ColumnsView *self, const SDL_Rect *frame)
 * @memberof ColumnsView
 */
static ColumnsView *initWithFrame(ColumnsView *self, const SDL_Rect *frame) {

  self = (ColumnsView *) super(StackView, self, initWithFrame, frame);
  if (self) {
    self->minColumnWidth = COLUMNS_VIEW_MIN_COLUMN_WIDTH;
    self->stackView.axis = StackViewAxisHorizontal;
    self->stackView.distribution = StackViewDistributionDefault;
    self->stackView.view.autoresizingMask = ViewAutoresizingContain | ViewAutoresizingWidth;
  }

  return self;
}

/**
 * @fn int32_t ColumnsView_WidthForColumn(ColumnsView *, int32_t, const View *)
 */
int32_t ColumnsView_WidthForColumn(ColumnsView *self, int32_t width,
                                   const View *column) {

  assert(self);
  assert(column);

  columns_flow_t flow = { .place = NULL, .query = column };

  (void) flowColumns(self, width, &flow);

  return flow.queryWidth;
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ViewInterface *) clazz->interface)->applyStyle = applyStyle;
  ((ViewInterface *) clazz->interface)->init = init;
  ((ViewInterface *) clazz->interface)->layoutSubviews = layoutSubviews;
  ((ViewInterface *) clazz->interface)->sizeThatContains = sizeThatContains;
  ((ViewInterface *) clazz->interface)->sizeThatFits = sizeThatFits;

  ((ColumnsViewInterface *) clazz->interface)->initWithFrame = initWithFrame;
}

/**
 * @fn Class *ColumnsView::_ColumnsView(void)
 * @memberof ColumnsView
 */
Class *_ColumnsView(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "ColumnsView",
      .superclass = _StackView(),
      .instanceSize = sizeof(ColumnsView),
      .interfaceOffset = offsetof(ColumnsView, interface),
      .interfaceSize = sizeof(ColumnsViewInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
