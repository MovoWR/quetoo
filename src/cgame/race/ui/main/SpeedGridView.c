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

#include "SpeedGridView.h"
#include "cg_race_presentation.h"

#define _Class _SpeedGridView

enum {
  /* Depth bands drawn. The far end has to sit past the point the fade reaches
     zero, or the grid ends on a visible last rung instead of on nothing. */
  GRID_ROWS = 20,
  /* Rails each side of the vanishing point. */
  GRID_RAILS = 6,
  /* World units per cell. Quetoo's own brush grid, so the cadence at a given
     speed is the cadence the player already reads off the map geometry. */
  GRID_CELL = 96
};

/* The horizon, as a fraction of the plane's height from its top. The grid
   fades out well short of it - see gridFade - so this is where the convergence
   points, not where the geometry ends. */
#define GRID_HORIZON 0.42f

/* The vanishing point's horizontal position; 0.5 is centred. The design
   originally threw this right of centre, under the backdrop's own corner
   light and away from the lower-left quadrant the routes fill. Centred was
   asked for instead, and it also squares the span: scaleX throws the
   outermost rail GRID_SPAN_SLACK / 2 of the width either side of this
   point, so off-centre spends that slack on one edge and leaves the other
   barely covered. At 0.5 both edges clear by the same margin. */
#define GRID_VANISH 0.5f

/* The depth that lands on the plane's bottom edge - three cells out, not one.
   Both scales are derived from it, so it is the one number that sets how much
   grid the plane holds. At one cell the first cell alone eats half the ground
   band and the remaining two dozen rows pile into a sliver under the horizon;
   at three the rows distribute, and the crowding that is left reads as the
   distance falling away rather than as rows running out. */
#define GRID_NEAR_DEPTH 288.f

/* How far past the frame edge the outermost rail is thrown, so the grid never
   ends inside the window on a wide aspect. */
#define GRID_SPAN_SLACK 1.18f

/* Rows are culled below this multiple of GRID_NEAR_DEPTH rather than followed
   to z -> 0, which would put a vertex several hundred window-heights down. The
   same z still drives both axes, so the rails stay straight through the clamp. */
#define GRID_NEAR_CLAMP 0.35f

/* Speed smoothing, matching the HUD readout's own anti-strobe lerp. */
#define GRID_SPEED_LERP 0.18f

/* The drift a standing player gets, and the window real speeds are held to.
   Zero would stop the grid dead, which reads as a bug rather than as rest. */
#define GRID_IDLE_SPEED 180.f
#define GRID_MIN_SPEED 120.f
#define GRID_MAX_SPEED 1400.f

/* A frame long enough to have been an alt-tab rather than a slow frame is
   dropped, so the grid does not jump a screen's worth of cells on focus. */
#define GRID_MAX_FRAME_MS 250.f

/* The most cg_race_menu_grid_speed may multiply the scroll by. High enough to
   be useful for looking at the motion, low enough that a typo cannot strobe the
   whole backdrop. */
#define GRID_SCALE_MAX 8.f

/* Grid alpha at rest and at speed, and the speed the upper bound is reached
   at. The plane behind is dark and low-contrast, so these stay low: the grid is
   the floor of the room, not a feature. */
/* Raised from the handoff's 0.22/0.46 to carry the brand palette. Those values
   were tuned against #73c7f2, and #a5444e has under half its luminance - 89
   against 184 - so at the original alpha the near field lost more than half its
   weight on screen and the grid read as barely there. Full compensation wants
   0.95 at speed, which would draw the near rows solid; this takes most of the
   way back instead. Retune these, not the hexes, if the grid reads wrong. */
#define GRID_ALPHA_IDLE 0.40f
#define GRID_ALPHA_FAST 0.80f
#define GRID_ALPHA_SPEED 900.f

/* Where in the ground band the distance fade starts and finishes, measured
   from the horizon. A uniform grid crowds towards its vanishing point without
   limit - past GRID_FADE_START the rows are a handful of pixels apart, and
   drawing them at any alpha at all turns the far field into one muddy band with
   a visible last rung on top of it. Ending the fade there instead leaves the
   distance falling away into nothing, which is what the crowding should look
   like. A power curve cannot do this: it approaches zero without reaching it. */
