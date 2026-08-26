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

#include "bg_pmove.h"
#include "race_physics.h"
#include "race_pmove_policy.h"
#include "race_training.h"

/**
 * @brief `PM_BOUNDS` is the default bounding box, scaled by `PM_SCALE`
 * in `Pm_Init`. They are referenced in a few other places e.g. to create effects
 * at a certain body position on the player model.
 */
const box3_t PM_BOUNDS = {
  .mins = { { -16.f, -16.f, -24.f } },
  .maxs = { {  16.f,  16.f,  36.f } }
};

const box3_t PM_CROUCHED_BOUNDS = {
  .mins = { { -16.f, -16.f, -24.f } },
  .maxs = { {  16.f,  16.f,  6.f } }
};

static box3_t Pm_PlayerBoundsForPolicy(
    const bool ducked, const race_pmove_policy_t *policy) {
  box3_t bounds = Box3_Scale(
    ducked ? PM_CROUCHED_BOUNDS : PM_BOUNDS, PM_SCALE);
  bounds.maxs.z = ducked ? policy->ducked_max_z : policy->standing_max_z;
  return bounds;
}

box3_t Pm_PlayerBounds(const bool ducked) {
  const race_physics_config_t *config = Race_Physics_Current();
  const race_physics_preset_descriptor_t *preset =
    Race_Physics_Preset(config->preset);
  const race_pmove_policy_t *policy = preset
    ? Race_PmovePolicy(preset->pm_policy)
    : NULL;
  assert(policy);
  return Pm_PlayerBoundsForPolicy(ducked, policy);
}

static const box3_t PM_DEAD_BOUNDS = {
  .mins = { { -16.f, -16.f, -24.f } },
  .maxs = { {  16.f,  16.f,  -4.f } }
};

static const box3_t PM_GIBLET_BOUNDS = {
  .mins = { { -8.f, -8.f, -8.f } },
  .maxs = { {  8.f,  8.f,  8.f } }
};

static pm_move_t *pm;
static race_physics_family_id_t pm_physics_family;
static const race_pmove_policy_t *pm_physics_policy;
static race_physics_q2_snap_mode_t pm_q2_snap_mode;
static race_strafe_observer_t pm_strafe_observer;
static void *pm_strafe_observer_context;

void Pm_RaceTraining_SetObserver(const race_strafe_observer_t observer,
                                 void *context) {
  pm_strafe_observer = observer;
  pm_strafe_observer_context = context;
}

void Pm_RaceTraining_ClearObserver(void) {
  pm_strafe_observer = NULL;
  pm_strafe_observer_context = NULL;
}

static box3_t Pm_CurrentPlayerBounds(const bool ducked) {
  return Pm_PlayerBoundsForPolicy(ducked, pm_physics_policy);
}

/**
 * @brief Binds one complete immutable physics identity for the current move or
 * focused test entry point.
 */
static void Pm_BindPhysics(void) {
  const race_physics_config_t *config = Race_Physics_Current();
  const race_physics_preset_descriptor_t *preset = config
    ? Race_Physics_Preset(config->preset)
    : NULL;
  const race_pmove_policy_t *policy = preset
    ? Race_PmovePolicy(preset->pm_policy)
    : NULL;

  assert(config);
  assert(preset);
  assert(policy);
  assert(preset->family == config->family);

  pm_physics_family = config->family;
  pm_physics_policy = policy;
  pm_q2_snap_mode = config->q2_snap_mode;
}

#define MAX_CLIP_PLANES  6

#define PM_Q2_CLIP_PLANES      5
#define PM_Q2_CLIP_BUMPS       4
#define PM_Q2_CLIP_OVERBOUNCE  1.01f
#define PM_Q2_STEP_HEIGHT      18.f
#define PM_Q2_LADDER_PROBE_DIST 1.f

#define PM_Q2_JUMP_UP_MIN       10
#define PM_Q2_TIME_WATER_JUMP 2040
#define PM_Q2_TIME_SHIFT          3

#define PM_Q2_SNAP_SCALE          8.f
#define PM_Q2_SNAP_DISTANCE       .125f

#define PM_Q2_GROUND_NORMAL_MIN          .7f
#define PM_Q2_GROUND_PROBE_DIST          .25f
#define PM_Q2_UPWARD_SPEED_EPSILON       .1f

static bool Pm_Q2FamilyPhysics(void) {
  return pm_physics_family == RACE_PHYSICS_FAMILY_Q2;
}

#if defined(RACE_PHYSICS_TEST)
static float pm_q2_air_wishspeed_cap;
static race_physics_q2_snap_mode_t pm_q2_snap_mode_for_test =
  RACE_PHYSICS_Q2_SNAP_OFF;

void Pm_SetQ2AirWishspeedCapForTest(float cap) {
  pm_q2_air_wishspeed_cap = cap;
}

void Pm_SetQ2SnapEnabledForTest(bool enabled) {
  pm_q2_snap_mode_for_test = enabled
    ? RACE_PHYSICS_Q2_SNAP_NEAREST
    : RACE_PHYSICS_Q2_SNAP_OFF;
}

void Pm_SetQ2SnapModeForTest(race_physics_q2_snap_mode_t mode) {
  pm_q2_snap_mode_for_test = mode;
}
#endif

static float Pm_Q2AirWishspeedCap(void) {
#if defined(RACE_PHYSICS_TEST)
  return pm_q2_air_wishspeed_cap;
#else
  return 0.f;
#endif
}

/**
 * @return The map-fixed legacy Q2 state-snapping policy.
 */
static race_physics_q2_snap_mode_t Pm_Q2SnapMode(void) {
#if defined(RACE_PHYSICS_TEST)
  return pm_q2_snap_mode_for_test;
#else
  if (!Pm_Q2FamilyPhysics()) {
    return RACE_PHYSICS_Q2_SNAP_OFF;
  }

  return pm_q2_snap_mode;
#endif
}

static bool Pm_Q2SnapEnabled(void) {
  return Pm_Q2SnapMode() != RACE_PHYSICS_Q2_SNAP_OFF;
}

/**
 * @return Legacy Q2 command-time ticks, with every command consuming at
 * least one 8 ms tick.
 */
static uint16_t Pm_Q2Time(const uint16_t msec) {
  const uint16_t time = msec >> PM_Q2_TIME_SHIFT;
  return time ? time : 1;
}

static uint16_t Pm_MovementTimeStep(void) {
  return Pm_Q2FamilyPhysics() ? Pm_Q2Time(pm->cmd.msec) : pm->cmd.msec;
}

static void Pm_DropMovementTime(void) {
  if (!pm->s.time) {
    return;
  }

  const uint16_t step = Pm_MovementTimeStep();
  if (step >= pm->s.time) {
    pm->s.flags &= ~PMF_TIME_MASK;
    pm->s.time = 0;
  } else {
    pm->s.time -= step;
  }
}

static bool Pm_Q2MovementTimer(void) {
  return pm->s.time &&
         (pm->s.flags &
          (PMF_TIME_TELEPORT | PMF_TIME_WATER_JUMP | PMF_TIME_LAND));
}

static float Pm_Q2SnapFloat(const float value) {
  switch (Pm_Q2SnapMode()) {
    case RACE_PHYSICS_Q2_SNAP_NEAREST:
      return roundf(value * PM_Q2_SNAP_SCALE) * PM_Q2_SNAP_DISTANCE;
    case RACE_PHYSICS_Q2_SNAP_TRUNCATE:
      return (float) ((int32_t) (value * PM_Q2_SNAP_SCALE)) *
             PM_Q2_SNAP_DISTANCE;
    default:
      return value;
  }
}

static void Pm_Q2SnapMoveInput(void) {
  if (!Pm_Q2SnapEnabled()) {
    return;
  }

  pm->s.origin = Vec3(Pm_Q2SnapFloat(pm->s.origin.x),
                      Pm_Q2SnapFloat(pm->s.origin.y),
                      Pm_Q2SnapFloat(pm->s.origin.z));
  pm->s.velocity = Vec3(Pm_Q2SnapFloat(pm->s.velocity.x),
                        Pm_Q2SnapFloat(pm->s.velocity.y),
                        Pm_Q2SnapFloat(pm->s.velocity.z));
}

#if defined(RACE_PHYSICS_TEST)
uint16_t Pm_Q2TimeForTest(uint16_t msec) {
  return Pm_Q2Time(msec);
}

float Pm_Q2SnapFloatForTest(float value) {
  return Pm_Q2SnapFloat(value);
}
#endif

/**
 * @brief A structure containing full floating point precision copies of all
 * movement variables. This is initialized with the player's last movement
 * at each call to `Pm_Move` (this is obviously not thread-safe).
 */
static struct {

  /**
   * @brief Previous (incoming) origin, in case movement fails and must be reverted.
   */
  vec3_t previous_origin;

  /**
   * @brief Previous (incoming) velocity, used for detecting landings.
   */
  vec3_t previous_velocity;

  /**
   * @brief Directional vectors based on command angles, with Z component.
   */
  vec3_t forward, right, up;

  /**
   * @brief Directional vectors without Z component, for air and ground movement.
   */
  vec3_t forward_xy, right_xy;

  /**
   * @brief The current movement command duration, in seconds.
   */
  float time;

  /**
   * @brief The player's ground interaction.
   */
  cm_trace_t ground;

  /**
   * @brief Transient DP2 rising-ramp contact for the current movement command.
   */
  bool ramp_contact_slide;

  /**
   * @brief The clipping planes per slide-move.
   */
  cm_bsp_plane_t clip_planes[MAX_CLIP_PLANES];

  /**
   * @brief The number of clipping planes per slide-move.
   */
  int32_t num_clip_planes;

} pm_locals;

/**
 * @brief Unlike the game and the client game, this keeps its own mask test: it is
 * called per move, from the movement loop, where the arguments it would otherwise
 * format are worth skipping. `do while` rather than a statement expression, so it
 * is standard C.
 */
#define Pm_Debug(...) \
  do { \
    if (pm->DebugMask() & pm->debug_mask) { \
      pm->Debug(pm->debug_mask, __func__, __VA_ARGS__); \
    } \
  } while (0)

/**
 * @brief Mark the specified entity as touched. This enables the game module to
 * detect player -> entity interactions.
 */
static void Pm_TouchEntity(const cm_trace_t *trace) {

  if (trace->ent == NULL) {
    return;
  }

  if (pm->num_touched == PM_MAX_TOUCHS) {
    Pm_Debug("MAX_TOUCH_ENTS\n");
    return;
  }

  for (int32_t i = 0; i < pm->num_touched; i++) {
    if (pm->touched[i].ent == trace->ent) {
      return;
    }
  }

  pm->touched[pm->num_touched++] = *trace;
}

/**
 * Adapted from Quake III, this function adjusts a trace so that if it starts inside of a wall,
 * it is adjusted so that the trace begins outside of the solid it impacts.
 * @return The actual trace.
 */
static cm_trace_t Pm_Trace(const vec3_t start, const vec3_t end, const box3_t bounds) {

  if (Pm_Q2FamilyPhysics()) {
    return pm->Trace(start, end, bounds);
  }

  const float offsets[] = { 0.f, 1.f, -1.f };

  // jitter around
  for (uint32_t i = 0; i < lengthof(offsets); i++) {
    for (uint32_t j = 0; j < lengthof(offsets); j++) {
      for (uint32_t k = 0; k < lengthof(offsets); k++) {
        const vec3_t point = Vec3_Add(start, Vec3(offsets[i], offsets[j], offsets[k]));
        const cm_trace_t trace = pm->Trace(point, end, bounds);

        if (!trace.all_solid) {

          if (i != 0 || j != 0 || k != 0) {
            Pm_Debug("Fixed all-solid\n");
          }

          return trace;
        }
      }
    }
  }

  Pm_Debug("No good position\n");
  return pm->Trace(start, end, bounds);
}

