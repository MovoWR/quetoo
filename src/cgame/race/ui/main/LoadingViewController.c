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

#include "LoadingViewController.h"

#include "cg_race_hud.h"

#include <ObjectivelyMVC/ProgressBar.h>
#include <ObjectivelyMVC/Text.h>

#define _Class _LoadingViewController

/**
 * @brief The design's fixed vertical rhythm, in points.
 * @details "Menu v2 - Loading" states the status bar as two bottom-aligned
 * columns over a three-band footer. Every value here is the design's own, at
 * its 1920x1080 reference: a label's height carries the design's line box plus
 * the margin that follows it, so a column's height is the plain sum of its
 * rows. Everything the design clamps against the window - the gutters, the
 * bar's vertical padding, the columns, the lockup - is computed in
 * layoutForBounds instead, because the stylesheet dialect has neither clamp()
 * nor viewport units.
 */
enum {
  // The window the design's own measurements were taken against, and the
  // geometry the tree is built with before it has ever been laid out.
  LOAD_REFERENCE_WIDTH = 1920,
  LOAD_REFERENCE_HEIGHT = 1080,

  LOAD_EYEBROW_HEIGHT = 24,     // 12px line + 6 margin-bottom
  LOAD_TITLE_HEIGHT = 48,       // 38px line at 1.04
  LOAD_PATH_HEIGHT = 27,        // 7 margin-top + 14px line
  LOAD_PERCENT_HEIGHT = 40,     // 32px line
  LOAD_HOST_HEIGHT = 24,        // 6 margin-top + 13px line

  LOAD_STATUS_LEFT_HEIGHT = LOAD_EYEBROW_HEIGHT + LOAD_TITLE_HEIGHT + LOAD_PATH_HEIGHT,
  LOAD_STATUS_RIGHT_HEIGHT = LOAD_PERCENT_HEIGHT + LOAD_HOST_HEIGHT,

  LOAD_RAIL_HEIGHT = 3,
  LOAD_RULE_HEIGHT = 1,
  LOAD_KEYS_PAD_TOP = 10,
  LOAD_KEYS_LINE_HEIGHT = 14,   // 12px line
  LOAD_KEYS_PAD_BOTTOM = 14,
  LOAD_KEYS_HEIGHT = LOAD_KEYS_PAD_TOP + LOAD_KEYS_LINE_HEIGHT + LOAD_KEYS_PAD_BOTTOM,

  LOAD_COLUMN_GAP = 24,

  // clamp(16px, 2.5vw, 56px), the shell gutter MainView lays its chrome out
  // on. The design's own clamp for this screen starts at 20px; matching the
  // menu matters more, because the menu is what this screen resolves into and
  // a gutter that shifts across the handover is the one thing an eye catches.
  LOAD_GUTTER_MIN = 16,
  LOAD_GUTTER_MAX = 56,

  LOAD_BAR_PAD_TOP_MIN = 16,    // clamp(16px, 2.4vh, 30px)
  LOAD_BAR_PAD_TOP_MAX = 30,
  LOAD_BAR_PAD_BOTTOM_MIN = 18, // clamp(18px, 3vh, 36px)
  LOAD_BAR_PAD_BOTTOM_MAX = 36,

  LOAD_STATUS_RIGHT_MIN = 180,
  LOAD_STATUS_RIGHT_MAX = 360,

  // width: min(480px, 44vw), on the lockup's own 3:2 box.
  LOAD_LOCKUP_WIDTH = 480,
  LOAD_LOCKUP_HEIGHT = 320,

  // The scrim and the bar fill are stretched to their frames, so these are
  // sampling resolutions rather than sizes. 16:9 for the scrim, because its
  // ellipse is stated in percentages of a 16:9 box.
  LOAD_SCRIM_WIDTH = 256,
  LOAD_SCRIM_HEIGHT = 144,
  LOAD_BAR_SHADE_HEIGHT = 64,
};

