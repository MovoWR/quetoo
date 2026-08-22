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

void Cg_InputViewer_Init(void);
void Cg_InputViewer_Clear(void);
void Cg_InputViewer_CaptureCommand(const pm_cmd_t *cmd);
void Cg_InputViewer_Draw(const player_state_t *ps);

/**
 * @brief Draws the input cluster with its left edge at `x` and its base at
 * `bottom`: ATTACK over DUCK, the WASD chevrons, then JUMP.
 * @param flags The Race input flags to paint.
 * @remarks Shared with the replay HUD, which drives it from the recorded
 * input track rather than from live input.
 */
void Cg_InputViewer_DrawCluster(int32_t x, int32_t bottom, int16_t flags);

/**
 * @brief Draws the stand-in for a demo with no input track, on the same
 * anchor as the cluster, so the corner never goes empty without explanation.
 */
void Cg_InputViewer_DrawUnavailable(int32_t x, int32_t bottom);
