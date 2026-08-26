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

void Cg_RaceAdminAuth_Init(void);
void Cg_RaceAdminAuth_Clear(void);
void Cg_RaceAdminAuth_Shutdown(void);
bool Cg_RaceAdminAuth_ParseMessage(int32_t command);