/**
 * @brief One stop of a CSS gradient: a straight-alpha colour at a position
 * along the gradient's parameter.
 */
typedef struct {
  float t;
  float rgba[4];
} load_gradient_stop_t;

/**
 * @brief Interpolates one channel between two stops.
 */
static float mixChannel(const float from, const float to, const float t) {
  return from + (to - from) * Clampf(t, 0.f, 1.f);
}

/**
 * @brief Samples a stop list at `t`, the way a CSS gradient does.
 * @param stops The stops, in ascending `t` order.
 * @param count The number of stops.
 * @param t The gradient parameter.
 * @param rgba Receives the sampled colour, premultiplied by its own alpha.
 * @details Interpolation is premultiplied, which is what the browsers do and
 * what keeps a ramp towards `transparent` one hue all the way down rather than
 * darkening into black as the alpha falls. Terminal stops extend, per spec.
 */
static void sampleGradient(const load_gradient_stop_t *stops, const size_t count,
                           const float t, float *rgba) {

  assert(count);

  size_t upper = 0;
  while (upper < count && stops[upper].t < t) {
    upper++;
  }

  if (upper == 0 || upper == count) {
    const load_gradient_stop_t *stop = stops + (upper ? count - 1 : 0);
    for (int32_t c = 0; c < 3; c++) {
      rgba[c] = stop->rgba[c] * stop->rgba[3];
    }
    rgba[3] = stop->rgba[3];
    return;
  }

  const load_gradient_stop_t *from = stops + upper - 1, *to = stops + upper;
  const float span = to->t - from->t;
  const float f = span > 0.f ? (t - from->t) / span : 0.f;

  for (int32_t c = 0; c < 3; c++) {
    rgba[c] = mixChannel(from->rgba[c] * from->rgba[3],
                         to->rgba[c] * to->rgba[3], f);
  }
  rgba[3] = mixChannel(from->rgba[3], to->rgba[3], f);
}

/**
 * @brief Writes one premultiplied sample into an RGBA32 surface.
 * @details Both of this screen's generated surfaces are translucent - they are
 * laid over a map shot, not over a known colour - so unlike MainView's opaque
 * plane they cannot be flattened onto a backdrop here. SDL surfaces are
 * straight-alpha and the renderer blends them as such, so the premultiplied
 * sample is divided back out once, on the way in.
 */
static void storeSample(uint8_t *pixel, const float *rgba) {

  const float alpha = Clampf(rgba[3], 0.f, 1.f);

  for (int32_t c = 0; c < 3; c++) {
    const float straight = alpha > 0.f ? rgba[c] / alpha : 0.f;
    pixel[c] = (uint8_t) Clampf(straight + 0.5f, 0.f, 255.f);
  }
  pixel[3] = (uint8_t) Clampf(alpha * 255.f + 0.5f, 0.f, 255.f);
}

/**
 * @brief Renders the design's scrim over the map shot.
 * @return A new RGBA surface the caller owns, or `NULL`.
 * @details The design states it as
 *
 *   radial-gradient(120% 100% at 50% 42%,
 *     rgba(4,11,19,.28) 0%, rgba(3,9,16,.72) 62%, #020710 100%)
 *
 * A map shot is a screenshot: it has its own subject, its own exposure, and no
 * relationship at all to the type that has to sit over it. The scrim is the
 * whole of what makes this screen readable whatever loaded into it - open and
 * nearly clear behind the lockup, closing to opaque at the frame. The
 * stylesheet dialect has one fill primitive, a flat `background-color`, so
 * there is no CSS port of a three-stop ellipse; rendering it means rendering
 * it, once, into a texture the ImageView stretches over the window.
 */
