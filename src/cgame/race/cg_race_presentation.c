/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cg_race_presentation.h"

const char *Cg_Race_ModeLabel(race_mode_t mode) {
  switch (mode) {
    case RACE_MODE_RACE:
      return "Race";
    case RACE_MODE_PRACTICE:
      return "Practice";
    case RACE_MODE_SPECTATOR:
      return "Spectator";
    default:
      return "Unknown";
  }
}

const char *Cg_Race_RunStateLabel(race_run_state_t state) {
  switch (state) {
    case RACE_RUN_IDLE:
      return "Idle";
    case RACE_RUN_ACTIVE:
      return "Running";
    case RACE_RUN_FINISHED:
      return "Finished";
    default:
      return "Unknown";
  }
}

uint16_t Cg_Race_CheckpointProgress(uint16_t checkpoint, uint16_t total) {
  return checkpoint < total ? checkpoint : total;
}

void Cg_Race_FormatElapsed(uint32_t elapsed, char *string, size_t size) {

  if (!string || !size) {
    return;
  }

  const uint32_t minutes = elapsed / 60000u;
  const uint32_t seconds = elapsed / 1000u % 60u;
  const uint32_t millis = elapsed % 1000u;

  snprintf(string, size, "%u:%02u.%03u", minutes, seconds, millis);
}

int32_t Cg_Race_HorizontalSpeed(vec3_t velocity) {
  const float speed = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);

  if (!isfinite(speed) || speed <= 0.f) {
    return 0;
  }

  if (speed >= (float) INT32_MAX) {
    return INT32_MAX;
  }

  return (int32_t) floorf(speed + .5f);
}

cg_race_hud_layout_t Cg_Race_HudLayout(const int32_t screen_width,
                                        const int32_t screen_height,
                                        const int32_t mode_width) {
  const int32_t center = screen_width / 2;
  const int32_t center_clearance = mode_width / 2 + 16 > 64
    ? mode_width / 2 + 16
    : 64;
  const int32_t status_x = center + center_clearance + 16 < screen_width - 16
    ? center + center_clearance + 16
    : screen_width - 16;
  return (cg_race_hud_layout_t) {
    .bar_y = screen_height - 64,
    .mode_x = center - mode_width / 2,
    .status_x = status_x,
    .status_width = screen_width - 16 - status_x
  };
}

bool Cg_Race_RunHudVisible(const bool enabled, const bool intermission,
                           const bool editor_mode, const bool scores,
                           const bool spectator, const bool chasing) {
  return enabled && !intermission && !editor_mode && !scores &&
         (!spectator || chasing);
}

cg_race_climb_state_t Cg_Race_ClimbState(const float distance) {
  if (distance < 32.f) {
    return CG_RACE_CLIMB_READY;
  }
  if (distance < 64.f) {
    return CG_RACE_CLIMB_CLOSER;
  }
  return CG_RACE_CLIMB_TOO_FAR;
}

const char *Cg_Race_ClimbLabel(const cg_race_climb_state_t state) {
  switch (state) {
    case CG_RACE_CLIMB_READY:
      return "CLIMB";
    case CG_RACE_CLIMB_CLOSER:
      return "CLOSER";
    default:
      return "TOO FAR";
  }
}

bool Cg_Race_DescribeMarker(const char *classname, bool checkpoint_is_integer,
                            int32_t checkpoint, cg_race_marker_descriptor_t *descriptor) {

  if (!descriptor) {
    return false;
  }

  *descriptor = (cg_race_marker_descriptor_t) {
    .type = CG_RACE_MARKER_NONE
  };

  if (!classname) {
    return false;
  }

  if (strcmp(classname, "trigger_race_start") == 0) {
    descriptor->type = CG_RACE_MARKER_START;
    return true;
  }

  if (strcmp(classname, "trigger_race_finish") == 0) {
    descriptor->type = CG_RACE_MARKER_FINISH;
    return true;
  }

  if (strcmp(classname, "trigger_race_cp") == 0 && checkpoint_is_integer &&
      checkpoint >= 1 && checkpoint <= RACE_MAX_CHECKPOINTS) {
    descriptor->type = CG_RACE_MARKER_CHECKPOINT;
    descriptor->checkpoint = (uint16_t) checkpoint;
    return true;
  }

  return false;
}
