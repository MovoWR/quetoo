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

bool Race_SpawnEntity(g_entity_t *ent);
void Race_Trigger_ConfigureLevel(void);
void Race_Trigger_FinalizeCourse(void);
bool Race_Trigger_ShouldClipMovementEntity(g_entity_t *mover,
                                           const g_entity_t *candidate,
                                           const vec3_t start,
                                           const vec3_t end,
                                           const box3_t bounds);