static SDL_Surface *createScrimSurface(void) {

  SDL_Surface *surface = SDL_CreateSurface(LOAD_SCRIM_WIDTH, LOAD_SCRIM_HEIGHT,
                                           SDL_PIXELFORMAT_RGBA32);
  if (surface == NULL) {
    Cg_Warn("Failed to create the loading scrim: %s\n", SDL_GetError());
    return NULL;
  }

  const load_gradient_stop_t stops[] = {
    { 0.00f, { 4.f, 11.f, 19.f, 0.28f } },
    { 0.62f, { 3.f,  9.f, 16.f, 0.72f } },
    { 1.00f, { 2.f,  7.f, 16.f, 1.00f } },
  };

  const float w = LOAD_SCRIM_WIDTH, h = LOAD_SCRIM_HEIGHT;
  const float cx = 0.50f * w, cy = 0.42f * h;
  const float rx = 1.20f * w, ry = 1.00f * h;

  uint8_t *pixels = surface->pixels;

  for (int32_t y = 0; y < LOAD_SCRIM_HEIGHT; y++) {
    for (int32_t x = 0; x < LOAD_SCRIM_WIDTH; x++) {

      // Sample at the pixel centre, so the edges of the surface land where the
      // gradient's 0% and 100% actually are.
      const float dx = (x + 0.5f - cx) / rx;
      const float dy = (y + 0.5f - cy) / ry;

      float rgba[4];
      sampleGradient(stops, lengthof(stops), sqrtf(dx * dx + dy * dy), rgba);

      storeSample(pixels + y * surface->pitch + x * 4, rgba);
    }
  }

  return surface;
}

/**
 * @brief Renders the status bar's fill.
 * @return A new RGBA surface the caller owns, or `NULL`.
 * @details `linear-gradient(rgba(2,7,16,0), rgba(2,7,16,.78) 46%,
 * rgba(2,7,16,.94))`. The bar does not begin at an edge, it rises out of the
 * scrim - the map shot carries on behind the top of it, so there is no seam to
 * see and no panel for the type to sit in. One pixel wide, because nothing
 * about it varies horizontally.
 */
static SDL_Surface *createBarShadeSurface(void) {

  SDL_Surface *surface = SDL_CreateSurface(1, LOAD_BAR_SHADE_HEIGHT,
                                           SDL_PIXELFORMAT_RGBA32);
  if (surface == NULL) {
    Cg_Warn("Failed to create the loading status bar fill: %s\n", SDL_GetError());
    return NULL;
  }

  const load_gradient_stop_t stops[] = {
    { 0.00f, { 2.f, 7.f, 16.f, 0.00f } },
    { 0.46f, { 2.f, 7.f, 16.f, 0.78f } },
    { 1.00f, { 2.f, 7.f, 16.f, 0.94f } },
  };

  uint8_t *pixels = surface->pixels;

  for (int32_t y = 0; y < LOAD_BAR_SHADE_HEIGHT; y++) {

    float rgba[4];
    sampleGradient(stops, lengthof(stops),
                   (y + 0.5f) / LOAD_BAR_SHADE_HEIGHT, rgba);

    storeSample(pixels + y * surface->pitch, rgba);
  }

  return surface;
}

/**
 * @brief Sets an ImageView's image from a generated surface, then discards it.
 */
static void setGeneratedImage(ImageView *imageView, SDL_Surface *surface) {

  if (surface) {
    $(imageView, setImageWithSurface, surface);
    SDL_DestroySurface(surface);
  }
}

/**
 * @brief Fits a value to a Label, adding an ellipsis when it does not fit.
 * @details Both of the left column's lines are server-supplied: a map's title
 * is whatever the mapper typed, and an asset path grows without bound. Left to
 * overflow they would run under the percentage, because a Text is a texture
 * and its Label does not clip it.
 */
