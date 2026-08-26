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

void Cg_RaceFinishReport_Init(void);
void Cg_RaceFinishReport_Clear(void);
bool Cg_RaceFinishReport_ParseMessage(int32_t command);

/**
 * @return True while the finish bar owns the bottom band of the screen.
 * @remarks The bar is full-width and draws its own map clock, so the live
 * HUD's bottom cluster -- the checkpoint ribbon, the pips and the clock --
 * stands down for its duration rather than showing through the scrim.
 */
bool Cg_RaceFinishReport_Active(const player_state_t *ps);

void Cg_RaceFinishReport_Draw(const player_state_t *ps);
