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

#pragma once

#include "game/race/race_training.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CG_STRAFE_HELPER_EPSILON 0.0001f
#define CG_STRAFE_HELPER_DYNAMIC_SPEED_EPSILON 0.01f
#define CG_STRAFE_HELPER_DYNAMIC_COLOR_HOLD_MILLIS 100u

typedef struct {
  float angle_optimal;
  float angle_minimum;
  float angle_maximum;
  float angle_current;
  float angle_diff;
  float velocity_norm;
  float speed_ceiling;
  float velocity_yaw;
  float view_yaw;
  float velocity_angle_diff;
} cg_strafe_helper_sample_t;

typedef struct {
  cg_strafe_helper_sample_t raw;
  cg_strafe_helper_sample_t visual;
} cg_strafe_helper_state_t;

typedef enum {
  CG_STRAFE_HELPER_SPEED_LOSS = -1,
  CG_STRAFE_HELPER_SPEED_NEUTRAL,
  CG_STRAFE_HELPER_SPEED_GAIN
} cg_strafe_helper_speed_trend_t;

typedef struct {
  bool initialized;
  float reference_speed;
  uint32_t change_time;
  cg_strafe_helper_speed_trend_t trend;
} cg_strafe_helper_dynamic_trend_t;

typedef struct {
  vec3_t velocity;
  bool valid;
} cg_strafe_helper_velocity_sample_t;

static inline bool Cg_StrafeHelper_ShouldObservePredictionCommand(const size_t index,
                                                                   const size_t count) {
  return count > 0 && index < count - 1;
}

static inline void Cg_StrafeHelper_ClearVelocitySample(
  cg_strafe_helper_velocity_sample_t *sample) {

  if (sample) {
    *sample = (cg_strafe_helper_velocity_sample_t) {};
  }
}

static inline void Cg_StrafeHelper_SetVelocitySample(
  cg_strafe_helper_velocity_sample_t *sample,
  const vec3_t velocity) {

  if (sample) {
    sample->velocity = velocity;
    sample->valid = true;
  }
}

static inline cg_strafe_helper_speed_trend_t Cg_StrafeHelper_UpdateDynamicTrend(
  cg_strafe_helper_dynamic_trend_t *state,
  const float speed,
  const uint32_t time) {

  if (!state) {
    return CG_STRAFE_HELPER_SPEED_NEUTRAL;
  }

  if (!state->initialized) {
    state->initialized = true;
    state->reference_speed = speed;
    state->change_time = time;
    state->trend = CG_STRAFE_HELPER_SPEED_NEUTRAL;
    return state->trend;
  }

  const float delta = speed - state->reference_speed;
  if (fabsf(delta) > CG_STRAFE_HELPER_DYNAMIC_SPEED_EPSILON) {
    state->reference_speed = speed;
    state->change_time = time;
    state->trend = delta > 0.f
                   ? CG_STRAFE_HELPER_SPEED_GAIN
                   : CG_STRAFE_HELPER_SPEED_LOSS;
  } else if (state->trend != CG_STRAFE_HELPER_SPEED_NEUTRAL &&
             time - state->change_time >= CG_STRAFE_HELPER_DYNAMIC_COLOR_HOLD_MILLIS) {
    state->trend = CG_STRAFE_HELPER_SPEED_NEUTRAL;
  }

  return state->trend;
}

static inline float Cg_StrafeHelper_Sign(const float value) {

  if (value < 0.f) {
    return -1.f;
  }
  if (value > 0.f) {
    return 1.f;
  }
  return 0.f;
}

static inline float Cg_StrafeHelper_Cross2(const vec3_t v, const vec3_t w) {
  return v.x * w.y - v.y * w.x;
}

static inline float Cg_StrafeHelper_Dot2(const vec3_t v, const vec3_t w) {
  return v.x * w.x + v.y * w.y;
}

static inline float Cg_StrafeHelper_Norm2(const vec3_t v) {
  return sqrtf(Cg_StrafeHelper_Dot2(v, v));
}

static inline float Cg_StrafeHelper_AngleBetween(const vec3_t v, const vec3_t w) {
  return atan2f(Cg_StrafeHelper_Cross2(v, w), Cg_StrafeHelper_Dot2(v, w));
}

static inline float Cg_StrafeHelper_SafeAcos(const float value) {
  return acosf(Clampf(value, -1.f, 1.f));
}