/**
 * @brief Slide off of the impacted plane.
 */
static vec3_t Pm_ClipVelocity(const vec3_t in, const vec3_t normal, float bounce) {

  float backoff = Vec3_Dot(in, normal);

  if (backoff < 0.f) {
    backoff *= bounce;
  } else {
    backoff /= bounce;
  }

  return Vec3_Subtract(in, Vec3_Scale(normal, backoff));
}

/**
 * @brief Collide with the results of the trace, clipping our velocity along the normal.
 */
static void Pm_ClipMove(const cm_trace_t *trace) {

  if (trace->ent == NULL) {
    return;
  }

  if (pm_locals.num_clip_planes == MAX_CLIP_PLANES) {
    Pm_Debug("MAX_CLIP_PLANES\n");
    return;
  }

  // determine if this plane is new to this move
  for (int32_t i = 0; i < pm_locals.num_clip_planes; i++) {
    if (Vec3_Dot(trace->plane.normal, pm_locals.clip_planes[i].normal) > 1.f - ON_EPSILON) {
      return;
    }
  }

  pm_locals.clip_planes[pm_locals.num_clip_planes++] = trace->plane;

  // it is, so clip to it, and nudge out along the normal
  pm->s.velocity = Pm_ClipVelocity(pm->s.velocity, trace->plane.normal, PM_CLIP_BOUNCE);
  pm->s.origin = Vec3_Fmaf(pm->s.origin, TRACE_EPSILON, trace->plane.normal);

  // re-clip to all previously intersected planes, too
  for (int32_t i = 0; i < pm_locals.num_clip_planes - 1; i++) {
    pm->s.velocity = Pm_ClipVelocity(pm->s.velocity, pm_locals.clip_planes[i].normal, PM_CLIP_BOUNCE);
  }
}

/**
 * @brief Slides velocity along a plane with legacy Q2 overbounce and snapping.
 */
static vec3_t Pm_Q2ClipVelocity(const vec3_t in, const vec3_t normal) {

  const float backoff = Vec3_Dot(in, normal) * PM_Q2_CLIP_OVERBOUNCE;
  vec3_t out = Vec3_Subtract(in, Vec3_Scale(normal, backoff));

  if (out.x > -PM_STOP_EPSILON && out.x < PM_STOP_EPSILON) {
    out.x = 0.f;
  }
  if (out.y > -PM_STOP_EPSILON && out.y < PM_STOP_EPSILON) {
    out.y = 0.f;
  }
  if (out.z > -PM_STOP_EPSILON && out.z < PM_STOP_EPSILON) {
    out.z = 0.f;
  }

  return out;
}

#if defined(RACE_PHYSICS_TEST)
vec3_t Pm_Q2ClipVelocityForTest(const vec3_t in, const vec3_t normal) {
  return Pm_Q2ClipVelocity(in, normal);
}
#endif

/**
 * @brief Resolves one Q2-family impact against all planes in this slide move.
 */
static void Pm_Q2ClipMove(const cm_trace_t *trace) {

  if (pm_locals.num_clip_planes >= PM_Q2_CLIP_PLANES) {
    Pm_Debug("PM_Q2_CLIP_PLANES\n");
    pm->s.velocity = Vec3_Zero();
    return;
  }

  pm_locals.clip_planes[pm_locals.num_clip_planes++] = trace->plane;

  for (int32_t i = 0; i < pm_locals.num_clip_planes; i++) {
    pm->s.velocity = Pm_Q2ClipVelocity(pm->s.velocity,
                                      pm_locals.clip_planes[i].normal);

    int32_t j;
    for (j = 0; j < pm_locals.num_clip_planes; j++) {
      if (j != i &&
          Vec3_Dot(pm->s.velocity, pm_locals.clip_planes[j].normal) < 0.f) {
        break;
      }
    }

    if (j == pm_locals.num_clip_planes) {
      return;
    }
  }

  if (pm_locals.num_clip_planes != 2) {
    pm->s.velocity = Vec3_Zero();
    return;
  }

  const vec3_t dir = Vec3_Cross(pm_locals.clip_planes[0].normal,
                                pm_locals.clip_planes[1].normal);
  const float scale = Vec3_Dot(dir, pm->s.velocity);
  pm->s.velocity = Vec3_Scale(dir, scale);
}

/**
 * @brief Slides through the world with legacy Q2 bump and plane handling.
 */
static float Pm_Q2SlideMove(void) {

  const vec3_t org0 = pm->s.origin;
  const vec3_t primal_velocity = pm->s.velocity;

  memset(pm_locals.clip_planes, 0, sizeof(pm_locals.clip_planes));
  pm_locals.num_clip_planes = 0;

  float time = pm_locals.time;
  for (int32_t bump = 0; bump < PM_Q2_CLIP_BUMPS && time > 0.f; bump++) {
    const vec3_t pos = Vec3_Fmaf(pm->s.origin, time, pm->s.velocity);
    const cm_trace_t trace = Pm_Trace(pm->s.origin, pos, pm->bounds);

    if (trace.all_solid) {
      pm->s.velocity.z = 0.f;
      break;
    }

    pm->s.origin = trace.end;

    if (trace.fraction > 0.f) {
      memset(pm_locals.clip_planes, 0, sizeof(pm_locals.clip_planes));
      pm_locals.num_clip_planes = 0;
    }

    Pm_TouchEntity(&trace);

    if (trace.fraction == 1.f) {
      break;
    }

    time -= time * trace.fraction;
    Pm_Q2ClipMove(&trace);

    if (Vec3_Dot(pm->s.velocity, primal_velocity) <= 0.f) {
      pm->s.velocity = Vec3_Zero();
      break;
    }
  }

  if (Pm_Q2MovementTimer()) {
    pm->s.velocity = primal_velocity;
  }

  const vec3_t org1 = pm->s.origin;
  return fabsf(Vec2_Distance(Vec3_XY(org0), Vec3_XY(org1)));
}

/**
 * @brief Slide through the world, clipping to impacted planes.
 */
static float Pm_SlideMove(void) {

  if (Pm_Q2FamilyPhysics()) {
    return Pm_Q2SlideMove();
  }

  const vec3_t org0 = pm->s.origin;

  memset(pm_locals.clip_planes, 0, sizeof(pm_locals.clip_planes));
  pm_locals.num_clip_planes = 0;

  float time = pm_locals.time;
  while (time > 0.f) {

    // project desired destination
    const vec3_t pos = Vec3_Fmaf(pm->s.origin, time, pm->s.velocity);

    // and move distance
    const float dist0 = Vec3_Distance(pos, org0);

    // trace to it
    const cm_trace_t trace = Pm_Trace(pm->s.origin, pos, pm->bounds);

    // move to the end position
    pm->s.origin = trace.end;

    // store a reference to the entity for firing game events
    Pm_TouchEntity(&trace);

    // clip along the plane
    Pm_ClipMove(&trace);

    // calculate the actual move distance, which includes nudging along the normal
    const float dist1 = Vec3_Distance(pm->s.origin, org0);

    // calculate the trace fraction based on actual distance moved
    float fraction = Maxf(trace.fraction, dist1 / dist0);

    // if we didn't move at all, we're done
    if (fraction == 0.f || isnan(fraction)) {
      break;
    }

    // and update the movement time remaining
    time -= time * fraction;
  }

  const vec3_t org1 = pm->s.origin;

  return fabsf(Vec2_Distance(Vec3_XY(org0), Vec3_XY(org1)));
}

/**
 * @brief Performs the legacy Q2 lower and stepped slide candidates, retaining
 * the one with the greatest horizontal progress.
 */
static void Pm_Q2StepSlideMove(void) {

  const vec3_t start_origin = pm->s.origin;
  const vec3_t start_velocity = pm->s.velocity;

  Pm_SlideMove();

  const vec3_t lower_origin = pm->s.origin;
  const vec3_t lower_velocity = pm->s.velocity;

  const vec3_t up = Vec3_Fmaf(start_origin, PM_Q2_STEP_HEIGHT, Vec3_Up());
  const cm_trace_t occupancy = Pm_Trace(up, up, pm->bounds);

  if (occupancy.all_solid) {
    return;
  }

  pm->s.origin = up;
  pm->s.velocity = start_velocity;

  Pm_SlideMove();

  const vec3_t down = Vec3_Fmaf(pm->s.origin, PM_Q2_STEP_HEIGHT, Vec3_Down());
  const cm_trace_t step_down = Pm_Trace(pm->s.origin, down, pm->bounds);

  if (!step_down.all_solid) {
    pm->s.origin = step_down.end;
  }

  const float lower_distance = Vec2_DistanceSquared(Vec3_XY(lower_origin),
                                                     Vec3_XY(start_origin));
  const float upper_distance = Vec2_DistanceSquared(Vec3_XY(pm->s.origin),
                                                     Vec3_XY(start_origin));

  if (lower_distance > upper_distance ||
      step_down.plane.normal.z < PM_STEP_NORMAL) {
    pm->s.origin = lower_origin;
    pm->s.velocity = lower_velocity;
    return;
  }

  pm->s.velocity.z = lower_velocity.z;
}

/**
 * @brief Performs DP2's fraction-aware raised slide candidate and guarded
 * settlement trace while retaining Q2's horizontal candidate selection.
 */
static void Pm_Dp2StepSlideMove(void) {

  const vec3_t start_origin = pm->s.origin;
  const vec3_t start_velocity = pm->s.velocity;

  Pm_SlideMove();

  const vec3_t lower_origin = pm->s.origin;
  const vec3_t lower_velocity = pm->s.velocity;

  const vec3_t up = Vec3_Fmaf(start_origin, PM_Q2_STEP_HEIGHT, Vec3_Up());
  const cm_trace_t step_up = Pm_Trace(start_origin, up, pm->bounds);
  const float step_fraction = step_up.fraction;

  if (step_up.all_solid) {
    return;
  }

  pm->s.origin = step_up.end;
  pm->s.velocity = start_velocity;

  Pm_SlideMove();

  const float down_min = pm->s.origin.z - PM_Q2_STEP_HEIGHT * step_fraction;
  vec3_t down = pm->s.origin;

  if (start_origin.z - pm_physics_policy->step_down_extra < down_min &&
      step_fraction == 1.f &&
      !(pm->s.flags & PMF_TIME_WATER_JUMP)) {
    down.z = start_origin.z - pm_physics_policy->step_down_extra;
  } else {
    down.z = down_min;
  }

  const float upper_end_z = pm->s.origin.z;
  cm_trace_t step_down = Pm_Trace(pm->s.origin, down, pm->bounds);

  if (step_down.all_solid) {
    box3_t inset = pm->bounds;
    inset.mins.x += pm_physics_policy->step_inset;
    inset.mins.y += pm_physics_policy->step_inset;
    inset.maxs.x -= pm_physics_policy->step_inset;
    inset.maxs.y -= pm_physics_policy->step_inset;
    inset.maxs.z -= pm_physics_policy->step_inset;
    step_down = Pm_Trace(pm->s.origin, down, inset);
  }

  if (!step_down.all_solid) {
    pm->s.origin = step_down.end;

    if (pm->s.origin.z < down_min) {
      pm->s.origin.z = down_min;
    }

    if (pm->s.origin.z < lower_origin.z) {
      pm->s.origin.z = upper_end_z > lower_origin.z
        ? lower_origin.z
        : upper_end_z;
    }
  }

  const float lower_distance = Vec2_DistanceSquared(Vec3_XY(lower_origin),
                                                     Vec3_XY(start_origin));
  const float upper_distance = Vec2_DistanceSquared(Vec3_XY(pm->s.origin),
                                                     Vec3_XY(start_origin));

  if (lower_distance > upper_distance ||
      step_down.plane.normal.z < PM_STEP_NORMAL) {
    pm->s.origin = lower_origin;
    pm->s.velocity = lower_velocity;
    return;
  }

  pm->s.velocity.z = lower_velocity.z;
}

