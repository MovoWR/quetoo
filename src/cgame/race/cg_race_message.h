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
#include <stddef.h>

typedef void (*cg_race_message_read_data_t)(void *data, size_t length);

bool Cg_RaceMessage_StringComplete(const char *payload, size_t *length);
bool Cg_RaceMessage_Drain(cg_race_message_read_data_t read_data,
                          size_t length);