static void setFittedLabelText(Label *label, const char *value,
                               const int32_t availableWidth) {

  int32_t textWidth = 0;

  if (availableWidth <= 0 || label->text->font == NULL) {
    $(label->text, setText, value);
    return;
  }

  $(label->text->font, sizeCharacters, value, &textWidth, NULL);
  if (textWidth <= availableWidth) {
    $(label->text, setText, value);
    return;
  }

  char candidate[MAX_STRING_CHARS];
  char truncated[MAX_STRING_CHARS];

  for (size_t bytes = strlen(value); bytes > 0; bytes--) {
    SDL_utf8strlcpy(truncated, value, min(bytes + 1, sizeof(truncated)));
    snprintf(candidate, sizeof(candidate), "%s...", truncated);
    $(label->text->font, sizeCharacters, candidate, &textWidth, NULL);
    if (textWidth <= availableWidth) {
      $(label->text, setText, candidate);
      return;
    }
  }

  $(label->text, setText, "");
}

/**
 * @brief Marks a view as needing layout, along with everything above it.
 * @details View::layoutIfNeeded lays out only a view that carries the flag,
 * and View::resize is what normally sets it. Writing a frame or a padding
 * directly - which is the whole of what layoutForBounds does - sets nothing,
 * and a container whose padding moved but whose size did not would otherwise
 * never re-place its children.
 */
static void invalidateLayoutChain(View *view) {

  while (view) {
    view->needsLayout = true;
    view = view->superview;
  }
}

/**
 * @brief Applies the design's structure: what resizes, what aligns where, and
 * every height the design states as a constant.
 * @details Structure is set here and not in the stylesheet, which carries
 * colour, type, and the alignment of each Text inside its Label - nothing
 * else. Two reasons, and the first is the sharp one: layoutForBounds writes
 * frames and padding directly, so a stylesheet that also declared them would
 * silently reset the responsive geometry the moment anything triggered a
 * restyle. The second is that a stylesheet is a resource, and a resource can
 * be stale - which on this screen would leave a container at zero size and
 * throw its centred contents off the top-left corner. The loading screen is
 * the one screen that has to survive a half-installed tree.
 */
static void structureViewTree(LoadingViewController *self) {

  View *view = ((ViewController *) self)->view;

  view->autoresizingMask = ViewAutoresizingFill;

  // The plate and the scrim over it, both edge to edge.
  self->mapShot->view.autoresizingMask = ViewAutoresizingFill;
  self->scrim->view.autoresizingMask = ViewAutoresizingFill;

  // `.stage { flex: 1 }`: it fills the window, and its bottom padding takes
  // the footer's band out of the bounds the lockup centres against.
  self->stage->autoresizingMask = ViewAutoresizingFill;
  self->logo->view.alignment = ViewAlignmentMiddleCenter;

  self->footer->view.alignment = ViewAlignmentBottomCenter;
  self->footer->view.autoresizingMask = ViewAutoresizingWidth;
  self->footer->axis = StackViewAxisVertical;
  self->footer->distribution = StackViewDistributionDefault;
  self->footer->spacing = 0;

  self->statusBar->view.autoresizingMask = ViewAutoresizingWidth;

  // The two columns, bottom-aligned to opposite corners of the bar.
  StackView *const columns[] = { self->statusLeft, self->statusRight };
  const ViewAlignment columnAlignment[] = {
    ViewAlignmentBottomLeft, ViewAlignmentBottomRight
  };

  for (size_t i = 0; i < lengthof(columns); i++) {
    columns[i]->view.alignment = columnAlignment[i];
    columns[i]->view.autoresizingMask = ViewAutoresizingNone;
    columns[i]->axis = StackViewAxisVertical;
    columns[i]->distribution = StackViewDistributionDefault;
    columns[i]->spacing = 0;
  }

  // Each Label is its design line box plus the margin that follows it, and
  // spans its column so the Text inside it can align to either edge.
  Label *const labels[] = {
    self->eyebrowLabel, self->mapTitle, self->mapPath,
    self->percentLabel, self->hostLabel, self->keysHint
  };
  const int32_t labelHeights[] = {
    LOAD_EYEBROW_HEIGHT, LOAD_TITLE_HEIGHT, LOAD_PATH_HEIGHT,
    LOAD_PERCENT_HEIGHT, LOAD_HOST_HEIGHT, LOAD_KEYS_LINE_HEIGHT
  };

  for (size_t i = 0; i < lengthof(labels); i++) {
    labels[i]->view.autoresizingMask = ViewAutoresizingWidth;
    labels[i]->view.frame.h = labelHeights[i];
    labels[i]->view.minSize.w = 0;
  }

  // The rail, its rule, and the band they sit above.
  self->progressBar->view.autoresizingMask = ViewAutoresizingWidth;
  self->progressBar->view.frame.h = LOAD_RAIL_HEIGHT;
  self->progressBar->view.padding = MakePadding(0, 0, 0, 0);
  ((View *) self->progressBar->background)->autoresizingMask = ViewAutoresizingFill;
  ((View *) self->progressBar->foreground)->autoresizingMask = ViewAutoresizingHeight;

  self->keysRule->autoresizingMask = ViewAutoresizingWidth;
  self->keysRule->frame.h = LOAD_RULE_HEIGHT;

  self->keysBar->autoresizingMask = ViewAutoresizingWidth;
  self->keysBar->frame.h = LOAD_KEYS_HEIGHT;
  self->keysBar->padding = MakePadding(LOAD_KEYS_PAD_TOP, 0, LOAD_KEYS_PAD_BOTTOM, 0);
}