#if defined(RACE_PHYSICS_TEST)
void Pm_Q2StepSlideMoveForTest(pm_move_t *move) {
  Pm_BindPhysics();
  pm = move;

  if (pm_physics_policy->step_variant == RACE_PM_STEP_DP2) {
    pm->bounds = Pm_CurrentPlayerBounds(!!(pm->s.flags & PMF_DUCKED));
  }

  memset(&pm_locals, 0, sizeof(pm_locals));
  pm_locals.previous_origin = pm->s.origin;
  pm_locals.previous_velocity = pm->s.velocity;
  pm_locals.time = pm->cmd.msec * .001f;

  switch (pm_physics_policy->step_variant) {
    case RACE_PM_STEP_Q2:
      Pm_Q2StepSlideMove();
      break;
    case RACE_PM_STEP_DP2:
      Pm_Dp2StepSlideMove();
      break;
    default:
      assert(false);
  }
}
#endif

/**
 * @return True if the downward trace yielded a step, false otherwise.
 */
static bool Pm_CheckStep(const cm_trace_t *trace) {

  if (!trace->all_solid) {
    if (trace->ent && trace->plane.normal.z >= PM_STEP_NORMAL) {
      return true;
    }
  }

  return false;
}

/**
 * @brief Moves the player origin to the end of a step-down trace and records the step height.
 */
static void Pm_StepDown(const cm_trace_t *trace) {

  pm->s.origin = trace->end;

  const float step_height = pm->s.origin.z - pm_locals.previous_origin.z;

  if (fabsf(step_height) >= PM_STEP_HEIGHT_MIN) {
    pm->step = step_height;
  }
}

/**
 * @brief Performs a slide move with stair stepping, attempting to step up over obstacles.
 */
static void Pm_StepSlideMove(void) {

  if (Pm_Q2FamilyPhysics()) {
    switch (pm_physics_policy->step_variant) {
      case RACE_PM_STEP_Q2:
        Pm_Q2StepSlideMove();
        break;
      case RACE_PM_STEP_DP2:
        Pm_Dp2StepSlideMove();
        break;
      default:
        assert(false);
    }
    return;
  }

  // store pre-move parameters
  const vec3_t org0 = pm->s.origin;
  const vec3_t vel0 = pm->s.velocity;

  // attempt to move
  float dist0 = Pm_SlideMove();

  // attempt to step down to remain on ground
  if ((pm->s.flags & PMF_ON_GROUND) && pm->cmd.up <= 0) {

    const vec3_t down = Vec3_Fmaf(pm->s.origin, PM_STEP_HEIGHT + PM_GROUND_DIST, Vec3_Down());
    const cm_trace_t step_down = Pm_Trace(pm->s.origin, down, pm->bounds);

    if (Pm_CheckStep(&step_down)) {
      Pm_StepDown(&step_down);
    }
  }

  // now that we're on the ground, try to step over any obstacles
  const vec3_t org1 = pm->s.origin;
  const vec3_t vel1 = pm->s.velocity;

  const vec3_t up = Vec3_Fmaf(org0, PM_STEP_HEIGHT, Vec3_Up());
  const cm_trace_t step_up = Pm_Trace(org0, up, pm->bounds);

  if (step_up.fraction == 1.f) {

    // step from the higher position, with the original velocity
    pm->s.origin = step_up.end;
    pm->s.velocity = vel0;

    const float dist1 = Pm_SlideMove();
    if (dist1 > dist0) {

      // settle to the new ground, keeping the step if and only if it was successful
      const vec3_t down = Vec3_Fmaf(pm->s.origin, PM_STEP_HEIGHT + PM_GROUND_DIST, Vec3_Down());
      const cm_trace_t step_down = Pm_Trace(pm->s.origin, down, pm->bounds);

      if (Pm_CheckStep(&step_down)) {
        // Quake2 trick jump secret sauce
        if ((pm->s.flags & PMF_ON_GROUND) || vel0.z < PM_SPEED_UP) {
          Pm_StepDown(&step_down);
        } else {
          pm->step = pm->s.origin.z - pm_locals.previous_origin.z;
        }

        return;
      }
    }
  }

  // stepping up was not helpful, so take the lower movement
  pm->s.origin = org1;
  pm->s.velocity = vel1;
}

/**
 * @brief Handles friction against user intentions, and based on contents.
 * @param flying Whether we should clear Z velocity as well if we are going to stop
 */
static void Pm_Friction(const bool flying) {
  vec3_t vel = pm->s.velocity;

  if (pm->s.flags & PMF_ON_GROUND) {
    vel.z = 0.f;
  }

  const float speed = Vec3_Length(vel);

  if (speed < 1.f) {
    pm->s.velocity.x = pm->s.velocity.y = 0.f;

    if (flying) {
      pm->s.velocity.z = 0.f;
    }

    return;
  }

  const float control = Maxf(pm->s.params.speed_stop, speed);

  float friction = 0.f;

  if (pm->s.type == PM_SPECTATOR) { // spectator friction
    friction = pm->s.params.friction_spectator;
  } else if (pm->s.flags & PMF_ON_LADDER) { // ladder friction
    friction = pm->s.params.friction_ladder;
  } else if (pm->water_level > WATER_FEET) { // water friction
    friction = pm->s.params.friction_water;
  } else if (pm->s.flags & PMF_ON_GROUND) { // ground friction
    if (pm_locals.ground.ent && (pm_locals.ground.surface & SURF_SLICK)) {
      friction = pm->s.params.friction_ground_slick;
    } else {
      friction = pm->s.params.friction_ground;
    }
  } else { // everything else friction
    friction = pm->s.params.friction_air;
  }

  friction = Maxf(0.f, friction); // never reverse direction

  // scale the velocity, taking care to not reverse direction
  const float scale = Maxf(0.f, speed - (friction * control * pm_locals.time)) / speed;

  pm->s.velocity = Vec3_Scale(pm->s.velocity, scale);
}

/**
 * @brief Handles user intended acceleration.
 */
static bool Pm_StrafeHelperShouldTrack(void) {
  if (!pm_strafe_observer || pm->s.type != PM_NORMAL) {
    return false;
  }
  if ((pm->s.flags & PMF_ON_LADDER) || pm->water_level >= WATER_WAIST) {
    return false;
  }
  if (pm->s.flags & (PMF_TIME_TELEPORT | PMF_TIME_WATER_JUMP)) {
    return false;
  }
  return pm->cmd.right != 0;
}

static void Pm_UpdateStrafeHelper(const vec3_t dir, const float speed,
                                  const float accel) {
  if (!pm_strafe_observer) {
    return;
  }

  race_strafe_sample_t sample = { 0 };
  if (Pm_StrafeHelperShouldTrack()) {
    sample.active = true;
    sample.forward = pm_locals.forward_xy;
    sample.velocity = pm->s.velocity;
    sample.wishdir = dir;
    sample.wishspeed = speed;
    sample.accel = accel;
    sample.frametime = pm_locals.time;
    sample.view_yaw = pm->angles.y;
  }
  pm_strafe_observer(&sample, pm_strafe_observer_context);
}

static void Pm_Accelerate(const vec3_t dir, float speed, float accel) {
  Pm_UpdateStrafeHelper(dir, speed, accel);

  const float current_speed = Vec3_Dot(pm->s.velocity, dir);
  const float add_speed = speed - current_speed;

  if (add_speed <= 0.f) {
    return;
  }

  float accel_speed = accel * pm_locals.time * speed;

  if (accel_speed > add_speed) {
    accel_speed = add_speed;
  }

  pm->s.velocity = Vec3_Fmaf(pm->s.velocity, accel_speed, dir);
}

/**
 * @brief Applies gravity to the current movement.
 */
static void Pm_Gravity(void) {

  if (pm->s.type == PM_HOOK_PULL) {
    return;
  }

  float gravity = pm->s.params.gravity;

  if (pm->water_level > WATER_WAIST) {
    gravity *= pm->s.params.gravity_water;
  }

  pm->s.velocity.z -= gravity * pm_locals.time;
}

/**
 * @brief Applies water and conveyor belt current velocities to the player.
 */
static void Pm_Currents(void) {
  vec3_t current = Vec3_Zero();

  // add water currents
  if (pm->water_level) {
    if (pm->water_type & CONTENTS_CURRENT_0) {
      current.x += 1.f;
    }
    if (pm->water_type & CONTENTS_CURRENT_90) {
      current.y += 1.f;
    }
    if (pm->water_type & CONTENTS_CURRENT_180) {
      current.x -= 1.f;
    }
    if (pm->water_type & CONTENTS_CURRENT_270) {
      current.y -= 1.f;
    }
    if (pm->water_type & CONTENTS_CURRENT_UP) {
      current.z += 1.f;
    }
    if (pm->water_type & CONTENTS_CURRENT_DOWN) {
      current.z -= 1.f;
    }
  }

  // add conveyer belt velocities
  if (pm->ground.ent) {
    if (pm_locals.ground.contents & CONTENTS_CURRENT_0) {
      current.x += 1.f;
    }
    if (pm_locals.ground.contents & CONTENTS_CURRENT_90) {
      current.y += 1.f;
    }
    if (pm_locals.ground.contents & CONTENTS_CURRENT_180) {
      current.x -= 1.f;
    }
    if (pm_locals.ground.contents & CONTENTS_CURRENT_270) {
      current.y -= 1.f;
    }
    if (pm_locals.ground.contents & CONTENTS_CURRENT_UP) {
      current.z += 1.f;
    }
    if (pm_locals.ground.contents & CONTENTS_CURRENT_DOWN) {
      current.z -= 1.f;
    }
  }

  if (!Vec3_Equal(current, Vec3_Zero())) {
    current = Vec3_Normalize(current);
  }

  pm->s.velocity = Vec3_Fmaf(pm->s.velocity, PM_SPEED_CURRENT, current);
}

/**
 * @return True if the player will be eligible for trick jumping should they
 * impact the ground on this frame, false otherwise.
 */
static bool Pm_CheckTrickJump(void) {

  if (pm->ground.ent) {
    return false;
  }

  if (pm_locals.previous_velocity.z < PM_SPEED_UP) {
    return false;
  }

  if (pm->cmd.up < 1) {
    return false;
  }

  if (pm->s.flags & PMF_JUMP_HELD) {
    return false;
  }

  if (pm->s.flags & PMF_TIME_MASK) {
    return false;
  }

  return true;
}

/**
 * @return True if the player is attempting to leave the ground via grappling hook.
 */
static bool Pm_CheckHookJump(void) {

  if ((pm->s.type >= PM_HOOK_PULL && pm->s.type <= PM_HOOK_SWING_AUTO) && (pm->s.velocity.z > 1.f)) {

    pm->s.flags &= ~PMF_ON_GROUND;
    memset(&pm->ground, 0, sizeof(pm->ground));

    return true;
  }

  return false;
}

/**
 * @brief Validates and processes grappling hook state, updating movement type as needed.
 */
