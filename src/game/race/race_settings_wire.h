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

#include "race_settings.h"

/**
 * @brief The settings registry mirror published in CS_RACE_SETTINGS_STATUS.
 *
 * @details The client already compiles `race_settings.c`, so it holds the whole
 * catalog - aliases, cvars, types, ranges, defaults and activation. What it
 * cannot see is *state*: which rows carry an assignment, and what those
 * assignments say. `cgi.Print` runs module-to-console only and nothing carries
 * console output back, so a `gget` reply is unreadable from the cgame. This
 * mirror is the read path, and it carries state alone: two bitmasks plus the
 * values of the rows those masks light.
 *
 * That keeps the wire small in the common case. An unassigned row costs one
 * bit and no bytes, so a server with the documented sample state - two global
 * assignments and two map overrides - encodes in well under a hundred bytes
 * against the 1023 a ConfigString allows.
 */

#define RACE_SETTINGS_STATUS_VERSION 1u

/**
 * @brief The longest mirrored value, before escaping.
 * @details Every numeric and enum descriptor canonicalizes far shorter than
 * this; only `music` can approach `RACE_SETTING_VALUE_MAX`. Capping here bounds
 * the wire without truncating any value an operator is likely to set.
 */
#define RACE_SETTINGS_STATUS_VALUE_MAX 95u
#define RACE_SETTINGS_STATUS_VALUE_SIZE (RACE_SETTINGS_STATUS_VALUE_MAX + 1u)

#define RACE_SETTINGS_STATUS_MAX 1023u
#define RACE_SETTINGS_STATUS_SIZE (RACE_SETTINGS_STATUS_MAX + 1u)

/**
 * @brief One decoded snapshot of both registry stores.
 */
typedef struct {
  /**
   * @brief The map the overrides belong to, empty when no level is running.
   */
  char map[RACE_SETTING_NAME_SIZE];

  /**
   * @brief Bit per descriptor id assigned in the global basis, `race/gset.cfg`.
   */
  uint32_t global_mask;

  /**
   * @brief Bit per descriptor id overridden on `map`, from `sv_map_list`.
   */
  uint32_t map_mask;

  /**
   * @brief Set when a value was too long to mirror and was elided.
   * @details The row still reports as assigned; only its value is unknown, and
   * the menu says so rather than showing a default the server is not using.
   */
  bool truncated;

  /**
   * @brief Canonical global assignments, indexed by descriptor id.
   * @details Only entries whose `global_mask` bit is set are meaningful. An
   * empty string on a set bit means the value was elided; see `truncated`.
   */
  char global[RACE_SETTINGS_MAX][RACE_SETTINGS_STATUS_VALUE_SIZE];

  /**
   * @brief Canonical map overrides, indexed by descriptor id.
   */
  char overrides[RACE_SETTINGS_MAX][RACE_SETTINGS_STATUS_VALUE_SIZE];
} race_settings_status_t;

/**
 * @brief Encodes @p status into a ConfigString payload.
 * @details Strict: a status that does not fit `RACE_SETTINGS_STATUS_MAX` is
 * rejected rather than silently shortened, so the caller decides what to drop
 * and records the decision in `truncated`. That keeps this function a pure
 * function of the struct, which is what lets `Race_Settings_StatusDecode`
 * validate by re-encoding.
 * @return True when the whole status was written.
 */
bool Race_Settings_StatusEncode(const race_settings_status_t *status,
                                char *output, size_t capacity);

/**
 * @brief Decodes a ConfigString payload into @p status.
 * @details Rejects any payload that does not re-encode to itself byte for byte,
 * so a malformed or non-canonical wire never reaches the menu.
 * @return True when @p input was a canonical payload.
 */
bool Race_Settings_StatusDecode(const char *input,
                                race_settings_status_t *status);

/**
 * @brief The effective value of @p id, following the documented chain.
 * @details A map override wins over a global assignment, which wins over the
 * descriptor default. This is the same precedence the settings service applies
 * server-side; keeping one implementation of it means the menu cannot drift
 * from the store.
 * @param assigned Set to true when the result came from either store rather
 * than from the catalog default. May be NULL.
 * @param from_map Set to true when the result came from the map store. May be
 * NULL.
 * @return The effective value, or NULL when @p id has no descriptor or the
 * winning assignment had its value elided.
 */
const char *Race_Settings_StatusEffective(const race_settings_status_t *status,
                                          race_setting_id_t id, bool *assigned,
                                          bool *from_map);
