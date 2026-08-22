/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
#include "cg_strafe_helper.h"
#include "cg_strafe_helper_math.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define SH_GRADIENT_MAX_SEGMENTS 64
#define SH_GRADIENT_TARGET_SEGMENT_WIDTH 6.f
#define SH_GRADIENT_PEAK_FRACTION 0.7f
#define SH_UPS_THRESHOLD_LOW 400.f
#define SH_UPS_THRESHOLD_HIGH 600.f

#define CG_RACE_STRAFE_HELPER_CVAR_PREFIX "cg_race_strafe_helper_"
#define CG_RACE_STRAFE_HELPER_LEGACY_PREFIX "sh_"

typedef enum {
  SH_BAR_STYLE_SOLID,
  SH_BAR_STYLE_GRADIENT,
  SH_BAR_STYLE_OUTLINE,
  SH_BAR_STYLE_MINIMAL
} sh_bar_style_t;

typedef struct {
  bool center;
  bool center_marker;
  float scale;
  float height;
  float y;
} cg_strafe_helper_params_t;

static cvar_t *sh_draw;
static cvar_t *sh_center;
static cvar_t *sh_centermarker;
static cvar_t *sh_height;
static cvar_t *sh_scale;
static cvar_t *sh_y;
static cvar_t *sh_ups;
static cvar_t *sh_ups_scale;
static cvar_t *sh_ups_y;
static cvar_t *sh_ups_shadow;
static cvar_t *sh_ups_hide_zero;
static cvar_t *sh_ups_color_mode;
static cvar_t *sh_ups_color_gain;
static cvar_t *sh_ups_color_loss;
static cvar_t *sh_ups_color_neutral;
static cvar_t *sh_ups_format;
static cvar_t *sh_ups_3d;
static cvar_t *sh_alpha;
static cvar_t *sh_bar_style;
static cvar_t *sh_center_width;
static cvar_t *sh_optimal_width;
static cvar_t *sh_optimal_outline;
static cvar_t *sh_color_accelerating;
static cvar_t *sh_color_optimal;
static cvar_t *sh_color_centermarker;
static cvar_t *sh_max_speed;
static cvar_t *sh_max_speed_scale;
static cvar_t *sh_max_speed_y;
static cvar_t *sh_max_speed_shadow;
static cvar_t *sh_max_speed_color;
static cvar_t *sh_velocity_angle;
static cvar_t *sh_velocity_angle_scale;
static cvar_t *sh_velocity_angle_y;
static cvar_t *sh_velocity_angle_shadow;
static cvar_t *sh_velocity_angle_color;
static cvar_t *sh_velocity_angle_format;

static cg_strafe_helper_state_t cg_strafe_helper;
static cg_strafe_helper_velocity_sample_t cg_strafe_helper_velocity;
static cg_strafe_helper_dynamic_trend_t cg_strafe_helper_dynamic_trend;

/**
 * @brief Registers one canonical strafe-helper cvar and migrates its legacy value.
 * @details A pre-existing canonical value always wins. Legacy names are read once
 * and are not registered or kept synchronized after initialization.
 */
static cvar_t *Cg_StrafeHelper_AddCvar(const char *suffix, const char *default_value,
                                       const char *description) {
  char name[MAX_QPATH];
  char legacy_name[MAX_QPATH];

  q_snprintf(name, sizeof(name), "%s%s", CG_RACE_STRAFE_HELPER_CVAR_PREFIX, suffix);
  q_snprintf(legacy_name, sizeof(legacy_name), "%s%s",
             CG_RACE_STRAFE_HELPER_LEGACY_PREFIX, suffix);

  const cvar_t *canonical = cgi.GetCvar(name);
  const cvar_t *legacy = cgi.GetCvar(legacy_name);
  cvar_t *var = cgi.AddCvar(name, default_value, CVAR_ARCHIVE, description);

  if (!canonical && legacy && legacy->string && var) {
    cgi.SetCvarString(name, legacy->string);
  }

  return var;
}

static bool Cg_StrafeHelper_StringEquals(const char *value, const char *expected) {
  return value && expected && !q_strcasecmp(value, expected);
}

static float Cg_StrafeHelper_CvarValue(const cvar_t *cvar, const float fallback) {
  return cvar ? cvar->value : fallback;
}

static int32_t Cg_StrafeHelper_CvarInteger(const cvar_t *cvar, const int32_t fallback) {
  return cvar ? cvar->integer : fallback;
}

