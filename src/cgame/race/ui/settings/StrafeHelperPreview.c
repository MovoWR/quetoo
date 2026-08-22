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

#include "StrafeHelperPreview.h"

#define _Class _StrafeHelperPreview

/**
 * @brief The bar geometry the preview stands in with, in preview pixels.
 * @details The helper's real zone width comes from the acceleration angles of
 * the frame it is drawing, so a menu with no frame to ask has nothing to derive
 * it from. These are the design's stand-in proportions ("Settings - Strafe
 * helper"): a zone pair sized by `cg_race_strafe_helper_scale` either side of a fixed gap, which
 * is what makes `cg_race_strafe_helper_scale`, `cg_race_strafe_helper_height` and the marker widths legible without
 * claiming to predict a real frame.
 */
#define STRAFE_PREVIEW_ZONE_WIDTH 150.f
#define STRAFE_PREVIEW_ZONE_GAP 93.f
#define STRAFE_PREVIEW_DRAW_SCALE 0.62f

/**
 * @brief The screen-space-to-preview mapping for the `_y` offsets.
 * @details The offsets run +/-200 screen pixels and the preview box is a
 * fraction of that tall. Mapping them by `min(drawScale, room / 200)` is what
 * keeps the whole of every offset slider inside the box: at the design's height
 * the far end of the slider still moves the readout, rather than parking it
 * against an edge and reading as dead travel.
 */
#define STRAFE_PREVIEW_OFFSET_RANGE 200.f
#define STRAFE_PREVIEW_EDGE_INSET 14.f

/**
 * @brief The helper's own gradient constants, so the preview bands the way the
 * bar does rather than fading to transparent.
 * @see Cg_StrafeHelper_DrawGradient
 */
#define STRAFE_PREVIEW_GRADIENT_SEGMENTS 24
#define STRAFE_PREVIEW_GRADIENT_PEAK_FRACTION 0.7f

/**
 * @brief What an element that is not the one being edited draws at.
 * @details A fraction of its own computed alpha rather than an outline around
 * the one that is: the optimal and centre markers are two pixels wide, and an
 * outline would double them - which changes the very geometry the player is
 * looking at to judge a colour. Low enough that the edited element is
 * unambiguous, high enough that the others still say where they are.
 */
#define STRAFE_PREVIEW_ISOLATION_ALPHA 0.16f

/**
 * @brief The stand-in sample the readouts print.
 * @details Fixed rather than animated: the page is about how the readouts are
 * drawn, and a number that moves on its own makes a scale or offset change hard
 * to attribute.
 */
#define STRAFE_PREVIEW_SAMPLE_SPEED 712
#define STRAFE_PREVIEW_SAMPLE_SPEED_3D 728
#define STRAFE_PREVIEW_SAMPLE_MAX_SPEED 934

/**
 * @brief The readouts, in the order they are stacked and bound.
 */
typedef enum {
  StrafePreviewReadoutSpeed,
  StrafePreviewReadoutMaxSpeed,
  StrafePreviewReadoutVelocityAngle
} StrafePreviewReadout;

/**
 * @brief The bar styles `cg_race_strafe_helper_bar_style` names.
 * @see Cg_StrafeHelper_BarStyle
 */
typedef enum {
  StrafePreviewBarGradient,
  StrafePreviewBarSolid,
  StrafePreviewBarOutline,
  StrafePreviewBarMinimal
} StrafePreviewBarStyle;

#pragma mark - Cvar reading

/**
 * @brief Reads a cvar the strafe helper owns, without registering it.
 * @remarks The helper registers every `cg_race_strafe_helper_*` cvar in `Cg_InitStrafeHelper`, so
 * by the time this route is reachable they all exist. GetCvar is still the
 * right call rather than a cached pointer: the route is built once, and the
 * console can replace a value at any point behind it.
 */
static const cvar_t *previewCvar(const char *name) {
  return cgi.GetCvar(name);
}

static float previewValue(const char *name, const float fallback) {

  const cvar_t *var = previewCvar(name);
  return var ? var->value : fallback;
}

static int32_t previewInteger(const char *name, const int32_t fallback) {

  const cvar_t *var = previewCvar(name);
  return var ? var->integer : fallback;
}

static const char *previewString(const char *name, const char *fallback) {

  const cvar_t *var = previewCvar(name);
  return var && var->string ? var->string : fallback;
}

