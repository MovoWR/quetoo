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

void Race_Profiles_Init(void);
void Race_Profiles_ClientBegin(g_client_t *cl);
void Race_Profiles_ClientDisconnect(g_client_t *cl);
void Race_Profiles_ClientUserInfoChanged(g_client_t *cl);
bool Race_Profiles_ClientCommand(g_client_t *cl, const char *cmd);
const char *Race_Profiles_AuthenticatedUid(const g_client_t *cl);