static const char *Cg_StrafeHelper_CvarString(const cvar_t *cvar, const char *fallback) {
  return cvar && cvar->string ? cvar->string : fallback;
}

static color_t Cg_StrafeHelper_ParseColor(const cvar_t *cvar, const color_t fallback,
                                          const bool helper_alpha) {
  int32_t r = 255, g = 255, b = 255, a = 255;

  const char *string = Cg_StrafeHelper_CvarString(cvar, NULL);
  if (!string || sscanf(string, "%d %d %d %d", &r, &g, &b, &a) < 3) {
    color_t color = fallback;
    if (helper_alpha) {
      color.a *= Clampf(Cg_StrafeHelper_CvarValue(sh_alpha, 1.f), 0.f, 1.f);
    }
    return color;
  }

  color_t color = Color4f(Clampf(r, 0, 255) / 255.f,
                          Clampf(g, 0, 255) / 255.f,
                          Clampf(b, 0, 255) / 255.f,
                          Clampf(a, 0, 255) / 255.f);

  if (helper_alpha) {
    color.a *= Clampf(Cg_StrafeHelper_CvarValue(sh_alpha, 1.f), 0.f, 1.f);
  }

  return color;
}

static color_t Cg_StrafeHelper_LerpColor(const color_t a, const color_t b, const float t) {
  const float f = Clampf(t, 0.f, 1.f);
  return Color4f(a.r + (b.r - a.r) * f,
                 a.g + (b.g - a.g) * f,
                 a.b + (b.b - a.b) * f,
                 a.a + (b.a - a.a) * f);
}

static color_t Cg_StrafeHelper_ElementColor(const sh_color_element_t element) {

  switch (element) {
    case SH_COLOR_ACCELERATING:
      return Cg_StrafeHelper_ParseColor(sh_color_accelerating,
                                        Color4f(0.f, 0.5f, 0.f, 0.5f), true);
    case SH_COLOR_OPTIMAL:
      return Cg_StrafeHelper_ParseColor(sh_color_optimal,
                                        Color4f(1.f, 0.843f, 0.f, 1.f), true);
    case SH_COLOR_CENTER_MARKER:
    default:
      return Cg_StrafeHelper_ParseColor(sh_color_centermarker,
                                        color_white, true);
  }
}

static void Cg_StrafeHelper_Fill(float x, const float y, float w, const float h,
                                 const color_t color) {
  if (w < 0.f) {
    x += w;
    w = -w;
  }

  if (w <= 0.f || h <= 0.f || color.a <= 0.f) {
    return;
  }

  cgi.Draw2DFill((int32_t) roundf(x), (int32_t) roundf(y),
                 (int32_t) roundf(w), (int32_t) roundf(h), color);
}

static void Cg_StrafeHelper_DrawElement(const float x, const float y, const float w,
                                        const float h, const sh_color_element_t element) {
  Cg_StrafeHelper_Fill(x, y, w, h, Cg_StrafeHelper_ElementColor(element));
}

static int32_t Cg_StrafeHelper_GradientSegmentCount(const float width) {
  const int32_t iw = Maxf(1.f, roundf(width));
  const int32_t count = ceilf(width / SH_GRADIENT_TARGET_SEGMENT_WIDTH);
  return Clampf(count, 1, Minf(iw, SH_GRADIENT_MAX_SEGMENTS));
}

static void Cg_StrafeHelper_DrawGradient(float x, const float y, float w, const float h,
                                         const float peak_x,
                                         const sh_color_element_t edge_element,
                                         const sh_color_element_t peak_element) {
  if (w < 0.f) {
    x += w;
    w = -w;
  }

  if (w <= 0.f || h <= 0.f) {
    return;
  }

  const int32_t segments = Cg_StrafeHelper_GradientSegmentCount(w);
  const float start_x = x;
  const float end_x = x + w;
  const float clamped_peak = Clampf(peak_x, start_x, end_x);
  const color_t edge_color = Cg_StrafeHelper_ElementColor(edge_element);
  const color_t peak_color = Cg_StrafeHelper_ElementColor(peak_element);

  for (int32_t i = 0; i < segments; i++) {
    const float segment_x = x + w * i / segments;
    const float segment_end = x + w * (i + 1) / segments;
    const float segment_width = segment_end - segment_x;
    const float midpoint = segment_x + segment_width * 0.5f;
    float quality;

    if (midpoint <= clamped_peak) {
      const float distance = clamped_peak - start_x;
      quality = distance > 0.f ? (midpoint - start_x) / distance : 1.f;
    } else {
      const float distance = end_x - clamped_peak;
      quality = distance > 0.f ? (end_x - midpoint) / distance : 1.f;
    }

    Cg_StrafeHelper_Fill(segment_x, y, segment_width, h,
                         Cg_StrafeHelper_LerpColor(edge_color, peak_color,
                                                   quality * SH_GRADIENT_PEAK_FRACTION));
  }
}

