/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_score_model.h"

#include <stdlib.h>
#include <string.h>

static int32_t Cg_ScoreModelCompare(const void *a, const void *b) {
  const g_score_t *sa = a;
  const g_score_t *sb = b;
  const int32_t group_a = (sa->flags & SCORE_SPECTATOR) ||
                          sa->race_mode == RACE_MODE_SPECTATOR
                            ? 2
                            : sa->race_mode == RACE_MODE_PRACTICE ? 1 : 0;
  const int32_t group_b = (sb->flags & SCORE_SPECTATOR) ||
                          sb->race_mode == RACE_MODE_SPECTATOR
                            ? 2
                            : sb->race_mode == RACE_MODE_PRACTICE ? 1 : 0;

  if (group_a != group_b) {
    return group_a < group_b ? -1 : 1;
  }
  return (int32_t) sa->client - (int32_t) sb->client;
}

bool Cg_ScoreModelRangeValid(const int32_t index, const int32_t count) {
  return index >= 0 && count >= 0 &&
         (size_t) index <= CG_SCORE_MODEL_CAPACITY &&
         (size_t) count <= CG_SCORE_MODEL_CAPACITY - (size_t) index;
}

cg_score_model_result_t Cg_ScoreModelApply(
    cg_score_model_t *model, const int32_t index, const g_score_t *scores,
    const int32_t count, const bool final) {
  if (!model || !Cg_ScoreModelRangeValid(index, count) ||
      (count && !scores)) {
    return CG_SCORE_MODEL_INVALID;
  }

  if (index == 0) {
    memset(model->scores, 0, sizeof(model->scores));
    model->num_scores = 0;
    model->pending_scores = 0;
    model->assembling = true;
  } else if (!model->assembling || (size_t) index != model->pending_scores) {
    return CG_SCORE_MODEL_INVALID;
  }

  if (count) {
    memcpy(model->scores + index, scores, (size_t) count * sizeof(*scores));
  }
  model->pending_scores = (size_t) index + (size_t) count;

  if (!final) {
    return CG_SCORE_MODEL_PARTIAL;
  }

  model->num_scores = model->pending_scores;
  model->assembling = false;
  qsort(model->scores, model->num_scores, sizeof(*model->scores),
        Cg_ScoreModelCompare);
  return CG_SCORE_MODEL_COMPLETE;
}

bool Cg_ScoreOverlayVisible(const bool ui_active, const bool scores_held) {
  return !ui_active && scores_held;
}
