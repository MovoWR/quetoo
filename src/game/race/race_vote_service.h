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
#include <stdint.h>

typedef struct g_client_s g_client_t;

void Race_VoteService_Init(void);
void Race_VoteService_Shutdown(void);
void Race_VoteService_ConfigureLevel(void);
void Race_VoteService_ClientUserInfoChanged(g_client_t *cl);
void Race_VoteService_ClientDisconnect(g_client_t *cl);
void Race_VoteService_NoteActivity(g_client_t *cl);
uint64_t Race_VoteService_ConnectionId(g_client_t *cl);
bool Race_VoteService_ClientCanCast(g_client_t *cl);
void Race_VoteService_Frame(void);
void Race_VoteService_NextMapVoteBegin(void);
void Race_VoteService_ClientCommand(g_client_t *cl);
bool Race_VoteService_LegacyCommand(g_client_t *cl, const char *cmd);
void Race_VoteService_AdminCancel(g_client_t *cl);