/**
 * @brief Parses an "R G B A" cvar the way the helper parses it, and applies the
 * same `cg_race_strafe_helper_alpha` multiplier the bar elements carry.
 * @see Cg_StrafeHelper_ParseColor
 */
static SDL_Color previewColor(const char *name, const SDL_Color fallback,
                              const bool helperAlpha) {

  SDL_Color color = fallback;

  int32_t r, g, b, a = 255;
  const char *string = previewString(name, NULL);
  if (string && sscanf(string, "%d %d %d %d", &r, &g, &b, &a) >= 3) {
    color.r = (uint8_t) Clampf(r, 0, 255);
    color.g = (uint8_t) Clampf(g, 0, 255);
    color.b = (uint8_t) Clampf(b, 0, 255);
    color.a = (uint8_t) Clampf(a, 0, 255);
  }

  if (helperAlpha) {
    color.a = (uint8_t) Clampf(color.a * Clampf(previewValue("cg_race_strafe_helper_alpha", 1.f), 0.f, 1.f),
                               0.f, 255.f);
  }

  return color;
}

/**
 * @brief Matches a cvar string against a name, case-insensitively.
 * @see Cg_StrafeHelper_StringEquals
 */
static bool previewStringEquals(const char *string, const char *name) {
  return string && !SDL_strcasecmp(string, name);
}

/**
 * @see Cg_StrafeHelper_BarStyle
 */
static StrafePreviewBarStyle previewBarStyle(void) {

  const char *style = previewString("cg_race_strafe_helper_bar_style", "gradient");

  if (previewStringEquals(style, "solid") || previewStringEquals(style, "0")) {
    return StrafePreviewBarSolid;
  }
  if (previewStringEquals(style, "outline") || previewStringEquals(style, "2")) {
    return StrafePreviewBarOutline;
  }
  if (previewStringEquals(style, "minimal") || previewStringEquals(style, "3")) {
    return StrafePreviewBarMinimal;
  }

  return StrafePreviewBarGradient;
}

/**
 * @brief Fades an element the Colours sub-tab is not editing.
 * @details A no-op whenever nothing is isolated, which is every sub-tab but
 * one - the preview is the page's readout first and the editor's second.
 */
static SDL_Color previewIsolate(const StrafeHelperPreview *self,
                                const sh_color_element_t element, SDL_Color color) {

  if (self->isolated != SH_COLOR_NONE && self->isolated != element) {
    color.a = (uint8_t) Clampf(color.a * STRAFE_PREVIEW_ISOLATION_ALPHA, 0.f, 255.f);
  }

  return color;
}

static SDL_Color previewLerpColor(const SDL_Color a, const SDL_Color b, const float t) {

  const float f = Clampf(t, 0.f, 1.f);

  return (SDL_Color) {
    .r = (uint8_t) (a.r + (b.r - a.r) * f),
    .g = (uint8_t) (a.g + (b.g - a.g) * f),
    .b = (uint8_t) (a.b + (b.b - a.b) * f),
    .a = (uint8_t) (a.a + (b.a - a.a) * f)
  };
}

#pragma mark - Geometry

/**
 * @brief Returns the preview pixels one screen pixel of `_y` offset is worth.
 */
static float previewOffsetScale(const SDL_Rect *frame) {

  const float room = Maxf(0.f, frame->h * 0.5f - STRAFE_PREVIEW_EDGE_INSET);

  return Minf(STRAFE_PREVIEW_DRAW_SCALE, room / STRAFE_PREVIEW_OFFSET_RANGE);
}

#pragma mark - Drawing

static void previewFill(Renderer *renderer, const float x, const float y,
                        const float w, const float h, const SDL_Color color) {

  if (w <= 0.f || h <= 0.f || color.a == 0) {
    return;
  }

  const SDL_Rect rect = MakeRect((int32_t) roundf(x), (int32_t) roundf(y),
                                 (int32_t) roundf(w), (int32_t) roundf(h));

  $(renderer, drawRectFilled, &rect, &color);
}

/**
 * @brief Draws one acceleration zone, in the style `cg_race_strafe_helper_bar_style` names.
 * @param peakX The optimal marker's centre, which the gradient peaks toward.
 */
