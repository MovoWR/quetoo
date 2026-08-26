/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "race_physics.h"

struct g_client_s;

void Race_PhysicsService_Init(void);
void Race_PhysicsService_Shutdown(void);
void Race_PhysicsService_ConfigureLevel(const char *map);
void Race_PhysicsService_RefreshLevelParams(void);
bool Race_PhysicsService_Rankable(void);
void Race_PhysicsService_SeedClient(struct g_client_s *cl);
void Race_PhysicsService_ClientThink(struct g_client_s *cl,
                                     const pm_cmd_t *cmd);

#if defined(RACE_PHYSICS_TEST)
bool Race_PhysicsService_ConfigureTestLevel(
  const char *map, race_physics_preset_id_t preset);
#endif
