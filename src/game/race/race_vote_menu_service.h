/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <stdbool.h>

typedef struct g_client_s g_client_t;

void Race_VoteMenuService_Init(void);
void Race_VoteMenuService_Shutdown(void);
void Race_VoteMenuService_ConfigureLevel(void);
void Race_VoteMenuService_Frame(void);
void Race_VoteMenuService_ClientDisconnect(g_client_t *cl);
bool Race_VoteMenuService_ClientCanCast(const g_client_t *cl);
bool Race_VoteMenuService_ClientCanNominate(const g_client_t *cl);
void Race_VoteMenuService_Nominate(g_client_t *cl, const char *map);
bool Race_VoteMenuService_ClientCommand(g_client_t *cl, const char *cmd);
bool Race_VoteMenuService_IntermissionReady(void);
