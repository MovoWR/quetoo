/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Writes one inactive client-owned file slot and verifies its bytes.
 * @details The caller must not publish or select `path` until this succeeds.
 * The frozen CGAME import has no rename or flush operation, so Race preserves
 * the last selected slot and commits by switching logical ownership only after
 * a complete write, successful close, and exact readback.
 */
bool Cg_RaceClientFile_WriteVerified(const char *path,
                                     const void *data, size_t length);