static void Pm_CheckHook(void) {

  // hookers only
  if (pm->s.type < PM_HOOK_PULL || pm->s.type > PM_HOOK_SWING_AUTO) {
    pm->s.flags &= ~PMF_HOOK_RELEASED;
    return;
  }

  // if we let go of hook, just go back to normal
  if ((pm->s.type == PM_HOOK_PULL || pm->s.type == PM_HOOK_SWING_AUTO) && !(pm->cmd.buttons & BUTTON_HOOK)) {
    pm->s.type = PM_NORMAL;
    return;
  }

  // get chain length
  if (pm->s.type == PM_HOOK_PULL) {

    pm->cmd.forward = pm->cmd.right = 0;

    // pull physics
    const float dist = Vec3_DistanceDir(pm->s.hook_position, pm->s.origin, &pm->s.velocity);
    if (dist > PM_HOOK_MIN_DIST && !Pm_CheckHookJump()) {
      pm->s.velocity = Vec3_Scale(pm->s.velocity, pm->hook_pull_speed);
    } else {
      pm->s.velocity = Vec3_Zero();
    }
  } else {

    // check for disable
    if (!(pm->s.flags & PMF_HOOK_RELEASED)) {

      if (!(pm->cmd.buttons & BUTTON_HOOK)) {
        pm->s.flags |= PMF_HOOK_RELEASED;
      }
    } else {

      // if we let go of hook, just go back to normal.
      if (pm->cmd.buttons & BUTTON_HOOK) {
        pm->s.type = PM_NORMAL;
        pm->s.flags &= ~PMF_HOOK_RELEASED;
        return;
      }
    }

    const float hook_rate = (pm->hook_pull_speed / 1.5f) * pm_locals.time;

    // chain physics
    // grow/shrink chain based on input
    if ((pm->cmd.up > 0 || !(pm->s.flags & PMF_HOOK_RELEASED)) && (pm->s.hook_length > PM_HOOK_MIN_DIST)) {
      pm->s.hook_length = Maxf(pm->s.hook_length - hook_rate, PM_HOOK_MIN_DIST);
    } else if ((pm->cmd.up < 0) && (pm->s.hook_length < PM_HOOK_MAX_DIST)) {
      pm->s.hook_length = Minf(pm->s.hook_length + hook_rate, PM_HOOK_MAX_DIST);
    }

    vec3_t chain_vec = Vec3_Subtract(pm->s.hook_position, pm->s.origin);
    float chain_len = Vec3_Length(chain_vec);

    // if player's location is already within the chain's reach
    if (chain_len <= pm->s.hook_length) {
      return;
    }

    // reel us in!
    vec3_t vel_part;

    // determine player's velocity component of chain vector
    vel_part = Vec3_Scale(chain_vec, Vec3_Dot(pm->s.velocity, chain_vec) / Vec3_Dot(chain_vec, chain_vec));

    // restrainment default force
    float force = (chain_len - pm->s.hook_length) * 5.f;

    // if player's velocity heading is away from the hook
    if (Vec3_Dot(pm->s.velocity, chain_vec) < 0.f) {

      // if chain has streched for PM_HOOK_MIN_DIST units
      if (chain_len > pm->s.hook_length + PM_HOOK_MIN_DIST) {

        // remove player's velocity component moving away from hook
        pm->s.velocity = Vec3_Subtract(pm->s.velocity, vel_part);
      }
    } else { // if player's velocity heading is towards the hook

      if (Vec3_Length(vel_part) < force) {
        force -= Vec3_Length(vel_part);
      } else {
        force = 0.f;
      }
    }

    if (force) {
      // applies chain restrainment
      chain_vec = Vec3_Normalize(chain_vec);
      pm->s.velocity = Vec3_Fmaf(pm->s.velocity, force, chain_vec);
    }
  }
}

/**
 * @brief Clears both forms of ground state used by movement callers.
 */
static void Pm_ClearGround(void) {
  pm->s.flags &= ~PMF_ON_GROUND;
  memset(&pm->ground, 0, sizeof(pm->ground));
}

/**
 * @return The recovered deliberate ground-loss threshold for the active Q2
 * preset.
 */
static float Pm_Q2RampGroundLossSpeed(void) {
  return pm_physics_policy->ramp_ground_loss_speed;
}

/**
 * @return True when the Quetoo Fix hybrid may use its predictive ground probe.
 * Quake II disables trick probing.
 */
static bool Pm_Q2CheckTrickJump(void) {
  if (!pm_physics_policy->trick_probe) {
    return false;
  }

  if (pm->ground.ent) {
    return false;
  }

  if (pm_locals.previous_velocity.z < PM_Q2_UPWARD_SPEED_EPSILON) {
    return false;
  }

  if (pm->cmd.up < 1) {
    return false;
  }

  if (pm->s.flags & (PMF_JUMP_HELD | PMF_TIME_MASK)) {
    return false;
  }

  return true;
}

/**
 * @brief Applies the recovered Q2 ground classification and ramp-contact
 * policy. Accepted traces classify contact without sinking the player origin.
 */
static void Pm_Q2CheckGround(void) {
  pm_locals.ramp_contact_slide = false;

  const bool ladder_blocks_ground =
    (pm->s.flags & PMF_ON_LADDER) &&
    !pm_physics_policy->ladder_retains_ground;
  if ((pm->s.flags & (PMF_JUMPED | PMF_TIME_PUSHED)) ||
      ladder_blocks_ground) {
    if (pm->s.flags & PMF_TIME_PUSHED) {
      Pm_ClearGround();
    }
    return;
  }

  if (pm->s.velocity.z > Pm_Q2RampGroundLossSpeed() &&
      (!pm_physics_policy->ramp_ground_loss_requires_jump_held ||
       (pm->s.flags & PMF_JUMP_HELD))) {
    Pm_ClearGround();
    return;
  }

  const bool trick_jump = Pm_Q2CheckTrickJump();
  vec3_t pos = pm->s.origin;

  if (trick_jump) {
    pos = Vec3_Fmaf(pos, pm_locals.time, pm->s.velocity);
  }
  pos.z -= PM_Q2_GROUND_PROBE_DIST;

  const cm_trace_t trace = pm_locals.ground =
    Pm_Trace(pm->s.origin, pos, pm->bounds);

  if (trace.ent &&
      (trace.plane.normal.z >= PM_Q2_GROUND_NORMAL_MIN ||
       trace.start_solid)) {
    const bool was_grounded = pm->s.flags & PMF_ON_GROUND;

    if (!was_grounded) {
      if (pm->s.flags & PMF_TIME_WATER_JUMP) {
        pm->s.flags &= ~PMF_TIME_WATER_JUMP;
        pm->s.time = 0;
      }

      if (trick_jump) {
        pm->s.flags |= PMF_TIME_TRICK_JUMP;
        pm->s.time = 32;
      }
    }

    // Existing held state is intentionally retained: ramp contact is not an
    // automatic jump edge. Release and a later fresh press clear it in Pm_Init.
    pm->s.flags |= PMF_ON_GROUND;
    pm->ground = trace;

    if (pm_physics_policy->ramp_contact_slide &&
        pm->s.velocity.z > pm_physics_policy->ramp_ground_loss_speed &&
        !(pm->s.flags & PMF_JUMP_HELD)) {
      pm_locals.ramp_contact_slide = true;
    }
  } else {
    Pm_ClearGround();
  }

  Pm_TouchEntity(&trace);
}

/**
 * @brief Checks for ground interaction, enabling trick jumping and dealing with landings.
 */
static void Pm_CheckGround(void) {

  if (Pm_CheckHookJump()) {
    return;
  }

  if (Pm_Q2FamilyPhysics()) {
    Pm_Q2CheckGround();
    return;
  }

  // if we jumped, or been pushed, do not attempt to seek ground
  if (pm->s.flags & (PMF_JUMPED | PMF_TIME_PUSHED | PMF_ON_LADDER)) {
    return;
  }

  // seek ground eagerly if the player wishes to trick jump
  const bool trick_jump = Pm_CheckTrickJump();
  vec3_t pos;

  if (trick_jump) {
    pos = Vec3_Fmaf(pm->s.origin, pm_locals.time, pm->s.velocity);
    pos.z -= PM_GROUND_DIST_TRICK;
  } else {
    pos = pm->s.origin;
    pos.z -= PM_GROUND_DIST;
  }

  // seek the ground
  cm_trace_t trace = pm_locals.ground = Pm_Trace(pm->s.origin, pos, pm->bounds);

  // if we hit an upward facing plane, make it our ground
  if (trace.ent && trace.plane.normal.z >= PM_STEP_NORMAL) {

    // if we had no ground, then handle landing events
    if (!pm->ground.ent) {

      // any landing terminates the water jump
      if (pm->s.flags & PMF_TIME_WATER_JUMP) {
        pm->s.flags &= ~PMF_TIME_WATER_JUMP;
        pm->s.time = 0;
      }

      // hard landings disable jumping briefly
      if (pm_locals.previous_velocity.z <= PM_SPEED_LAND) {
        pm->s.flags |= PMF_TIME_LAND;
        pm->s.time = 1;

        if (pm_locals.previous_velocity.z <= PM_SPEED_FALL) {
          pm->s.time = 16;

          if (pm_locals.previous_velocity.z <= PM_SPEED_FALL_FAR) {
            pm->s.time = 256;
          }
        }
      } else { // soft landings with upward momentum grant trick jumps
        if (trick_jump) {
          pm->s.flags |= PMF_TIME_TRICK_JUMP;
          pm->s.time = 32;
        }
      }
    }

    // save a reference to the ground
    pm->s.flags |= PMF_ON_GROUND;
    pm->ground = trace;

    // and sink down to it if not trick jumping
    if (!(pm->s.flags & PMF_TIME_TRICK_JUMP)) {
      pm->s.origin = trace.end;
    }
  } else {
    pm->s.flags &= ~PMF_ON_GROUND;
    memset(&pm->ground, 0, sizeof(pm->ground));
  }

  // always touch the entity, even if we couldn't stand on it
  Pm_TouchEntity(&trace);
}

#if defined(RACE_PHYSICS_TEST)
void Pm_Q2CheckGroundForTest(pm_move_t *move,
                             const vec3_t previous_velocity) {
  Pm_BindPhysics();
  pm = move;
  memset(&pm_locals, 0, sizeof(pm_locals));
  pm_locals.previous_origin = move->s.origin;
  pm_locals.previous_velocity = previous_velocity;
  pm_locals.time = move->cmd.msec * .001f;
  Pm_CheckGround();
}
#endif

/**
 * @brief Checks for water interaction, accounting for player ducking, etc.
 */
static void Pm_CheckWater(void) {

  pm->water_level = WATER_NONE;
  pm->water_type = 0;

  vec3_t pos = pm->s.origin;
  pos.z = pm->s.origin.z + pm->bounds.mins.z +
          (Pm_Q2FamilyPhysics() ? PM_Q2_GROUND_PROBE_DIST : PM_GROUND_DIST);

  int32_t contents = pm->PointContents(pos);
  if (contents & CONTENTS_MASK_LIQUID) {

    pm->water_type = contents;
    pm->water_level = WATER_FEET;

    pos.z = pm->s.origin.z;

    contents = pm->PointContents(pos);

    if (contents & CONTENTS_MASK_LIQUID) {

      pm->water_type |= contents;
      pm->water_level = WATER_WAIST;

      pos.z = pm->s.origin.z + pm->s.view_offset.z + 1.f;

      contents = pm->PointContents(pos);

      if (contents & CONTENTS_MASK_LIQUID) {
        pm->water_type |= contents;
        pm->water_level = WATER_UNDER;

        pm->s.flags |= PMF_UNDER_WATER;
      }
    }
  }
}

/**
 * @brief Handles ducking, adjusting both the player's bounding box and view
 * offset accordingly. Players must be on the ground in order to duck.
 */
