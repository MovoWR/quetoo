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

#include "LoadingGridView.h"

#define _Class _LoadingGridView

enum {
  /* Depth bands. Six more than SpeedGridView, because this plane is full-bleed
     rather than a band between two chrome bars, so the far field has further to
     dissolve in. */
  GRID_ROWS = 26,
  /* Rails each side of the vanishing point. */
  GRID_RAILS = 7,
  /* World units per cell, matching SpeedGridView and Quetoo's brush grid. */
  GRID_CELL = 96
};

/* The horizon, as a fraction of the plane's height. Measured against the plane
   above the footer, not the window: the footer is chrome standing on the floor,
   not part of it. */
#define GRID_HORIZON 0.36f

/* Centred, unlike the menu's 0.62. The menu pushed its vanishing point right to
   clear the lower-left quadrant its route content lives in; this screen has no
   such quadrant - the lockup is centred and the footer's two columns sit in
   opposite corners, so the composition is symmetric and the vanishing point
   belongs on the axis. */
#define GRID_VANISH 0.5f

/* The depth that lands on the plane's bottom edge, and the slack that throws
   the outermost rail past the frame on a wide aspect. Both as SpeedGridView. */
#define GRID_NEAR_DEPTH 288.f
#define GRID_SPAN_SLACK 1.2f

/* The distance fade window, from the horizon. Starts earlier than the menu's
   0.10 because there is no chrome bar cropping the top of the plane, so the
   rows immediately under the horizon are visible and must not read as a hard
   edge where the grid begins. */
#define GRID_FADE_START 0.05f
#define GRID_FADE_END 0.74f

/* Fallback footer height when layoutForBounds has not run. */
#define GRID_FOOTER_FRACTION 0.205f

/* Grid alpha. SpeedGridView runs 0.32 at rest and 0.58 at speed, calibrated in
   engine; this sits near the top of that range because nothing competes with it
   - the loading screen has a lockup and a status bar, not tables of records. */
#define GRID_RUNG_ALPHA 0.52f
#define GRID_RAIL_ALPHA 0.46f

/* Line weight, near to far, as SpeedGridView. */
#define GRID_WIDTH_NEAR 1.7f
#define GRID_WIDTH_FAR 0.7f

/* The leading edge: the only bright element on the plane, and the only one that
   moves. The wide low-alpha pass is a bloom stand-in - the renderer has no blur
   and the stylesheet dialect has no shadow. */
#define GRID_EDGE_ALPHA 0.90f
#define GRID_EDGE_WIDTH_FAR 1.4f
#define GRID_EDGE_WIDTH_NEAR 3.0f
#define GRID_EDGE_GLOW_ALPHA 0.16f
#define GRID_EDGE_GLOW_WIDTH 9.f

/* The lit-edge blue the shell uses for hairlines, and the near-white the design
   system reserves for a highlighted value. */
#define GRID_R 0x73
#define GRID_G 0xc7
#define GRID_B 0xf2
#define EDGE_R 0xd3
#define EDGE_G 0xef
#define EDGE_B 0xff

#pragma mark - Geometry

/**
 * @brief The camera, resolved from a frame and the footer's height.
 * @details Two independent scales, as `SpeedGridView`: `scaleY` puts
 * `GRID_NEAR_DEPTH` exactly on the plane's bottom edge at any window size, and
 * `scaleX` clears the frame at any aspect. Nothing here is a view of a real
 * scene, so a single consistent eye height would be a third constant to keep in
 * sync for no gain.
 */
typedef struct {
  float horizonY;
  float bottomY;
  float ground;
  float vanishX;
  float scaleY;
  float scaleX;
} grid_plane_t;

static grid_plane_t gridPlane(const SDL_Rect *frame, const int32_t footer) {

  const int32_t band = footer > 0 && footer < frame->h
    ? footer : (int32_t) (frame->h * GRID_FOOTER_FRACTION);

  grid_plane_t p;

  p.bottomY = (float) (frame->y + frame->h - band);
  p.horizonY = frame->y + (p.bottomY - frame->y) * GRID_HORIZON;
  p.ground = Maxf(1.f, p.bottomY - p.horizonY);
  p.vanishX = frame->x + frame->w * GRID_VANISH;
  p.scaleY = p.ground * GRID_NEAR_DEPTH;
  p.scaleX = frame->w * 0.5f * GRID_SPAN_SLACK * GRID_NEAR_DEPTH /
             (GRID_RAILS * (float) GRID_CELL);

  return p;
}

/**
 * @brief One row of the floor, at world depth `z`.
 */
typedef struct {
  float y;
  float sx;
  float fade;
} grid_row_t;

static grid_row_t gridRow(const grid_plane_t *p, const float z) {

  grid_row_t r;

  r.y = p->horizonY + p->scaleY / z;
  r.sx = p->scaleX / z;

  const float t = Clampf((r.y - p->horizonY) / p->ground, 0.f, 1.f);
  const float x = Clampf((t - GRID_FADE_START) / (GRID_FADE_END - GRID_FADE_START),
                         0.f, 1.f);

  r.fade = x * x * (3.f - 2.f * x);

  return r;
}

