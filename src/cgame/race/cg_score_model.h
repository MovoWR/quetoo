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

#include "g_types.h"

#define CG_SCORE_MODEL_CAPACITY (MAX_CLIENTS + MAX_TEAMS)

typedef enum {
  CG_SCORE_MODEL_INVALID,
  CG_SCORE_MODEL_PARTIAL,
  CG_SCORE_MODEL_COMPLETE
} cg_score_model_result_t;

typedef struct {
  g_score_t scores[CG_SCORE_MODEL_CAPACITY];
  size_t num_scores;
  size_t pending_scores;
  bool assembling;
} cg_score_model_t;

bool Cg_ScoreModelRangeValid(int32_t index, int32_t count);
cg_score_model_result_t Cg_ScoreModelApply(
  cg_score_model_t *model, int32_t index, const g_score_t *scores,
  int32_t count, bool final);
bool Cg_ScoreOverlayVisible(bool ui_active, bool scores_held);
