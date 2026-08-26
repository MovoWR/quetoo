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
#include "MainViewController.h"
#include "QuickSettingsHostView.h"
#include "SpeedGridView.h"
#include "cg_module_compat.h"
#include "cg_race_physics.h"
#include "cg_race_weapon_tuning.h"
#include "cg_score.h"
#include "race_physics.h"

#include <ObjectivelyMVC/CollectionView.h>
#include <ObjectivelyMVC/PageView.h>
#include <ObjectivelyMVC/Panel.h>
#include <ObjectivelyMVC/TableView.h>
#include <ObjectivelyMVC/Text.h>
#include <ObjectivelyMVC/TextView.h>

#define _Class _MainView

#pragma mark - View

/**
 * @brief Refreshes Race-owned live presentation before the visible native UI
 * is traversed, using the stock ObjectivelyMVC render callback while UI owns
 * input.
 */
static void render(View *self, Renderer *renderer) {

  if (*cgi.state == CL_ACTIVE && cgi.GetKeyDest() == KEY_UI && cgi.client) {
    Cg_Module_UpdateUi(&cgi.client->frame.ps);
  }

  super(View, self, render, renderer);
}

enum {
  MAIN_TOP_BAR_HEIGHT = 58,
  MAIN_BOTTOM_BAR_HEIGHT = 58,
  MAIN_WINDOW_HORIZONTAL_INSET_MIN = 16,
  MAIN_WINDOW_HORIZONTAL_INSET_MAX = 56,
  MAIN_WINDOW_HEADER_HEIGHT = 150,
  MAIN_TUNING_WARNING_HEIGHT = 30,
  MAIN_WINDOW_CONTENT_INSET = 0,
  MAIN_WINDOW_MAX_WIDTH = 2240,
  MAIN_WINDOW_MAX_HEIGHT = 1400,
  MAIN_BRAND_WIDTH = 132,
  MAIN_TOP_ACTIONS_WIDTH = 196,
  /* Disconnect and Quit, plus the staged routes' Revert and Apply. */
  MAIN_BOTTOM_ACTIONS_WIDTH = 380,
  MAIN_COMMIT_STATUS_WIDTH = 260,
  MAIN_SHELL_GAP = 12,
  MAIN_HEADER_GAP_MIN = 12,
  MAIN_HEADER_GAP_MAX = 40,
  MAIN_VOTE_HEIGHT = 86,
  /* Fixed desktop widths at or above this value are treated as preferred
     maxima once they would overflow the live route viewport. */
  MAIN_RESPONSIVE_WIDTH_PIN_MIN = 600,
  /* The rail a route document reserves for its own scroller, matching
     `#main ScrollBar { width: 12 }` in MainView.css. A ScrollBar is drawn over
     its ScrollView's right edge rather than beside it, so a document sized to
     the whole viewport puts its last 12 points under the rail - and a route
     whose right column ends there hands the rail a second one, drawn just
     inside it, which is the pair of parallel rails the Create server capture
     shows. Reserved unconditionally rather than only while the bar is visible:
     conditional would change the document width every time content crossed the
     scrolling threshold, and re-flow the columns with it. */
  MAIN_PAGE_SCROLL_RAIL = 12,
  MAIN_DRAWER_WIDTH = 380,
  MAIN_KEY_HINT_BREAKPOINT = 1180,
  MAIN_KEY_HINT_WIDTH = 360,
  MAIN_DIALOG_WIDTH = 520,
  MAIN_CLIPPED_BORDER_INSET = 1,
  /* The design's session metrics are separated by clamp(16px, 2.2vw, 40px). */
  MAIN_METRIC_GAP_MIN = 16,
  MAIN_METRIC_GAP_MAX = 40,
  MAIN_MAX_SESSION_METRICS = 8,
  /* The connected-session backdrop is rendered once into a texture of this
     size and stretched to the window, rather than computed per pixel per
     frame. Both of the design's gradients are positioned and sized in
     percentages of the box, so stretching a fixed-aspect source to any window
     is what the CSS itself describes - an ellipse scaled to its box, not a
     circle. 512 x 288 is smooth enough that a 4K upscale shows no banding the
     8-bit destination would not have anyway. */
  MAIN_BACKGROUND_WIDTH = 512,
  MAIN_BACKGROUND_HEIGHT = 288
};

/**
 * @brief The normalised distance of (x, y) from an ellipse centred at
 * (cx, cy) with radii (rx, ry), which is the parameter a CSS radial gradient
 * interpolates its stops along.
 */
static float radialGradientStop(const float x, const float y,
                                const float cx, const float cy,
                                const float rx, const float ry) {
  const float dx = (x - cx) / rx;
  const float dy = (y - cy) / ry;

  return sqrtf(dx * dx + dy * dy);
}

/**
 * @brief Interpolates one channel between two stops.
 */
static float mixChannel(const float from, const float to, const float t) {
  return from + (to - from) * Clampf(t, 0.f, 1.f);
}

/**
 * @brief One stop of a CSS gradient: a straight-alpha colour at a position
 * along the gradient's parameter.
 */
typedef struct {
  float t;
  float rgba[4];
} main_gradient_stop_t;

/**
 * @brief Samples a stop list at `t`, the way a CSS gradient does.
 * @param stops The stops, in ascending `t` order.
 * @param count The number of stops.
 * @param t The gradient parameter.
 * @param rgba Receives the sampled colour, premultiplied by its own alpha.
 * @details Interpolation is premultiplied, which is what the browsers do and
 * what keeps a ramp towards `transparent` the same hue all the way down rather
 * than darkening into black as the alpha falls. Before the first stop and past
 * the last, the terminal stop extends - also per spec.
 */
static void sampleGradient(const main_gradient_stop_t *stops, const size_t count,
                           const float t, float *rgba) {

  assert(count);

  size_t upper = 0;
  while (upper < count && stops[upper].t < t) {
    upper++;
  }

  if (upper == 0 || upper == count) {
    const main_gradient_stop_t *stop = stops + (upper ? count - 1 : 0);
    for (int32_t c = 0; c < 3; c++) {
      rgba[c] = stop->rgba[c] * stop->rgba[3];
    }
    rgba[3] = stop->rgba[3];
    return;
  }

  const main_gradient_stop_t *from = stops + upper - 1, *to = stops + upper;
  const float span = to->t - from->t;
  const float f = span > 0.f ? (t - from->t) / span : 0.f;

  for (int32_t c = 0; c < 3; c++) {
    rgba[c] = mixChannel(from->rgba[c] * from->rgba[3],
                         to->rgba[c] * to->rgba[3], f);
  }
  rgba[3] = mixChannel(from->rgba[3], to->rgba[3], f);
}

/**
 * @brief Renders the design's connected-session content plane.
 * @return A new RGBA surface the caller owns, or `NULL`.
 * @details This is candidate G, "corner-lit room", from the design's
 * `Home - background depth candidates.html`, which supersedes the earlier
 * off-centre red glow. It fills only the plane *between* the chrome bars -
 * the bars themselves are a flat, darker surface, which is the other half of
 * the treatment and lives in `MainView.css`. The design states the plane as
 * two layered backgrounds:
 *
 *   radial-gradient(112% 128% at 100% 0%,
 *     rgba(122,176,214,.26) 0%, rgba(46,89,126,.16) 26%,
 *     rgba(12,28,44,.5) 58%, #040b13 82%),
 *   linear-gradient(#0a1724, #050d16)
 *
 * The ObjectivelyMVC dialect has one fill primitive - a flat
 * `background-color` - so there is no CSS port of a multi-stop, multi-layer
 * gradient. Rendering it properly means rendering it, which is what this does:
 * once, into a texture the ImageView then stretches to the plane's bounds.
 *
 * The light source is off-canvas beyond the top-right corner and falls away
 * towards the lower left, which is where the routes put their content - so
 * text lands in the calmest part of the frame and the empty right side carries
 * the interest. No brand red anywhere; the whole treatment stays blue.
 */
static SDL_Surface *createActiveBackgroundSurface(void) {

  SDL_Surface *surface = SDL_CreateSurface(MAIN_BACKGROUND_WIDTH,
                                           MAIN_BACKGROUND_HEIGHT,
                                           SDL_PIXELFORMAT_RGBA32);
  if (surface == NULL) {
    Cg_Warn("Failed to create the menu backdrop: %s\n", SDL_GetError());
    return NULL;
  }

  const float w = MAIN_BACKGROUND_WIDTH, h = MAIN_BACKGROUND_HEIGHT;

  // linear-gradient(#0a1724, #050d16), the plane's opaque base.
  const float baseTop[] = { 0x0a, 0x17, 0x24 };
  const float baseBottom[] = { 0x05, 0x0d, 0x16 };

  // radial-gradient(112% 128% at 100% 0%, ...), the corner light over it. The
  // last stop is opaque, so past 82% the base plays no part at all.
  const main_gradient_stop_t light[] = {
    { 0.00f, { 122.f, 176.f, 214.f, 0.26f } },
    { 0.26f, {  46.f,  89.f, 126.f, 0.16f } },
    { 0.58f, {  12.f,  28.f,  44.f, 0.50f } },
    { 0.82f, {   4.f,  11.f,  19.f, 1.00f } },
  };

  const float lightCx = 1.00f * w, lightCy = 0.00f * h;
  const float lightRx = 1.12f * w, lightRy = 1.28f * h;

  uint8_t *pixels = surface->pixels;

  for (int32_t y = 0; y < MAIN_BACKGROUND_HEIGHT; y++) {
    for (int32_t x = 0; x < MAIN_BACKGROUND_WIDTH; x++) {

      // Sample at the pixel centre, so the corners of the surface land where
      // the gradient's 0% and 100% actually are.
      const float px = x + 0.5f, py = y + 0.5f;

      float color[3];
      for (int32_t c = 0; c < 3; c++) {
        color[c] = mixChannel(baseTop[c], baseBottom[c], py / h);
      }

      float rgba[4];
      sampleGradient(light, lengthof(light),
                     radialGradientStop(px, py, lightCx, lightCy,
                                        lightRx, lightRy), rgba);

      uint8_t *pixel = pixels + y * surface->pitch + x * 4;
      for (int32_t c = 0; c < 3; c++) {
        // Source-over onto an opaque backdrop: the light is already
        // premultiplied, so this is one multiply-add per channel.
        pixel[c] = (uint8_t) Clampf(rgba[c] + color[c] * (1.f - rgba[3]) + 0.5f,
                                    0.f, 255.f);
      }
      pixel[3] = 0xff;
    }
  }

  return surface;
}

