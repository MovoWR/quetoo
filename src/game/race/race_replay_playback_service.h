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

void Race_ReplayPlaybackService_Init(void);
void Race_ReplayPlaybackService_Shutdown(void);
void Race_ReplayPlaybackService_ConfigureLevel(void);
void Race_ReplayPlaybackService_Frame(void);
void Race_ReplayPlaybackService_FinalizeClientFrames(void);
bool Race_ReplayPlaybackService_ClientActive(const g_client_t *cl);
bool Race_ReplayPlaybackService_ClientInput(g_client_t *cl,
                                            const pm_cmd_t *cmd);
bool Race_ReplayPlaybackService_ExitClient(g_client_t *cl);
void Race_ReplayPlaybackService_ClientRunStarted(g_client_t *cl);
void Race_ReplayPlaybackService_ClientCommand(g_client_t *cl);
void Race_ReplayPlaybackService_ControlCommand(g_client_t *cl);
void Race_ReplayPlaybackService_CancelCommand(g_client_t *cl);
void Race_ReplayPlaybackService_RacelineCommand(g_client_t *cl);
void Race_ReplayPlaybackService_RaceSelect(g_client_t *cl, const char *argument);