static void previewDrawZone(Renderer *renderer, const float x, const float y,
                            const float w, const float h, const float peakX,
                            const SDL_Color accelerating, const SDL_Color optimal,
                            const StrafePreviewBarStyle style) {

  switch (style) {

    case StrafePreviewBarGradient: {

      // The helper bands its gradient rather than interpolating per pixel, and
      // peaks at a fraction of the way to the optimal colour - so a preview
      // that faded to transparent instead would misreport both the banding and
      // the colour the zone actually reaches.
      for (int32_t i = 0; i < STRAFE_PREVIEW_GRADIENT_SEGMENTS; i++) {

        const float segmentX = x + w * i / STRAFE_PREVIEW_GRADIENT_SEGMENTS;
        const float segmentEnd = x + w * (i + 1) / STRAFE_PREVIEW_GRADIENT_SEGMENTS;
        const float midpoint = (segmentX + segmentEnd) * 0.5f;

        float quality;
        if (midpoint <= peakX) {
          const float distance = peakX - x;
          quality = distance > 0.f ? (midpoint - x) / distance : 1.f;
        } else {
          const float distance = (x + w) - peakX;
          quality = distance > 0.f ? ((x + w) - midpoint) / distance : 1.f;
        }

        previewFill(renderer, segmentX, y, segmentEnd - segmentX, h,
                    previewLerpColor(accelerating, optimal,
                                     quality * STRAFE_PREVIEW_GRADIENT_PEAK_FRACTION));
      }
      break;
    }

    case StrafePreviewBarSolid:
      previewFill(renderer, x, y, w, h, accelerating);
      break;

    case StrafePreviewBarOutline: {
      const float thickness = Clampf(1.f, 1.f, h * 0.5f);
      previewFill(renderer, x, y, w, thickness, accelerating);
      previewFill(renderer, x, y + h - thickness, w, thickness, accelerating);
      previewFill(renderer, x, y, thickness, h, accelerating);
      previewFill(renderer, x + w - thickness, y, thickness, h, accelerating);
      break;
    }

    case StrafePreviewBarMinimal:
      // Minimal draws the markers and nothing behind them.
      break;
  }
}

/**
 * @see View::render(View *, Renderer *)
 */