/**
 * @brief Renders the hairline the design draws along the top of the footer.
 * @return A new RGBA surface the caller owns, or `NULL`.
 * @details `linear-gradient(90deg, transparent, rgba(115,199,242,.16),
 * transparent)` - brightest at the centre, gone at both ends, so the seam
 * reads as a lit edge rather than a rule drawn across the window. One pixel
 * tall and stretched only horizontally, which is why it is its own texture
 * instead of the bottom row of the plane's: a row of a 288-tall source blurs
 * across three or four pixels once the plane is scaled to a real window.
 *
 * Premultiplied interpolation towards `transparent` keeps the hue constant and
 * ramps the alpha alone, so the whole strip is one colour and a triangular
 * alpha.
 */
static SDL_Surface *createFooterHairlineSurface(void) {

  SDL_Surface *surface = SDL_CreateSurface(MAIN_BACKGROUND_WIDTH, 1,
                                           SDL_PIXELFORMAT_RGBA32);
  if (surface == NULL) {
    Cg_Warn("Failed to create the menu footer hairline: %s\n", SDL_GetError());
    return NULL;
  }

  uint8_t *pixels = surface->pixels;

  for (int32_t x = 0; x < MAIN_BACKGROUND_WIDTH; x++) {

    const float t = (x + 0.5f) / MAIN_BACKGROUND_WIDTH;
    const float alpha = 0.16f * (1.f - fabsf(t * 2.f - 1.f));

    uint8_t *pixel = pixels + x * 4;
    pixel[0] = 0x73;
    pixel[1] = 0xc7;
    pixel[2] = 0xf2;
    pixel[3] = (uint8_t) Clampf(alpha * 255.f + 0.5f, 0.f, 255.f);
  }

  return surface;
}

static void cacheMenuFont(void) {
  static Once once;
  do_once(&once, {
    Resource *resource = $$(Resource, resourceWithName,
      "ui/fonts/Coda-Regular.ttf");
    assert(resource);
    $$(Font, cacheFont, resource->data, "Coda");
    release(resource);
  });
}

/**
 * @brief Returns the native scrolling surface owned by a View or one of its ancestors.
 */
static ScrollView *scrollViewForView(View *view) {

  while (view) {
    if ($((Object *) view, isKindOfClass, _ScrollView())) {
      return (ScrollView *) view;
    }
    if ($((Object *) view, isKindOfClass, _TableView())) {
      return ((TableView *) view)->scrollView;
    }
    if ($((Object *) view, isKindOfClass, _CollectionView())) {
      return ((CollectionView *) view)->scrollView;
    }

    view = view->superview;
  }

  return NULL;
}

/**
 * @brief Applies document-style keyboard scrolling to a ScrollView.
 */
static bool scrollWithKey(ScrollView *scrollView, SDL_Keycode key) {

  if (scrollView == NULL || scrollView->contentView == NULL) {
    return false;
  }

  const SDL_Rect bounds = $((View *) scrollView, bounds);
  const SDL_Size contentSize = $(scrollView->contentView, size);
  if (contentSize.h <= bounds.h) {
    return false;
  }

  SDL_Point offset = scrollView->contentOffset;
  const int32_t page = max(48, bounds.h - 48);

  switch (key) {
    case SDLK_PAGEUP:
      offset.y += page;
      break;
    case SDLK_PAGEDOWN:
      offset.y -= page;
      break;
    case SDLK_HOME:
      offset.y = 0;
      break;
    case SDLK_END:
      offset.y = -(contentSize.h - bounds.h);
      break;
    default:
      return false;
  }

  $(scrollView, scrollToOffset, &offset);
  return true;
}

/**
 * @brief The container class names authored as a single row of route columns.
 */
static const char *columnsClassNames[] = {
  "esc-columns", "columns", "controlsColumns", "settingsColumns", "adminColumns",
  "joinToolbar", "joinColumns", "createColumns", "playerColumns", "mapContent",
  "voteWorkspace"
};

/**
 * @brief ident enumerator for copying a class name onto the promoted container.
 */
static void addClassName_enumerate(const Set *set, ident obj, ident data) {
  $((View *) data, addClassName, ((String *) obj)->chars);
}

/**
 * @brief Replaces a column container with a ColumnsView, which wraps its columns
 * onto further rows instead of overflowing a viewport that cannot hold them all.
 * @details The columns themselves are moved across in order, so identifiers,
 * bindings, style selectors, and focus order are all preserved.
 */
static void promoteColumnsView(View *view) {

  View *superview = view->superview;
  if (superview == NULL || $((Object *) view, isKindOfClass, _ColumnsView())) {
    return;
  }

  ColumnsView *columnsView = $(alloc(ColumnsView), initWithFrame, &view->frame);
  assert(columnsView);

  View *promoted = (View *) columnsView;

  if (view->identifier) {
    promoted->identifier = q_strdup(view->identifier);
    assert(promoted->identifier);
  }

  $((Set *) view->classNames, enumerateObjects, addClassName_enumerate, promoted);

  promoted->alignment = view->alignment;
  promoted->autoresizingMask = view->autoresizingMask;
  promoted->clipsSubviews = view->clipsSubviews;
  promoted->hidden = view->hidden;
  promoted->maxSize = view->maxSize;
  promoted->minSize = view->minSize;
  promoted->padding = view->padding;

  if ($((Object *) view, isKindOfClass, _StackView())) {
    columnsView->stackView.spacing = ((StackView *) view)->spacing;
  }

  while (((Array *) view->subviews)->count) {
    View *column = ((Array *) view->subviews)->elements[0];
    retain(column);
    $(view, removeSubview, column);
    $(promoted, addSubview, column);
    release(column);
  }

  $(superview, replaceSubview, view, promoted);
  release(columnsView);
}

/**
 * @brief Promotes every column container in this subtree, deepest first.
 * @remarks Promotion replaces a subview in place, so live indices stay stable.
 */
static void prepareColumnsViews(View *view) {

  const Array *subviews = (Array *) view->subviews;
  for (size_t i = 0; i < subviews->count; i++) {
    prepareColumnsViews(subviews->elements[i]);
  }

  for (size_t i = 0; i < lengthof(columnsClassNames); i++) {
    if ($(view, hasClassName, columnsClassNames[i])) {
      promoteColumnsView(view);
      break;
    }
  }
}

/**
 * @brief Marks native lists and installs explicitly requested vertical page scrolling.
 * @return True when this subtree already owns native list scrolling.
 */
static bool preparePageScrollViews(View *view) {

  bool containsNativeList = false;

  if ($((Object *) view, isKindOfClass, _TableView())) {
    TableView *tableView = (TableView *) view;
    $(view, addClassName, "esc-list-scroll");
    $((View *) tableView->scrollView, addClassName, "esc-list-scroll");
    containsNativeList = true;
  } else if ($((Object *) view, isKindOfClass, _CollectionView())) {
    CollectionView *collectionView = (CollectionView *) view;
    $(view, addClassName, "esc-list-scroll");
    $((View *) collectionView->scrollView, addClassName, "esc-list-scroll");
    containsNativeList = true;
  }

  const Array *subviews = (Array *) view->subviews;
  for (size_t i = 0; i < subviews->count; i++) {
    containsNativeList |= preparePageScrollViews(subviews->elements[i]);
  }

  if ($(view, hasClassName, "esc-page-scroll-content") &&
      view->superview && !$((Object *) view->superview, isKindOfClass, _ScrollView())) {
    MainView_WrapScrollContent(view, "esc-page-scroll");
  }

  return containsNativeList;
}

/**
 * @brief Returns a non-empty configstring or the provided fallback.
 */
static const char *configStringOr(int32_t index, const char *fallback) {
  const char *value = cgi.ConfigString(index);
  return value && *value ? value : fallback;
}

/**
 * @brief Fits a prefixed value to a label and adds an ellipsis when needed.
 */