/**
 * @brief Settles the geometry the design states against the window.
 * @details Only sizes and padding are written here; every position is the
 * alignment pass's, which is what lets this run from setProgress rather than
 * from a View subclass's layoutSubviews: View::layoutSubviews reads a
 * subview's frame back as its size, so a height written here survives the
 * layout that follows. Loading calls setProgress on every asset it touches,
 * so a window resized mid-load settles on the next one.
 */
static void layoutForBounds(LoadingViewController *self, const SDL_Rect bounds) {

  const int32_t gutter = clamp(bounds.w / 40, LOAD_GUTTER_MIN, LOAD_GUTTER_MAX);
  const int32_t padTop = clamp(bounds.h * 24 / 1000,
    LOAD_BAR_PAD_TOP_MIN, LOAD_BAR_PAD_TOP_MAX);
  const int32_t padBottom = clamp(bounds.h * 30 / 1000,
    LOAD_BAR_PAD_BOTTOM_MIN, LOAD_BAR_PAD_BOTTOM_MAX);

  // The two columns are bottom-aligned to opposite corners of one box, so the
  // bar is as tall as the taller of them plus the design's padding.
  const int32_t columnHeight = max(LOAD_STATUS_LEFT_HEIGHT, LOAD_STATUS_RIGHT_HEIGHT);
  const int32_t statusHeight = padTop + columnHeight + padBottom;
  const int32_t footerHeight = statusHeight + LOAD_RAIL_HEIGHT +
    LOAD_RULE_HEIGHT + LOAD_KEYS_HEIGHT;

  const int32_t rightWidth = clamp(bounds.w / 5,
    LOAD_STATUS_RIGHT_MIN, LOAD_STATUS_RIGHT_MAX);
  const int32_t leftWidth = max(0,
    bounds.w - 2 * gutter - rightWidth - LOAD_COLUMN_GAP);

  self->statusBar->view.frame.h = statusHeight;
  self->statusBar->view.padding = MakePadding(padTop, gutter, padBottom, gutter);

  self->statusLeft->view.frame.w = leftWidth;
  self->statusLeft->view.frame.h = LOAD_STATUS_LEFT_HEIGHT;
  self->statusRight->view.frame.w = rightWidth;
  self->statusRight->view.frame.h = LOAD_STATUS_RIGHT_HEIGHT;

  self->keysBar->padding.left = gutter;
  self->keysBar->padding.right = gutter;

  self->footer->view.frame.h = footerHeight;

  // The lockup is centred in what the footer leaves, not in the window: the
  // stage still fills the frame, but its padding takes the footer's band out
  // of the bounds the middle-center alignment resolves against.
  self->stage->padding.bottom = min(footerHeight, bounds.h);

  // The same number the stage takes as padding: the floor ends where the
  // chrome begins. Zero is valid - the view falls back to a fraction of
  // its own frame - so this is deliberately unasserted.
  self->grid->footer = footerHeight;

  const int32_t lockupWidth = min(LOAD_LOCKUP_WIDTH, bounds.w * 44 / 100);
  self->logo->view.frame.w = lockupWidth;
  self->logo->view.frame.h = lockupWidth * LOAD_LOCKUP_HEIGHT / LOAD_LOCKUP_WIDTH;

  // Every view whose own geometry moved above, each marking the chain up to
  // the root on its way.
  invalidateLayoutChain((View *) self->statusBar);
  invalidateLayoutChain((View *) self->statusLeft);
  invalidateLayoutChain((View *) self->statusRight);
  invalidateLayoutChain(self->keysBar);
  invalidateLayoutChain((View *) self->footer);
  invalidateLayoutChain(self->stage);
  invalidateLayoutChain((View *) self->logo);
}

