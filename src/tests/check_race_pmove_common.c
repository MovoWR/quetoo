/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

/*
 * Test-only names keep a then-current common mover in the same executable as
 * the Race-owned mover. This makes Phase 8A parity a direct state comparison;
 * production modules never compile both implementations.
 */
#define PM_BOUNDS PM_BOUNDS_CommonReference
#define PM_CROUCHED_BOUNDS PM_CROUCHED_BOUNDS_CommonReference
#define Pm_PlayerBounds Pm_PlayerBounds_CommonReference
#define Pm_Move Pm_Move_CommonReference

#include "game/common/bg_pmove.c"

box3_t Pm_PlayerBounds(const bool ducked) {
  return Box3_Scale(ducked ? PM_CROUCHED_BOUNDS : PM_BOUNDS, PM_SCALE);
}