static void Cg_StrafeHelper_DrawOutline(float x, const float y, float w, const float h,
                                        const float thickness,
                                        const sh_color_element_t element) {
  if (w < 0.f) {
    x += w;
    w = -w;
  }

  if (w <= 0.f || h <= 0.f || thickness <= 0.f) {
    return;
  }

  const float t = Clampf(thickness, 1.f, h * 0.5f);

  Cg_StrafeHelper_DrawElement(x, y, w, t, element);
  Cg_StrafeHelper_DrawElement(x, y + h - t, w, t, element);
  Cg_StrafeHelper_DrawElement(x, y, t, h, element);
  Cg_StrafeHelper_DrawElement(x + w - t, y, t, h, element);
}

static sh_bar_style_t Cg_StrafeHelper_BarStyle(void) {
  const char *style = Cg_StrafeHelper_CvarString(sh_bar_style, "gradient");

  if (Cg_StrafeHelper_StringEquals(style, "solid") ||
      Cg_StrafeHelper_StringEquals(style, "0")) {
    return SH_BAR_STYLE_SOLID;
  }
  if (Cg_StrafeHelper_StringEquals(style, "outline") ||
      Cg_StrafeHelper_StringEquals(style, "2")) {
    return SH_BAR_STYLE_OUTLINE;
  }
  if (Cg_StrafeHelper_StringEquals(style, "minimal") ||
      Cg_StrafeHelper_StringEquals(style, "3")) {
    return SH_BAR_STYLE_MINIMAL;
  }

  return SH_BAR_STYLE_GRADIENT;
}

static void Cg_StrafeHelper_DrawAccelerationZone(const float accel_start,
                                                 const float accel_end,
                                                 const float y,
                                                 const float h,
                                                 const float optimal_x,
                                                 const float optimal_width) {
  const bool optimal_outline = Cg_StrafeHelper_CvarInteger(sh_optimal_outline, 1) != 0;
  const float marker_gap_half_width = optimal_outline
                                      ? optimal_width * 0.5f + 1.f
                                      : optimal_width * 0.5f;
  const float gap_start = Clampf(optimal_x - marker_gap_half_width, accel_start, accel_end);
  const float gap_end = Clampf(optimal_x + marker_gap_half_width, accel_start, accel_end);

  switch (Cg_StrafeHelper_BarStyle()) {
    case SH_BAR_STYLE_GRADIENT:
      if (gap_start > accel_start) {
        Cg_StrafeHelper_DrawGradient(accel_start, y, gap_start - accel_start, h,
                                     optimal_x, SH_COLOR_ACCELERATING, SH_COLOR_OPTIMAL);
      }
      if (gap_end < accel_end) {
        Cg_StrafeHelper_DrawGradient(gap_end, y, accel_end - gap_end, h,
                                     optimal_x, SH_COLOR_ACCELERATING, SH_COLOR_OPTIMAL);
      }
      break;
    case SH_BAR_STYLE_SOLID:
      if (gap_start > accel_start) {
        Cg_StrafeHelper_DrawElement(accel_start, y, gap_start - accel_start, h,
                                    SH_COLOR_ACCELERATING);
      }
      if (gap_end < accel_end) {
        Cg_StrafeHelper_DrawElement(gap_end, y, accel_end - gap_end, h,
                                    SH_COLOR_ACCELERATING);
      }
      break;
    case SH_BAR_STYLE_OUTLINE:
      if (gap_start > accel_start) {
        Cg_StrafeHelper_DrawOutline(accel_start, y, gap_start - accel_start, h,
                                    1.f, SH_COLOR_ACCELERATING);
      }
      if (gap_end < accel_end) {
        Cg_StrafeHelper_DrawOutline(gap_end, y, accel_end - gap_end, h,
                                    1.f, SH_COLOR_ACCELERATING);
      }
      break;
    case SH_BAR_STYLE_MINIMAL:
      break;
  }
}