#define GRID_FADE_START 0.10f
#define GRID_FADE_END 0.75f

/* Line weight, near to far. Tapering does what a per-vertex alpha ramp would
   have done, which the renderer does not offer: Renderer::pushDrawArrays
   overwrites every vertex colour with its own argument. */
#define GRID_WIDTH_NEAR 1.7f
#define GRID_WIDTH_FAR 0.7f

/* The two brand colours, blue at the horizon and red in the near field, with a
   lifted red for speed.

   Warm advances and cool recedes, so blue far / red near is the way round that
   reads as depth rather than as two halves. The path between them runs through
   violet - saturation dips to about a quarter at the midpoint - and that is the
   real hue between blue and red, not a neutral. It is worth stating the
   difference, because the same mix from a cyan base is what made gold
   unusable here: cyan and gold are near-complementary and interpolate through
   grey, landing on a sickly (167,197,180). Blue and red do not; they land on a
   dusky violet that belongs in the ramp.

   This deliberately departs from the handoff, which specified one blue and "no
   red anywhere". Asked for, and the red is the design system's own brand-danger
   rather than a new colour. */
#define GRID_FAR_R 0x3e
#define GRID_FAR_G 0x6b
#define GRID_FAR_B 0x9d

#define GRID_NEAR_R 0xa5
#define GRID_NEAR_G 0x44
#define GRID_NEAR_B 0x4e

/* Brand red lifted, not a third hue: speed makes the near field burn brighter
   red rather than turn some other colour. */
#define GRID_HEAT_R 0xd4
#define GRID_HEAT_G 0x70
#define GRID_HEAT_B 0x7a

#define GRID_HEAT_MAX 0.45f

/* Distance markers. Every Nth cell boundary *in the world* draws brighter and
   wider - not every Nth row on screen, which would stand still under a moving
   grid and read as a banding artefact. Keyed to the travelled distance instead,
   a marker belongs to one boundary and sweeps in with it, so the cadence has a
   beat to count rather than a texture that shimmers. Eight cells is 768 units:
   about one and a quarter seconds at 620ups. */
#define GRID_MARKER_EVERY 8
#define GRID_MARKER_WIDTH 2.1f
#define GRID_MARKER_ALPHA 1.75f
#define GRID_MARKER_HEAT 0.4f

/* The horizon bloom. The fade deliberately ends short of the vanishing point,
   which leaves the convergence unmarked - the lines simply stop, and the plane
   reads as a floor that ran out rather than as distance. A soft band centred on
   the horizon gives them something to arrive at. Banded for the same reason the
   fade is: one colour per draw call. */
/* Enough bands that the step between two neighbours' alpha lands under one
   level of 255 at the peak. Sixteen put it around five, which is a visible
   stripe: flat-alpha quads cannot hide a gradient, they can only make its
   steps too small to see. */
#define GRID_GLOW_BANDS 48
#define GRID_GLOW_SPREAD 0.26f
#define GRID_GLOW_IDLE 0.15f
#define GRID_GLOW_FAST 0.28f

/**
 * @brief Scrolling rate, as a multiple of the speed the player was carrying.
 * @details Looked up once in initWithFrame and only read here, which is what
 * the handoff asks of any constant that becomes a cvar: AddCvar is a hash
 * insert and has no business in a render path.
 */
static cvar_t *cg_race_menu_grid_speed;

/**
 * @brief Whether to draw the backdrop at all.
 */
static cvar_t *cg_race_menu_grid;

#pragma mark - Geometry

/**
 * @brief The distance fade at `t`, the row's position in the ground band with
 * zero at the horizon and one at the plane's bottom edge.
 */
static float gridFade(const float t) {

  const float x = Clampf((t - GRID_FADE_START) / (GRID_FADE_END - GRID_FADE_START),
                         0.f, 1.f);

  return x * x * (3.f - 2.f * x);
}

/**
 * @brief The line colour for a row at `fade`, warmed by `heat`.
 * @details Depth first, so a row's hue is a function of where it sits rather
 * than of how fast the player is going; heat second and weighted by the same
 * fade, so speed lifts the arriving rows without touching the distance.
 */