#pragma mark - ViewController

/**
 * @see ViewController::loadView(ViewController *)
 */
static void loadView(ViewController *self) {

  super(ViewController, self, loadView);

  LoadingViewController *this = (LoadingViewController *) self;

  Outlet outlets[] = MakeOutlets(
    MakeOutlet("mapShot", &this->mapShot),
    MakeOutlet("scrim", &this->scrim),
    MakeOutlet("stage", &this->stage),
    MakeOutlet("logo", &this->logo),
    MakeOutlet("footer", &this->footer),
    MakeOutlet("statusBar", &this->statusBar),
    MakeOutlet("statusLeft", &this->statusLeft),
    MakeOutlet("statusRight", &this->statusRight),
    MakeOutlet("loadingEyebrow", &this->eyebrowLabel),
    MakeOutlet("mapTitle", &this->mapTitle),
    MakeOutlet("mapPath", &this->mapPath),
    MakeOutlet("loadingPercent", &this->percentLabel),
    MakeOutlet("loadingHost", &this->hostLabel),
    MakeOutlet("keysRule", &this->keysRule),
    MakeOutlet("keysBar", &this->keysBar),
    MakeOutlet("keysHint", &this->keysHint),
    MakeOutlet("progress", &this->progressBar)
  );

  $(self->view, awakeWithResourceName, "ui/main/LoadingViewController.json");
  $(self->view, resolve, outlets);

  // Not in the JSON: `awakeWithResourceName` can only build classes it knows,
  // and this one is Race-owned. Positioned relative to `stage`, so it has to
  // come after the outlets resolve - before that, `stage` is still NULL.
  this->grid = (LoadingGridView *) $((View *) alloc(LoadingGridView), init);
  assert(this->grid);
  ((View *) this->grid)->identifier = q_strdup("loadingGrid");
  assert(((View *) this->grid)->identifier);
  ((View *) this->grid)->autoresizingMask = ViewAutoresizingFill;

  $(self->view, addSubviewRelativeTo, (View *) this->grid, this->stage,
    ViewPositionBefore);
  release(this->grid);

  self->view->stylesheet = $$(Stylesheet, stylesheetWithResourceName, "ui/main/LoadingViewController.css");
  assert(self->view->stylesheet);

  $(this->logo, setImageWithResourceName, "ui/main/loading_lockup.png");

  setGeneratedImage(this->scrim, createScrimSurface());
  setGeneratedImage(this->statusBar, createBarShadeSurface());

  // The rail is the design's whole progress indicator: three points tall, no
  // chrome, no label. ProgressBar always builds a label, so the readout the
  // design does state - the percentage, at the head of the bar's right column
  // - is a Label of this screen's own, and the control's own is hidden rather
  // than formatted into a corner it was never designed to occupy.
  ((View *) this->progressBar->label)->hidden = true;

  // Stand the tree up at the design's own reference window, so the first frame
  // is already the right shape and every later one is a refinement of it
  // rather than a correction.
  structureViewTree(this);
  layoutForBounds(this, MakeRect(0, 0, LOAD_REFERENCE_WIDTH, LOAD_REFERENCE_HEIGHT));
}

