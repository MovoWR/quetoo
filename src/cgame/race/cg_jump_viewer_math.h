#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "shared/shared.h"

/** Conservative maximum movement accepted between consecutive samples. */
#define CG_JUMP_DISCONTINUITY 512.f

typedef enum {
  CG_JUMP_SOURCE_PREDICTION,
  CG_JUMP_SOURCE_FRAME,
  CG_JUMP_SOURCE_CHASE,
  CG_JUMP_SOURCE_REPLAY
} cg_jump_source_t;

typedef enum {
  CG_JUMP_LAUNCH_AIRBORNE,
  CG_JUMP_LAUNCH_NORMAL,
  CG_JUMP_LAUNCH_RAMP
} cg_jump_launch_t;

typedef struct {
  vec3_t origin;
  float vertical_velocity;
  uint64_t time;
  cg_jump_source_t source;
  uint32_t identity;
  uint64_t lifecycle;
  bool grounded;
  bool valid;
  bool teleport;
  bool predicted;
  cg_jump_launch_t launch;
} cg_jump_sample_t;

typedef struct {
  bool valid;
  cg_jump_launch_t launch;
  float length;
  float peak;
  float path;
  uint64_t airtime;
} cg_jump_result_t;

typedef struct {
  bool has_previous;
  bool active;
  cg_jump_sample_t previous;
  vec3_t takeoff_origin;
  uint64_t takeoff_time;
  float maximum_z;
  cg_jump_result_t live;
  cg_jump_result_t completed;
  uint64_t completed_time;
} cg_jump_tracker_t;

typedef struct {
  bool initialized;
  bool paused;
  uint32_t generation;
  uint32_t playhead_ms;
  uint32_t frame_cursor;
  uint32_t telemetry_time_ms;
} cg_jump_replay_sample_state_t;

typedef struct {
  char header[64];
  char length[64];
  char peak[64];
  char air[96];
} cg_jump_viewer_text_t;

typedef enum {
  CG_JUMP_VIEWER_DRAW,
  CG_JUMP_VIEWER_HIDE,
  CG_JUMP_VIEWER_HIDE_AND_CLEAR
} cg_jump_viewer_visibility_t;

static inline bool Cg_JumpViewer_ClearOnUpdate(bool registered, bool enabled) {
  return !registered || !enabled;
}

static inline bool Cg_JumpViewer_AcceptReplaySample(
  cg_jump_replay_sample_state_t *state, uint32_t generation,
  uint32_t playhead_ms, uint32_t frame_cursor, bool paused) {
  if (!state || !generation) {
    return false;
  }
  const bool first = !state->initialized || state->generation != generation;
  const bool time_changed = state->playhead_ms != playhead_ms;
  const uint32_t telemetry_time_ms = first
                                       ? playhead_ms
                                       : state->telemetry_time_ms;
  const bool accepted = first ||
                        (time_changed && playhead_ms > telemetry_time_ms);
  *state = (cg_jump_replay_sample_state_t) {
    .initialized = true,
    .paused = paused,
    .generation = generation,
    .playhead_ms = playhead_ms,
    .frame_cursor = frame_cursor,
    .telemetry_time_ms = accepted ? playhead_ms : telemetry_time_ms
  };
  return accepted;
}

static inline uint32_t Cg_JumpViewer_ReplayTelemetryTime(
  const cg_jump_replay_sample_state_t *state) {
  return state && state->initialized ? state->telemetry_time_ms : 0u;
}

static inline int32_t Cg_JumpViewer_PanelX(int32_t screen_width) {
  (void) screen_width;
  return 24;
}

static inline cg_jump_viewer_visibility_t Cg_JumpViewer_Visibility(bool hud_enabled,
                                                                   bool viewer_enabled,
                                                                   bool intermission,
                                                                   bool editor,
                                                                   bool dead,
                                                                   bool free_spectator,
                                                                   bool scoreboard) {
  if (!viewer_enabled) {
    return CG_JUMP_VIEWER_HIDE_AND_CLEAR;
  }
  if (!hud_enabled || intermission || editor || dead || free_spectator || scoreboard) {
    return CG_JUMP_VIEWER_HIDE;
  }
  return CG_JUMP_VIEWER_DRAW;
}

static inline void Cg_JumpViewer_FormatResult(const cg_jump_result_t *result, bool live,
                                               bool show_path, cg_jump_viewer_text_t *text) {
  const char *launch = result->launch == CG_JUMP_LAUNCH_RAMP ? "RAMP JUMP" :
                       result->launch == CG_JUMP_LAUNCH_NORMAL ? "JUMP" : "AIRBORNE";
  if (live) {
    q_snprintf(text->header, sizeof(text->header), "%s - LIVE", launch);
  } else {
    q_snprintf(text->header, sizeof(text->header), "JUMP RESULT");
  }
  q_snprintf(text->length, sizeof(text->length), "Length  %.1f", result->length);
  q_snprintf(text->peak, sizeof(text->peak), "Peak     %.1f", result->peak);
  if (show_path) {
    q_snprintf(text->air, sizeof(text->air), "Air %.2fs | Path %.1f",
               result->airtime * 0.001f, result->path);
  } else {
    q_snprintf(text->air, sizeof(text->air), "Air %.2fs", result->airtime * 0.001f);
  }
}