static void Pm_CheckDuck(void) {

  if (pm->s.type == PM_DEAD) {
    if (pm->s.flags & PMF_GIBLET) {
      pm->s.view_offset.z = 0.f;
    } else {
      pm->s.view_offset.z = -16.f;
    }
  } else {

    const bool is_ducking = pm->s.flags & PMF_DUCKED;

    if (Pm_Q2FamilyPhysics()) {
      const box3_t standing_bounds = Pm_CurrentPlayerBounds(false);
      const bool wants_ducking = pm->cmd.up < 0 &&
                                 !(pm->s.flags & PMF_ON_LADDER) &&
                                 (pm->s.flags & PMF_ON_GROUND);

      pm->bounds = standing_bounds;

      if (!is_ducking && wants_ducking) {
        pm->s.flags |= PMF_DUCKED;
      } else if (is_ducking && !wants_ducking) {
        const cm_trace_t trace = Pm_Trace(
          pm->s.origin, pm->s.origin, standing_bounds);

        if (!trace.all_solid && !trace.start_solid) {
          pm->s.flags &= ~PMF_DUCKED;
        }
      }

      if (pm->s.flags & PMF_DUCKED) {
        pm->bounds = Pm_CurrentPlayerBounds(true);
        pm->s.view_offset.z = -2.f;
      } else {
        pm->s.view_offset.z = 22.f;
      }

      return;
    }

    const bool wants_ducking = (pm->cmd.up < 0) && !(pm->s.flags & PMF_ON_LADDER);

    if (!is_ducking && wants_ducking) {
      pm->s.flags |= PMF_DUCKED;
    } else if (is_ducking && !wants_ducking) {
      const cm_trace_t trace = Pm_Trace(pm->s.origin, pm->s.origin, pm->bounds);

      if (!trace.all_solid && !trace.start_solid) {
        pm->s.flags &= ~PMF_DUCKED;
      }
    }

    const float height = Box3_Size(pm->bounds).z;
    const float duck_stand_speed = Maxf(0.f, pm->s.params.speed_duck_stand); // never reverse the transition

    if (pm->s.flags & PMF_DUCKED) { // ducked, reduce height
      const float target = pm->bounds.mins.z + height * 0.5f;

      if (pm->s.view_offset.z > target) { // go down
        pm->s.view_offset.z -= pm_locals.time * duck_stand_speed;
      }

      if (pm->s.view_offset.z < target) {
        pm->s.view_offset.z = target;
      }

      // change the bounding box to reflect ducking
      pm->bounds = PM_CROUCHED_BOUNDS;
    } else {
      const float target = pm->bounds.mins.z + height * 0.9f;

      if (pm->s.view_offset.z < target) { // go up
        pm->s.view_offset.z += pm_locals.time * duck_stand_speed;
      }

      if (pm->s.view_offset.z > target) {
        pm->s.view_offset.z = target;
      }
    }
  }

  pm->s.view_offset = pm->s.view_offset;
}

/**
 * @brief Check for jumping and trick jumping.
 *
 * @return True if a jump occurs, false otherwise.
 */
static bool Pm_Dp2CheckJump(void) {
  pm->s.flags |= PMF_JUMP_HELD;
  Pm_ClearGround();
  memset(&pm_locals.ground, 0, sizeof(pm_locals.ground));

  if (pm->s.velocity.z >= pm_physics_policy->jump_speed_max) {
    return false;
  }

  float jump = pm_physics_policy->jump_impulse;
  if (pm->s.velocity.z + jump > pm_physics_policy->jump_speed_max) {
    jump = pm_physics_policy->jump_speed_max - pm->s.velocity.z;
  }

  pm->s.velocity.z += jump;
  if (pm->s.velocity.z < jump) {
    pm->s.velocity.z = jump;
  }

  pm->s.flags |= PMF_JUMPED;
  Pm_Debug("DP2 jump: %d\n", pm->cmd.up);
  return true;
}

static bool Pm_CheckJump(void) {

  if (Pm_CheckHookJump()) {
    return true;
  }

  // must wait for landing damage to subside
  if (pm->s.flags & PMF_TIME_LAND) {
    return false;
  }

  const bool q2 = Pm_Q2FamilyPhysics();

  if (q2 && pm->cmd.up < PM_Q2_JUMP_UP_MIN) {
    // Preserve the established ground and ladder positive-up latch. The wider
    // Q2 release threshold belongs only to ordinary liquid input.
    if (pm->water_level >= WATER_WAIST &&
        !(pm->s.flags & PMF_ON_LADDER)) {
      pm->s.flags &= ~PMF_JUMP_HELD;
    }
    return false;
  }

  // must wait for jump key to be released
  if (pm->s.flags & PMF_JUMP_HELD) {
    return false;
  }

  // didn't ask to jump
  if (pm->cmd.up < 1) {
    return false;
  }

  if (q2 && pm->water_level >= WATER_WAIST) {
    pm->s.flags &= ~PMF_ON_GROUND;
    memset(&pm->ground, 0, sizeof(pm->ground));
    memset(&pm_locals.ground, 0, sizeof(pm_locals.ground));

    if (pm->s.velocity.z <= -300.f) {
      return false;
    }

    if (pm->water_type == CONTENTS_WATER) {
      pm->s.velocity.z = 100.f;
    } else if (pm->water_type == CONTENTS_SLIME) {
      pm->s.velocity.z = 80.f;
    } else {
      pm->s.velocity.z = 50.f;
    }

    pm->s.flags |= PMF_JUMP_HELD;
    return false;
  }

  if (q2 && !(pm->s.flags & PMF_ON_GROUND)) {
    return false;
  }

  if (q2 && !pm->ground.ent) {
    pm->s.flags &= ~PMF_ON_GROUND;
    return false;
  }

  if (q2) {
    switch (pm_physics_policy->jump_variant) {
      case RACE_PM_JUMP_STANDARD:
        break;
      case RACE_PM_JUMP_DP2:
        return Pm_Dp2CheckJump();
      default:
        assert(false);
        return false;
    }
  }

  // finally, do the jump
  float jump = Maxf(0.f, pm->s.params.speed_jump);

  // factoring in water level
  if (pm->water_level > WATER_FEET) {
    jump *= PM_SPEED_JUMP_MOD_WATER;
  }

  // adding the trick jump if eligible
  if (pm_physics_policy->trick_probe &&
      (pm->s.flags & PMF_TIME_TRICK_JUMP)) {
    jump += pm_physics_policy->trick_jump_speed;

    pm->s.flags &= ~PMF_TIME_TRICK_JUMP;
    pm->s.time = 0;

    Pm_Debug("Q2 trick jump: %d\n", pm->cmd.up);
  } else if (!Pm_Q2FamilyPhysics() &&
             (pm->s.flags & PMF_TIME_TRICK_JUMP)) {
    jump += PM_SPEED_TRICK_JUMP;

    pm->s.flags &= ~PMF_TIME_TRICK_JUMP;
    pm->s.time = 0;

    Pm_Debug("Trick jump: %d\n", pm->cmd.up);
  } else {
    Pm_Debug("Jump: %d\n", pm->cmd.up);
  }

  if (pm->s.velocity.z < 0.f) {
    pm->s.velocity.z = jump;
  } else {
    pm->s.velocity.z += jump;
  }

  // indicate that jump is currently held
  pm->s.flags |= (PMF_JUMPED | PMF_JUMP_HELD);

  // clear the ground indicators
  pm->s.flags &= ~PMF_ON_GROUND;
  memset(&pm->ground, 0, sizeof(pm->ground));

  if (!Pm_Q2FamilyPhysics()) {
    // we can trick jump soon
    pm->s.flags |= PMF_TIME_TRICK_START;
    pm->s.time = 100;
  }

  return true;
}

/**
 * @brief Check for ladder interaction.
 *
 * @return True if the player is on a ladder, false otherwise.
 */
static void Pm_CheckLadder(void) {

  if (pm->s.flags & PMF_TIME_MASK) {
    return;
  }

  if (pm->s.type >= PM_HOOK_PULL && pm->s.type <= PM_HOOK_SWING_AUTO) {
    return;
  }

  const bool q2 = Pm_Q2FamilyPhysics();
  const float probe_dist = q2 ? PM_Q2_LADDER_PROBE_DIST : 4.f;
  const box3_t bounds = q2 ? Pm_CurrentPlayerBounds(false) : pm->bounds;
  const vec3_t pos = Vec3_Fmaf(pm->s.origin, probe_dist,
                              pm_locals.forward_xy);
  const cm_trace_t trace = Pm_Trace(pm->s.origin, pos, bounds);

  const bool attach = q2
    ? trace.fraction < 1.f && (trace.contents & CONTENTS_LADDER)
    : trace.contents & CONTENTS_LADDER;

  if (attach) {
    pm->s.flags |= PMF_ON_LADDER;

    if (!pm_physics_policy->ladder_retains_ground) {
      memset(&pm->ground, 0, sizeof(pm->ground));
      pm->s.flags &= ~(PMF_ON_GROUND | PMF_DUCKED);

      if (q2 && pm->cmd.up > 0) {
        pm->s.flags |= PMF_JUMP_HELD;
      }
    }
  }
}

/**
 * @brief Checks for water exit. The player may exit the water when they can
 * see a usable step out of the water.
 *
 * @return True if a water jump has occurred, false otherwise.
 */
static bool Pm_CheckWaterJump(void) {

  if (pm->s.type >= PM_HOOK_PULL && pm->s.type <= PM_HOOK_SWING_AUTO) {
    return false;
  }

  if (Pm_Q2FamilyPhysics()) {
    if (pm->s.time || pm->water_level != WATER_WAIST) {
      return false;
    }

    vec3_t spot = Vec3_Fmaf(pm->s.origin, 30.f, pm_locals.forward_xy);
    spot.z += 4.f;

    if (!(pm->PointContents(spot) & CONTENTS_SOLID)) {
      return false;
    }

    spot.z += 16.f;

    if (pm->PointContents(spot)) {
      return false;
    }

    pm->s.velocity = Vec3_Scale(pm_locals.forward_xy, 50.f);
    pm->s.velocity.z = Maxf(0.f, pm->s.params.speed_water_jump);
    pm->s.flags |= PMF_TIME_WATER_JUMP;
    pm->s.time = Pm_Q2Time(PM_Q2_TIME_WATER_JUMP);
    return true;
  }

  if (pm->s.flags & PMF_TIME_WATER_JUMP) {
    return false;
  }

  if (pm->water_level != WATER_WAIST) {
    return false;
  }

  if (pm->cmd.up < 1 && pm->cmd.forward < 1) {
    return false;
  }

  vec3_t pos = Vec3_Fmaf(pm->s.origin, 16.f, pm_locals.forward);
  cm_trace_t trace = Pm_Trace(pm->s.origin, pos, pm->bounds);

  if (trace.contents & CONTENTS_MASK_SOLID) {

    pos.z += PM_STEP_HEIGHT + Box3_Size(pm->bounds).z;

    trace = Pm_Trace(pos, pos, pm->bounds);

    if (trace.start_solid) {
      Pm_Debug("Can't exit water: blocked\n");
      return false;
    }

    vec3_t pos2 = Vec3(pos.x, pos.y, pm->s.origin.z);

    trace = Pm_Trace(pos, pos2, pm->bounds);

    if (!(trace.ent && trace.plane.normal.z >= PM_STEP_NORMAL)) {
      Pm_Debug("Can't exit water: not a step\n");
      return false;
    }

    // jump out of water
    pm->s.velocity.z = Maxf(0.f, pm->s.params.speed_water_jump);

    pm->s.flags |= PMF_TIME_WATER_JUMP | PMF_JUMP_HELD;
    pm->s.time = 2000;

    return true;
  }

  return false;
}

/**
 * @brief Handles player movement while climbing a ladder.
 */
static void Pm_Q2LadderMove(void);