static void setEllipsizedLabelText(Label *label, const char *prefix,
                                   const char *value, int32_t availableWidth) {

  char candidate[MAX_STRING_CHARS];
  int32_t textWidth = 0;

  snprintf(candidate, sizeof(candidate), "%s%s", prefix, value);
  if (availableWidth <= 0 || label->text->font == NULL) {
    $(label->text, setText, candidate);
    return;
  }

  $(label->text->font, sizeCharacters, candidate, &textWidth, NULL);
  if (textWidth <= availableWidth) {
    $(label->text, setText, candidate);
    return;
  }

  char truncated[MAX_STRING_CHARS];
  for (size_t bytes = strlen(value); bytes > 0; bytes--) {
    SDL_utf8strlcpy(truncated, value, min(bytes + 1, sizeof(truncated)));
    snprintf(candidate, sizeof(candidate), "%s%s...", prefix, truncated);
    $(label->text->font, sizeCharacters, candidate, &textWidth, NULL);
    if (textWidth <= availableWidth) {
      $(label->text, setText, candidate);
      return;
    }
  }

  $(label->text, setText, prefix);
}

/**
 * @brief Returns the part of the route viewport available at this nested View.
 */
static SDL_Size pageScrollViewport(const View *view, const View *page,
                                   int32_t viewportWidth, int32_t viewportHeight) {

  const SDL_Rect pageFrame = $(page, renderFrame);
  const SDL_Rect viewFrame = $(view, renderFrame);
  int32_t rightInset = 0;
  int32_t bottomInset = 0;

  const View *child = view;
  while (child != page && child->superview) {
    const View *parent = child->superview;
    if (child->alignment != ViewAlignmentInternal) {
      rightInset += parent->padding.right;
      bottomInset += parent->padding.bottom;
    }
    child = parent;
  }

  return MakeSize(max(0, viewportWidth - max(0, viewFrame.x - pageFrame.x) - rightInset),
                  max(0, viewportHeight - max(0, viewFrame.y - pageFrame.y) - bottomInset));
}

/**
 * @brief Collapses width-flexible views onto the width now available to them.
 * @details ObjectivelyMVC only ever grows a container to contain its subviews,
 * and a StackView measures a non-contained subview at whatever width it already
 * has. A route laid out once at a wide viewport therefore stays wide when the
 * viewport shrinks. Resetting the flexible widths top-down before the layout
 * pass lets each container measure itself against the viewport it actually has;
 * anything that genuinely needs more room still grows back during layout.
 */
static void collapsePageWidths(View *view, int32_t width, bool strict) {

  const int32_t available = max(0, width - view->padding.left - view->padding.right);

  // A ColumnsView is a row whose shares are not the StackView's to guess: the
  // flow decides them, from the widths the columns are composed at and from how
  // many of them the viewport can hold. Asking it, rather than reasoning about
  // it a second time here, is what keeps the split this pass measures against
  // the same split the layout pass draws.
  const bool columnsFlow = $((Object *) view, isKindOfClass, _ColumnsView());

  // A row shares its width, so pre-apply the distribution the StackView is
  // about to apply anyway. Measuring the row before that happens is what lets
  // the old, wider child widths hold the row open.
  int32_t rowWidth = available, rowRequested = 0;
  size_t rowCount = 0;

  if (!columnsFlow && $((Object *) view, isKindOfClass, _StackView())) {
    const StackView *stackView = (StackView *) view;
    if (stackView->axis == StackViewAxisHorizontal) {
      Array *visible = $(view, visibleSubviews);
      rowCount = visible->count;
      for (size_t i = 0; i < visible->count; i++) {
        rowRequested += ((View *) visible->elements[i])->frame.w;
      }
      if (rowCount) {
        rowWidth = max(0, available - (int32_t) (rowCount - 1) * stackView->spacing);
      }
      release(visible);

      if (rowCount && stackView->distribution == StackViewDistributionFillEqually) {
        rowWidth /= (int32_t) rowCount;
      } else {
        rowWidth = available;
      }
    }
  }

  const Array *subviews = (Array *) view->subviews;
  for (size_t i = 0; i < subviews->count; i++) {

    View *subview = subviews->elements[i];
    int32_t subviewWidth = subview->frame.w;
    int32_t subviewLimit = min(rowWidth, available);

    if (columnsFlow) {
      // The flow's own bounds, not this pass' `available`: `layoutSubviews`
      // places the columns from `bounds.w`, and a column measured here against
      // anything else is a second answer to a question that may only have one.
      // Two answers is exactly what leaves a column with a caption row at one
      // width and the panel under it at another.
      const SDL_Rect flowBounds = $(view, bounds);
      const int32_t flowed = ColumnsView_WidthForColumn((ColumnsView *) view,
                                                        flowBounds.w, subview);
      if (flowed > 0) {
        subviewLimit = min(flowed, available);
      }
    }

    if (rowCount && rowRequested > available && rowWidth == available) {
      // Proportional rows keep their relative weights, as `fill` would.
      subviewLimit = min(subviewLimit,
                         (int32_t) ((int64_t) subviewWidth * available / rowRequested));
    }

    const bool tabPage = $(subview, hasClassName, "tabPageView");
    bool responsivePin = $(subview, hasClassName, "esc-responsive-width-pin");

    // Race's desktop resources contain exact structural widths for their
    // preferred large layout. Once one would overflow, remember it as a finite
    // maximum and release its minimum. The marker survives stylesheet passes,
    // so shrinking and then growing cannot ratchet the page permanently narrow.
    if (!responsivePin &&
        subview->minSize.w == subview->maxSize.w &&
        subview->maxSize.w >= MAIN_RESPONSIVE_WIDTH_PIN_MIN &&
        subview->maxSize.w > subviewLimit) {
      $(subview, addClassName, "esc-responsive-width-pin");
      responsivePin = true;
    }

    if (tabPage) {
      // common.css authors `.tabPageView` at 1440. Race owns a fluid route
      // viewport, so reset that inherited frame explicitly on every pass.
      subview->autoresizingMask |= ViewAutoresizingWidth | ViewAutoresizingContain;
      subview->minSize.w = 0;
      subview->maxSize.w = INT32_MAX;
      subviewWidth = subviewLimit;
    } else if (responsivePin) {
      subview->minSize.w = 0;
      subviewWidth = min(subview->maxSize.w, subviewLimit);
    } else if (subview->autoresizingMask & (ViewAutoresizingWidth |
                                            ViewAutoresizingContain |
                                            ViewAutoresizingFit)) {
      subviewWidth = clamp(min(subviewWidth, subviewLimit),
                           subview->minSize.w, subview->maxSize.w);
    }

    // One exception to the clamp below, and it is about what a width *means* on
    // that type. A `Text` that does not wrap carries its whole string in one
    // texture and draws it from its frame's origin, so narrowing the frame
    // moves a right-aligned line further right and it spills past the edge
    // instead of fitting inside it. A `Text` that does wrap is the opposite -
    // its frame is the measure it wraps to, so narrowing is the entire fix.
    const bool unwrappedText = $((Object *) subview, isKindOfClass, _Text()) &&
                               !((Text *) subview)->lineWrap;

    // Inside a column, the column's width is not a preference - it is the whole
    // of the space that exists. Each of the three branches above declines for a
    // reason (no flexible mask, a minimum that is not also the maximum, a fixed
    // width too small to read as a desktop structure), and every decline leaves
    // a child holding the width the design composed it at rather than the width
    // its column actually got. That is the residue these routes kept showing: a
    // caption row still 701 wide inside a 660 column, right-aligned, its last
    // glyph under the window edge. Nothing flexible here may be wider than what
    // contains it.
    //
    // The exception is the one case that residue never described. A view the
    // design composed at a fixed width below the pin threshold is
    // not a structure that gives way - it is a slider, a numeric field, a unit
    // cell, and the row holding it already fits the column it was authored
    // for. Releasing its floor here is what let a transient limit collapse it:
    // a `contain` parent measures zero before its children are laid out, so on
    // the pass that runs first every fixed-width cell under one looks too wide
    // for a limit that is not real yet. The floor came back on the next
    // restyle and went again on the next collapse, which is the flicker the
    // Weapons rows showed under a live drag. Leave those alone. Anything at or
    // above the threshold is a desktop structure and still gives way exactly
    // as before, so the caption-wider-than-its-column case this guards is
    // unaffected.
    const bool responsiveStructure =
      subview->maxSize.w >= MAIN_RESPONSIVE_WIDTH_PIN_MIN;

    if (strict && !unwrappedText && subviewWidth > subviewLimit &&
        (subview->minSize.w <= subviewLimit || responsiveStructure)) {
      if (subview->minSize.w > subviewLimit) {
        if (!responsivePin) {
          $(subview, addClassName, "esc-responsive-width-pin");
        }
        subview->minSize.w = 0;
      }
      subviewWidth = subviewLimit;
    }

    if (subview->frame.w != subviewWidth) {
      subview->frame.w = subviewWidth;
      subview->needsLayout = true;
      view->needsLayout = true;
    }

    // A panel that holds a picture has to keep the picture's shape, and the
    // only pass that knows its final width is this one - a stylesheet can state
    // a height but not a ratio, and ImageView blits straight to its frame, so a
    // height that does not follow the width stretches every image in the panel.
    // Pinned through min/max because a bare `height` in the sheet is re-applied
    // on every restyle and `View::resize` clamps against exactly these two.
    if (subviewWidth > 0 && $(subview, hasClassName, "esc-aspect-16-9")) {
      const int32_t aspectHeight = subviewWidth * 9 / 16;
      if (subview->maxSize.h != aspectHeight) {
        subview->frame.h = subview->minSize.h = subview->maxSize.h = aspectHeight;
        subview->needsLayout = true;
        view->needsLayout = true;
      }
    }

    // The widget is measured against its column; what is inside it is not. A
    // TableView lays its columns out across a document that is meant to be
    // wider than the viewport it shows through, and a CollectionView's grid is
    // the same shape - squeezing either one's contents to the column would
    // collapse the very geometry the list scrolls to reveal.
    //
    // Not merely non-strict - not at all. A TableRowView is a `fill` stack, so
    // every layout pass rescales its cells to span the row; recursing in here
    // still ran the flexible-mask clamp over those same cells and wrote them a
    // second, different answer whenever this pass's limit was transient. Two
    // authorities alternating over one width was the Home roster's flicker -
    // the status cell snapping between the row's edge and its content width.
    // The interior already tracks the widget through the scroll view's own
    // autoresizing, so the collapse pass has nothing to add below this node.
    const bool nativeList = $((Object *) subview, isKindOfClass, _TableView()) ||
                            $((Object *) subview, isKindOfClass, _CollectionView());

    if (!nativeList) {
      collapsePageWidths(subview, min(subviewWidth, available), strict);
    }
  }
}

