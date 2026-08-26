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
#include <stdint.h>

#include "game/race/race_settings_wire.h"

/**
 * @brief The client's read-only view of the GSET and MSET registry stores.
 *
 * @details The cgame compiles `race_settings.c`, so the catalog - aliases,
 * cvars, types, ranges, defaults, activation - is already local. State is not:
 * `cgi.Print` runs module-to-console and no import carries console output back,
 * so a `gget` reply cannot be read from here. `CS_RACE_SETTINGS_STATUS` is the
 * read path, and this module is where it lands.
 *
 * Everything here is presentation. The server revalidates every `gset`, `mset`,
 * `gclear` and `mclear` it receives, so a stale or absent mirror can only make
 * the menu draw the wrong label - never write the wrong value.
 */

/**
 * @brief Resets the mirror and adopts whatever the current ConfigString holds.
 */
void Cg_RaceSettings_Init(void);

/**
 * @brief Drops the mirror. Called on disconnect and on level change.
 */
void Cg_RaceSettings_Clear(void);

/**
 * @brief Adopts a CS_RACE_SETTINGS_STATUS payload.
 * @details A payload that does not decode leaves the mirror invalid rather than
 * partially applied, so the menu reports "unavailable" instead of mixing fresh
 * and stale rows.
 */
void Cg_RaceSettings_UpdateStatus(const char *status);

/**
 * @brief True when the server has published a payload this session.
 * @details False against a server that predates the mirror, which is the case
 * the registry tabs have to explain rather than silently show defaults for.
 */
bool Cg_RaceSettings_Valid(void);

/**
 * @brief The decoded mirror, or NULL when none is valid.
 */
const race_settings_status_t *Cg_RaceSettings_Status(void);

/**
 * @brief The map the overrides belong to, or NULL when no level is named.
 */
const char *Cg_RaceSettings_Map(void);

/**
 * @brief True when a value was too long to mirror and the menu must say so.
 */
bool Cg_RaceSettings_Truncated(void);

/**
 * @brief True when @p id carries an assignment in the named store.
 * @param map_store True for MSET, false for GSET.
 */
bool Cg_RaceSettings_Assigned(race_setting_id_t id, bool map_store);

/**
 * @brief The raw assignment @p id carries in the named store.
 * @return The canonical value, or NULL when the row is unassigned or its value
 * was elided. An assigned row with a NULL value is the truncation case.
 */
const char *Cg_RaceSettings_Assignment(race_setting_id_t id, bool map_store);

/**
 * @brief The effective value of @p id: map override, else global, else default.
 * @param assigned Set when the result came from a store. May be NULL.
 * @param from_map Set when the result came from the map store. May be NULL.
 * @return The value, or NULL when the winning assignment's value was elided.
 */
const char *Cg_RaceSettings_Effective(race_setting_id_t id, bool *assigned,
                                      bool *from_map);

/**
 * @brief How many descriptors carry an assignment in the named store.
 * @details The `n of 15 assigned` / `overridden` count in each card head.
 */
size_t Cg_RaceSettings_AssignedCount(bool map_store);
