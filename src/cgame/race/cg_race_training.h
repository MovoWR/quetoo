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

void Cg_RaceTraining_Init(void);
void Cg_RaceTraining_Clear(void);
void Cg_RaceTraining_PreparePredictionCommand(
  pm_move_t *pm, size_t index, size_t count);
void Cg_RaceTraining_CompletePredictionCommand(
  const pm_move_t *pm, size_t index, size_t count);
void Cg_RaceTraining_CompletePrediction(const pm_move_t *pm);
void Cg_RaceTraining_UpdateFrame(const player_state_t *ps);
void Cg_RaceTraining_DrawStrafeHelper(const player_state_t *ps);
void Cg_RaceTraining_ReplayTelemetry(
  const race_replay_telemetry_message_t *telemetry);
