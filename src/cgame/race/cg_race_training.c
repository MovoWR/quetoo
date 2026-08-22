/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"
#include "cg_race_training.h"

#include "cg_jump_viewer.h"
#include "cg_input_viewer.h"
#include "cg_input_viewer_math.h"
#include "cg_race_replay.h"
#include "cg_strafe_helper.h"
#include "cg_strafe_helper_math.h"

static race_strafe_sample_t cg_race_prediction_strafe;

static void Cg_RaceTraining_Observe(
    const race_strafe_sample_t *sample, void *context) {
  race_strafe_sample_t *capture = context;
  if (sample && capture) {
    *capture = *sample;
    Cg_UpdateStrafeHelper(sample);
  }
}

void Cg_RaceTraining_Init(void) {
  Cg_InputViewer_Init();
  Cg_InitStrafeHelper();
  Cg_InitJumpViewer();
  Cg_RaceTraining_Clear();
}

void Cg_RaceTraining_Clear(void) {
  memset(&cg_race_prediction_strafe, 0, sizeof(cg_race_prediction_strafe));
  Pm_RaceTraining_ClearObserver();
  Cg_InputViewer_Clear();
  Cg_ClearStrafeHelper();
  Cg_ClearJumpViewer();
}

void Cg_RaceTraining_PreparePredictionCommand(
    pm_move_t *pm, const size_t index, const size_t count) {
  (void) pm;
  memset(&cg_race_prediction_strafe, 0, sizeof(cg_race_prediction_strafe));
  if (Cg_StrafeHelper_ShouldObservePredictionCommand(index, count)) {
    Pm_RaceTraining_SetObserver(
      Cg_RaceTraining_Observe, &cg_race_prediction_strafe);
  } else {
    Pm_RaceTraining_ClearObserver();
  }
}

void Cg_RaceTraining_CompletePredictionCommand(
    const pm_move_t *pm, const size_t index, const size_t count) {
  if (Cg_InputViewer_ShouldCapturePredictionCommand(index, count)) {
    Cg_InputViewer_CaptureCommand(&pm->cmd);
  }
  if (Cg_StrafeHelper_ShouldObservePredictionCommand(index, count)) {
    Cg_SetStrafeHelperVelocity(pm->s.velocity);
  }
}

void Cg_RaceTraining_CompletePrediction(const pm_move_t *pm) {
  Cg_JumpViewer_UpdatePredicted(&pm->s);
}

void Cg_RaceTraining_UpdateFrame(const player_state_t *ps) {
  Cg_JumpViewer_UpdateFrame(ps);
}

void Cg_RaceTraining_DrawStrafeHelper(const player_state_t *ps) {
  pm_state_t replay_pm;
  race_strafe_sample_t replay_strafe;
  if (Cg_RaceReplay_Telemetry(&replay_pm, &replay_strafe, NULL)) {
    Cg_UpdateStrafeHelper(&replay_strafe);
    Cg_SetStrafeHelperVelocity(replay_pm.velocity);
  }
  Cg_DrawStrafeHelper(ps);
}

void Cg_RaceTraining_ReplayTelemetry(
    const race_replay_telemetry_message_t *telemetry) {
  if (!telemetry) {
    return;
  }
  Cg_UpdateStrafeHelper(&telemetry->strafe_helper);
  Cg_SetStrafeHelperVelocity(telemetry->velocity);
}
