/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>

#include "race_settings_wire.h"

/**
 * @brief Every bit a mask may legally light.
 */
#define RACE_SETTINGS_STATUS_MASK ((1u << RACE_SETTING_TOTAL) - 1u)

/**
 * @brief The field count that precedes the value list.
 * @details version, map, global mask, map mask, truncated.
 */
#define RACE_SETTINGS_STATUS_FIXED_FIELDS 5u

/**
 * @brief The most fields a payload can carry.
 */
#define RACE_SETTINGS_STATUS_MAX_FIELDS \
  (RACE_SETTINGS_STATUS_FIXED_FIELDS + (RACE_SETTING_TOTAL * 2u))

/**
 * @brief True when @p c may travel in a value unescaped.
 * @details Backslash is the field separator and percent introduces an escape,
 * so both are spelled out. Everything else a canonical value can hold is
 * printable ASCII, which the payload carries verbatim.
 */
static bool Race_SettingsWire_Literal(const char c) {
  return c >= 0x20 && c < 0x7f && c != '\\' && c != '%';
}

/**
 * @brief Appends @p value to @p output, escaping the two reserved characters.
 * @return The length written, or 0 when it did not fit.
 */
static size_t Race_SettingsWire_Escape(const char *value, char *output,
                                       const size_t capacity) {
  size_t length = 0u;
  for (const char *c = value; *c; c++) {
    if (Race_SettingsWire_Literal(*c)) {
      if (length + 1u >= capacity) {
        return 0u;
      }
      output[length++] = *c;
    } else if (*c == '\\' || *c == '%') {
      if (length + 3u >= capacity) {
        return 0u;
      }
      output[length++] = '%';
      output[length++] = *c == '\\' ? '5' : '2';
      output[length++] = *c == '\\' ? 'C' : '5';
    } else {
      return 0u;
    }
  }
  output[length] = '\0';
  return length;
}

/**
 * @brief Reverses Race_SettingsWire_Escape.
 * @return True when @p field was a well-formed escaped value that fit.
 */
static bool Race_SettingsWire_Unescape(const char *field, char *output,
                                       const size_t capacity) {
  size_t length = 0u;
  for (const char *c = field; *c; c++) {
    if (length + 1u >= capacity) {
      return false;
    }
    if (*c != '%') {
      if (!Race_SettingsWire_Literal(*c)) {
        return false;
      }
      output[length++] = *c;
      continue;
    }
    if (c[1] == '5' && c[2] == 'C') {
      output[length++] = '\\';
    } else if (c[1] == '2' && c[2] == '5') {
      output[length++] = '%';
    } else {
      return false;
    }
    c += 2;
  }
  output[length] = '\0';
  return true;
}

/**
 * @brief True when @p map is a legal map field.
 */