static SDL_Color gridColor(const float fade, const float heat,
                           const float alpha) {

  const float warm = heat * fade;

  const float r = Mixf(Mixf((float) GRID_FAR_R, (float) GRID_NEAR_R, fade),
                       (float) GRID_HEAT_R, warm);
  const float g = Mixf(Mixf((float) GRID_FAR_G, (float) GRID_NEAR_G, fade),
                       (float) GRID_HEAT_G, warm);
  const float b = Mixf(Mixf((float) GRID_FAR_B, (float) GRID_NEAR_B, fade),
                       (float) GRID_HEAT_B, warm);

  return MakeColor((uint8_t) Clampf(r + 0.5f, 0.f, 255.f),
                   (uint8_t) Clampf(g + 0.5f, 0.f, 255.f),
                   (uint8_t) Clampf(b + 0.5f, 0.f, 255.f),
                   (uint8_t) Clampf(alpha * fade * 255.f + 0.5f, 0.f, 255.f));
}

/**
 * @brief Writes the six vertices of one line segment of the given width.
 * @return The number of vertices written, which is zero for a degenerate
 * segment.
 * @details Renderer::drawLines would do this, but it takes SDL_Points: every
 * endpoint would be truncated to whole pixels, and a rung sweeping up the
 * plane a third of a pixel per frame would advance in visible steps. Emitting
 * the quads here keeps the positions in floats, and lets one draw call carry a
 * whole band of rails instead of one call per rail.
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

/**
 * @brief The speed the grid should be scrolling at this frame, in units per
 * second.
 * @details In a live session this is the player's own horizontal speed, read
 * from the same field and by the same helper the HUD readout uses. Out of a
 * session there is no player state to ask, so the last speed seen this process
 * stands in - which is the point of the treatment: the menu keeps the pace of
 * the run it was opened out of. Before any session at all, the idle drift.
 */
static float gridTargetSpeed(void) {

  static float remembered = GRID_IDLE_SPEED;

  if (*cgi.state == CL_ACTIVE && cgi.client) {
    const vec3_t velocity = cgi.client->frame.ps.pm_state.velocity;
    remembered = (float) Cg_Race_HorizontalSpeed(velocity);
  }

  return Clampf(remembered, GRID_MIN_SPEED, GRID_MAX_SPEED);
}

/**
 * @brief Advances the smoothed speed and the scroll phase, and returns the
 * phase: the fraction of a cell the grid is currently offset by.
 */
static float gridAdvance(SpeedGridView *self) {

  const uint64_t now = SDL_GetTicks();
  const float elapsed = self->ticks ? Minf((float) (now - self->ticks),
                                           GRID_MAX_FRAME_MS) : 0.f;
  self->ticks = now;

  const float target = gridTargetSpeed();
  self->speed += (target - self->speed) * GRID_SPEED_LERP;

  // The multiplier scales the travel and nothing else, so brightness, heat and
  // the marker cadence keep reading the speed actually carried into the menu.
  // Holding the grid still with 0 leaves it lit for the run it came out of
  // rather than dimming it to a standstill.
  const float scale = cg_race_menu_grid_speed
    ? Clampf(cg_race_menu_grid_speed->value, 0.f, GRID_SCALE_MAX)
    : 1.f;

  self->travelled += self->speed * elapsed * 0.001f * scale;

  const float phase = fmodf(self->travelled / (float) GRID_CELL, 1.f);

  return phase < 0.f ? phase + 1.f : phase;
}

#pragma mark - View

/**
 * @see View::render(View *, Renderer *)
 */