static inline cg_jump_launch_t Cg_JumpViewer_ClassifyLaunch(uint16_t flags,
                                                             float vertical_velocity) {
  if ((flags & PMF_JUMPED) ||
      ((flags & PMF_JUMP_HELD) && vertical_velocity > 0.f)) {
    return CG_JUMP_LAUNCH_NORMAL;
  }
  return CG_JUMP_LAUNCH_AIRBORNE;
}

static inline cg_jump_source_t Cg_JumpViewer_SelectSource(bool prediction, bool replay, int16_t chase) {
  if (prediction) {
    return CG_JUMP_SOURCE_PREDICTION;
  }
  if (replay) {
    return CG_JUMP_SOURCE_REPLAY;
  }
  if (chase) {
    return CG_JUMP_SOURCE_CHASE;
  }
  return CG_JUMP_SOURCE_FRAME;
}

static inline const cg_jump_result_t *Cg_JumpViewer_SelectResult(const cg_jump_tracker_t *self,
                                                                 uint64_t now, uint64_t hold_time,
                                                                 bool *live) {
  if (self->active && self->live.valid) {
    *live = true;
    return &self->live;
  }

  *live = false;
  if (self->completed.valid && now - self->completed_time <= hold_time) {
    return &self->completed;
  }

  return NULL;
}

static inline bool Cg_JumpTracker_OriginIsFinite(const vec3_t origin) {
  return isfinite(origin.x) && isfinite(origin.y) && isfinite(origin.z);
}

static inline void Cg_JumpTracker_Clear(cg_jump_tracker_t *self) {
  memset(self, 0, sizeof(*self));
}

static inline void Cg_JumpTracker_Invalidate(cg_jump_tracker_t *self) {
  Cg_JumpTracker_Clear(self);
}

static inline void Cg_JumpTracker_Begin(cg_jump_tracker_t *self,
                                        const cg_jump_sample_t *grounded,
                                        const cg_jump_sample_t *airborne) {
  self->active = true;
  self->takeoff_origin = grounded->origin;
  self->takeoff_time = airborne->time;
  self->maximum_z = fmaxf(grounded->origin.z, airborne->origin.z);
  self->live = (cg_jump_result_t) {
    .valid = true,
    .launch = airborne->launch,
    .length = Vec2_Distance(grounded->origin.xy, airborne->origin.xy),
    .peak = fmaxf(0.f, self->maximum_z - grounded->origin.z),
    .path = Vec3_Distance(grounded->origin, airborne->origin)
  };
}

static inline void Cg_JumpTracker_Accumulate(cg_jump_tracker_t *self, const cg_jump_sample_t *sample) {
  self->live.path += Vec3_Distance(self->previous.origin, sample->origin);
  self->maximum_z = fmaxf(self->maximum_z, sample->origin.z);
  self->live.length = Vec2_Distance(self->takeoff_origin.xy, sample->origin.xy);
  self->live.peak = fmaxf(0.f, self->maximum_z - self->takeoff_origin.z);
  self->live.airtime = sample->time - self->takeoff_time;
}

static inline void Cg_JumpTracker_Complete(cg_jump_tracker_t *self, const cg_jump_sample_t *sample) {
  Cg_JumpTracker_Accumulate(self, sample);
  self->completed = self->live;
  self->completed_time = sample->time;
  self->active = false;
  self->live.valid = false;
}

static inline void Cg_JumpTracker_Update(cg_jump_tracker_t *self, const cg_jump_sample_t *sample) {
  if (!sample->valid || sample->teleport || !Cg_JumpTracker_OriginIsFinite(sample->origin)) {
    Cg_JumpTracker_Invalidate(self);
    return;
  }

  if (self->has_previous && sample->predicted && self->previous.predicted &&
      sample->source == self->previous.source && sample->identity == self->previous.identity &&
      sample->lifecycle == self->previous.lifecycle && sample->time == self->previous.time) {
    if (Vec3_Distance(sample->origin, self->previous.origin) > CG_JUMP_DISCONTINUITY) {
      Cg_JumpTracker_Invalidate(self);
    }
    return;
  }

  if (self->has_previous &&
      (sample->source != self->previous.source || sample->identity != self->previous.identity ||
       sample->lifecycle != self->previous.lifecycle ||
       sample->time <= self->previous.time ||
       Vec3_Distance(sample->origin, self->previous.origin) > CG_JUMP_DISCONTINUITY)) {
    Cg_JumpTracker_Invalidate(self);
  }

  if (!self->has_previous) {
    self->previous = *sample;
    self->has_previous = true;
    return;
  }

  if (!self->active && self->previous.grounded && !sample->grounded) {
    Cg_JumpTracker_Begin(self, &self->previous, sample);
  } else if (self->active && !sample->grounded) {
    Cg_JumpTracker_Accumulate(self, sample);
  } else if (self->active && sample->grounded) {
    Cg_JumpTracker_Complete(self, sample);
  }

  self->previous = *sample;
}

