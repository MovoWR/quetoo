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

void Race_Init(void);
void Race_Reset(g_client_t *cl);
void Race_ResetClientKillRate(g_client_t *cl);
bool Race_Start(g_client_t *cl);
bool Race_RequestStart(g_client_t *cl);
bool Race_Checkpoint(g_client_t *cl, uint16_t checkpoint);
bool Race_Split(g_client_t *cl, uint16_t split, const char *label);
bool Race_Stage(g_client_t *cl, g_entity_t *trigger);
bool Race_Finish(g_client_t *cl);
bool Race_ClientInput(g_client_t *cl, const pm_cmd_t *cmd);
void Race_MarkInvalid(g_client_t *cl, race_invalid_flags_t flag);
void Race_PrintStatus(g_client_t *cl);
void Race_ClientThink(g_client_t *cl, const pm_cmd_t *cmd);
void Race_ClientStats(g_client_t *cl);
void Race_ClientScore(const g_client_t *cl, g_score_t *score);