static float Cg_StrafeHelper_AngleDiffToPixels(const float angle_difference,
                                               const float scale,
                                               const float hud_width) {
  return angle_difference * (hud_width * 0.5f) * scale / (float) M_PI;
}

static float Cg_StrafeHelper_AngleToPixel(const float angle, const float scale,
                                          const float hud_width) {
  return hud_width * 0.5f - 0.5f +
         Cg_StrafeHelper_AngleDiffToPixels(angle, scale, hud_width);
}

static bool Cg_StrafeHelper_HasData(void) {
  return cg_strafe_helper.raw.velocity_norm > CG_STRAFE_HELPER_EPSILON;
}

void Cg_ClearStrafeHelper(void) {
  Cg_StrafeHelper_Clear(&cg_strafe_helper);
  Cg_StrafeHelper_ClearVelocitySample(&cg_strafe_helper_velocity);
}

static const char *Cg_StrafeHelper_FontForScale(const float scale) {
  if (scale <= 0.75f) {
    return "small";
  }
  if (scale >= 1.25f) {
    return "large";
  }
  return "medium";
}

static void Cg_StrafeHelper_DrawCenteredText(const char *text, const float y,
                                             const cvar_t *scale_cvar,
                                             const bool shadow,
                                             const color_t color) {
  int32_t cw, ch;
  const float scale = Clampf(Cg_StrafeHelper_CvarValue(scale_cvar, 1.f), 0.25f, 8.f);
  cgi.BindFont(Cg_StrafeHelper_FontForScale(scale), &cw, &ch);

  const int32_t x = (cgi.context->w - cgi.StringWidth(text)) / 2;
  const int32_t iy = (int32_t) roundf((cgi.context->h - ch) * 0.5f + y);

  if (shadow) {
    cgi.Draw2DString(x + 1, iy + 1, text, Color4f(0.f, 0.f, 0.f, color.a * 0.75f));
  }

  cgi.Draw2DString(x, iy, text, color);
  cgi.BindFont(NULL, NULL, NULL);
}

static color_t Cg_StrafeHelper_RainbowColor(void) {
  const float time = (float) cgi.client->unclamped_time * 0.006f;

  return Color4f(0.5f + sinf(time) * 0.5f,
                 0.5f + sinf(time + 2.094395f) * 0.5f,
                 0.5f + sinf(time + 4.188790f) * 0.5f,
                 1.f);
}

static color_t Cg_StrafeHelper_UpsColor(const float speed) {
  static float previous_speed = -1.f;
  static float smoothed_accel;

  if (previous_speed < 0.f) {
    previous_speed = speed;
  }

  const char *mode = Cg_StrafeHelper_CvarString(sh_ups_color_mode, "dynamic");
  const color_t gain = Cg_StrafeHelper_ParseColor(sh_ups_color_gain, color_green, false);
  const color_t loss = Cg_StrafeHelper_ParseColor(sh_ups_color_loss, color_red, false);
  const color_t neutral = Cg_StrafeHelper_ParseColor(sh_ups_color_neutral, color_white, false);
  color_t color;

  if (Cg_StrafeHelper_StringEquals(mode, "rainbow")) {
    color = Cg_StrafeHelper_RainbowColor();
  } else if (Cg_StrafeHelper_StringEquals(mode, "static")) {
    color = neutral;
  } else if (Cg_StrafeHelper_StringEquals(mode, "threshold")) {
    if (speed >= SH_UPS_THRESHOLD_HIGH) {
      color = gain;
    } else if (speed >= SH_UPS_THRESHOLD_LOW) {
      color = neutral;
    } else {
      color = loss;
    }
  } else if (Cg_StrafeHelper_StringEquals(mode, "gradient")) {
    const float frametime = Maxf(cgi.client->frame_msec / 1000.f, 0.001f);
    const float current_accel = (speed - previous_speed) / frametime;
    smoothed_accel += 0.15f * (current_accel - smoothed_accel);

    const float factor = Clampf(smoothed_accel / 1000.f, -1.f, 1.f);
    color = factor > 0.f
            ? Cg_StrafeHelper_LerpColor(neutral, gain, factor)
            : Cg_StrafeHelper_LerpColor(neutral, loss, -factor);
  } else if (Cg_StrafeHelper_StringEquals(mode, "strafing")) {
    if (cg_strafe_helper.raw.velocity_norm < 10.f) {
      color = neutral;
    } else {
      const float diff = fabsf(cg_strafe_helper.visual.angle_diff);
      if (diff <= 0.01f) {
        color = gain;
      } else if (diff <= 0.08f) {
        color = Cg_StrafeHelper_LerpColor(gain, neutral, (diff - 0.01f) / 0.07f);
      } else if (diff <= 0.2f) {
        color = Cg_StrafeHelper_LerpColor(neutral, loss, (diff - 0.08f) / 0.12f);
      } else {
        color = loss;
      }
    }
  } else {
    switch (Cg_StrafeHelper_UpdateDynamicTrend(&cg_strafe_helper_dynamic_trend,
                                               speed,
                                               cgi.client->unclamped_time)) {
      case CG_STRAFE_HELPER_SPEED_GAIN:
        color = gain;
        break;
      case CG_STRAFE_HELPER_SPEED_LOSS:
        color = loss;
        break;
      case CG_STRAFE_HELPER_SPEED_NEUTRAL:
      default:
        color = neutral;
        break;
    }
  }

  previous_speed = speed;
  return color;
}