static void render(View *self, Renderer *renderer) {

  super(View, self, render, renderer);

  SpeedGridView *this = (SpeedGridView *) self;

  const SDL_Rect frame = $(self, renderFrame);
  if (frame.w <= 0 || frame.h <= 0) {
    return;
  }

  const float phase = gridAdvance(this);

  // Which world boundary the nearest row is, so a marker rides one cell edge in
  // rather than staying pinned to a row index.
  const int32_t travelled = (int32_t) (this->travelled / (float) GRID_CELL);

  const float horizonY = frame.y + frame.h * GRID_HORIZON;
  const float bottomY = (float) (frame.y + frame.h);
  const float vanishX = frame.x + frame.w * GRID_VANISH;
  const float ground = bottomY - horizonY;

  // Two independent scales rather than one camera. The vertical one is fixed
  // by the design: the nearest cell boundary lands on the plane's bottom edge,
  // whatever the window is, so the grid always fills the plane exactly.
  const float scaleY = ground * GRID_NEAR_DEPTH;

  // The horizontal one is fixed by the width instead, so the outermost rail
  // clears the frame at 21:9 and at 4:3 alike rather than at 16:9 alone. An
  // eye height that satisfied both would be a third constant to keep in sync
  // with these two for no gain: nothing here is a view of a real scene.
  const float scaleX = frame.w * 0.5f * GRID_SPAN_SLACK * GRID_NEAR_DEPTH /
                       (GRID_RAILS * (float) GRID_CELL);

  const float speed = Clampf(this->speed / GRID_ALPHA_SPEED, 0.f, 1.f);
  const float alpha = Mixf(GRID_ALPHA_IDLE, GRID_ALPHA_FAST, speed);
  const float heat = GRID_HEAT_MAX * speed;

  // One extra boundary: a rail segment spans two rungs, so GRID_ROWS bands
  // need GRID_ROWS + 1 of them.
  float rowY[GRID_ROWS + 1], rowX[GRID_ROWS + 1], rowFade[GRID_ROWS + 1];

  for (int32_t i = 0; i <= GRID_ROWS; i++) {

    const float z = Maxf(GRID_CELL * ((float) i + 1.f - phase),
                         GRID_NEAR_DEPTH * GRID_NEAR_CLAMP);

    rowY[i] = horizonY + scaleY / z;
    rowX[i] = scaleX / z;

    const float t = Clampf((rowY[i] - horizonY) / ground, 0.f, 1.f);
    rowFade[i] = gridFade(t);
  }

  MVC_Vertex verts[(GRID_RAILS * 2 + 1) * 6];

  // The bloom first, so the grid draws over it rather than under.
  {
    const float spread = ground * GRID_GLOW_SPREAD;
    const float glow = Mixf(GRID_GLOW_IDLE, GRID_GLOW_FAST, speed);
    const float step = spread * 2.f / (float) GRID_GLOW_BANDS;

    for (int32_t i = 0; i < GRID_GLOW_BANDS; i++) {

      const float y = horizonY - spread + step * ((float) i + 0.5f);
      const float d = Clampf(fabsf(y - horizonY) / spread, 0.f, 1.f);
      const float falloff = (1.f - d) * (1.f - d);

      const SDL_Color color = MakeColor(
        GRID_FAR_R, GRID_FAR_G, GRID_FAR_B,
        (uint8_t) Clampf(glow * falloff * 255.f + 0.5f, 0.f, 255.f));

      // Exactly `step` tall, never a pixel more. Overlapping neighbours was
      // meant to hide the seams and did the opposite: two translucent quads
      // over the same pixel composite to about twice the alpha, so the overlap
      // drew a bright line every band instead of blending them. Sharing an edge
      // leaves the rasteriser's top-left rule to fill each pixel exactly once.
      const size_t count = gridSegment(verts, (float) frame.x, y,
                                       (float) (frame.x + frame.w), y, step);
      if (count) {
        $(renderer, pushDrawArrays, verts, count, NULL, &color);
      }
    }
  }

  for (int32_t i = 0; i < GRID_ROWS; i++) {

    // The band's far rung is already below the plane, so nothing between them
    // is on screen. Everything nearer than this row is off the bottom edge and
    // has been drawn; everything further is still to come.
    if (rowY[i + 1] >= bottomY) {
      continue;
    }

    const float bandFade = (rowFade[i] + rowFade[i + 1]) * 0.5f;
    const float bandWidth = Mixf(GRID_WIDTH_FAR, GRID_WIDTH_NEAR, bandFade);

    const SDL_Color bandColor = gridColor(bandFade, heat, alpha);

    // The rails, as one draw call for the whole band: every segment in it is
    // at the same depth, so they share the one colour pushDrawArrays applies.
    size_t count = 0;
    for (int32_t j = -GRID_RAILS; j <= GRID_RAILS; j++) {

      const float worldX = (float) (j * GRID_CELL);

      count += gridSegment(verts + count,
                           vanishX + worldX * rowX[i], rowY[i],
                           vanishX + worldX * rowX[i + 1], rowY[i + 1],
                           bandWidth);
    }

    if (count) {
      $(renderer, pushDrawArrays, verts, count, NULL, &bandColor);
    }

    // The rung, at its own depth's fade rather than the band's, so the cadence
    // sweeping towards the viewer brightens as it arrives.
    const float rungFade = rowFade[i];

    // Every eighth boundary carries the marker. The index is the world's, so
    // the bright line travels in with its cell instead of the pattern sitting
    // still while the grid moves under it.
    const bool marker = ((travelled + i + 1) % GRID_MARKER_EVERY) == 0;
    const float extent = (float) (GRID_RAILS * GRID_CELL);

    const SDL_Color rungColor = gridColor(
      rungFade,
      marker ? Minf(heat + GRID_MARKER_HEAT, 1.f) : heat,
      marker ? Minf(alpha * GRID_MARKER_ALPHA, 1.f) : alpha);

    const size_t rung = gridSegment(verts,
                                    vanishX - extent * rowX[i], rowY[i],
                                    vanishX + extent * rowX[i], rowY[i],
                                    Mixf(GRID_WIDTH_FAR,
                                         marker ? GRID_MARKER_WIDTH
                                                : GRID_WIDTH_NEAR,
                                         rungFade));

    if (rung) {
      $(renderer, pushDrawArrays, verts, rung, NULL, &rungColor);
    }
  }
}