/**
 * @brief Collapses the width-flexible views of a subtree onto `width`.
 */
void MainView_CollapseWidths(View *view, int32_t width) {
  collapsePageWidths(view, width, true);
}

/**
 * @brief Releases the height of every view that derives it from its content, so
 * that the layout pass can measure it downward as well as upward.
 * @details `View::sizeThatContains` is `max(frame, sizeThatFits)`, so a view
 * laid out by `contain` or `fit` is a high-water mark: it grows to the tallest
 * content it has ever held and never comes back down. That is invisible while a
 * route only ever gets taller, and wrong the moment one gets shorter - a tab
 * strip switched from a long page to a short one, a filtered list, a table that
 * reloaded with fewer rows. What is left is dead space under the content, and a
 * document still tall enough to make the shell's page ScrollView offer a scroll
 * into nothing.
 *
 * Resetting to `minSize.h` rather than to zero keeps any floor the stylesheet
 * states; everything above that floor is re-earned from the subviews during the
 * pass that follows. Widths get the same treatment for the same reason a few
 * lines up, in `collapsePageWidths` - this is that function's other half.
 */
static void collapsePageHeights(View *view) {

  if (view->autoresizingMask & (ViewAutoresizingContain | ViewAutoresizingFit)) {
    if (view->frame.h != view->minSize.h) {
      view->frame.h = view->minSize.h;
      view->needsLayout = true;
      if (view->superview) {
        view->superview->needsLayout = true;
      }
    }
  }

  const Array *subviews = (Array *) view->subviews;
  for (size_t i = 0; i < subviews->count; i++) {
    collapsePageHeights(subviews->elements[i]);
  }
}

/**
 * @brief Releases the height caps on a wrapping column flow and its document ancestors.
 * @return True when this subtree contains a ColumnsView.
 * @details Desktop route resources frequently state an exact canvas height. Once
 * columns stack, that height is no longer the content height; keeping it as a
 * maximum clips the lower rows before the page ScrollView can expose them.
 */
static bool releaseResponsiveDocumentHeights(View *view) {

  bool containsColumns = $((Object *) view, isKindOfClass, _ColumnsView());

  const Array *subviews = (Array *) view->subviews;
  for (size_t i = 0; i < subviews->count; i++) {
    containsColumns |= releaseResponsiveDocumentHeights(subviews->elements[i]);
  }

  if (containsColumns) {
    view->autoresizingMask |= ViewAutoresizingContain;
    view->minSize.h = 0;
    view->maxSize.h = INT32_MAX;
    view->needsLayout = true;
  }

  return containsColumns;
}

/**
 * @brief Sizes every explicit document ScrollView without changing its parent structure.
 */
static void sizePageScrollViews(View *view, View *page,
                                int32_t viewportWidth, int32_t viewportHeight) {

  if ($((Object *) view, isKindOfClass, _ScrollView()) &&
      $(view, hasClassName, "esc-page-scroll")) {
    ScrollView *scrollView = (ScrollView *) view;
    const SDL_Size available = pageScrollViewport(view, page, viewportWidth, viewportHeight);
    view->frame.w = available.w;
    view->frame.h = available.h;
    view->autoresizingMask = ViewAutoresizingWidth | ViewAutoresizingHeight;
    view->minSize = available;
    view->maxSize = available;

    if (scrollView->contentView) {
      View *content = scrollView->contentView;
      const int32_t contentWidth = max(0, available.w - MAIN_PAGE_SCROLL_RAIL);

      content->autoresizingMask |= ViewAutoresizingWidth | ViewAutoresizingContain;
      content->frame.w = contentWidth;
      content->minSize.w = 0;
      content->maxSize.w = INT32_MAX;
      content->minSize.h = 0;
      content->maxSize.h = INT32_MAX;

      releaseResponsiveDocumentHeights(content);
      collapsePageHeights(content);

      // Lay the document out at its final width first, so a wrapped ColumnsView
      // reports the height it actually occupies at this viewport. The second
      // pass settles the heights the first pass' re-flow invalidated.
      collapsePageWidths(content, contentWidth, false);
      content->needsLayout = true;
      $(content, layoutIfNeeded);
      $(content, layoutIfNeeded);

      // sizeThatFits, not sizeThatContains: the latter is max()'d against
      // content's own frame, which this line assigned last pass, so the height
      // could only ever grow. A route that gets shorter - a smaller tab, or a
      // wider viewport that costs the flow a row - would keep the taller
      // document and leave dead space below it to scroll into.
      const SDL_Size contained = $(content, sizeThatFits);
      content->frame.h = max(available.h, contained.h);
      content->needsLayout = true;
      view->needsLayout = true;
      $(view, layoutIfNeeded);
    }
  }

  const Array *subviews = (Array *) view->subviews;
  for (size_t i = 0; i < subviews->count; i++) {
    sizePageScrollViews(subviews->elements[i], page, viewportWidth, viewportHeight);
  }
}

/**
 * @brief Returns the active route View from the navigation host.
 */
static View *navigationPage(const MainView *self) {

  const Array *navigationViews = (Array *) self->contentView->subviews;
  for (size_t i = 0; i < navigationViews->count; i++) {
    const View *navigationView = navigationViews->elements[i];
    const Array *pages = (Array *) navigationView->subviews;
    for (size_t j = 0; j < pages->count; j++) {
      View *page = pages->elements[j];
      if (!page->hidden) {
        return page;
      }
    }
  }

  return NULL;
}

/**
 * @brief Captures the route's native ObjectivelyMVC size before installing
 * viewport-sized overflow wrappers.
 */
static SDL_Size pageSize(MainView *self) {

  View *page = navigationPage(self);
  if (page == NULL) {
    return MakeSize(0, 0);
  }

  if (self->sizedPage != page) {

    // Promotion is owned by the navigation boundary - see
    // MainViewController::navigateToViewController - because this gate is not a
    // reliable place to hang structural correctness: `navigationPage` reports
    // the first unhidden child of the navigation host, and a route-level scroll
    // wrapper can remain there while navigation replaces its wrapped content.
    // That stale wrapper can answer here instead of the route the player
    // actually opened. This call remains only as an idempotent
    // safeguard for any page that reaches layout without being navigated to;
    // `promoteColumnsView` returns immediately on an existing ColumnsView, so it
    // costs a walk and never restyles anything.
    prepareColumnsViews(page);
    $(page, layoutIfNeeded);

    if ($((Object *) page, isKindOfClass, _Panel())) {
      Panel *panel = (Panel *) page;
      const SDL_Size contentSize = $((View *) panel->contentView, sizeThatContains);
      const SDL_Size panelSize = $(page, size);
      self->pageSize = MakeSize(max(contentSize.w, panelSize.w),
                                max(contentSize.h, panelSize.h));
    } else {
      self->pageSize = $(page, sizeThatContains);
    }

    preparePageScrollViews(page);
    self->sizedPage = navigationPage(self);
  }

  return self->pageSize;
}

/**
 * @brief Sizes the navigation host to the viewport while leaving ordinary
 * routes on one stable top-aligned canvas.
 */
static void sizeNavigation(MainView *self, int32_t width, int32_t height) {

  const Array *navigationViews = (Array *) self->contentView->subviews;
  for (size_t i = 0; i < navigationViews->count; i++) {
    View *navigationView = navigationViews->elements[i];
    navigationView->alignment = ViewAlignmentTopLeft;
    navigationView->frame = MakeRect(0, 0, width, height);
    navigationView->minSize = MakeSize(width, height);
    navigationView->maxSize = MakeSize(width, height);
    navigationView->needsLayout = true;

    const Array *pages = (Array *) navigationView->subviews;
    for (size_t j = 0; j < pages->count; j++) {
      View *page = pages->elements[j];

      page->alignment = ViewAlignmentTopLeft;
      page->frame = MakeRect(0, 0, width, height);
      page->minSize = MakeSize(width, height);
      page->maxSize = MakeSize(width, height);

      page->needsLayout = true;
      collapsePageHeights(page);
      collapsePageWidths(page, width, false);
      $(page, layoutIfNeeded);
      sizePageScrollViews(page, page, width, height);

      // The first pass measured the route's ancestors against a document that
      // had not been clamped to the viewport yet; settle them against it now.
      collapsePageWidths(page, width, false);
      page->needsLayout = true;
      $(page, layoutIfNeeded);
    }
  }
}

/**
 * @brief Clamps existing offsets after viewport changes while preserving valid positions.
 */