/**
 * @brief The depth the floor's near edge has reached at progress `p`.
 * @details Interpolated in `1 / z`, not in `z`. Screen position is the
 * reciprocal of depth, so only this makes the edge advance evenly across the
 * plane - a linear walk through depth crawls for the first half of the load and
 * then covers most of the plane in the last few percent.
 */
static float gridEdgeDepth(const float p) {

  // Not `near` and `far`: those are legacy MSVC macros out of `windef.h`,
  // defined to nothing for 16-bit segment compatibility, so declaring locals by
  // those names compiles to `const float = ...` on this toolchain.
  const float farDepth = GRID_CELL * (float) GRID_ROWS;
  const float nearDepth = GRID_NEAR_DEPTH;

  return 1.f / (1.f / farDepth +
                Clampf(p, 0.f, 1.f) * (1.f / nearDepth - 1.f / farDepth));
}

/**
 * @brief Writes the six vertices of one line segment of the given width.
 * @return The number of vertices written, zero for a degenerate segment.
 * @details Deliberately a copy of `SpeedGridView`'s, not a shared helper. The
 * two views share this one primitive and nothing else - every constant above
 * differs - so a common header would have been a header of two views' tuning.
 * `Renderer::drawLines` is not usable here: it takes `SDL_Point`, and an edge
 * that lands a third of a pixel further down the plane per asset would advance
 * in visible steps.
 */
static size_t gridSegment(MVC_Vertex *out, const float ax, const float ay,
                          const float bx, const float by, const float width) {

  const float dx = bx - ax, dy = by - ay;
  const float len = sqrtf(dx * dx + dy * dy);

  if (len < 0.01f) {
    return 0;
  }

  const float nx = (-dy / len) * width * 0.5f;
  const float ny = ( dx / len) * width * 0.5f;

  out[0] = (MVC_Vertex) { { { ax - nx, ay - ny } }, { { 0.f, 0.f } }, { 0 } };
  out[1] = (MVC_Vertex) { { { ax + nx, ay + ny } }, { { 0.f, 0.f } }, { 0 } };
  out[2] = (MVC_Vertex) { { { bx - nx, by - ny } }, { { 0.f, 0.f } }, { 0 } };
  out[3] = (MVC_Vertex) { { { ax + nx, ay + ny } }, { { 0.f, 0.f } }, { 0 } };
  out[4] = (MVC_Vertex) { { { bx + nx, by + ny } }, { { 0.f, 0.f } }, { 0 } };
  out[5] = (MVC_Vertex) { { { bx - nx, by - ny } }, { { 0.f, 0.f } }, { 0 } };

  return 6;
}

#pragma mark - View

/**
 * @see View::render(View *, Renderer *)
 */
