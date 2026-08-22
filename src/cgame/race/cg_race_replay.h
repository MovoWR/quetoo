/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "cg_types.h"
#include "race_replay_transport.h"

void Cg_RaceReplay_Init(void);
void Cg_RaceReplay_Clear(void);
void Cg_RaceReplay_LoadMedia(void);
bool Cg_RaceReplay_ParseMessage(int32_t command);
void Cg_RaceReplay_PopulateScene(void);
void Cg_RaceReplay_DrawHud(const player_state_t *ps);
bool Cg_ReplayActive(void);
bool Cg_ReplayTimeline(uint32_t *generation, uint32_t *playhead_ms,
                       uint32_t *frame_cursor, bool *paused);
bool Cg_RaceReplay_Telemetry(pm_state_t *pm,
                             race_strafe_sample_t *strafe_helper,
                             int16_t *input_flags);