static void Pm_LadderMove(void) {

  if (Pm_Q2FamilyPhysics() && pm->water_level < WATER_WAIST) {
    Pm_Q2LadderMove();
    return;
  }

  Pm_Debug("%s\n", vtos(pm->s.origin));

  Pm_Friction(false);

  Pm_Currents();

  const float ladder_speed = Maxf(0.f, pm->s.params.speed_ladder);
  const float ladder_accel = Maxf(0.f, pm->s.params.accel_ladder);

  // user intentions in X/Y
  vec3_t vel = Vec3_Zero();
  vel = Vec3_Fmaf(vel, pm->cmd.forward, pm_locals.forward_xy);
  vel = Vec3_Fmaf(vel, pm->cmd.right, pm_locals.right_xy);

  const float s = ladder_speed * 0.125f;

  // limit horizontal speed when on a ladder
  vel.x = Clampf(vel.x, -s, s);
  vel.y = Clampf(vel.y, -s, s);
  vel.z = 0.f;

  // handle Z intentions differently
  if (fabsf(pm->s.velocity.z) < ladder_speed) {

    if ((pm->angles.x <= -15.f) && (pm->cmd.forward > 0)) {
      vel.z = ladder_speed;
    } else if ((pm->angles.x >= 15.f) && (pm->cmd.forward > 0)) {
      vel.z = -ladder_speed;
    } else if (pm->cmd.up > 0) {
      vel.z = ladder_speed;
    } else if (pm->cmd.up < 0) {
      vel.z = -ladder_speed;
    } else {
      vel.z = 0.f;
    }
  }

  if (pm->cmd.up > 0) { // avoid jumps when exiting ladders
    pm->s.flags |= PMF_JUMP_HELD;
  }

  float speed;
  const vec3_t dir = Vec3_NormalizeLength(vel, &speed);
  speed = Clampf(speed, 0.f, ladder_speed);

  if (speed < PM_STOP_EPSILON) {
    speed = 0.f;
  }

  Pm_Accelerate(dir, speed, ladder_accel);

  Pm_StepSlideMove();
}

/**
 * @brief Handles player movement during a water jump, propelling the player out of the water.
 */
static void Pm_WaterJumpMove(void) {

  Pm_Debug("%s\n", vtos(pm->s.origin));

  if (Pm_Q2FamilyPhysics()) {
    Pm_Gravity();

    if (pm->s.velocity.z <= 0.f) {
      pm->s.flags &= ~PMF_TIME_MASK;
      pm->s.time = 0;
    }

    Pm_StepSlideMove();
    return;
  }

  Pm_Friction(false);

  Pm_Gravity();

  // check for a usable spot directly in front of us
  const vec3_t pos = Vec3_Fmaf(pm->s.origin, 30.f, pm_locals.forward_xy);

  // if we've reached a usable spot, clamp the jump to avoid launching
  if (Pm_Trace(pm->s.origin, pos, pm->bounds).fraction == 1.f) {
    pm->s.velocity.z = Clampf(pm->s.velocity.z, 0.f, Maxf(0.f, pm->s.params.speed_jump));
  }

  // if we're falling back down, clear the timer to regain control
  if (pm->s.velocity.z <= 0.f) {
    pm->s.flags &= ~PMF_TIME_MASK;
    pm->s.time = 0;
  }

  Pm_StepSlideMove();
}

/**
 * @brief Handles player movement while submerged or wading in water.
 */
static void Pm_WaterMove(void) {

  if (Pm_CheckWaterJump()) {
    Pm_WaterJumpMove();
    return;
  }

  Pm_Debug("%s\n", vtos(pm->s.origin));

  const float water_speed = Maxf(1.f, pm->s.params.speed_water); // also a loop divisor below

  // apply friction, slowing rapidly when first entering the water
  float speed = Vec3_Length(pm->s.velocity);

  for (int32_t i = speed / water_speed; i >= 0; i--) {
    Pm_Friction(true);
  }

  // and sink
  if (!pm->cmd.forward && !pm->cmd.right && !pm->cmd.up && (pm->s.type < PM_HOOK_PULL || pm->s.type > PM_HOOK_SWING_AUTO)) {
    if (pm->s.velocity.z > PM_SPEED_WATER_SINK) {
      Pm_Gravity();
    }
  }

  Pm_Currents();

  // user intentions on X/Y/Z
  vec3_t vel = Vec3_Zero();
  vel = Vec3_Fmaf(vel, pm->cmd.forward, pm_locals.forward);
  vel = Vec3_Fmaf(vel, pm->cmd.right, pm_locals.right);

  // add explicit Z
  vel.z += pm->cmd.up;

  // disable water skiing
  if (pm->s.type < PM_HOOK_PULL || pm->s.type > PM_HOOK_SWING_AUTO) {
    if (pm->water_level == WATER_WAIST) {
      vec3_t view = Vec3_Add(pm->s.origin, pm->s.view_offset);
      view.z -= 4.f;

      if (!(pm->PointContents(view) & CONTENTS_MASK_LIQUID)) {
        pm->s.velocity.z = Minf(pm->s.velocity.z, 0.f);
        vel.z = Minf(vel.z, 0.f);
      }
    }
  }

  const vec3_t dir = Vec3_NormalizeLength(vel, &speed);
  speed = Clampf(speed, 0, water_speed);

  if (speed < PM_STOP_EPSILON) {
    speed = 0.f;
  }

  Pm_Accelerate(dir, speed, Maxf(0.f, pm->s.params.accel_water));

  if (pm->cmd.up > 0) {
    Pm_SlideMove();
  } else {
    Pm_StepSlideMove();
  }
}

/**
 * @brief Applies the legacy Q2 acceleration-only air wish-speed cap.
 */
static void Pm_Q2AirAccelerate(const vec3_t dir, float speed, float accel) {
  Pm_UpdateStrafeHelper(dir, speed, accel);

  float add_speed_cap = speed;
  const float wishspeed_cap = Pm_Q2AirWishspeedCap();

  if (wishspeed_cap > 0.f) {
    add_speed_cap = Minf(add_speed_cap, wishspeed_cap);
  }

  const float current_speed = Vec3_Dot(pm->s.velocity, dir);
  const float add_speed = add_speed_cap - current_speed;

  if (add_speed <= 0.f) {
    return;
  }

  float accel_speed = accel * pm_locals.time * speed;

  if (accel_speed > add_speed) {
    accel_speed = add_speed;
  }

  pm->s.velocity = Vec3_Fmaf(pm->s.velocity, accel_speed, dir);
}

/**
 * @brief Adds Q2 water and ground currents to the desired movement vector
 * before it is normalized.
 */
static vec3_t Pm_Q2AddCurrents(vec3_t wish_velocity) {
  const float ladder_speed = Maxf(0.f, pm->s.params.speed_ladder);
  if ((pm->s.flags & PMF_ON_LADDER) &&
      fabsf(pm->s.velocity.z) <= ladder_speed) {
    if (pm->angles.x <= -15.f && pm->cmd.forward > 0) {
      wish_velocity.z = ladder_speed;
    } else if (pm->angles.x >= 15.f && pm->cmd.forward > 0) {
      wish_velocity.z = -ladder_speed;
    } else if (pm->cmd.up > 0) {
      wish_velocity.z = ladder_speed;
    } else if (pm->cmd.up < 0) {
      wish_velocity.z = -ladder_speed;
    } else {
      wish_velocity.z = 0.f;
    }

    const float horizontal_speed = ladder_speed * .125f;
    wish_velocity.x = Clampf(wish_velocity.x,
                            -horizontal_speed, horizontal_speed);
    wish_velocity.y = Clampf(wish_velocity.y,
                            -horizontal_speed, horizontal_speed);
  }

  if (pm->water_type) {
    vec3_t current = Vec3_Zero();

    if (pm->water_type & CONTENTS_CURRENT_0) {
      current.x += 1.f;
    }
    if (pm->water_type & CONTENTS_CURRENT_90) {
      current.y += 1.f;
    }
    if (pm->water_type & CONTENTS_CURRENT_180) {
      current.x -= 1.f;
    }
    if (pm->water_type & CONTENTS_CURRENT_270) {
      current.y -= 1.f;
    }
    if (pm->water_type & CONTENTS_CURRENT_UP) {
      current.z += 1.f;
    }
    if (pm->water_type & CONTENTS_CURRENT_DOWN) {
      current.z -= 1.f;
    }

    float speed = Maxf(0.f, pm->s.params.speed_water);
    if (pm->water_level == WATER_FEET && pm->ground.ent) {
      speed *= .5f;
    }

    wish_velocity = Vec3_Fmaf(wish_velocity, speed, current);
  }

  if (pm->ground.ent) {
    vec3_t current = Vec3_Zero();

    if (pm_locals.ground.contents & CONTENTS_CURRENT_0) {
      current.x += 1.f;
    }
    if (pm_locals.ground.contents & CONTENTS_CURRENT_90) {
      current.y += 1.f;
    }
    if (pm_locals.ground.contents & CONTENTS_CURRENT_180) {
      current.x -= 1.f;
    }
    if (pm_locals.ground.contents & CONTENTS_CURRENT_270) {
      current.y -= 1.f;
    }
    if (pm_locals.ground.contents & CONTENTS_CURRENT_UP) {
      current.z += 1.f;
    }
    if (pm_locals.ground.contents & CONTENTS_CURRENT_DOWN) {
      current.z -= 1.f;
    }

    wish_velocity = Vec3_Fmaf(wish_velocity, PM_SPEED_CURRENT, current);
  }

  return wish_velocity;
}

/**
 * @brief Builds the Q2-family command wish vector shared by ground and air.
 */
static vec3_t Pm_Q2WishVelocity(void) {
  vec3_t angles = pm->angles;
  if (angles.x > 180.f) {
    angles.x -= 360.f;
  }
  angles.x /= 3.f;

  vec3_t forward, right;
  Vec3_Vectors(angles, &forward, &right, NULL);

  vec3_t velocity = Vec3_Zero();
  velocity = Vec3_Fmaf(velocity, pm->cmd.forward, forward);
  velocity = Vec3_Fmaf(velocity, pm->cmd.right, right);
  velocity.z = 0.f;
  return Pm_Q2AddCurrents(velocity);
}

/**
 * @brief Handles Q2-family waist and under-water movement, including
 * water-covered ladders.
 */
static void Pm_Q2WaterMove(void) {
  Pm_Debug("%s\n", vtos(pm->s.origin));

  vec3_t velocity = Vec3_Zero();
  velocity = Vec3_Fmaf(velocity, pm->cmd.forward, pm_locals.forward);
  velocity = Vec3_Fmaf(velocity, pm->cmd.right, pm_locals.right);

  if (!pm->cmd.forward && !pm->cmd.right && !pm->cmd.up) {
    velocity.z -= 60.f;
  } else {
    velocity.z += pm->cmd.up;
  }

  velocity = Pm_Q2AddCurrents(velocity);

  float speed;
  const vec3_t dir = Vec3_NormalizeLength(velocity, &speed);
  speed = Clampf(speed, 0.f, Maxf(0.f, pm->s.params.speed_ground));
  speed *= .5f;

  if (speed < PM_STOP_EPSILON) {
    speed = 0.f;
  }

  Pm_Accelerate(dir, speed, Maxf(0.f, pm->s.params.accel_water));
  Pm_StepSlideMove();
}

/**
 * @brief Handles the bounded Q2-family airborne wish and acceleration path.
 */
static void Pm_Q2AirMove(void) {
  Pm_Debug("%s\n", vtos(pm->s.origin));

  const vec3_t velocity = Pm_Q2WishVelocity();

  float speed;
  const vec3_t dir = Vec3_NormalizeLength(velocity, &speed);
  const float max_speed = Maxf(0.f, (pm->s.flags & PMF_DUCKED)
    ? pm->s.params.speed_ducked
    : pm->s.params.speed_ground);
  speed = Clampf(speed, 0.f, max_speed);

  if (speed < PM_STOP_EPSILON) {
    speed = 0.f;
  }

  const float accel = Maxf(0.f, pm->s.params.accel_air);
  if (Pm_Q2AirWishspeedCap() > 0.f) {
    Pm_Q2AirAccelerate(dir, speed, accel);
  } else {
    Pm_Accelerate(dir, speed, accel);
  }

  Pm_Gravity();
  Pm_StepSlideMove();
}

