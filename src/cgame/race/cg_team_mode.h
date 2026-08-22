/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <stddef.h>

typedef struct {
  const char *var;
  const char *value;
} cg_team_mode_cvar_t;

typedef struct {
  const char *name;
  const cg_team_mode_cvar_t *cvars;
} cg_team_mode_t;

const cg_team_mode_t *Cg_TeamModes(size_t *count);
const cg_team_mode_t *Cg_TeamMode(size_t index);
