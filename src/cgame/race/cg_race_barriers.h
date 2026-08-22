/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "cg_local.h"

void Cg_RaceBarriers_Clear(void);
void Cg_RaceBarriers_Load(void);

bool Cg_RaceBarriers_ResolveModelIndex(
  const char *model,
  const char catalog[][MAX_STRING_CHARS],
  size_t catalog_count,
  uint8_t *model_index);

cm_trace_t Cg_RaceBarriers_TracePrediction(const vec3_t start,
                                           const vec3_t end,
                                           const box3_t bounds);
void Cg_RaceBarriers_PreparePredictionCommand(size_t index);
void Cg_RaceBarriers_Draw(void);
