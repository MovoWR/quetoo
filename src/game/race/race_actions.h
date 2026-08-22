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

#include "race_map_state.h"

typedef struct g_client_s g_client_t;

void Race_Actions_Init(void);
void Race_Actions_Shutdown(void);
bool Race_Actions_ValidateMap(const char *input,
                              char canonical[RACE_MAP_IDENTITY_SIZE]);
bool Race_Actions_ScheduleMap(const char *canonical);
bool Race_Actions_KickClient(g_client_t *target, const char *reason);
