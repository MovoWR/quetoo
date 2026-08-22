/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_training_service.h"

#include <string.h>

#include "g_local.h"
#include "race_training.h"

static PrepareMove race_training_previous_prepare_move;
static bool race_training_prepare_move_installed;

static void Race_TrainingService_Observe(
    const race_strafe_sample_t *sample, void *context) {
  g_client_t *cl = context;
  if (cl && sample) {
    cl->race_strafe_sample = *sample;
  }
}

static void Race_TrainingService_PrepareMove(g_client_t *cl, pm_move_t *pm) {
  race_training_previous_prepare_move(cl, pm);
  memset(&cl->race_strafe_sample, 0, sizeof(cl->race_strafe_sample));
  Pm_RaceTraining_SetObserver(Race_TrainingService_Observe, cl);
}

void Race_TrainingService_Init(void) {
  if (!race_training_prepare_move_installed) {
    race_training_previous_prepare_move = G_PrepareMove;
    G_PrepareMove = Race_TrainingService_PrepareMove;
    race_training_prepare_move_installed = true;
  }
}

void Race_TrainingService_Shutdown(void) {
  Pm_RaceTraining_ClearObserver();
}