static void render(View *self, Renderer *renderer) {

  super(View, self, render, renderer);

  const SDL_Rect frame = $(self, renderFrame);
  if (frame.w <= 0 || frame.h <= 0) {
    return;
  }

  const float centerX = frame.x + frame.w * 0.5f;
  const float centerY = frame.y + frame.h * 0.5f;

  // The dashed rule naming screen centre, which every `_y` offset is measured
  // from. Drawn here rather than as a subview because it is a dash pattern, and
  // the dialect has no dashed border.
  const SDL_Color rule = MakeColor(0x2f, 0x4a, 0x5e, 0xa6);
  const float ruleStart = centerX - frame.w * 0.22f;
  const float ruleEnd = centerX + frame.w * 0.22f;
  for (float x = ruleStart; x < ruleEnd; x += 9.f) {
    previewFill(renderer, x, centerY, Minf(4.f, ruleEnd - x), 1.f, rule);
  }

  if (!previewInteger("cg_race_strafe_helper_draw", 1)) {
    return;
  }

  const float offsetScale = previewOffsetScale(&frame);
  const float barY = centerY + previewValue("cg_race_strafe_helper_y", 100.f) * offsetScale;

  const float height = Maxf(3.f, Clampf(previewValue("cg_race_strafe_helper_height", 15.f), 0.f, 128.f) *
                            STRAFE_PREVIEW_DRAW_SCALE * 1.6f);
  const float zoneWidth = STRAFE_PREVIEW_ZONE_WIDTH *
                          Clampf(previewValue("cg_race_strafe_helper_scale", 1.5f), 0.25f, 8.f) *
                          STRAFE_PREVIEW_DRAW_SCALE;
  const float gap = STRAFE_PREVIEW_ZONE_GAP * STRAFE_PREVIEW_DRAW_SCALE;

  const StrafeHelperPreview *this = (StrafeHelperPreview *) self;

  const SDL_Color accelerating =
    previewIsolate(this, SH_COLOR_ACCELERATING,
                   previewColor("cg_race_strafe_helper_color_accelerating",
                                MakeColor(0, 128, 0, 128), true));
  const SDL_Color optimal =
    previewIsolate(this, SH_COLOR_OPTIMAL,
                   previewColor("cg_race_strafe_helper_color_optimal",
                                MakeColor(255, 215, 0, 255), true));

  const StrafePreviewBarStyle style = previewBarStyle();
  const float optimalWidth = Clampf(previewValue("cg_race_strafe_helper_optimal_width", 2.f), 1.f, 8.f);
  const bool outline = previewInteger("cg_race_strafe_helper_optimal_outline", 1) != 0;

  const float top = barY - height * 0.5f;
  const float leftZoneX = centerX - gap * 0.5f - zoneWidth;
  const float rightZoneX = centerX + gap * 0.5f;

  // The optimal angle sits at the inner edge of each zone - the point the
  // gradient peaks toward, and the point the marker straddles.
  previewDrawZone(renderer, leftZoneX, top, zoneWidth, height,
                  leftZoneX + zoneWidth, accelerating, optimal, style);
  previewDrawZone(renderer, rightZoneX, top, zoneWidth, height,
                  rightZoneX, accelerating, optimal, style);

  // The outline follows the marker it outlines: a faded marker inside a solid
  // black frame reads as a black bar, which is neither of the two colours the
  // player is comparing.
  const SDL_Color outlineColor =
    previewIsolate(this, SH_COLOR_OPTIMAL, MakeColor(0, 0, 0, 0xd9));
  const float markers[] = { leftZoneX + zoneWidth - optimalWidth, rightZoneX };
  for (size_t i = 0; i < lengthof(markers); i++) {
    if (outline) {
      previewFill(renderer, markers[i] - 1.f, top - 1.f,
                  optimalWidth + 2.f, height + 2.f, outlineColor);
    }
    previewFill(renderer, markers[i], top, optimalWidth, height, optimal);
  }

  if (previewInteger("cg_race_strafe_helper_centermarker", 1)) {

    const float width = Clampf(previewValue("cg_race_strafe_helper_center_width", 2.f), 1.f, 8.f);

    previewFill(renderer, centerX - width * 0.5f, top, width, height,
                previewIsolate(this, SH_COLOR_CENTER_MARKER,
                               previewColor("cg_race_strafe_helper_color_centermarker",
                                            MakeColor(255, 255, 255, 255), true)));
  }
}

#pragma mark - Readouts

/**
 * @brief The cvars behind one readout.
 */
typedef struct {
  const char *draw;
  const char *scale;
  const char *y;
  const char *shadow;
} StrafePreviewReadoutDescriptor;

static const StrafePreviewReadoutDescriptor previewReadouts[STRAFE_PREVIEW_READOUT_COUNT] = {
  [StrafePreviewReadoutSpeed] = { "cg_race_strafe_helper_ups", "cg_race_strafe_helper_ups_scale", "cg_race_strafe_helper_ups_y", "cg_race_strafe_helper_ups_shadow" },
  [StrafePreviewReadoutMaxSpeed] = { "cg_race_strafe_helper_max_speed", "cg_race_strafe_helper_max_speed_scale",
                                     "cg_race_strafe_helper_max_speed_y", "cg_race_strafe_helper_max_speed_shadow" },
  [StrafePreviewReadoutVelocityAngle] = { "cg_race_strafe_helper_velocity_angle", "cg_race_strafe_helper_velocity_angle_scale",
                                          "cg_race_strafe_helper_velocity_angle_y", "cg_race_strafe_helper_velocity_angle_shadow" }
};

/**
 * @brief Prints the speed readout the way `Cg_StrafeHelper_UpsText` prints it.
 */
static void previewSpeedText(char *buffer, const size_t size) {

  const int32_t speed = previewInteger("cg_race_strafe_helper_ups_3d", 0)
    ? STRAFE_PREVIEW_SAMPLE_SPEED_3D : STRAFE_PREVIEW_SAMPLE_SPEED;

  const char *format = previewString("cg_race_strafe_helper_ups_format", "plain");

  if (previewStringEquals(format, "suffix") || previewStringEquals(format, "ups") ||
      previewStringEquals(format, "1")) {
    snprintf(buffer, size, "%d ups", speed);
  } else if (previewStringEquals(format, "prefix") || previewStringEquals(format, "label") ||
             previewStringEquals(format, "2")) {
    snprintf(buffer, size, "UPS: %d", speed);
  } else {
    snprintf(buffer, size, "%d", speed);
  }
}

