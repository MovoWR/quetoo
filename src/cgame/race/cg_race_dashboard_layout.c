/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_race_dashboard_layout.h"

bool Cg_RaceDashboardLayout_ShouldRun(
    cg_race_dashboard_layout_state_t *state, const View *dashboard,
    const SDL_Size size, const bool title_wraps) {

  if (state == NULL || dashboard == NULL) {
    return false;
  }

  if (state->valid &&
      state->size.w == size.w &&
      state->size.h == size.h &&
      state->title_wraps == title_wraps &&
      !dashboard->needsLayout) {
    return false;
  }

  state->size = size;
  state->title_wraps = title_wraps;
  state->valid = true;
  return true;
}
