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

typedef enum {
  RACE_PERSISTENCE_OK,
  RACE_PERSISTENCE_NOT_FOUND,
  RACE_PERSISTENCE_NOT_REGULAR,
  RACE_PERSISTENCE_TOO_LARGE,
  RACE_PERSISTENCE_IO_ERROR,
  RACE_PERSISTENCE_INVALID_ARGUMENT
} race_persistence_result_t;

bool Race_Persistence_CopyRealPath(const char *virtual_path,
                                   const char *real_path,
                                   char *output, size_t capacity);
race_persistence_result_t Race_Persistence_Read(const char *path,
                                                void *buffer, size_t capacity,
                                                size_t *length);
race_persistence_result_t Race_Persistence_WriteCandidate(const char *path,
                                                          const void *data,
                                                          size_t length);
race_persistence_result_t Race_Persistence_Promote(const char *candidate,
                                                   const char *committed);
race_persistence_result_t Race_Persistence_Remove(const char *path);
const char *Race_Persistence_ResultName(race_persistence_result_t result);
