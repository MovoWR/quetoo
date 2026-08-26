/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "race_admin_types.h"

typedef struct g_client_s g_client_t;

void Race_AdminService_Init(void);
void Race_AdminService_PostInit(void);
void Race_AdminService_ClientChallenge(g_client_t *cl, const char *account);
void Race_AdminService_ClientProof(g_client_t *cl, const char *account,
                                   const char *nonce, const char *proof);
void Race_AdminService_ClientLogout(g_client_t *cl);
void Race_AdminService_ClientAccountCommand(g_client_t *cl);
void Race_AdminService_ClientCvarAllowlistCommand(g_client_t *cl,
                                                   const char *operation,
                                                   const char *name);
uint32_t Race_AdminService_ClientCapabilities(const g_client_t *cl);
bool Race_AdminService_ClientCvarAllowed(const g_client_t *cl,
                                         const char *name);
bool Race_AdminService_AuthorizeClientAction(g_client_t *cl,
                                             race_admin_action_t action);
void Race_AdminService_AuditClientAction(const g_client_t *cl,
                                         race_admin_action_t action,
                                         const char *subject,
                                         const char *result);
void Race_AdminService_PrintClientStatus(g_client_t *cl);
