/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "race_types.h"

typedef enum {
  CG_RACE_MARKER_NONE,
  CG_RACE_MARKER_START,
  CG_RACE_MARKER_CHECKPOINT,
  CG_RACE_MARKER_FINISH
} cg_race_marker_type_t;

typedef struct {
  cg_race_marker_type_t type;
  uint16_t checkpoint;
} cg_race_marker_descriptor_t;

typedef struct {
  int32_t bar_y;
  int32_t mode_x;
  int32_t status_x;
  int32_t status_width;
} cg_race_hud_layout_t;

typedef enum {
  CG_RACE_CLIMB_READY,
  CG_RACE_CLIMB_CLOSER,
  CG_RACE_CLIMB_TOO_FAR
} cg_race_climb_state_t;

const char *Cg_Race_ModeLabel(race_mode_t mode);
const char *Cg_Race_RunStateLabel(race_run_state_t state);
uint16_t Cg_Race_CheckpointProgress(uint16_t checkpoint, uint16_t total);
void Cg_Race_FormatElapsed(uint32_t elapsed, char *string, size_t size);
int32_t Cg_Race_HorizontalSpeed(vec3_t velocity);
cg_race_hud_layout_t Cg_Race_HudLayout(int32_t screen_width,
                                        int32_t screen_height,
                                        int32_t mode_width);
bool Cg_Race_RunHudVisible(bool enabled, bool intermission, bool editor,
                           bool scores, bool spectator, bool chasing);
cg_race_climb_state_t Cg_Race_ClimbState(float distance);
cg_race_climb_state_t Cg_Race_ClimbStateForRange(float distance, float range);
const char *Cg_Race_ClimbLabel(cg_race_climb_state_t state);
bool Cg_Race_DescribeMarker(const char *classname, bool checkpoint_is_integer,
                            int32_t checkpoint, cg_race_marker_descriptor_t *descriptor);