static void clampScrollOffsets(View *view) {

  if ($((Object *) view, isKindOfClass, _ScrollView())) {
    ScrollView *scrollView = (ScrollView *) view;
    const SDL_Point offset = scrollView->contentOffset;
    $(scrollView, scrollToOffset, &offset);
  }

  const Array *subviews = (Array *) view->subviews;
  for (size_t i = 0; i < subviews->count; i++) {
    clampScrollOffsets(subviews->elements[i]);
  }
}

/**
 * @brief Handles Page Up / Down and Home / End after focused controls decline them.
 */
static void respondToEvent(View *self, const SDL_Event *event) {

  if (event->type == SDL_EVENT_KEY_DOWN && self->window) {
    View *keyResponder = SDL_GetPointerProperty(SDL_GetWindowProperties(self->window),
                                                "keyResponder", NULL);
    if (scrollWithKey(scrollViewForView(keyResponder), event->key.key)) {
      return;
    }
  }

  super(View, self, respondToEvent, event);
}

static void didDismissQuickSettings(ident self) {
  (void) self;
  MainViewController_CloseQuickSettings();
}

#pragma mark - View

/**
 * @see View::layoutSubviews(View *)
 */
static void layoutSubviews(View *self) {

  super(View, self, layoutSubviews);

  MainView *this = (MainView *) self;
  const SDL_Rect bounds = $(self, bounds);
  const bool isActive = *cgi.state == CL_ACTIVE;
  const int32_t bottomPaintHeight = MAIN_BOTTOM_BAR_HEIGHT + 1;
  const int32_t horizontalInset = clamp(bounds.w / 40,
    MAIN_WINDOW_HORIZONTAL_INSET_MIN, MAIN_WINDOW_HORIZONTAL_INSET_MAX);

  // The design's plane is the band *between* the chrome bars, not the window:
  // the bars are a darker surface of their own, and that value separation is
  // what stops the whole window reading as one flat sheet. ImageView::render
  // stretches its texture to the render frame, so the plane is this one
  // assignment - the gradient is stated in percentages of its box, and
  // stretching a fixed-aspect source is exactly the ellipse scaling the CSS
  // describes.
  const int32_t planeTop = min(MAIN_TOP_BAR_HEIGHT, bounds.h);
  const int32_t planeBottom = max(planeTop, bounds.h - MAIN_BOTTOM_BAR_HEIGHT);

  this->activeBackground->frame = MakeRect(0, planeTop, bounds.w,
                                           planeBottom - planeTop);

  // Same frame as the gradient. The grid derives its whole geometry from
  // its own frame, so this is all it needs at any window size.
  this->speedGrid->frame = this->activeBackground->frame;

  // The plane's last row, so the lit seam sits directly above the footer's own
  // hairline rather than under it.
  this->footerHairline->frame = MakeRect(0, max(0, planeBottom - 1), bounds.w, 1);

  this->overlayShade->frame = bounds;
  this->topBar->frame = MakeRect(0, 0, bounds.w, MAIN_TOP_BAR_HEIGHT);
  this->bottomBar->frame = MakeRect(0, max(0, bounds.h - MAIN_BOTTOM_BAR_HEIGHT),
                                    bounds.w, bottomPaintHeight);

  if (!isActive) {
    // Keep the version label above the bottom action rail; at the supported
    // 1024-wide floor it otherwise sits under the same rail as Quit.
    this->version->view.frame.y = max(MAIN_TOP_BAR_HEIGHT,
      bounds.h - MAIN_BOTTOM_BAR_HEIGHT - this->version->view.frame.h);
  }

  this->menuHeading->view.hidden = true;
  this->brand->view.frame = MakeRect(horizontalInset, 0, MAIN_BRAND_WIDTH,
                                     MAIN_TOP_BAR_HEIGHT);
  this->brand->view.minSize = MakeSize(MAIN_BRAND_WIDTH, MAIN_TOP_BAR_HEIGHT);
  this->brand->view.maxSize = MakeSize(MAIN_BRAND_WIDTH, MAIN_TOP_BAR_HEIGHT);
  this->brand->view.needsLayout = true;
  $((View *) this->brand, layoutIfNeeded);

  const int32_t topActionsWidth = isActive ? MAIN_TOP_ACTIONS_WIDTH : 0;
  this->topActions->view.frame = MakeRect(
    max(0, bounds.w - horizontalInset - topActionsWidth), 0,
                                          topActionsWidth, MAIN_TOP_BAR_HEIGHT);
  this->topActions->view.minSize = MakeSize(topActionsWidth, MAIN_TOP_BAR_HEIGHT);
  this->topActions->view.maxSize = MakeSize(topActionsWidth, MAIN_TOP_BAR_HEIGHT);

  const int32_t headerGap = clamp(bounds.w / 50, MAIN_HEADER_GAP_MIN,
                                  MAIN_HEADER_GAP_MAX);
  const int32_t routesX = horizontalInset + MAIN_BRAND_WIDTH + headerGap;
  const int32_t routesWidth = max(0, bounds.w - horizontalInset - routesX -
                                      topActionsWidth);
  this->primaryMenu->view.frame = MakeRect(routesX, 0, routesWidth, MAIN_TOP_BAR_HEIGHT);
  this->primaryMenu->view.minSize = MakeSize(routesWidth, MAIN_TOP_BAR_HEIGHT);
  this->primaryMenu->view.maxSize = MakeSize(routesWidth, MAIN_TOP_BAR_HEIGHT);
  this->primaryMenu->view.needsLayout = true;
  $((View *) this->primaryMenu, layoutIfNeeded);
  this->topActions->view.needsLayout = true;
  $((View *) this->topActions, layoutIfNeeded);

  const int32_t stageHeight = max(0, bounds.h - MAIN_TOP_BAR_HEIGHT - MAIN_BOTTOM_BAR_HEIGHT);
  pageSize(this);
  const int32_t availableWindowWidth = max(0,
    bounds.w - horizontalInset * 2);
  const int32_t availableWindowHeight = stageHeight;
  const int32_t voteHeight = isActive && this->activeVoteHost &&
    !this->activeVoteHost->hidden ? MAIN_VOTE_HEIGHT : 0;
  const int32_t windowWidth = min(availableWindowWidth, MAIN_WINDOW_MAX_WIDTH);
  const int32_t windowHeight = min(availableWindowHeight, MAIN_WINDOW_MAX_HEIGHT);
  const int32_t windowX = max(0, (bounds.w - windowWidth) / 2);
  const int32_t windowY = MAIN_TOP_BAR_HEIGHT;
  const bool tuningWarningVisible = isActive && this->weaponTuningWarning &&
    !this->weaponTuningWarning->view.hidden;
  const int32_t tuningWarningHeight = tuningWarningVisible
    ? MAIN_TUNING_WARNING_HEIGHT
    : 0;
  const int32_t windowHeaderHeight = MAIN_WINDOW_HEADER_HEIGHT +
                                     tuningWarningHeight;

  this->menuWindow->frame = MakeRect(windowX, windowY, windowWidth, windowHeight);
  this->windowHeader->frame = MakeRect(0, 0, windowWidth, windowHeaderHeight);
  if (this->weaponTuningWarning) {
    this->weaponTuningWarning->view.frame = tuningWarningVisible
      ? MakeRect(0, 0, windowWidth, tuningWarningHeight)
      : MakeRect(0, 0, 0, 0);
    this->weaponTuningWarning->view.minSize =
      MakeSize(this->weaponTuningWarning->view.frame.w,
               this->weaponTuningWarning->view.frame.h);
    this->weaponTuningWarning->view.maxSize =
      this->weaponTuningWarning->view.minSize;
  }

  const int32_t metricsWidth = isActive ? min(420, max(0, windowWidth / 2)) : 0;
  const int32_t routeSummaryWidth = max(0, windowWidth - metricsWidth);
  this->routeSummary->view.frame = MakeRect(0, tuningWarningHeight,
    routeSummaryWidth, MAIN_WINDOW_HEADER_HEIGHT);
  this->routeSummary->view.minSize = MakeSize(routeSummaryWidth,
    MAIN_WINDOW_HEADER_HEIGHT);
  this->routeSummary->view.maxSize = MakeSize(routeSummaryWidth,
    MAIN_WINDOW_HEADER_HEIGHT);
  this->sessionMetrics->view.hidden = !isActive;
  this->sessionMetrics->view.frame = MakeRect(routeSummaryWidth,
                                              tuningWarningHeight,
                                              metricsWidth, MAIN_WINDOW_HEADER_HEIGHT);
  this->sessionMetrics->view.minSize = MakeSize(metricsWidth, MAIN_WINDOW_HEADER_HEIGHT);
  this->sessionMetrics->view.maxSize = MakeSize(metricsWidth, MAIN_WINDOW_HEADER_HEIGHT);
  // The design packs the metrics against the right edge of the header, each
  // column only as wide as its own widest line, rather than spreading them over
  // a fixed half. Measure the columns first, then place them right-to-left.
  const Array *metricViews = (Array *) this->sessionMetrics->view.subviews;
  if (metricViews->count) {
    const int32_t metricGap = clamp(bounds.w / 46, MAIN_METRIC_GAP_MIN,
                                    MAIN_METRIC_GAP_MAX);
    static const int32_t designMetricWidths[] = { 44, 52, 48 };
    int32_t metricWidths[MAIN_MAX_SESSION_METRICS] = { 0 };
    const size_t metricCount = min(metricViews->count, lengthof(metricWidths));
    int32_t metricsTotal = (int32_t) (metricCount - 1) * metricGap;

    for (size_t i = 0; i < metricCount; i++) {
      metricWidths[i] = i < lengthof(designMetricWidths)
        ? designMetricWidths[i]
        : designMetricWidths[lengthof(designMetricWidths) - 1];
      metricsTotal += metricWidths[i];
    }

    const int32_t packedMetricsWidth = min(metricsWidth, metricsTotal);
    const int32_t packedMetricsX = max(0, windowWidth - packedMetricsWidth);
    this->sessionMetrics->view.frame = MakeRect(packedMetricsX,
      tuningWarningHeight,
      packedMetricsWidth, MAIN_WINDOW_HEADER_HEIGHT);
    this->sessionMetrics->view.minSize = MakeSize(packedMetricsWidth,
      MAIN_WINDOW_HEADER_HEIGHT);
    this->sessionMetrics->view.maxSize = MakeSize(packedMetricsWidth,
      MAIN_WINDOW_HEADER_HEIGHT);
    this->sessionMetrics->spacing = metricGap;
    this->routeSummary->view.frame.w = packedMetricsX;
    this->routeSummary->view.minSize.w = packedMetricsX;
    this->routeSummary->view.maxSize.w = packedMetricsX;

    int32_t metricX = 0;
    for (size_t i = 0; i < metricCount; i++) {
      View *metric = metricViews->elements[i];
      const int32_t width = metricWidths[i];
      metric->frame = MakeRect(metricX, 0, width, MAIN_WINDOW_HEADER_HEIGHT);
      metric->minSize = MakeSize(width, MAIN_WINDOW_HEADER_HEIGHT);
      metric->maxSize = MakeSize(width, MAIN_WINDOW_HEADER_HEIGHT);
      const int32_t labelWidth = max(0, width - metric->padding.left - metric->padding.right);
      const Array *labels = (Array *) metric->subviews;
      for (size_t j = 0; j < labels->count; j++) {
        View *label = labels->elements[j];
        label->frame.w = labelWidth;
        label->minSize.w = labelWidth;
        label->maxSize.w = labelWidth;
        label->needsLayout = true;
        $(label, layoutIfNeeded);
        if ($((Object *) label, isKindOfClass, _Label())) {
          View *text = (View *) ((Label *) label)->text;
          text->frame.x = max(0, labelWidth - text->frame.w);
        }
      }
      metric->needsLayout = true;
      $(metric, layoutIfNeeded);
      metricX += width + metricGap;
    }
  }
  this->routeSummary->view.needsLayout = true;
  $((View *) this->routeSummary, layoutIfNeeded);
  this->sessionMetrics->view.needsLayout = true;
  $((View *) this->sessionMetrics, layoutIfNeeded);

  const int32_t innerX = windowX + MAIN_WINDOW_CONTENT_INSET;
  const int32_t innerWidth = max(0, windowWidth - MAIN_WINDOW_CONTENT_INSET * 2);
  int32_t contentY = windowY + windowHeaderHeight + MAIN_WINDOW_CONTENT_INSET;
  int32_t contentHeight = max(0, windowHeight - windowHeaderHeight -
                                  MAIN_WINDOW_CONTENT_INSET * 2);

  if (this->activeVoteHost) {
    const int32_t hostHeight = voteHeight
      ? voteHeight + MAIN_CLIPPED_BORDER_INSET * 2
      : 0;
    this->activeVoteHost->frame = MakeRect(innerX - MAIN_CLIPPED_BORDER_INSET,
      contentY - MAIN_CLIPPED_BORDER_INSET,
      innerWidth + MAIN_CLIPPED_BORDER_INSET * 2, hostHeight);

    const Array *voteViews = (Array *) this->activeVoteHost->subviews;
    for (size_t i = 0; i < voteViews->count; i++) {
      View *vote = voteViews->elements[i];
      vote->frame = MakeRect(MAIN_CLIPPED_BORDER_INSET, MAIN_CLIPPED_BORDER_INSET,
                             innerWidth, voteHeight);
      vote->minSize = MakeSize(innerWidth, voteHeight);
      vote->maxSize = MakeSize(innerWidth, voteHeight);

      const Array *bannerViews = (Array *) vote->subviews;
      if (bannerViews->count == 3) {
        View *copy = bannerViews->elements[0];
        const View *remaining = bannerViews->elements[1];
        const View *actions = bannerViews->elements[2];
        const StackView *stack = (StackView *) vote;
        const int32_t bannerWidth = max(0, innerWidth - vote->padding.left - vote->padding.right);
        const int32_t copyWidth = max(0, bannerWidth - stack->spacing * 2 -
          remaining->frame.w - actions->frame.w);
        copy->frame.w = copyWidth;
        copy->minSize.w = copyWidth;
        copy->maxSize.w = copyWidth;
      }

      vote->needsLayout = true;
    }
  }

  if (voteHeight) {
    contentY += voteHeight + MAIN_SHELL_GAP;
    contentHeight = max(0, contentHeight - voteHeight - MAIN_SHELL_GAP);
  }

  this->contentView->frame = MakeRect(MAIN_WINDOW_CONTENT_INSET,
                                      contentY - windowY,
                                      innerWidth, contentHeight);
  this->contentView->minSize = MakeSize(innerWidth, contentHeight);
  this->contentView->maxSize = MakeSize(innerWidth, contentHeight);
  sizeNavigation(this, innerWidth, contentHeight);

  const int32_t bottomActionsWidth = isActive ? MAIN_BOTTOM_ACTIONS_WIDTH : 60;

  // `.m-keys`. The design drops the keyboard hint under 1180px so it cannot
  // crowd the session line, which is the one footer element that must stay
  // readable; the dialect has no media query, so the breakpoint lives here.
  const bool keysVisible = isActive && bounds.w >= MAIN_KEY_HINT_BREAKPOINT;
  const int32_t keysWidth = keysVisible ? MAIN_KEY_HINT_WIDTH : 0;

  // `.m-dirty` sits between the keyboard hint and the footer actions, and takes
  // no room at all on a route that is holding nothing.
  const bool commitVisible = isActive && this->commitStatus &&
    this->commitStatus->text->text && *this->commitStatus->text->text;
  const int32_t commitWidth = commitVisible ? MAIN_COMMIT_STATUS_WIDTH : 0;

  const int32_t actionsX = max(0, bounds.w - horizontalInset - bottomActionsWidth);
  const int32_t commitX = max(0, actionsX - MAIN_SHELL_GAP - commitWidth);

  // Center the keyboard hint until the commit/actions rail forces it left.
  // The session line then owns only the space before that final position, so
  // its labels can ellipsize without ever drawing under the hint.
  const int32_t keysMax = max(horizontalInset,
    (commitVisible ? commitX : actionsX) - MAIN_SHELL_GAP - keysWidth);
  const int32_t keysX = keysVisible
    ? clamp((bounds.w - keysWidth) / 2, horizontalInset, keysMax)
    : 0;
  const int32_t statusRight = keysVisible
    ? keysX
    : (commitVisible ? commitX : actionsX);
  const int32_t statusWidth = isActive
    ? max(0, statusRight - horizontalInset - MAIN_SHELL_GAP)
    : 0;
  this->serverInfo->view.hidden = !isActive;
  this->serverInfo->view.frame = MakeRect(horizontalInset, 0, statusWidth,
                                          bottomPaintHeight);
  this->serverInfo->view.minSize = MakeSize(statusWidth, bottomPaintHeight);
  this->serverInfo->view.maxSize = MakeSize(statusWidth, bottomPaintHeight);
  this->serverInfo->view.needsLayout = true;
  $((View *) this->serverInfo, layoutIfNeeded);
  if (isActive) {
    const char *server = cgi.server_name && *cgi.server_name
      ? cgi.server_name : "Local server";
    setEllipsizedLabelText(this->serverName, "", server,
                           this->serverName->view.frame.w);
  }
  if (this->commitStatus) {
    this->commitStatus->view.hidden = !commitVisible;
    this->commitStatus->view.frame = commitVisible
      ? MakeRect(commitX, 0, commitWidth, bottomPaintHeight)
      : MakeRect(0, 0, 0, 0);
    this->commitStatus->view.minSize = MakeSize(this->commitStatus->view.frame.w,
                                                this->commitStatus->view.frame.h);
    this->commitStatus->view.maxSize = this->commitStatus->view.minSize;
  }

  this->secondaryMenu->view.frame = MakeRect(actionsX, 0,
                                              bottomActionsWidth, bottomPaintHeight);
  this->secondaryMenu->view.minSize = MakeSize(bottomActionsWidth, bottomPaintHeight);
  this->secondaryMenu->view.maxSize = MakeSize(bottomActionsWidth, bottomPaintHeight);
  this->secondaryMenu->view.needsLayout = true;
  $((View *) this->secondaryMenu, layoutIfNeeded);
  this->contextHint->view.hidden = !keysVisible;
  this->contextHint->view.frame = keysVisible
    ? MakeRect(keysX, 0, keysWidth, bottomPaintHeight)
    : MakeRect(0, 0, 0, 0);
  this->contextHint->view.minSize = MakeSize(this->contextHint->view.frame.w,
                                             this->contextHint->view.frame.h);
  this->contextHint->view.maxSize = this->contextHint->view.minSize;
  this->contextHint->view.needsLayout = true;
  $((View *) this->contextHint, layoutIfNeeded);

  if (this->quickSettingsHost) {
    // The design's tier-1 drawer is not a floating panel inside the content
    // plane: it is `min(380px, 100%)` wide, flush to the *viewport* edge and
    // full viewport height, over a scrim that covers everything including both
    // chrome bars. So the host is the scrim - it takes the whole bounds - and
    // the panel is pinned inside it.
    this->quickSettingsHost->frame = bounds;

    const int32_t drawerWidth = min(bounds.w, MAIN_DRAWER_WIDTH);

    // Only the drawer's left hairline is meant to land on screen. The dialect's
    // border-width is uniform, so the other three edges are pushed past the
    // viewport and the host clips them, the same trick the header and footer
    // bars use for their single inboard rule.
    const Array *drawerViews = (Array *) this->quickSettingsHost->subviews;
    for (size_t i = 0; i < drawerViews->count; i++) {
      View *drawer = drawerViews->elements[i];
      const int32_t drawerHeight = bounds.h + MAIN_CLIPPED_BORDER_INSET * 2;
      drawer->frame = MakeRect(bounds.w - drawerWidth,
                               -MAIN_CLIPPED_BORDER_INSET,
                               drawerWidth + MAIN_CLIPPED_BORDER_INSET,
                               drawerHeight);
      drawer->minSize = MakeSize(drawer->frame.w, drawerHeight);
      drawer->maxSize = MakeSize(drawer->frame.w, drawerHeight);
      drawer->needsLayout = true;
    }
  }

  View *dialog = $(self, descendantWithIdentifier, "dialog");
  if (dialog) {
    const int32_t dialogWidth = min(MAIN_DIALOG_WIDTH, max(0, innerWidth - 24));
    dialog->frame = bounds;
    dialog->minSize = MakeSize(bounds.w, bounds.h);
    dialog->maxSize = MakeSize(bounds.w, bounds.h);
    dialog->padding.left = windowX;
    dialog->padding.right = max(0, bounds.w - windowX - windowWidth);
    dialog->padding.top = windowY;
    dialog->padding.bottom = max(0, bounds.h - windowY - windowHeight);

    const Array *dialogSubviews = (Array *) dialog->subviews;
    if (dialogSubviews->count) {
      View *panel = dialogSubviews->elements[0];
      panel->frame.w = dialogWidth;
      panel->minSize.w = dialogWidth;
      panel->maxSize.w = dialogWidth;
      panel->needsLayout = true;
    }
    dialog->needsLayout = true;
  }

  clampScrollOffsets(this->contentView);
}