/**
 * @brief Handles player movement while airborne, applying friction, gravity, and air acceleration.
 */
static void Pm_AirMove(void) {

  if (Pm_Q2FamilyPhysics()) {
    Pm_Q2AirMove();
    return;
  }

  Pm_Debug("%s\n", vtos(pm->s.origin));

  Pm_Friction(false);

  Pm_Gravity();

  vec3_t vel = Vec3_Zero();
  vel = Vec3_Fmaf(vel, pm->cmd.forward, pm_locals.forward_xy);
  vel = Vec3_Fmaf(vel, pm->cmd.right, pm_locals.right_xy);
  vel.z = 0.f;

  float max_speed = Maxf(1.f, pm->s.params.speed_air); // air_speed must stay positive to bound the wish-speed

  // accounting for walk modulus
  if (pm->cmd.buttons & BUTTON_WALK) {
    max_speed *= PM_SPEED_MOD_WALK;
  }

  float speed;
  const vec3_t dir = Vec3_NormalizeLength(vel, &speed);
  speed = Clampf(speed, 0.f, max_speed);

  if (speed < PM_STOP_EPSILON) {
    speed = 0.f;
  }

  float accel = Maxf(0.f, pm->s.params.accel_air);

  if (pm->s.flags & PMF_DUCKED) {
    accel *= PM_ACCEL_AIR_MOD_DUCKED;
  }

  Pm_Accelerate(dir, speed, accel);

  Pm_StepSlideMove();
}

/**
 * @brief Applies legacy Q2 ground and ladder friction to the incoming
 * three-dimensional velocity.
 */
static void Pm_Q2Friction(void) {
  const float speed = Vec3_Length(pm->s.velocity);

  if (speed < 1.f) {
    pm->s.velocity.x = pm->s.velocity.y = 0.f;
    return;
  }

  float drop = 0.f;
  if (((pm->s.flags & PMF_ON_GROUND) && pm_locals.ground.ent &&
       !pm_locals.ramp_contact_slide &&
       !(pm_locals.ground.surface & SURF_SLICK)) ||
      (pm->s.flags & PMF_ON_LADDER)) {
    const float control = Maxf(pm->s.params.speed_stop, speed);
    drop += control * Maxf(0.f, pm->s.params.friction_ground) *
            pm_locals.time;
  }

  if (pm->water_level && !(pm->s.flags & PMF_ON_LADDER)) {
    drop += speed * Maxf(0.f, pm->s.params.friction_water) *
            pm->water_level * pm_locals.time;
  }

  const float scale = Maxf(0.f, speed - drop) / speed;
  pm->s.velocity = Vec3_Scale(pm->s.velocity, scale);
}

/**
 * @brief Handles the Q2-family dry and feet-depth ladder path.
 */
static void Pm_Q2LadderMove(void) {
  Pm_Debug("%s\n", vtos(pm->s.origin));

  const vec3_t velocity = Pm_Q2WishVelocity();
  float speed;
  const vec3_t dir = Vec3_NormalizeLength(velocity, &speed);
  const float max_speed = Maxf(0.f, (pm->s.flags & PMF_DUCKED)
    ? pm->s.params.speed_ducked
    : pm->s.params.speed_ground);
  speed = Clampf(speed, 0.f, max_speed);

  if (speed < PM_STOP_EPSILON) {
    speed = 0.f;
  }

  Pm_Accelerate(dir, speed, Maxf(0.f, pm->s.params.accel_ladder));

  if (velocity.z == 0.f) {
    const float gravity = pm->s.params.gravity * pm_locals.time;
    if (pm->s.velocity.z > 0.f) {
      pm->s.velocity.z = Maxf(0.f, pm->s.velocity.z - gravity);
    } else {
      pm->s.velocity.z = Minf(0.f, pm->s.velocity.z + gravity);
    }
  }

  Pm_StepSlideMove();
}

/**
 * @brief Handles the Q2-family grounded path without ground-plane wish
 * clipping or post-acceleration velocity normalization.
 */
static void Pm_Q2GroundMove(void) {
  Pm_Debug("%s\n", vtos(pm->s.origin));

  const vec3_t velocity = Pm_Q2WishVelocity();
  float speed;
  const vec3_t dir = Vec3_NormalizeLength(velocity, &speed);
  const float max_speed = Maxf(0.f, (pm->s.flags & PMF_DUCKED)
    ? pm->s.params.speed_ducked
    : pm->s.params.speed_ground);
  speed = Clampf(speed, 0.f, max_speed);

  if (speed < PM_STOP_EPSILON) {
    speed = 0.f;
  }

  pm->s.velocity.z = 0.f;

  const float accel = Maxf(0.f, (pm_locals.ground.surface & SURF_SLICK)
    ? pm->s.params.accel_ground_slick
    : pm->s.params.accel_ground);
  Pm_Accelerate(dir, speed, accel);

  if (pm->s.params.gravity > 0.f) {
    pm->s.velocity.z = 0.f;
  } else {
    pm->s.velocity.z -= pm->s.params.gravity * pm_locals.time;
  }

  if (pm->s.velocity.x || pm->s.velocity.y) {
    Pm_StepSlideMove();
  }
}

/**
 * @brief Handles DP2's rising-ramp contact path. Direction-aligned contact
 * keeps its incoming vertical speed for one command and uses air-like
 * acceleration; rejected contact falls back to Q2 ground movement.
 */
static void Pm_Dp2GroundMove(void) {
  Pm_Debug("%s\n", vtos(pm->s.origin));

  const vec3_t velocity = Pm_Q2WishVelocity();
  float speed;
  const vec3_t dir = Vec3_NormalizeLength(velocity, &speed);
  const float max_speed = Maxf(0.f, (pm->s.flags & PMF_DUCKED)
    ? pm->s.params.speed_ducked
    : pm->s.params.speed_ground);
  speed = Clampf(speed, 0.f, max_speed);

  if (speed < PM_STOP_EPSILON) {
    speed = 0.f;
  }

  if (pm->s.velocity.z > 0.f &&
      pm->s.velocity.z < pm_physics_policy->bump_up_speed_max) {
    pm->s.velocity.z = 0.f;
  }

  float accel = Maxf(0.f, pm->s.params.accel_ground);
  if (pm_locals.ramp_contact_slide) {
    vec3_t current_dir = pm->s.velocity;
    current_dir.z = 0.f;
    current_dir = Vec3_Normalize(current_dir);

    if (Vec3_Dot(current_dir, dir) >
        pm_physics_policy->ramp_slide_direction_min) {
      accel = pm_physics_policy->ramp_slide_accel;
    } else {
      pm_locals.ramp_contact_slide = false;
      Pm_Q2Friction();
      Pm_Q2GroundMove();
      return;
    }
  }

  Pm_Gravity();
  Pm_Accelerate(dir, speed, accel);

  if (pm->s.velocity.x || pm->s.velocity.y) {
    Pm_StepSlideMove();
  }
}

/**
 * @brief Runs the complete Q2-family movement order after initial
 * ladder/hook/duck, water and ground classification.
 */
static void Pm_Q2Move(void) {
  if (!pm->s.time) {
    Pm_CheckWaterJump();
  }

  Pm_DropMovementTime();

  if (pm->s.flags & PMF_TIME_TELEPORT) {
    return;
  }

  if (pm->s.flags & PMF_TIME_WATER_JUMP) {
    Pm_WaterJumpMove();
    return;
  }

  Pm_CheckJump();
  Pm_Q2Friction();

  if (pm->water_level >= WATER_WAIST) {
    Pm_Q2WaterMove();
  } else if (pm->s.flags & PMF_ON_LADDER) {
    Pm_Q2LadderMove();
  } else if (pm->s.flags & PMF_ON_GROUND) {
    switch (pm_physics_policy->ground_variant) {
      case RACE_PM_GROUND_Q2:
        Pm_Q2GroundMove();
        break;
      case RACE_PM_GROUND_DP2:
        if (pm_locals.ramp_contact_slide) {
          Pm_Dp2GroundMove();
        } else {
          Pm_Q2GroundMove();
        }
        break;
      default:
        assert(false);
    }
  } else {
    Pm_Q2AirMove();
  }
}

/**
 * @brief Called for movements where player is on ground, regardless of water level.
 */
static void Pm_WalkMove(void) {

  if (Pm_Q2FamilyPhysics()) {
    Pm_Q2GroundMove();
    return;
  }

  // check for beginning of a jump
  if (Pm_CheckJump()) {
    Pm_AirMove();
    return;
  }

  Pm_Debug("%s\n", vtos(pm->s.origin));

  Pm_Friction(false);

  Pm_Currents();

  // if the player is walking on the sea floor and wishes to swim, let them

  if (pm->water_level == WATER_UNDER && pm_locals.forward.z > 0.f) {

    pm->s.flags &= ~PMF_ON_GROUND;
    memset(&pm->ground, 0, sizeof(pm->ground));

    Pm_WaterMove();
    return;
  }

  // project the desired movement into the X/Y plane

  vec3_t vel = Vec3_Zero();
  vel = Vec3_Fmaf(vel, pm->cmd.forward, pm_locals.forward_xy);
  vel = Vec3_Fmaf(vel, pm->cmd.right, pm_locals.right_xy);

  // clip XY velocity to ground to enable ramp jumps
  vel = Pm_ClipVelocity(vel, pm_locals.ground.plane.normal, PM_CLIP_BOUNCE);

  float max_speed;

  // clamp to max speed
  if (pm->water_level > WATER_FEET) {
    max_speed = pm->s.params.speed_water;
  } else if (pm->s.flags & PMF_DUCKED) {
    max_speed = pm->s.params.speed_ducked;
  } else {
    max_speed = pm->s.params.speed_ground;
  }

  max_speed = Maxf(0.f, max_speed); // keep the Clampf range valid

  // accounting for walk modulus
  if (pm->cmd.buttons & BUTTON_WALK) {
    max_speed *= PM_SPEED_MOD_WALK;
  }

  // clamp the speed to min/max speed
  float speed;
  const vec3_t dir = Vec3_NormalizeLength(vel, &speed);
  speed = Clampf(speed, 0.f, max_speed);

  if (speed < PM_STOP_EPSILON) {
    speed = 0.f;
  }

  // accelerate based on slickness of ground surface
  const float accel = Maxf(0.f, (pm_locals.ground.surface & SURF_SLICK)
      ? pm->s.params.accel_ground_slick : pm->s.params.accel_ground);

  Pm_Accelerate(dir, speed, accel);

  // determine the speed after acceleration
  speed = Vec3_Length(pm->s.velocity);

  // and now scale by the speed to avoid slowing down on slopes
  pm->s.velocity = Vec3_Normalize(pm->s.velocity);
  pm->s.velocity = Vec3_Scale(pm->s.velocity, speed);

  // and finally, step if moving in X/Y
  if (pm->s.velocity.x || pm->s.velocity.y) {
    Pm_StepSlideMove();
  }
}

/**
 * @brief Handles spectator movement, allowing free-fly navigation through the world.
 */
static void Pm_SpectatorMove(void) {

  Pm_Friction(true);

  // user intentions on X/Y/Z
  vec3_t vel = Vec3_Zero();
  vel = Vec3_Fmaf(vel, pm->cmd.forward, pm_locals.forward);
  vel = Vec3_Fmaf(vel, pm->cmd.right, pm_locals.right);

  // add explicit Z
  vel.z += pm->cmd.up;

  float speed;
  vel = Vec3_NormalizeLength(vel, &speed);
  speed = Clampf(speed, 0.f, Maxf(0.f, pm->s.params.speed_spectator));

  if (speed < PM_STOP_EPSILON) {
    speed = 0.f;
  }

  // accelerate
  Pm_Accelerate(vel, speed, Maxf(0.f, pm->s.params.accel_spectator));

  // do the move
  pm->s.origin = Vec3_Fmaf(pm->s.origin, pm_locals.time, pm->s.velocity);
}

