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

#include "race_map_browser_wire.h"

bool Cg_RaceMapBrowser_ParseMessage(int32_t command);
void Cg_RaceMapBrowser_Clear(void);
const race_map_browser_page_t *Cg_RaceMapBrowser_Page(void);
const race_map_browser_detail_t *Cg_RaceMapBrowser_Detail(void);