/**
 * @see View::updateBindings(View *)
 */
/**
 * @brief Adds or removes `className`, but only when that is a change.
 * @details `View::addClassName` and `removeClassName` both invalidate the
 * style of the whole subtree whether or not the set actually changed, and this
 * runs from `updateBindings` alongside the live session values - so an
 * unconditional pair of calls would restyle the entire shell on every refresh.
 */
static void setClassName(View *view, const char *className, const bool present) {

  if ($(view, hasClassName, className) == present) {
    return;
  }

  if (present) {
    $(view, addClassName, className);
  } else {
    $(view, removeClassName, className);
  }
}

static void markBindingState(View *view, ident data) {

  (void) data;
  TextView *textView = (TextView *) view;
  if (textView->defaultText && *textView->defaultText) {
    $(view, removeClassName, "unbound");
  } else {
    $(textView, setDefaultText, "Unbound");
    $(view, addClassName, "unbound");
  }
}

static void updateBindings(View *self) {

  super(View, self, updateBindings);

  MainView *this = (MainView *) self;
  const bool isActive = *cgi.state == CL_ACTIVE;

  // The content plane is this menu's backdrop in both states. It was
  // connected-only, and the disconnected route fell back to a random
  // screenshot out of `ui/backgrounds` - two different rooms for one menu, and
  // the seam showed the moment a player opened the route before connecting.
  // The plane is procedural, so it costs the same either way.
  this->activeBackground->hidden = false;
  this->speedGrid->hidden = !SpeedGridView_Enabled();
  this->footerHairline->hidden = false;

  // The design's recessed chrome bars belong to the connected shell. The
  // disconnected menu still puts its version string in the bottom-left corner,
  // which an opaque footer would cover - so there the bars stay the transparent
  // rails they have always been.
  setClassName(this->topBar, "connectedChrome", isActive);
  setClassName(this->bottomBar, "connectedChrome", isActive);
  this->version->view.hidden = isActive;
  this->overlayShade->hidden = false;
  this->serverInfo->view.hidden = !isActive;
  this->sessionMetrics->view.hidden = !isActive;
  const char *weaponTuningWarning = isActive
    ? Cg_RaceWeaponTuning_Warning()
    : NULL;
  $(this->weaponTuningWarning->text, setText,
    weaponTuningWarning ? weaponTuningWarning : "");
  this->weaponTuningWarning->view.hidden = weaponTuningWarning == NULL;

  $(this->menuHeading->text, setText, isActive ? "ESC MENU" : "MAIN MENU");
  const bool eyebrowOverridden = this->eyebrowOverride[0] != 0;
  $(this->connectionLabel->text, setText,
    eyebrowOverridden ? this->eyebrowOverride
                      : (isActive ? "Connected session" : "Race menu"));

  // Guarded, because adding or removing a class name re-applies the stylesheet
  // to the whole subtree and this runs on every layout pass.
  {
    View *eyebrow = (View *) this->connectionLabel;
    const bool offline = eyebrowOverridden && this->eyebrowOffline;
    const bool wasOffline = $(eyebrow, hasClassName, "offline");
    if (offline != wasOffline) {
      if (offline) {
        $(eyebrow, addClassName, "offline");
      } else {
        $(eyebrow, removeClassName, "offline");
      }
    }
  }

  if (isActive) {
    const char *bsp = configStringOr(CS_BSP, "unknown_map");
    const char *time = configStringOr(CS_TIME, "--:--");
    const char *server = cgi.server_name && *cgi.server_name ? cgi.server_name : "Local server";
    const race_physics_config_t *physicsConfig = Race_Physics_Current();
    const race_physics_preset_descriptor_t *physicsPreset = physicsConfig
      ? Race_Physics_Preset(physicsConfig->preset)
      : NULL;
    const char *physics = Cg_RacePhysics_Synchronized()
      ? physicsPreset ? physicsPreset->short_name : "Unknown"
      : "Syncing";
    const int16_t ping = Cg_LocalPing();
    char plainTime[MAX_QPATH];

    q_strcolorstrip(time, plainTime);

    $(this->topPlayers->text, setText,
      va("%d / %d", cg_state.num_clients, cg_state.max_clients));
    $(this->topTime->text, setText, plainTime);
    $(this->topPhysics->text, setText, physics);

    $(this->serverName->text, setText, server);
    $(this->serverMap->text, setText, bsp);
    $(this->serverPing->text, setText,
      ping >= 0 ? va("%d ms", ping) : "-- ms");
  } else {
    $(this->serverName->text, setText, "");
    $(this->serverMap->text, setText, "");
    $(this->serverPing->text, setText, "");
  }

  $(self, enumerateSelection, "BindTextView", markBindingState, NULL);

  self->needsLayout = true;
}

