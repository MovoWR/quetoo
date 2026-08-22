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
void Race_AdminService_ClientLogout(g_client_t *cl);
uint32_t Race_AdminService_ClientCapabilities(const g_client_t *cl);
bool Race_AdminService_AuthorizeClientAction(g_client_t *cl,
                                             race_admin_action_t action);
void Race_AdminService_AuditClientAction(const g_client_t *cl,
                                         race_admin_action_t action,
                                         const char *subject,
                                         const char *result);
void Race_AdminService_PrintClientStatus(g_client_t *cl);
