/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "g_local.h"
#include "race_leaderboard.h"

void Race_ReplayService_Init(void);
void Race_ReplayService_Shutdown(void);
void Race_ReplayService_ConfigureLevel(const char *map);
void Race_ReplayService_Reset(g_client_t *cl);
bool Race_ReplayService_Start(g_client_t *cl);
void Race_ReplayService_Frame(void);
bool Race_ReplayService_Finish(g_client_t *cl,
                               race_leaderboard_evaluation_t *evaluation,
                               uint64_t *replay_id);