#pragma mark - LoadingViewController

/**
 * @fn LoadingViewController *LoadingViewController::init(LoadingViewController *self)
 * @memberof LoadingViewController
 */
static LoadingViewController *init(LoadingViewController *self) {
  return (LoadingViewController *) super(ViewController, self, init);
}

/**
 * @fn void LoadingViewController::setProgress(LoadingViewController *self, const cl_loading_t loading)
 * @memberof LoadingViewController
 */
static void setProgress(LoadingViewController *self, const cl_loading_t loading) {

  // The whole animation. This screen has no frame loop - Cl_LoadingProgress
  // drives it synchronously, once per asset - so the floor is a pure function of
  // this one integer and of nothing time-derived.
  self->grid->percent = loading.percent;

  const char *message = cgi.ConfigString(CS_MESSAGE);
  const char *bsp = cgi.ConfigString(CS_BSP);
  const char *server = cgi.server_name;

  const SDL_Rect bounds = $(((ViewController *) self)->view, bounds);
  if (bounds.w > 0 && bounds.h > 0) {
    layoutForBounds(self, bounds);
  }

  const SDL_Rect leftBounds = $((View *) self->statusLeft, bounds);

  // The map's own title, falling back to the file the server is running: a
  // stock map always carries CS_MESSAGE, a hand-built one need not.
  char mapTitle[MAX_STRING_CHARS];
  Cg_RaceHud_ResolveEscapes(message, mapTitle, sizeof(mapTitle), false);

  setFittedLabelText(self->mapTitle,
    *mapTitle ? mapTitle : bsp && *bsp ? bsp : "Unknown map",
    leftBounds.w);

  // The design's second line is a path, and the one path that changes while
  // this screen is up is the asset being loaded - which is also the only
  // liveness the screen has between whole percentage points. It opens on the
  // .bsp, which is the value the design draws.
  setFittedLabelText(self->mapPath, loading.status ? loading.status : "",
    leftBounds.w);

  $(self->percentLabel->text, setText, va("%d%%", loading.percent));

  // The design pairs the host with its round-trip time. Ping is scoreboard
  // state, and no scoreboard exists until the first server frame - which
  // arrives after this screen is gone - so the host stands alone here.
  $(self->hostLabel->text, setText, server && *server ? server : "Local server");

  $(self->progressBar, setValue, loading.percent);

  if (loading.percent == 0) {
    if (loading.mapshot[0] != '\0') {
      SDL_Surface *surf = cgi.LoadSurface(loading.mapshot);
      if (surf) {
        cgi.BlurSurface(surf, 3);
        $(self->mapShot, setImageWithSurface, surf);
        SDL_DestroySurface(surf);
      } else {
        $(self->mapShot, setImageWithResourceName, loading.mapshot);
      }
    } else {
      // The plate the design itself is drawn over, and one of the six the menu
      // behind this screen draws from, so a map with no shot of its own still
      // resolves into the same room rather than into a black frame.
      $(self->mapShot, setImageWithResourceName, "ui/backgrounds/2.png");
    }
  }
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;

  ((LoadingViewControllerInterface *) clazz->interface)->init = init;
  ((LoadingViewControllerInterface *) clazz->interface)->setProgress = setProgress;
}

/**
 * @fn Class *LoadingViewController::_LoadingViewController(void)
 * @memberof LoadingViewController
 */
Class *_LoadingViewController(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "LoadingViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(LoadingViewController),
      .interfaceOffset = offsetof(LoadingViewController, interface),
      .interfaceSize = sizeof(LoadingViewControllerInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
