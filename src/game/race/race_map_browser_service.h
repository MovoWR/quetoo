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
typedef struct race_map_catalog_s race_map_catalog_t;

void Race_MapBrowserService_Init(void);
bool Race_MapBrowserService_LoadCatalog(race_map_catalog_t *catalog);
bool Race_MapBrowserService_ClientCommand(g_client_t *cl, const char *cmd);
