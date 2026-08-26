/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#pragma once

#include "shared/shared.h"

bool Cg_RaceAdminCommand_TokenValid(const char *value);
bool Cg_RaceAdminCommand_MapToken(const char *bsp,
                                  char output[MAX_QPATH]);
