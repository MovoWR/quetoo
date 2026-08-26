/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <ObjectivelyMVC/View.h>

typedef struct {
  SDL_Size size;
  bool title_wraps;
  bool valid;
} cg_race_dashboard_layout_state_t;

/**
 * @brief Claims a Home dashboard geometry pass when its inputs changed or the
 * actual ObjectivelyMVC dashboard View was invalidated.
 * @return True exactly when the caller must perform the layout.
 */
bool Cg_RaceDashboardLayout_ShouldRun(
  cg_race_dashboard_layout_state_t *state, const View *dashboard,
  SDL_Size size, bool title_wraps);
