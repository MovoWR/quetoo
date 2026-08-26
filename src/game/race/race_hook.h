/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#pragma once

#include "shared/shared.h"

#define RACE_HOOK_PULL_SPEED_DEFAULT 800.f
#define RACE_HOOK_PULL_SPEED_MIN 0.f
#define RACE_HOOK_PULL_SPEED_MAX MAX_WORLD_DIST

/**
 * @brief Parses the GAME-owned hook pull speed shared with CGAME prediction.
 * @details The bound prevents one configstring from introducing non-finite or
 * world-scale-breaking movement while retaining zero as an intentional stop.
 */
bool Race_HookPullSpeed_Parse(const char *text, float *speed);