/**
 * @brief Handles movement for a frozen or dead player, suppressing all movement.
 */
static void Pm_FreezeMove(void) {

  Pm_Debug("%s\n", vtos(pm->s.origin));
}

static bool Pm_GoodPosition(const vec3_t origin) {
  if (pm->s.type == PM_SPECTATOR) {
    return true;
  }

  const cm_trace_t trace = pm->Trace(origin, origin, pm->bounds);
  return !trace.all_solid;
}

/**
 * @brief Searches the legacy 27-position Q2 jitter lattice before collision
 * and movement begin. The first usable position becomes the command fallback.
 */
static void Pm_Q2InitialSnapPosition(void) {
  if (!Pm_Q2SnapEnabled()) {
    return;
  }

  const vec3_t base = pm->s.origin;
  static const float offsets[] = {
    0.f, -PM_Q2_SNAP_DISTANCE, PM_Q2_SNAP_DISTANCE
  };

  for (size_t z = 0; z < lengthof(offsets); z++) {
    for (size_t y = 0; y < lengthof(offsets); y++) {
      for (size_t x = 0; x < lengthof(offsets); x++) {
        const vec3_t candidate = Vec3(base.x + offsets[x],
                                      base.y + offsets[y],
                                      base.z + offsets[z]);
        if (Pm_GoodPosition(candidate)) {
          pm->s.origin = candidate;
          pm_locals.previous_origin = candidate;
          return;
        }
      }
    }
  }

  pm->s.origin = base;
}

/**
 * @brief Snaps outgoing Q2 state and searches the legacy ordered jitter
 * candidates. If every candidate is blocked, origin falls back to the accepted
 * initial command origin while the snapped velocity remains authoritative.
 */
static void Pm_SnapPosition(void) {
  if (!Pm_Q2SnapEnabled()) {
    return;
  }

  pm->s.velocity = Vec3(Pm_Q2SnapFloat(pm->s.velocity.x),
                        Pm_Q2SnapFloat(pm->s.velocity.y),
                        Pm_Q2SnapFloat(pm->s.velocity.z));

  int32_t sign[3];
  vec3_t base;
  for (size_t i = 0; i < lengthof(sign); i++) {
    sign[i] = pm->s.origin.xyz[i] >= 0.f ? 1 : -1;
    base.xyz[i] = Pm_Q2SnapFloat(pm->s.origin.xyz[i]);
    if (base.xyz[i] == pm->s.origin.xyz[i]) {
      sign[i] = 0;
    }
  }

  static const uint8_t jitter_bits[] = { 0, 4, 1, 2, 3, 5, 6, 7 };
  for (size_t j = 0; j < lengthof(jitter_bits); j++) {
    const uint8_t bits = jitter_bits[j];
    vec3_t candidate = base;

    for (size_t i = 0; i < lengthof(sign); i++) {
      if (bits & (1u << i)) {
        candidate.xyz[i] += sign[i] * PM_Q2_SNAP_DISTANCE;
      }
    }

    if (Pm_GoodPosition(candidate)) {
      pm->s.origin = candidate;
      return;
    }
  }

  pm->s.origin = pm_locals.previous_origin;
}

#if defined(RACE_PHYSICS_TEST)
void Pm_Q2InitialSnapPositionForTest(pm_move_t *move,
                                     vec3_t previous_origin) {
  Pm_BindPhysics();
  assert(Pm_Q2FamilyPhysics());
  pm = move;
  pm_locals.previous_origin = previous_origin;
  Pm_Q2InitialSnapPosition();
}

void Pm_Q2SnapPositionForTest(pm_move_t *move, vec3_t previous_origin) {
  Pm_BindPhysics();
  assert(Pm_Q2FamilyPhysics());
  pm = move;
  pm_locals.previous_origin = previous_origin;
  Pm_SnapPosition();
}
#endif

/**
 * @brief Initializes outgoing player movement state for a new move frame.
 */
static void Pm_Init(void) {

  // set the default bounding box
  if (pm->s.type == PM_DEAD) {

    if (pm->s.flags & PMF_GIBLET) {
      pm->bounds = PM_GIBLET_BOUNDS;
    } else {
      pm->bounds = Box3_Scale(PM_DEAD_BOUNDS, PM_SCALE);
    }
  } else {
    pm->bounds = Pm_CurrentPlayerBounds(false);
  }

  pm->angles = Vec3_Zero();

  pm->num_touched = 0;
  pm->water_level = WATER_NONE;
  pm->water_type = 0;

  pm->step = 0.f;

  // reset flags that we test each move. Q2 retains the serialized previous
  // ground bit so the first probe can distinguish contact from a new landing.
  if (Pm_Q2FamilyPhysics()) {
    pm->s.flags &= ~PMF_ON_LADDER;
    memset(&pm->ground, 0, sizeof(pm->ground));
  } else {
    pm->s.flags &= ~(PMF_ON_GROUND | PMF_ON_LADDER);
  }
  pm->s.flags &= ~(PMF_JUMPED | PMF_UNDER_WATER);

  if (pm->cmd.up < 1) { // jump key released
    pm->s.flags &= ~PMF_JUMP_HELD;
  }

  // Q2 drops timers after initial ground and water classification. This lets
  // a timer created by the current command consume that command's tick.
  if (!Pm_Q2FamilyPhysics()) {
    Pm_DropMovementTime();
  }
}

/**
 * @brief Copies command angles into view state and clamps pitch to prevent inversion.
 */
static void Pm_ClampAngles(void) {

  // copy the command angles into the outgoing state
  pm->s.view_angles = pm->cmd.angles;

  // add the delta angles
  pm->angles = Vec3_Add(pm->cmd.angles, pm->s.delta_angles);

  // clamp pitch to prevent the player from looking up or down more than 90º
  if (pm->angles.x > 90.f && pm->angles.x < 270.f) {
    pm->angles.x = 90.f;
  } else if (pm->angles.x <= 360.f && pm->angles.x >= 270.f) {
    pm->angles.x -= 360.f;
  }
}

/**
 * @brief Initializes local movement state, computing directional vectors and frame timing.
 */
static void Pm_InitLocal(void) {

  memset(&pm_locals, 0, sizeof(pm_locals));

  // save previous values in case move fails, and to detect landings
  pm_locals.previous_origin = pm->s.origin;
  pm_locals.previous_velocity = pm->s.velocity;

  // convert from milliseconds to seconds
  pm_locals.time = pm->cmd.msec * .001f;

  // calculate the directional vectors for this move
  Vec3_Vectors(pm->angles, &pm_locals.forward, &pm_locals.right, &pm_locals.up);

  // and calculate the directional vectors in the XY plane
  Vec3_Vectors(Vec3(0.f, pm->angles.y, 0.f), &pm_locals.forward_xy, &pm_locals.right_xy, NULL);
}

/**
 * @brief Updates the view step offset to smoothly interpolate the camera over stair steps.
 */
static void Pm_CheckViewStep(void) {

  // add the step offset we've made on this frame
  if (pm->step) {
    pm->s.step_offset += pm->step;
  }

  // calculate change to the step offset
  if (pm->s.step_offset) {

    const float step_speed = pm_locals.time * (PM_SPEED_STEP * (Maxf(1.f, fabsf(pm->s.step_offset) / PM_STEP_HEIGHT)));

    if (pm->s.step_offset > 0) {
      pm->s.step_offset = Maxf(0.f, pm->s.step_offset - step_speed);
    } else {
      pm->s.step_offset = Minf(0.f, pm->s.step_offset + step_speed);
    }
  }
}

#if defined(RACE_PHYSICS_TEST)
static void Pm_Q2TestContext(pm_move_t *move, const vec3_t angles) {
  Pm_BindPhysics();
  assert(Pm_Q2FamilyPhysics());
  pm = move;
  pm->angles = angles;
  pm->bounds = Pm_CurrentPlayerBounds(!!(pm->s.flags & PMF_DUCKED));
  Pm_InitLocal();
  pm_locals.ground = pm->ground;
}

vec3_t Pm_Q2LadderWishForTest(pm_move_t *move, const vec3_t angles) {
  Pm_Q2TestContext(move, angles);
  return Pm_Q2WishVelocity();
}

void Pm_Q2LadderMoveForTest(pm_move_t *move, const vec3_t angles) {
  Pm_Q2TestContext(move, angles);
  Pm_Q2Friction();
  Pm_Q2LadderMove();
}

void Pm_Q2CheckWaterForTest(pm_move_t *move) {
  Pm_Q2TestContext(move, Vec3_Zero());
  Pm_CheckWater();
}

vec3_t Pm_Q2AddCurrentsForTest(pm_move_t *move, const vec3_t angles,
                               const vec3_t wish_velocity) {
  Pm_Q2TestContext(move, angles);
  return Pm_Q2AddCurrents(wish_velocity);
}

void Pm_Q2FrictionForTest(pm_move_t *move, const vec3_t angles) {
  Pm_Q2TestContext(move, angles);
  Pm_Q2Friction();
}

void Pm_Q2WaterMoveForTest(pm_move_t *move, const vec3_t angles) {
  Pm_Q2TestContext(move, angles);
  Pm_Q2WaterMove();
}

bool Pm_Q2CheckWaterJumpForTest(pm_move_t *move, const vec3_t angles) {
  Pm_Q2TestContext(move, angles);
  return Pm_CheckWaterJump();
}

bool Pm_Q2CheckJumpForTest(pm_move_t *move, const vec3_t angles) {
  Pm_Q2TestContext(move, angles);
  return Pm_CheckJump();
}
#endif

/**
 * @brief Called by the game and the client game to update the player's
 * authoritative or predicted movement state, respectively.
 */
void Pm_Move(pm_move_t *pm_move) {
  Pm_BindPhysics();

  pm = pm_move;

  Pm_Q2SnapMoveInput();

  Pm_Init();

  Pm_ClampAngles();

  Pm_InitLocal();

  if (pm->s.type == PM_FREEZE) { // no movement
    Pm_FreezeMove();
    goto done;
  }

  if (pm->s.type == PM_SPECTATOR) { // no interaction
    Pm_SpectatorMove();
    Pm_SnapPosition();
    goto done;
  }

  if (pm->s.type == PM_DEAD) { // no control
    pm->cmd.forward = pm->cmd.right = pm->cmd.up = 0;
  }

  // check for ladders
  Pm_CheckLadder();

  // check for grapple hook
  Pm_CheckHook();

  // check for ducking
  Pm_CheckDuck();

  Pm_Q2InitialSnapPosition();

  // check for water level, water type
  Pm_CheckWater();

  // check for ground
  Pm_CheckGround();

  if (Pm_Q2FamilyPhysics()) {
    Pm_Q2Move();
  } else if (pm->s.flags & PMF_TIME_TELEPORT) {
    // pause in place briefly
  } else if (pm->s.flags & PMF_TIME_WATER_JUMP) {
    Pm_WaterJumpMove();
  } else if (pm->s.flags & PMF_ON_LADDER) {
    Pm_LadderMove();
  } else if (pm->s.flags & PMF_ON_GROUND) {
    Pm_WalkMove();
  } else if (pm->water_level > WATER_FEET) {
    Pm_WaterMove();
  } else {
    Pm_AirMove();
  }

  // check for ground at new spot
  Pm_CheckGround();

  // check for water level, water type at new spot
  Pm_CheckWater();

  Pm_SnapPosition();

  // check for offset changes for our view
  Pm_CheckViewStep();

done:
  Pm_RaceTraining_ClearObserver();
}