/**
 * @brief Returns the colour the speed readout resolves to for the sample.
 * @details Resolved for a still sample, which is what the preview has: the
 * modes that read a trend (`dynamic`, `gradient`) see no acceleration and
 * `strafing` sees no angle, so all three land on neutral, exactly as they would
 * on a player standing still. `threshold` is the one mode a still sample can
 * exercise - 712 clears its high mark - and `rainbow` is shown at one point of
 * a cycle the page cannot animate. So the preview reports which cvar is in
 * force and what it does at rest; it does not predict what it does at speed.
 * @see Cg_StrafeHelper_UpsColor
 */
static SDL_Color previewSpeedColor(void) {

  const char *mode = previewString("cg_race_strafe_helper_ups_color_mode", "dynamic");

  if (previewStringEquals(mode, "threshold")) {
    return previewColor("cg_race_strafe_helper_ups_color_gain", MakeColor(0, 255, 0, 255), false);
  }
  if (previewStringEquals(mode, "rainbow")) {
    return MakeColor(0xff, 0x5f, 0xa8, 0xff);
  }

  return previewColor("cg_race_strafe_helper_ups_color_neutral", MakeColor(255, 255, 255, 255), false);
}

/**
 * @brief Fills in one readout's text and colour, or hides it.
 */
static void previewBindReadout(StrafeHelperPreview *self, const StrafePreviewReadout readout) {

  Label *label = self->readouts[readout];
  const StrafePreviewReadoutDescriptor *descriptor = &previewReadouts[readout];

  const bool drawn = previewInteger("cg_race_strafe_helper_draw", 1) && previewInteger(descriptor->draw, 0);

  label->view.hidden = !drawn;
  if (!drawn) {
    return;
  }

  char text[64];
  SDL_Color color;

  switch (readout) {

    case StrafePreviewReadoutSpeed:
      previewSpeedText(text, sizeof(text));
      color = previewSpeedColor();
      break;

    case StrafePreviewReadoutMaxSpeed:
      snprintf(text, sizeof(text), "%d", STRAFE_PREVIEW_SAMPLE_MAX_SPEED);
      color = previewColor("cg_race_strafe_helper_max_speed_color", MakeColor(255, 255, 255, 255), false);
      break;

    case StrafePreviewReadoutVelocityAngle:
    default: {
      const char *format = previewString("cg_race_strafe_helper_velocity_angle_format", "diff");
      if (previewStringEquals(format, "absolute") || previewStringEquals(format, "abs") ||
          previewStringEquals(format, "1")) {
        q_strlcpy(text, "41.8", sizeof(text));
      } else {
        q_strlcpy(text, "+0.2", sizeof(text));
      }
      color = previewColor("cg_race_strafe_helper_velocity_angle_color", MakeColor(255, 255, 255, 255), false);
      break;
    }
  }

  if (q_strcmp(label->text->text ? label->text->text : "", text)) {
    $(label->text, setText, text);
  }

  // The helper draws its shadow as a second pass of the string in black, which
  // the dialect has no equivalent for; the page reports the cvar by dimming the
  // readout when the shadow is off - the same cue, one step removed.
  color.a = previewInteger(descriptor->shadow, 1) ? 255 : 150;
  label->text->color = color;

  const int32_t points = (int32_t) roundf(
    15.f * Clampf(previewValue(descriptor->scale, 1.f), 0.25f, 8.f) *
    STRAFE_PREVIEW_DRAW_SCALE * 1.6f);

  if (points != self->readoutPointSize[readout]) {

    Font *font = $$(Font, cachedFont, "Coda", points, FontStyleRegular);
    if (font) {
      $(label->text, setFont, font);
      self->readoutPointSize[readout] = points;
    }
  }
}

#pragma mark - View

/**
 * @see View::init(View *)
 */
static View *init(View *self) {
  return (View *) $((StrafeHelperPreview *) self, initWithFrame, NULL);
}

/**
 * @brief Places a subview, and marks it for layout when it actually moved.
 * @details A Label positions its own Text against its own bounds, and it only
 * does so on a pass where it carries `needsLayout` - assigning the frame alone
 * leaves the string where the previous size put it. Guarded on a change because
 * this runs on every layout pass, and the route drives those from pointer
 * motion.
 */
