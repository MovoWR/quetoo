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
#include "race_module_compat.h"

race_mode_t Race_Mode(const g_client_t *cl);
const char *Race_ModeName(race_mode_t mode);
bool Race_AllowsHook(const g_client_t *cl);
void Race_ModeCommand(g_client_t *cl);
void Race_AssignClientMode(g_client_t *cl);
void Race_DisconnectClient(g_client_t *cl);
bool Race_HandleClientModeChange(g_client_t *cl, bool spectator);
bool Race_HandleClientNoClip(g_client_t *cl);
void Race_SynchronizeMode(g_client_t *cl);
void Race_ClearStoredSpawn(g_client_t *cl);
void Race_StoreSpawn(g_client_t *cl);
void Race_RestartStage(g_client_t *cl);
void Race_PrepareClientSpawn(g_client_t *cl, g_client_spawn_t *spawn);