static bool Race_SettingsWire_Map(const char *map) {
  const size_t length = strlen(map);
  if (length > RACE_SETTING_NAME_MAX) {
    return false;
  }
  for (const char *c = map; *c; c++) {
    if (!Race_SettingsWire_Literal(*c)) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Appends one mask's values to @p output in ascending descriptor order.
 * @return True when every set bit's value fit.
 */
static bool Race_SettingsWire_EncodeValues(
    const char values[RACE_SETTINGS_MAX][RACE_SETTINGS_STATUS_VALUE_SIZE],
    const uint32_t mask, char *output, const size_t capacity,
    size_t *length) {

  for (uint16_t id = 0u; id < RACE_SETTING_TOTAL; id++) {
    if (!(mask & (1u << id))) {
      continue;
    }
    if (*length + 1u >= capacity) {
      return false;
    }
    output[(*length)++] = '\\';

    const char *value = values[id];
    if (strlen(value) > RACE_SETTINGS_STATUS_VALUE_MAX) {
      return false;
    }
    if (*value) {
      const size_t written = Race_SettingsWire_Escape(
        value, output + *length, capacity - *length);
      if (!written) {
        return false;
      }
      *length += written;
    } else {
      output[*length] = '\0';
    }
  }
  return true;
}

bool Race_Settings_StatusEncode(const race_settings_status_t *status,
                                char *output, const size_t capacity) {

  if (!status || !output || !capacity ||
      (status->global_mask & ~RACE_SETTINGS_STATUS_MASK) ||
      (status->map_mask & ~RACE_SETTINGS_STATUS_MASK) ||
      !Race_SettingsWire_Map(status->map)) {
    return false;
  }

  // A map override belongs to a map. Publishing one without naming the level it
  // came from would leave the menu unable to label the store, so the pairing is
  // enforced here rather than trusted.
  if (status->map_mask && !status->map[0]) {
    return false;
  }

  const int32_t header = snprintf(output, capacity, "v%u\\%s\\%04x\\%04x\\%u",
                                  RACE_SETTINGS_STATUS_VERSION, status->map,
                                  status->global_mask, status->map_mask,
                                  status->truncated ? 1u : 0u);
  if (header < 0 || (size_t) header >= capacity) {
    return false;
  }

  size_t length = (size_t) header;
  if (!Race_SettingsWire_EncodeValues(status->global, status->global_mask,
                                      output, capacity, &length) ||
      !Race_SettingsWire_EncodeValues(status->overrides, status->map_mask,
                                      output, capacity, &length)) {
    return false;
  }

  output[length] = '\0';
  return length <= RACE_SETTINGS_STATUS_MAX;
}

/**
 * @brief Reads a `%04x` mask field.
 */
static bool Race_SettingsWire_Mask(const char *field, uint32_t *mask) {

  if (strlen(field) != 4u) {
    return false;
  }
  uint32_t parsed = 0u;
  for (const char *c = field; *c; c++) {
    parsed <<= 4;
    if (*c >= '0' && *c <= '9') {
      parsed |= (uint32_t) (*c - '0');
    } else if (*c >= 'a' && *c <= 'f') {
      parsed |= (uint32_t) (*c - 'a') + 10u;
    } else {
      return false;
    }
  }
  if (parsed & ~RACE_SETTINGS_STATUS_MASK) {
    return false;
  }
  *mask = parsed;
  return true;
}

/**
 * @brief Consumes one mask's worth of value fields into @p values.
 */
static bool Race_SettingsWire_DecodeValues(
    char values[RACE_SETTINGS_MAX][RACE_SETTINGS_STATUS_VALUE_SIZE],
    const uint32_t mask, char *const *fields, const size_t field_count,
    size_t *cursor) {

  for (uint16_t id = 0u; id < RACE_SETTING_TOTAL; id++) {
    if (!(mask & (1u << id))) {
      continue;
    }
    if (*cursor >= field_count) {
      return false;
    }
    if (!Race_SettingsWire_Unescape(fields[(*cursor)++], values[id],
                                    RACE_SETTINGS_STATUS_VALUE_SIZE)) {
      return false;
    }
  }
  return true;
}

bool Race_Settings_StatusDecode(const char *input,
                                race_settings_status_t *status) {

  if (!input || !status || strlen(input) > RACE_SETTINGS_STATUS_MAX) {
    return false;
  }

  char copy[RACE_SETTINGS_STATUS_SIZE];
  memcpy(copy, input, strlen(input) + 1u);

  char *fields[RACE_SETTINGS_STATUS_MAX_FIELDS] = { 0 };
  size_t field_count = 0u;
  fields[field_count++] = copy;
  for (char *c = copy; *c; c++) {
    if (*c == '\\') {
      *c = '\0';
      if (field_count == RACE_SETTINGS_STATUS_MAX_FIELDS) {
        return false;
      }
      fields[field_count++] = c + 1;
    }
  }
  if (field_count < RACE_SETTINGS_STATUS_FIXED_FIELDS) {
    return false;
  }

  char version[8];
  snprintf(version, sizeof(version), "v%u", RACE_SETTINGS_STATUS_VERSION);
  if (strcmp(fields[0], version)) {
    return false;
  }

  race_settings_status_t parsed = { 0 };
  const size_t map_length = strlen(fields[1]);
  if (map_length > RACE_SETTING_NAME_MAX) {
    return false;
  }
  memcpy(parsed.map, fields[1], map_length + 1u);

  if (!Race_SettingsWire_Mask(fields[2], &parsed.global_mask) ||
      !Race_SettingsWire_Mask(fields[3], &parsed.map_mask)) {
    return false;
  }
  if (!strcmp(fields[4], "1")) {
    parsed.truncated = true;
  } else if (strcmp(fields[4], "0")) {
    return false;
  }

  size_t cursor = RACE_SETTINGS_STATUS_FIXED_FIELDS;
  if (!Race_SettingsWire_DecodeValues(parsed.global, parsed.global_mask, fields,
                                      field_count, &cursor) ||
      !Race_SettingsWire_DecodeValues(parsed.overrides, parsed.map_mask, fields,
                                      field_count, &cursor) ||
      cursor != field_count) {
    return false;
  }

  // The round trip is the validator: anything non-canonical - a stray escape, a
  // mask bit with no value behind it, a map name that needed quoting - fails
  // here rather than reaching the menu as plausible-looking state.
  char canonical[RACE_SETTINGS_STATUS_SIZE];
  if (!Race_Settings_StatusEncode(&parsed, canonical, sizeof(canonical)) ||
      strcmp(canonical, input)) {
    return false;
  }

  *status = parsed;
  return true;
}

const char *Race_Settings_StatusEffective(const race_settings_status_t *status,
                                          const race_setting_id_t id,
                                          bool *assigned, bool *from_map) {

  if (assigned) {
    *assigned = false;
  }
  if (from_map) {
    *from_map = false;
  }
  if (!status || id >= RACE_SETTING_TOTAL) {
    return NULL;
  }

  const uint32_t bit = 1u << (uint16_t) id;
  if (status->map_mask & bit) {
    if (assigned) {
      *assigned = true;
    }
    if (from_map) {
      *from_map = true;
    }
    return status->overrides[id][0] ? status->overrides[id] : NULL;
  }
  if (status->global_mask & bit) {
    if (assigned) {
      *assigned = true;
    }
    return status->global[id][0] ? status->global[id] : NULL;
  }

  size_t count = 0u;
  const race_setting_descriptor_t *catalog = Race_Settings_Catalog(&count);
  return (size_t) id < count ? catalog[id].default_value : NULL;
}