static bool Cg_StrafeHelper_UpsText(const float speed, char *buffer, const size_t size) {
  const int32_t rounded_speed = (int32_t) roundf(speed);
  const char *format = Cg_StrafeHelper_CvarString(sh_ups_format, "plain");

  if (Cg_StrafeHelper_CvarInteger(sh_ups_hide_zero, 1) && rounded_speed == 0) {
    *buffer = '\0';
    return false;
  }

  if (Cg_StrafeHelper_StringEquals(format, "suffix") ||
      Cg_StrafeHelper_StringEquals(format, "ups") ||
      Cg_StrafeHelper_StringEquals(format, "1")) {
    q_snprintf(buffer, size, "%d ups", rounded_speed);
  } else if (Cg_StrafeHelper_StringEquals(format, "prefix") ||
             Cg_StrafeHelper_StringEquals(format, "label") ||
             Cg_StrafeHelper_StringEquals(format, "2")) {
    q_snprintf(buffer, size, "UPS: %d", rounded_speed);
  } else {
    q_snprintf(buffer, size, "%d", rounded_speed);
  }

  return *buffer != '\0';
}

static float Cg_StrafeHelper_UpsSpeed(void) {
  vec3_t velocity = cg_strafe_helper_velocity.valid
                    ? cg_strafe_helper_velocity.velocity
                    : cgi.client->frame.ps.pm_state.velocity;

  if (!Cg_StrafeHelper_CvarInteger(sh_ups_3d, 0)) {
    velocity.z = 0.f;
  }

  return Vec3_Length(velocity);
}

bool Cg_StrafeHelper_OwnsSpeedReadout(void) {
  return Cg_StrafeHelper_CvarInteger(sh_ups, 1) != 0;
}

static void Cg_StrafeHelper_DrawUps(void) {
  char text[MAX_STRING_CHARS];

  if (!Cg_StrafeHelper_OwnsSpeedReadout()) {
    return;
  }

  const float speed = Cg_StrafeHelper_UpsSpeed();
  if (!Cg_StrafeHelper_UpsText(speed, text, sizeof(text))) {
    return;
  }

  Cg_StrafeHelper_DrawCenteredText(text,
                                   Clampf(Cg_StrafeHelper_CvarValue(sh_ups_y, -5.f),
                                          -cgi.context->h, cgi.context->h),
                                   sh_ups_scale,
                                   Cg_StrafeHelper_CvarInteger(sh_ups_shadow, 1) != 0,
                                   Cg_StrafeHelper_UpsColor(speed));
}

static void Cg_StrafeHelper_DrawMaxSpeed(void) {
  char text[MAX_STRING_CHARS];

  if (!Cg_StrafeHelper_CvarInteger(sh_max_speed, 0)) {
    return;
  }

  if (!Cg_StrafeHelper_HasData() || cg_strafe_helper.visual.speed_ceiling <= 0.f) {
    return;
  }

  q_snprintf(text, sizeof(text), "%d", (int32_t) roundf(cg_strafe_helper.visual.speed_ceiling));

  Cg_StrafeHelper_DrawCenteredText(text,
                                   Clampf(Cg_StrafeHelper_CvarValue(sh_max_speed_y, -20.f),
                                          -cgi.context->h, cgi.context->h),
                                   sh_max_speed_scale,
                                   Cg_StrafeHelper_CvarInteger(sh_max_speed_shadow, 1) != 0,
                                   Cg_StrafeHelper_ParseColor(sh_max_speed_color,
                                                              color_white, false));
}