#pragma mark - MainView

/**
 * @fn MainView *MainView::initWithFrame(MainView *self, const SDL_Rect *frame)
 * @memberof MainView
 */
static MainView *initWithFrame(MainView *self, const SDL_Rect *frame) {

  cacheMenuFont();
  self = (MainView *) super(View, self, initWithFrame, frame);
  if (self) {
    Outlet outlets[] = MakeOutlets(
      MakeOutlet("overlayShade", &self->overlayShade),
      MakeOutlet("version", &self->version),
      MakeOutlet("topBar", &self->topBar),
      MakeOutlet("brand", &self->brand),
      MakeOutlet("brandTitle", &self->brandTitle),
      MakeOutlet("menuHeading", &self->menuHeading),
      MakeOutlet("menuWindow", &self->menuWindow),
      MakeOutlet("windowHeader", &self->windowHeader),
      MakeOutlet("routeSummary", &self->routeSummary),
      MakeOutlet("connectionLabel", &self->connectionLabel),
      MakeOutlet("weaponTuningWarning", &self->weaponTuningWarning),
      MakeOutlet("windowTitle", &self->windowTitle),
      MakeOutlet("windowLockup", &self->windowLockup),
      MakeOutlet("commitStatus", &self->commitStatus),
      MakeOutlet("sessionMetrics", &self->sessionMetrics),
      MakeOutlet("topPlayers", &self->topPlayers),
      MakeOutlet("topTime", &self->topTime),
      MakeOutlet("topPhysics", &self->topPhysics),
      MakeOutlet("bottomBar", &self->bottomBar),
      MakeOutlet("contextHint", &self->contextHint),
      MakeOutlet("serverInfo", &self->serverInfo),
      MakeOutlet("serverName", &self->serverName),
      MakeOutlet("serverMap", &self->serverMap),
      MakeOutlet("serverPing", &self->serverPing),
      MakeOutlet("primaryMenu", &self->primaryMenu),
      MakeOutlet("topActions", &self->topActions),
      MakeOutlet("secondaryMenu", &self->secondaryMenu)
    );

    View *this = (View *) self;

    $(this, awakeWithResourceName, "ui/main/MainView.json");
    $(this, resolve, outlets);

    this->stylesheet = $$(Stylesheet, stylesheetWithResourceName, "ui/main/MainView.css");
    assert(this->stylesheet);

    // An ImageView rather than a stack of flat Views: the design's content
    // plane is a four-stop radial gradient over a linear one, which the
    // stylesheet dialect cannot express at all and a band stack can only
    // approximate along one axis. See createActiveBackgroundSurface.
    ImageView *activeBackground = $(alloc(ImageView), initWithImage, NULL);
    assert(activeBackground);
    activeBackground->view.identifier = q_strdup("activeBackground");
    assert(activeBackground->view.identifier);
    activeBackground->view.hidden = true;

    SDL_Surface *backdrop = createActiveBackgroundSurface();
    if (backdrop) {
      $(activeBackground, setImageWithSurface, backdrop);
      SDL_DestroySurface(backdrop);
    }

    $(this, addSubviewRelativeTo, (View *) activeBackground, self->overlayShade,
      ViewPositionBefore);
    self->activeBackground = (View *) activeBackground;
    release(activeBackground);

    // The moving half of the same plane: the gradient is still, this is
    // the floor of the room it lights. Above the gradient and below the
    // scrim, so the scrim still calms it under route content.
    SpeedGridView *speedGrid = (SpeedGridView *) $((View *) alloc(SpeedGridView),
      init);
    assert(speedGrid);
    speedGrid->view.identifier = q_strdup("speedGrid");
    assert(speedGrid->view.identifier);
    speedGrid->view.hidden = true;

    $(this, addSubviewRelativeTo, (View *) speedGrid, self->overlayShade,
      ViewPositionBefore);
    self->speedGrid = (View *) speedGrid;
    release(speedGrid);

    // Above the plane and below everything else, so the seam reads against the
    // plane it terminates rather than over the footer's contents.
    ImageView *footerHairline = $(alloc(ImageView), initWithImage, NULL);
    assert(footerHairline);
    footerHairline->view.identifier = q_strdup("footerHairline");
    assert(footerHairline->view.identifier);
    footerHairline->view.hidden = true;

    SDL_Surface *hairline = createFooterHairlineSurface();
    if (hairline) {
      $(footerHairline, setImageWithSurface, hairline);
      SDL_DestroySurface(hairline);
    }

    $(this, addSubviewRelativeTo, (View *) footerHairline, self->overlayShade,
      ViewPositionBefore);
    self->footerHairline = (View *) footerHairline;
    release(footerHairline);

    View *contentView = $(alloc(View), initWithFrame, NULL);
    assert(contentView);

    contentView->identifier = q_strdup("contentViewport");
    assert(contentView->identifier);
    $(self->menuWindow, addSubview, contentView);
    self->contentView = contentView;

    $(self->menuWindow, bringSubviewToFront, self->windowHeader);
    $(this, bringSubviewToFront, self->topBar);
    $(this, bringSubviewToFront, self->bottomBar);
    release(contentView);

    View *activeVoteHost = $(alloc(View), initWithFrame, NULL);
    assert(activeVoteHost);
    activeVoteHost->identifier = q_strdup("activeVoteHost");
    assert(activeVoteHost->identifier);
    activeVoteHost->hidden = true;
    activeVoteHost->clipsSubviews = true;
    $(this, addSubview, activeVoteHost);
    self->activeVoteHost = activeVoteHost;
    release(activeVoteHost);

    QuickSettingsHostView *quickSettingsHost =
      (QuickSettingsHostView *) $((View *) alloc(QuickSettingsHostView),
        initWithFrame, NULL);
    assert(quickSettingsHost);
    quickSettingsHost->view.identifier = q_strdup("quickSettingsHost");
    assert(quickSettingsHost->view.identifier);
    quickSettingsHost->view.hidden = true;
    quickSettingsHost->view.clipsSubviews = true;
    quickSettingsHost->delegate = (QuickSettingsHostViewDelegate) {
      .didDismiss = didDismissQuickSettings
    };
    $(this, addSubview, (View *) quickSettingsHost);
    self->quickSettingsHost = (View *) quickSettingsHost;
    release(quickSettingsHost);

    $(self->version->text, setText, va("Quetoo %s", cgi.GetCvarString("version")));

    $(this, updateBindings);
  }

  return self;
}