static void setFrame(View *view, const SDL_Rect frame) {

  if (view->frame.x == frame.x && view->frame.y == frame.y &&
      view->frame.w == frame.w && view->frame.h == frame.h) {
    return;
  }

  view->frame = frame;
  view->needsLayout = true;
}

/**
 * @see View::layoutSubviews(View *)
 */
static void layoutSubviews(View *self) {

  super(View, self, layoutSubviews);

  StrafeHelperPreview *this = (StrafeHelperPreview *) self;

  const SDL_Rect bounds = $(self, bounds);
  const float offsetScale = previewOffsetScale(&bounds);

  for (size_t i = 0; i < STRAFE_PREVIEW_READOUT_COUNT; i++) {

    View *label = (View *) this->readouts[i];
    if (label->hidden) {
      continue;
    }

    // The readouts are placed rather than stacked: each one carries its own
    // `_y`, and two of them at the same offset should overlap in the preview
    // exactly as they would overlap on screen.
    const SDL_Size size = $(label, sizeThatFits);
    const float y = bounds.h * 0.5f + previewValue(previewReadouts[i].y, 0.f) * offsetScale;

    setFrame(label, MakeRect((int32_t) roundf(bounds.w * 0.5f - size.w * 0.5f),
                             (int32_t) roundf(y - size.h * 0.5f), size.w, size.h));
  }

  const SDL_Size centerSize = $((View *) this->centerLabel, sizeThatFits);
  setFrame((View *) this->centerLabel,
           MakeRect(bounds.w - centerSize.w, 0, centerSize.w, centerSize.h));
}

/**
 * @see View::updateBindings(View *)
 */
static void updateBindings(View *self) {

  super(View, self, updateBindings);

  StrafeHelperPreview *this = (StrafeHelperPreview *) self;

  for (size_t i = 0; i < STRAFE_PREVIEW_READOUT_COUNT; i++) {
    previewBindReadout(this, (StrafePreviewReadout) i);
  }

  self->needsLayout = true;
}

#pragma mark - StrafeHelperPreview

/**
 * @fn StrafeHelperPreview *StrafeHelperPreview::initWithFrame(StrafeHelperPreview *self, const SDL_Rect *frame)
 * @memberof StrafeHelperPreview
 */
static StrafeHelperPreview *initWithFrame(StrafeHelperPreview *self, const SDL_Rect *frame) {

  self = (StrafeHelperPreview *) super(View, self, initWithFrame, frame);
  if (self) {

    View *view = (View *) self;

    $(view, addClassName, "strafePreview");
    view->clipsSubviews = true;

    for (size_t i = 0; i < STRAFE_PREVIEW_READOUT_COUNT; i++) {

      Label *label = $(alloc(Label), initWithText, "", NULL);
      assert(label);

      $((View *) label, addClassName, "strafePreviewReadout");
      ((View *) label)->hidden = true;

      $(view, addSubview, (View *) label);
      self->readouts[i] = label;
      release(label);
    }

    Label *centerLabel = $(alloc(Label), initWithText, "screen centre", NULL);
    assert(centerLabel);

    $((View *) centerLabel, addClassName, "strafePreviewCenterLabel");

    $(view, addSubview, (View *) centerLabel);
    self->centerLabel = centerLabel;
    release(centerLabel);

    self->isolated = SH_COLOR_NONE;
  }

  return self;
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ViewInterface *) clazz->interface)->init = init;
  ((ViewInterface *) clazz->interface)->layoutSubviews = layoutSubviews;
  ((ViewInterface *) clazz->interface)->render = render;
  ((ViewInterface *) clazz->interface)->updateBindings = updateBindings;

  ((StrafeHelperPreviewInterface *) clazz->interface)->initWithFrame = initWithFrame;
}

/**
 * @fn Class *StrafeHelperPreview::_StrafeHelperPreview(void)
 * @memberof StrafeHelperPreview
 */
Class *_StrafeHelperPreview(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "StrafeHelperPreview",
      .superclass = _View(),
      .instanceSize = sizeof(StrafeHelperPreview),
      .interfaceOffset = offsetof(StrafeHelperPreview, interface),
      .interfaceSize = sizeof(StrafeHelperPreviewInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