static void render(View *self, Renderer *renderer) {

  super(View, self, render, renderer);

  LoadingGridView *this = (LoadingGridView *) self;

  const SDL_Rect frame = $(self, renderFrame);
  if (frame.w <= 0 || frame.h <= 0) {
    return;
  }

  // The floor never retreats. See the field's remarks.
  if (this->percent <= 0) {
    this->reached = 0;
  } else {
    this->reached = Maxi(this->reached, Mini(100, this->percent));
  }

  const grid_plane_t plane = gridPlane(&frame, this->footer);

  const float edge = gridEdgeDepth(this->reached / 100.f);
  const grid_row_t edgeRow = gridRow(&plane, edge);

  const float extent = (float) (GRID_RAILS * GRID_CELL);

  MVC_Vertex verts[6];

  // Rails, one segment per band so the distance fade can ramp along them.
  // Renderer::pushDrawArrays overwrites every vertex colour with its argument,
  // so a per-vertex ramp is not available and this is what replaces it.
  for (int32_t j = -GRID_RAILS; j <= GRID_RAILS; j++) {

    const float worldX = (float) (j * GRID_CELL);

    bool have = false;
    float px = 0.f, py = 0.f, pf = 0.f;

    for (int32_t i = 0; i <= GRID_ROWS; i++) {

      const float z = GRID_CELL * (float) (i + 1);
      if (z < edge) {
        have = false;
        continue;
      }

      const grid_row_t row = gridRow(&plane, z);
      const float x = plane.vanishX + worldX * row.sx;

      if (have) {

        const float fade = (pf + row.fade) * 0.5f;

        const SDL_Color color = MakeColor(GRID_R, GRID_G, GRID_B,
          (uint8_t) Clampf(GRID_RAIL_ALPHA * fade * 255.f + 0.5f, 0.f, 255.f));

        const size_t count = gridSegment(verts, px, py, x, row.y,
          Mixf(GRID_WIDTH_FAR, GRID_WIDTH_NEAR, fade));

        if (count) {
          $(renderer, pushDrawArrays, verts, count, NULL, &color);
        }
      }

      px = x; py = row.y; pf = row.fade;
      have = true;
    }

    // The stub from the leading edge to the first whole band past it, so the
    // rails start on the edge rather than at the next cell boundary.
    if (have) {

      const float ex = plane.vanishX + worldX * edgeRow.sx;
      const float fade = edgeRow.fade;

      const SDL_Color color = MakeColor(GRID_R, GRID_G, GRID_B,
        (uint8_t) Clampf(GRID_RAIL_ALPHA * fade * 255.f + 0.5f, 0.f, 255.f));

      const grid_row_t first = gridRow(&plane,
        Maxf(edge, GRID_CELL * ceilf(edge / GRID_CELL)));

      const size_t count = gridSegment(verts, ex, edgeRow.y,
        plane.vanishX + worldX * first.sx, first.y,
        Mixf(GRID_WIDTH_FAR, GRID_WIDTH_NEAR, fade));

      if (count) {
        $(renderer, pushDrawArrays, verts, count, NULL, &color);
      }
    }
  }

  // Rungs.
  for (int32_t i = 0; i <= GRID_ROWS; i++) {

    const float z = GRID_CELL * (float) (i + 1);
    if (z < edge) {
      continue;
    }

    const grid_row_t row = gridRow(&plane, z);
    if (row.y >= plane.bottomY) {
      continue;
    }

    const SDL_Color color = MakeColor(GRID_R, GRID_G, GRID_B,
      (uint8_t) Clampf(GRID_RUNG_ALPHA * row.fade * 255.f + 0.5f, 0.f, 255.f));

    const size_t count = gridSegment(verts,
      plane.vanishX - extent * row.sx, row.y,
      plane.vanishX + extent * row.sx, row.y,
      Mixf(GRID_WIDTH_FAR, GRID_WIDTH_NEAR, row.fade));

    if (count) {
      $(renderer, pushDrawArrays, verts, count, NULL, &color);
    }
  }

  // The leading edge, glow first so the hairline sits on top of it.
  const float ex0 = plane.vanishX - extent * edgeRow.sx;
  const float ex1 = plane.vanishX + extent * edgeRow.sx;

  const SDL_Color glow = MakeColor(EDGE_R, EDGE_G, EDGE_B,
    (uint8_t) Clampf(GRID_EDGE_GLOW_ALPHA * 255.f + 0.5f, 0.f, 255.f));

  size_t count = gridSegment(verts, ex0, edgeRow.y, ex1, edgeRow.y,
                             GRID_EDGE_GLOW_WIDTH);
  if (count) {
    $(renderer, pushDrawArrays, verts, count, NULL, &glow);
  }

  const SDL_Color bright = MakeColor(EDGE_R, EDGE_G, EDGE_B,
    (uint8_t) Clampf(GRID_EDGE_ALPHA * 255.f + 0.5f, 0.f, 255.f));

  count = gridSegment(verts, ex0, edgeRow.y, ex1, edgeRow.y,
    Mixf(GRID_EDGE_WIDTH_FAR, GRID_EDGE_WIDTH_NEAR, edgeRow.fade));
  if (count) {
    $(renderer, pushDrawArrays, verts, count, NULL, &bright);
  }
}

#pragma mark - LoadingGridView

/**
 * @fn LoadingGridView *LoadingGridView::initWithFrame(LoadingGridView *self, const SDL_Rect *frame)
 * @memberof LoadingGridView
 */
static LoadingGridView *initWithFrame(LoadingGridView *self, const SDL_Rect *frame) {

  self = (LoadingGridView *) super(View, self, initWithFrame, frame);
  if (self) {

    View *view = (View *) self;

    $(view, addClassName, "loadingGrid");

    // The plane is thrown wider than the frame on purpose, and the leading
    // edge carries a 9px glow that at 100% sits on the plane's bottom edge.
    // Renderer::drawView scissors to the clipping frame, and this is what
    // keeps both off the status bar.
    view->clipsSubviews = true;
  }

  return self;
}

/**
 * @see View::init(View *)
 */
static View *init(View *self) {
  return (View *) $((LoadingGridView *) self, initWithFrame, NULL);
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ViewInterface *) clazz->interface)->init = init;
  ((ViewInterface *) clazz->interface)->render = render;

  ((LoadingGridViewInterface *) clazz->interface)->initWithFrame = initWithFrame;
}

/**
 * @fn Class *LoadingGridView::_LoadingGridView(void)
 * @memberof LoadingGridView
 */
Class *_LoadingGridView(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "LoadingGridView",
      .superclass = _View(),
      .instanceSize = sizeof(LoadingGridView),
      .interfaceOffset = offsetof(LoadingGridView, interface),
      .interfaceSize = sizeof(LoadingGridViewInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