static void Cg_StrafeHelper_DrawVelocityAngle(void) {
  char text[MAX_STRING_CHARS];

  if (!Cg_StrafeHelper_CvarInteger(sh_velocity_angle, 0)) {
    return;
  }

  if (!Cg_StrafeHelper_HasData()) {
    return;
  }

  const char *format = Cg_StrafeHelper_CvarString(sh_velocity_angle_format, "diff");

  if (Cg_StrafeHelper_StringEquals(format, "absolute") ||
      Cg_StrafeHelper_StringEquals(format, "abs") ||
      Cg_StrafeHelper_StringEquals(format, "1")) {
    float yaw = cg_strafe_helper.visual.velocity_yaw;
    if (yaw < 0.f) {
      yaw += 360.f;
    }
    q_snprintf(text, sizeof(text), "%.1f", yaw);
  } else if (Cg_StrafeHelper_StringEquals(format, "both") ||
             Cg_StrafeHelper_StringEquals(format, "2")) {
    float velocity_yaw = cg_strafe_helper.visual.velocity_yaw;
    float view_yaw = cg_strafe_helper.visual.view_yaw;
    if (velocity_yaw < 0.f) {
      velocity_yaw += 360.f;
    }
    if (view_yaw < 0.f) {
      view_yaw += 360.f;
    }
    q_snprintf(text, sizeof(text), "M:%.1f V:%.1f", velocity_yaw, view_yaw);
  } else {
    q_snprintf(text, sizeof(text), "%+.1f", cg_strafe_helper.visual.velocity_angle_diff);
  }

  Cg_StrafeHelper_DrawCenteredText(text,
                                   Clampf(Cg_StrafeHelper_CvarValue(sh_velocity_angle_y, -35.f),
                                          -cgi.context->h, cgi.context->h),
                                   sh_velocity_angle_scale,
                                   Cg_StrafeHelper_CvarInteger(sh_velocity_angle_shadow, 1) != 0,
                                   Cg_StrafeHelper_ParseColor(sh_velocity_angle_color,
                                                              color_white, false));
}

static void Cg_StrafeHelper_DrawBar(const cg_strafe_helper_params_t *params) {
  float angle_x, angle_width;

  if (!Cg_StrafeHelper_HasData() || params->height <= 0.f) {
    return;
  }

  const float hud_width = cgi.context->w;
  const float hud_height = cgi.context->h;
  const float y = (hud_height - params->height) * 0.5f + params->y;
  const float center_width = Clampf(Cg_StrafeHelper_CvarValue(sh_center_width, 2.f),
                                    0.1f, 5.f);
  const float optimal_width = Clampf(Cg_StrafeHelper_CvarValue(sh_optimal_width, 2.f),
                                     0.1f, 5.f);
  const float offset = params->center ? -cg_strafe_helper.visual.angle_current : 0.f;
  const cg_strafe_helper_sample_t *sample = &cg_strafe_helper.visual;

  if (sample->angle_minimum < sample->angle_maximum) {
    angle_x = sample->angle_minimum + offset;
    angle_width = sample->angle_maximum - sample->angle_minimum;
  } else {
    angle_x = sample->angle_maximum + offset;
    angle_width = sample->angle_minimum - sample->angle_maximum;
  }

  const float accel_x = Cg_StrafeHelper_AngleToPixel(angle_x, params->scale, hud_width);
  const float accel_width = Cg_StrafeHelper_AngleDiffToPixels(angle_width,
                                                              params->scale, hud_width);
  const float optimal_x = Cg_StrafeHelper_AngleToPixel(sample->angle_optimal + offset,
                                                       params->scale, hud_width);
  float accel_start = accel_x;
  float accel_end = accel_x + accel_width;

  if (accel_start > accel_end) {
    const float swap = accel_start;
    accel_start = accel_end;
    accel_end = swap;
  }

  Cg_StrafeHelper_DrawAccelerationZone(accel_start, accel_end, y, params->height,
                                       optimal_x, optimal_width);

  if (Cg_StrafeHelper_CvarInteger(sh_optimal_outline, 1)) {
    Cg_StrafeHelper_DrawOutline(optimal_x - optimal_width * 0.5f, y,
                                optimal_width, params->height, 1.f,
                                SH_COLOR_OPTIMAL);
  } else {
    Cg_StrafeHelper_DrawElement(optimal_x - optimal_width * 0.5f, y,
                                optimal_width, params->height,
                                SH_COLOR_OPTIMAL);
  }

  if (params->center_marker) {
    const float current_x = Cg_StrafeHelper_AngleToPixel(sample->angle_current + offset,
                                                        params->scale, hud_width);
    Cg_StrafeHelper_DrawElement(current_x - center_width * 0.5f,
                                y + params->height * 0.5f,
                                center_width, params->height * 0.5f,
                                SH_COLOR_CENTER_MARKER);
  }
}