#pragma mark - SpeedGridView

/**
 * @fn SpeedGridView *SpeedGridView::initWithFrame(SpeedGridView *self, const SDL_Rect *frame)
 * @memberof SpeedGridView
 */
static SpeedGridView *initWithFrame(SpeedGridView *self, const SDL_Rect *frame) {

  self = (SpeedGridView *) super(View, self, initWithFrame, frame);
  if (self) {

    View *view = (View *) self;

    $(view, addClassName, "speedGrid");

    // The grid is thrown wider than the plane on purpose, and the rows nearest
    // the viewer run off the bottom of it. Renderer::drawView scissors to the
    // clipping frame, so this is what keeps the overflow off the chrome bars.
    view->clipsSubviews = true;

    self->speed = GRID_IDLE_SPEED;
  }

  return self;
}

/**
 * @see View::init(View *)
 */
static View *init(View *self) {
  return (View *) $((SpeedGridView *) self, initWithFrame, NULL);
}

void SpeedGridView_Init(void) {

  cg_race_menu_grid = cgi.AddCvar(
    "cg_race_menu_grid", "1", CVAR_ARCHIVE,
    "Draw the animated speed grid behind the menu.");

  cg_race_menu_grid_speed = cgi.AddCvar(
    "cg_race_menu_grid_speed", "1", CVAR_ARCHIVE,
    "Scrolling rate of the menu's speed grid, as a multiple of the speed the "
    "player was carrying. 0 holds it still.");
}

bool SpeedGridView_Enabled(void) {
  // Absent rather than zero before Cg_Module_Init has run: the backdrop is on
  // by default, so an unregistered variable reads as on.
  return cg_race_menu_grid == NULL || cg_race_menu_grid->value != 0.f;
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ViewInterface *) clazz->interface)->init = init;
  ((ViewInterface *) clazz->interface)->render = render;

  ((SpeedGridViewInterface *) clazz->interface)->initWithFrame = initWithFrame;
}

/**
 * @fn Class *SpeedGridView::_SpeedGridView(void)
 * @memberof SpeedGridView
 */
Class *_SpeedGridView(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "SpeedGridView",
      .superclass = _View(),
      .instanceSize = sizeof(SpeedGridView),
      .interfaceOffset = offsetof(SpeedGridView, interface),
      .interfaceSize = sizeof(SpeedGridViewInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