static inline bool Cg_StrafeHelper_IsForwardOnlyWishdir(const vec3_t forward, const vec3_t wishdir) {
  const float wishdir_norm = Cg_StrafeHelper_Norm2(wishdir);
  const float forward_norm = Cg_StrafeHelper_Norm2(forward);

  if (wishdir_norm <= CG_STRAFE_HELPER_EPSILON || forward_norm <= CG_STRAFE_HELPER_EPSILON) {
    return false;
  }

  return fabsf(Cg_StrafeHelper_Cross2(wishdir, forward)) <=
         CG_STRAFE_HELPER_EPSILON * wishdir_norm * forward_norm &&
         Cg_StrafeHelper_Dot2(wishdir, forward) > 0.f;
}

static inline void Cg_StrafeHelper_Clear(cg_strafe_helper_state_t *state) {
  memset(state, 0, sizeof(*state));
}

static inline bool Cg_StrafeHelper_Update(cg_strafe_helper_state_t *state,
                                          const vec3_t forward,
                                          const vec3_t velocity,
                                          const vec3_t wishdir,
                                          const float wishspeed,
                                          const float accel,
                                          const float frametime,
                                          const float view_yaw) {
  const float wishdir_norm = Cg_StrafeHelper_Norm2(wishdir);
  const float velocity_norm = Cg_StrafeHelper_Norm2(velocity);
  const float forward_velocity_angle = Cg_StrafeHelper_AngleBetween(wishdir, forward);
  const float angle_sign = Cg_StrafeHelper_Sign(Cg_StrafeHelper_Cross2(wishdir, velocity));
  const float two_pi = 2.f * (float) M_PI;

  if (velocity_norm <= CG_STRAFE_HELPER_EPSILON ||
      wishdir_norm <= CG_STRAFE_HELPER_EPSILON ||
      Cg_StrafeHelper_IsForwardOnlyWishdir(forward, wishdir)) {
    Cg_StrafeHelper_Clear(state);
    return false;
  }

  cg_strafe_helper_sample_t raw = {};
  raw.velocity_norm = velocity_norm;

  raw.angle_optimal = (wishspeed * (1.f - accel * frametime) - velocity.z * wishdir.z) /
                      (velocity_norm * wishdir_norm);
  raw.angle_optimal = Cg_StrafeHelper_SafeAcos(raw.angle_optimal);
  raw.angle_optimal = angle_sign * raw.angle_optimal - forward_velocity_angle;

  const float minimum_denominator = (2.f - wishdir_norm * wishdir_norm) * velocity_norm;
  raw.angle_minimum = fabsf(minimum_denominator) > CG_STRAFE_HELPER_EPSILON
                      ? (wishspeed - velocity.z * wishdir.z) * wishdir_norm / minimum_denominator
                      : 1.f;
  raw.angle_minimum = Cg_StrafeHelper_SafeAcos(raw.angle_minimum);
  raw.angle_minimum = angle_sign * raw.angle_minimum - forward_velocity_angle;

  raw.angle_maximum = -0.5f * accel * frametime * wishspeed * wishdir_norm / velocity_norm;
  raw.angle_maximum = Cg_StrafeHelper_SafeAcos(raw.angle_maximum);
  raw.angle_maximum = angle_sign * raw.angle_maximum - forward_velocity_angle;

  raw.angle_current = Cg_StrafeHelper_AngleBetween(forward, velocity);
  raw.angle_current += truncf((raw.angle_minimum - raw.angle_current) / two_pi) * two_pi;
  raw.angle_current += truncf((raw.angle_maximum - raw.angle_current) / two_pi) * two_pi;
  raw.angle_diff = raw.angle_current - raw.angle_optimal;

  if (wishspeed > CG_STRAFE_HELPER_EPSILON && accel > CG_STRAFE_HELPER_EPSILON &&
      frametime > CG_STRAFE_HELPER_EPSILON) {
    raw.speed_ceiling = wishspeed * (1.f - accel * frametime) / wishdir_norm;
    raw.speed_ceiling = Maxf(0.f, raw.speed_ceiling);
  }

  raw.velocity_yaw = atan2f(velocity.y, velocity.x) * (180.f / (float) M_PI);
  raw.view_yaw = view_yaw;
  raw.velocity_angle_diff = raw.velocity_yaw - raw.view_yaw;
  while (raw.velocity_angle_diff > 180.f) {
    raw.velocity_angle_diff -= 360.f;
  }
  while (raw.velocity_angle_diff < -180.f) {
    raw.velocity_angle_diff += 360.f;
  }

  state->raw = raw;
  state->visual = raw;

  return true;
}