static bool Cg_StrafeHelper_EvaluateUpdate(const race_strafe_sample_t *update,
                                           cg_strafe_helper_state_t *state) {
  if (!update || !update->active) {
    return false;
  }

  return Cg_StrafeHelper_Update(state,
                                update->forward,
                                update->velocity,
                                update->wishdir,
                                update->wishspeed,
                                update->accel,
                                update->frametime,
                                update->view_yaw);
}

void Cg_SetStrafeHelperVelocity(const vec3_t velocity) {
  Cg_StrafeHelper_SetVelocitySample(&cg_strafe_helper_velocity, velocity);
}

void Cg_UpdateStrafeHelper(const race_strafe_sample_t *update) {

  if (!Cg_StrafeHelper_EvaluateUpdate(update, &cg_strafe_helper)) {
    Cg_ClearStrafeHelper();
    return;
  }

  Cg_SetStrafeHelperVelocity(update->velocity);
}

void Cg_DrawStrafeHelper(const player_state_t *ps) {
  if (!ps || !cg_draw_hud->integer || !ps->stats[STAT_TIME] ||
      editor->value || cgi.GetKeyDest() != KEY_GAME ||
      ps->pm_state.type == PM_DEAD ||
      (ps->stats[STAT_SPECTATOR] && !ps->stats[STAT_CHASE]) ||
      ps->stats[STAT_SCORES]) {
    return;
  }

  const cg_strafe_helper_params_t params = {
    .center = Cg_StrafeHelper_CvarInteger(sh_center, 1) != 0,
    .center_marker = Cg_StrafeHelper_CvarInteger(sh_centermarker, 1) != 0,
    .height = Clampf(Cg_StrafeHelper_CvarValue(sh_height, 15.f), 0.f, 128.f),
    .scale = Clampf(Cg_StrafeHelper_CvarValue(sh_scale, 1.5f), 0.25f, 8.f),
    .y = Clampf(Cg_StrafeHelper_CvarValue(sh_y, 100.f), -cgi.context->h, cgi.context->h)
  };

  if (Cg_StrafeHelper_CvarInteger(sh_draw, 1)) {
    Cg_StrafeHelper_DrawBar(&params);
  }

  Cg_StrafeHelper_DrawUps();
  Cg_StrafeHelper_DrawMaxSpeed();
  Cg_StrafeHelper_DrawVelocityAngle();
}

