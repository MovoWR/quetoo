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

#include "race_map_state.h"
#include "race_settings.h"

#define RACE_MAP_PROPERTIES_MAX_FILE_BYTES (256u * 1024u)
#define RACE_MAP_PROPERTIES_MAX_ROWS 256u
#define RACE_MAP_PROPERTIES_MAX_VIRTUAL_PATH 63u

typedef enum {
  RACE_MAP_PROPERTIES_OK,
  RACE_MAP_PROPERTIES_INVALID_ARGUMENT,
  RACE_MAP_PROPERTIES_UNSAFE_PATH,
  RACE_MAP_PROPERTIES_TOO_LARGE,
  RACE_MAP_PROPERTIES_MALFORMED,
  RACE_MAP_PROPERTIES_TOKEN_TOO_LONG,
  RACE_MAP_PROPERTIES_TOO_MANY_ROWS,
  RACE_MAP_PROPERTIES_INVALID_MAP,
  RACE_MAP_PROPERTIES_UNSUPPORTED_PROPERTY,
  RACE_MAP_PROPERTIES_INVALID_VALUE,
  RACE_MAP_PROPERTIES_DUPLICATE_MAP,
  RACE_MAP_PROPERTIES_DUPLICATE_PROPERTY,
  RACE_MAP_PROPERTIES_OUTPUT_TOO_SMALL,
  RACE_MAP_PROPERTIES_EXPECTATION_FAILED
} race_map_properties_result_t;

typedef struct {
  uint16_t occurrences;
  bool present;
  bool valid;
  bool duplicate;
  char canonical[RACE_SETTING_VALUE_SIZE];
} race_map_property_t;

/**
 * @brief The descriptor-backed properties found on one canonical map row.
 *
 * `map_matches` deliberately distinguishes an absent row, one authoritative
 * row, and an ambiguous catalog. Properties are populated only from the first
 * matching row; callers must require exactly one match before using them.
 */
typedef struct {
  char map[RACE_MAP_IDENTITY_SIZE];
  size_t rows;
  size_t map_matches;
  race_map_property_t properties[RACE_SETTING_TOTAL];
} race_map_properties_t;

typedef struct {
  size_t length;
  bool changed;
  bool appended_row;
  bool removed;
} race_map_properties_edit_t;

/**
 * @brief Validates a game-relative catalog filename before it is handed to a
 * write-directory resolver.
 */
race_map_properties_result_t Race_MapProperties_ValidateVirtualPath(
  const char *path, char *error, size_t error_size);

/**
 * @brief Parses the complete bounded catalog and projects the exact canonical
 * target row onto the shared settings registry.
 */
race_map_properties_result_t Race_MapProperties_Parse(
  const char *contents, size_t length, const char *map,
  race_map_properties_t *properties, char *error, size_t error_size);

/**
 * @brief Produces a complete candidate catalog with one property set or
 * cleared. A NULL `canonical_value` means clear. Setting on an absent map
 * appends a minimal row; clearing an absent property is a successful no-op.
 */
race_map_properties_result_t Race_MapProperties_Edit(
  const char *contents, size_t length, const char *map,
  const race_setting_descriptor_t *descriptor,
  const char *canonical_value,
  char *output, size_t output_size,
  race_map_properties_edit_t *edit,
  char *error, size_t error_size);

/**
 * @brief Fully reparses an edited candidate and verifies the expected target
 * property. A NULL `canonical_value` expects the property to be absent.
 */
race_map_properties_result_t Race_MapProperties_ValidateCandidate(
  const char *contents, size_t length, const char *map,
  const race_setting_descriptor_t *descriptor,
  const char *canonical_value,
  race_map_properties_t *properties,
  char *error, size_t error_size);

const char *Race_MapProperties_ResultName(race_map_properties_result_t result);