/**
 * @brief Prepares resource-owned page scrolling and native list markers once.
 */
void MainView_PrepareColumnsViews(View *page) {
  if (page) {
    prepareColumnsViews(page);
  }
}

void MainView_InvalidatePageSize(MainView *self) {
  if (self) {
    self->sizedPage = NULL;
    self->pageSize = MakeSize(0, 0);
  }
}

/**
 * @brief Reveals a newly focused control within each containing ScrollView.
 */
void MainView_RevealView(View *view) {

  if (view == NULL) {
    return;
  }

  View *ancestor = view->superview;
  while (ancestor) {
    if ($((Object *) ancestor, isKindOfClass, _ScrollView())) {
      ScrollView *scrollView = (ScrollView *) ancestor;
      if (scrollView->contentView) {
        const SDL_Rect bounds = $(ancestor, bounds);
        const SDL_Size contentSize = $(scrollView->contentView, size);
        if (contentSize.h > bounds.h || contentSize.w > bounds.w) {
          const SDL_Rect viewport = $(ancestor, renderFrame);
          const SDL_Rect focused = $(view, renderFrame);
          SDL_Point offset = scrollView->contentOffset;
          const int32_t padding = 12;

          if (focused.y < viewport.y + padding) {
            offset.y += viewport.y + padding - focused.y;
          } else if (focused.y + focused.h > viewport.y + viewport.h - padding) {
            offset.y -= focused.y + focused.h - (viewport.y + viewport.h - padding);
          }

          if (focused.x < viewport.x + padding) {
            offset.x += viewport.x + padding - focused.x;
          } else if (focused.x + focused.w > viewport.x + viewport.w - padding) {
            offset.x -= focused.x + focused.w - (viewport.x + viewport.w - padding);
          }

          $(scrollView, scrollToOffset, &offset);
          $(ancestor, layoutIfNeeded);
        }
      }
    }

    ancestor = ancestor->superview;
  }
}

/**
 * @brief Wraps explicitly overflowable content in a stable ScrollView.
 */
ScrollView *MainView_WrapScrollContent(View *content, const char *className) {

  if (content == NULL || content->superview == NULL) {
    return NULL;
  }

  if ($((Object *) content->superview, isKindOfClass, _ScrollView())) {
    ScrollView *scrollView = (ScrollView *) content->superview;
    if (className) {
      $((View *) scrollView, addClassName, className);
    }
    return scrollView;
  }

  View *parent = content->superview;
  PageView *pageView = $((Object *) parent, isKindOfClass, _PageView())
    ? (PageView *) parent
    : NULL;
  const bool wasCurrentPage = pageView && pageView->currentPage == content;
  const bool wasHidden = content->hidden;
  ScrollView *scrollView = $(alloc(ScrollView), initWithFrame, &content->frame);
  assert(scrollView);

  View *scrollViewView = (View *) scrollView;
  scrollViewView->alignment = content->alignment;
  scrollViewView->autoresizingMask = ViewAutoresizingWidth;
  scrollViewView->minSize = content->minSize;
  scrollViewView->maxSize = content->maxSize;
  scrollViewView->clipsSubviews = true;
  // One notch, one row: the menu's rosters are all 44pt rows, so a scroll that
  // moved 40 cut a different slice off the top row every time. It still cannot
  // guarantee a boundary - an intro line and a preview are not row-height - but
  // scrolling a roster now steps through it rather than across it.
  scrollView->step = 44.f;

  if (className) {
    $(scrollViewView, addClassName, className);
  }
  if (className && strcmp(className, "esc-page-scroll") == 0) {
    scrollViewView->autoresizingMask = ViewAutoresizingWidth | ViewAutoresizingHeight;
    scrollViewView->minSize = MakeSize(0, 0);
    scrollViewView->maxSize = MakeSize(INT32_MAX, INT32_MAX);
    content->autoresizingMask |= ViewAutoresizingWidth | ViewAutoresizingContain;
    content->minSize.w = 0;
    content->maxSize.w = INT32_MAX;
  }

  retain(content);
  $(parent, replaceSubview, content, scrollViewView);

  if (pageView) {
    if (wasCurrentPage) {
      $(pageView, setCurrentPage, scrollViewView);
    } else {
      scrollViewView->hidden = wasHidden;
    }
  }

  content->alignment = ViewAlignmentTopLeft;
  content->hidden = false;
  content->frame.x = content->frame.y = 0;
  $(scrollView, setContentView, content);
  release(content);

  release(scrollView);
  return scrollView;
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ViewInterface *) clazz->interface)->render = render;
  ((ViewInterface *) clazz->interface)->layoutSubviews = layoutSubviews;
  ((ViewInterface *) clazz->interface)->respondToEvent = respondToEvent;
  ((ViewInterface *) clazz->interface)->updateBindings = updateBindings;

  ((MainViewInterface *) clazz->interface)->initWithFrame = initWithFrame;
}

/**
 * @fn Class *MainView::_MainView(void)
 * @memberof MainView
 */
Class *_MainView(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "MainView",
      .superclass = _View(),
      .instanceSize = sizeof(MainView),
      .interfaceOffset = offsetof(MainView, interface),
      .interfaceSize = sizeof(MainViewInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