void Cg_InitStrafeHelper(void) {

  sh_draw = Cg_StrafeHelper_AddCvar("draw", "1", "Draw the race strafe helper bar.");
  sh_center = Cg_StrafeHelper_AddCvar("center", "1", "Center the strafe helper on the current angle.");
  sh_centermarker = Cg_StrafeHelper_AddCvar("centermarker", "1", "Draw the strafe helper center marker.");
  sh_height = Cg_StrafeHelper_AddCvar("height", "15", "Height of the strafe helper bar.");
  sh_scale = Cg_StrafeHelper_AddCvar("scale", "1.500000", "Horizontal scale of the strafe helper bar.");
  sh_y = Cg_StrafeHelper_AddCvar("y", "100", "Vertical strafe helper offset from screen center.");
  sh_ups = Cg_StrafeHelper_AddCvar("ups", "1",
    "Draw the strafe helper speed readout. Replaces the Race HUD's own readout under the crosshair while enabled.");
  sh_ups_scale = Cg_StrafeHelper_AddCvar("ups_scale", "1", "Scale of the strafe helper speed readout.");
  sh_ups_y = Cg_StrafeHelper_AddCvar("ups_y", "-5", "Vertical speed readout offset from screen center.");
  sh_ups_shadow = Cg_StrafeHelper_AddCvar("ups_shadow", "1", "Draw a shadow behind the speed readout.");
  sh_ups_hide_zero = Cg_StrafeHelper_AddCvar("ups_hide_zero", "1", "Hide the speed readout at zero speed.");
  sh_ups_color_mode = Cg_StrafeHelper_AddCvar("ups_color_mode", "dynamic", "Speed readout color mode.");
  sh_ups_color_gain = Cg_StrafeHelper_AddCvar("ups_color_gain", "0 255 0 255", "Speed gain readout color.");
  sh_ups_color_loss = Cg_StrafeHelper_AddCvar("ups_color_loss", "255 0 0 255", "Speed loss readout color.");
  sh_ups_color_neutral = Cg_StrafeHelper_AddCvar("ups_color_neutral", "255 255 255 255", "Neutral speed readout color.");
  sh_ups_format = Cg_StrafeHelper_AddCvar("ups_format", "plain", "Speed readout format.");
  sh_ups_3d = Cg_StrafeHelper_AddCvar("ups_3d", "0", "Include vertical velocity in the speed readout.");
  sh_alpha = Cg_StrafeHelper_AddCvar("alpha", "0.500000", "Strafe helper bar opacity multiplier.");
  sh_bar_style = Cg_StrafeHelper_AddCvar("bar_style", "gradient", "Strafe helper bar style.");
  sh_center_width = Cg_StrafeHelper_AddCvar("center_width", "2", "Width of the strafe helper center marker.");
  sh_optimal_width = Cg_StrafeHelper_AddCvar("optimal_width", "2", "Width of the strafe helper optimal marker.");
  sh_optimal_outline = Cg_StrafeHelper_AddCvar("optimal_outline", "1", "Outline the strafe helper optimal marker.");
  sh_color_accelerating = Cg_StrafeHelper_AddCvar("color_accelerating", "0 128 0 128", "Accelerating-angle color.");
  sh_color_optimal = Cg_StrafeHelper_AddCvar("color_optimal", "255 215 0 255", "Optimal-angle color.");
  sh_color_centermarker = Cg_StrafeHelper_AddCvar("color_centermarker", "255 255 255 255", "Center-marker color.");
  sh_max_speed = Cg_StrafeHelper_AddCvar("max_speed", "0", "Draw the strafe helper max-speed readout.");
  sh_max_speed_scale = Cg_StrafeHelper_AddCvar("max_speed_scale", "1", "Scale of the max-speed readout.");
  sh_max_speed_y = Cg_StrafeHelper_AddCvar("max_speed_y", "-20", "Vertical max-speed readout offset from screen center.");
  sh_max_speed_shadow = Cg_StrafeHelper_AddCvar("max_speed_shadow", "1", "Draw a shadow behind the max-speed readout.");
  sh_max_speed_color = Cg_StrafeHelper_AddCvar("max_speed_color", "255 255 255 255", "Max-speed readout color.");
  sh_velocity_angle = Cg_StrafeHelper_AddCvar("velocity_angle", "0", "Draw the strafe helper velocity-angle readout.");
  sh_velocity_angle_scale = Cg_StrafeHelper_AddCvar("velocity_angle_scale", "1", "Scale of the velocity-angle readout.");
  sh_velocity_angle_y = Cg_StrafeHelper_AddCvar("velocity_angle_y", "-35", "Vertical velocity-angle readout offset from screen center.");
  sh_velocity_angle_shadow = Cg_StrafeHelper_AddCvar("velocity_angle_shadow", "1", "Draw a shadow behind the velocity-angle readout.");
  sh_velocity_angle_color = Cg_StrafeHelper_AddCvar("velocity_angle_color", "255 255 255 255", "Velocity-angle readout color.");
  sh_velocity_angle_format = Cg_StrafeHelper_AddCvar("velocity_angle_format", "diff", "Velocity-angle readout format.");

  Cg_StrafeHelper_Clear(&cg_strafe_helper);
  Cg_StrafeHelper_ClearVelocitySample(&cg_strafe_helper_velocity);
  cg_strafe_helper_dynamic_trend = (cg_strafe_helper_dynamic_trend_t) {};
}
