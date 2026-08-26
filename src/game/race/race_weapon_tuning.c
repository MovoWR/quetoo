/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_weapon_tuning.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define INT_DESCRIPTOR(id_, group_, key_, group_key_, group_label_, label_, \
                       unit_, default_, min_, max_, step_) \
  { \
    .id = id_, .group = group_, .type = RACE_WEAPON_TUNING_TYPE_INT32, \
    .key = key_, .group_key = group_key_, .group_label = group_label_, \
    .label = label_, .unit = unit_, \
    .compiled_default.integer = default_, .minimum.integer = min_, \
    .maximum.integer = max_, .step.integer = step_ \
  }

#define UINT_DESCRIPTOR(id_, group_, key_, group_key_, group_label_, label_, \
                        unit_, default_, min_, max_, step_) \
  { \
    .id = id_, .group = group_, .type = RACE_WEAPON_TUNING_TYPE_UINT32, \
    .key = key_, .group_key = group_key_, .group_label = group_label_, \
    .label = label_, .unit = unit_, \
    .compiled_default.unsigned_integer = default_, \
    .minimum.unsigned_integer = min_, .maximum.unsigned_integer = max_, \
    .step.unsigned_integer = step_ \
  }

#define FLOAT_DESCRIPTOR(id_, group_, key_, group_key_, group_label_, label_, \
                         unit_, default_, min_, max_, step_) \
  { \
    .id = id_, .group = group_, .type = RACE_WEAPON_TUNING_TYPE_FLOAT, \
    .key = key_, .group_key = group_key_, .group_label = group_label_, \
    .label = label_, .unit = unit_, \
    .compiled_default.real = default_, .minimum.real = min_, \
    .maximum.real = max_, .step.real = step_ \
  }

static const race_weapon_tuning_descriptor_t race_weapon_tuning_catalog[] = {
  INT_DESCRIPTOR(RACE_WEAPON_TUNING_HYPER_KNOCKBACK,
    RACE_WEAPON_TUNING_GROUP_HYPER, "hyper.knockback", "hyper",
    "Hyperblaster", "Knockback", "scalar", 4, 0, 300, 1),
  UINT_DESCRIPTOR(RACE_WEAPON_TUNING_HYPER_REFIRE_MS,
    RACE_WEAPON_TUNING_GROUP_HYPER, "hyper.refire_ms", "hyper",
    "Hyperblaster", "Refire", "ms", 100u, 25u, 1000u, 25u),
  INT_DESCRIPTOR(RACE_WEAPON_TUNING_HYPER_SPEED,
    RACE_WEAPON_TUNING_GROUP_HYPER, "hyper.speed", "hyper",
    "Hyperblaster", "Speed", "UPS", 1800, 100, 2400, 25),
  FLOAT_DESCRIPTOR(RACE_WEAPON_TUNING_HYPER_CLIMB_IMPULSE_Z,
    RACE_WEAPON_TUNING_GROUP_HYPER, "hyper.climb_impulse_z", "hyper",
    "Hyperblaster", "Climb up", "UPS", 68.f, 0.f, 200.f, 1.f),
  FLOAT_DESCRIPTOR(RACE_WEAPON_TUNING_HYPER_CLIMB_IN,
    RACE_WEAPON_TUNING_GROUP_HYPER, "hyper.climb_in", "hyper",
    "Hyperblaster", "Climb in", "UPS", 0.f, 0.f, 200.f, 1.f),
  FLOAT_DESCRIPTOR(RACE_WEAPON_TUNING_HYPER_CLIMB_VELOCITY_BOOST,
    RACE_WEAPON_TUNING_GROUP_HYPER, "hyper.climb_velocity_boost", "hyper",
    "Hyperblaster", "Climb vel boost", "x", 0.f, 0.f, .5f, .01f),
  FLOAT_DESCRIPTOR(RACE_WEAPON_TUNING_HYPER_CLIMB_3D,
    RACE_WEAPON_TUNING_GROUP_HYPER, "hyper.climb_3d", "hyper",
    "Hyperblaster", "Climb 3D", "weight", 0.f, 0.f, 1.f, .05f),
  FLOAT_DESCRIPTOR(RACE_WEAPON_TUNING_HYPER_CLIMB_3D_UP,
    RACE_WEAPON_TUNING_GROUP_HYPER, "hyper.climb_3d_up", "hyper",
    "Hyperblaster", "Climb 3D up", "weight", 1.f, 0.f, 1.f, .05f),
  FLOAT_DESCRIPTOR(RACE_WEAPON_TUNING_HYPER_CLIMB_3D_SIDE,
    RACE_WEAPON_TUNING_GROUP_HYPER, "hyper.climb_3d_side", "hyper",
    "Hyperblaster", "Climb 3D side", "weight", 1.f, 0.f, 1.f, .05f),
  FLOAT_DESCRIPTOR(RACE_WEAPON_TUNING_HYPER_CLIMB_3D_IN,
    RACE_WEAPON_TUNING_GROUP_HYPER, "hyper.climb_3d_in", "hyper",
    "Hyperblaster", "Climb 3D in", "weight", 0.f, 0.f, 1.f, .05f),
  FLOAT_DESCRIPTOR(RACE_WEAPON_TUNING_HYPER_CLIMB_RANGE,
    RACE_WEAPON_TUNING_GROUP_HYPER, "hyper.climb_range", "hyper",
    "Hyperblaster", "Climb range", "u", 32.f, 0.f, 128.f, 1.f),

  INT_DESCRIPTOR(RACE_WEAPON_TUNING_STANDARD_ROCKET_SPEED,
    RACE_WEAPON_TUNING_GROUP_STANDARD_ROCKET, "standard_rocket.speed",
    "standard_rocket", "Quake II Rocket Launcher", "Speed", "UPS",
    1000, 100, 2400, 25),
  INT_DESCRIPTOR(RACE_WEAPON_TUNING_STANDARD_ROCKET_KNOCKBACK,
    RACE_WEAPON_TUNING_GROUP_STANDARD_ROCKET, "standard_rocket.knockback",
    "standard_rocket", "Quake II Rocket Launcher", "Knockback", "scalar",
    75, 0, 300, 1),
  UINT_DESCRIPTOR(RACE_WEAPON_TUNING_STANDARD_ROCKET_REFIRE_MS,
    RACE_WEAPON_TUNING_GROUP_STANDARD_ROCKET, "standard_rocket.refire_ms",
    "standard_rocket", "Quake II Rocket Launcher", "Refire", "ms",
    1000u, 100u, 3000u, 25u),

  INT_DESCRIPTOR(RACE_WEAPON_TUNING_STANDARD_GRENADE_SPEED,
    RACE_WEAPON_TUNING_GROUP_STANDARD_GRENADE, "standard_grenade.speed",
    "standard_grenade", "Quake II Grenade Launcher", "Speed", "UPS",
    800, 100, 2000, 25),
  INT_DESCRIPTOR(RACE_WEAPON_TUNING_STANDARD_GRENADE_KNOCKBACK,
    RACE_WEAPON_TUNING_GROUP_STANDARD_GRENADE,
    "standard_grenade.knockback", "standard_grenade",
    "Quake II Grenade Launcher",
    "Knockback", "scalar", 120, 0, 300, 1),
  UINT_DESCRIPTOR(RACE_WEAPON_TUNING_STANDARD_GRENADE_FUSE_MS,
    RACE_WEAPON_TUNING_GROUP_STANDARD_GRENADE, "standard_grenade.fuse_ms",
    "standard_grenade", "Quake II Grenade Launcher", "Fuse", "ms",
    2500u, 100u, 10000u, 100u),
  UINT_DESCRIPTOR(RACE_WEAPON_TUNING_STANDARD_GRENADE_REFIRE_MS,
    RACE_WEAPON_TUNING_GROUP_STANDARD_GRENADE, "standard_grenade.refire_ms",
    "standard_grenade", "Quake II Grenade Launcher", "Refire", "ms",
    1000u, 100u, 3000u, 25u),

  INT_DESCRIPTOR(RACE_WEAPON_TUNING_HAND_GRENADE_KNOCKBACK,
    RACE_WEAPON_TUNING_GROUP_HAND_GRENADE, "hand_grenade.knockback",
    "hand_grenade", "Quake II Hand Grenade", "Knockback", "scalar",
    120, 0, 300, 1),
  UINT_DESCRIPTOR(RACE_WEAPON_TUNING_HAND_GRENADE_REFIRE_MS,
    RACE_WEAPON_TUNING_GROUP_HAND_GRENADE, "hand_grenade.refire_ms",
    "hand_grenade", "Quake II Hand Grenade", "Refire", "ms",
    2000u, 100u, 3000u, 25u),

  FLOAT_DESCRIPTOR(RACE_WEAPON_TUNING_GLOBAL_SELF_KNOCKBACK,
    RACE_WEAPON_TUNING_GROUP_GLOBAL, "global.self_knockback", "global",
    "Global", "Self knockback", "x", 1.f, 0.f, 4.f, .05f),
  FLOAT_DESCRIPTOR(RACE_WEAPON_TUNING_GRENADE_INHERIT_FRACTION,
    RACE_WEAPON_TUNING_GROUP_GLOBAL, "grenade.inherit_fraction", "global",
    "Global", "Grenade inheritance", "fraction", .33f, 0.f, 1.f, .05f)
};

_Static_assert(sizeof(race_weapon_tuning_catalog) /
                 sizeof(race_weapon_tuning_catalog[0]) ==
                 RACE_WEAPON_TUNING_VALUE_COUNT,
               "Weapon tuning descriptor count changed");

static void Race_WeaponTuning_Error(char *error, const size_t error_size,
                                   const char *format, ...) {
  if (!error || !error_size) {
    return;
  }
  va_list args;
  va_start(args, format);
  vsnprintf(error, error_size, format, args);
  va_end(args);
  error[error_size - 1u] = '\0';
}

const race_weapon_tuning_descriptor_t *Race_WeaponTuning_Catalog(
    size_t *count) {
  if (count) {
    *count = RACE_WEAPON_TUNING_VALUE_COUNT;
  }
  return race_weapon_tuning_catalog;
}

const race_weapon_tuning_descriptor_t *Race_WeaponTuning_Descriptor(
    const race_weapon_tuning_id_t id) {
  return id >= 0 && id < RACE_WEAPON_TUNING_VALUE_TOTAL
    ? race_weapon_tuning_catalog + id : NULL;
}

const race_weapon_tuning_descriptor_t *Race_WeaponTuning_DescriptorForKey(
    const char *key) {
  if (key && *key) {
    for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
      if (!strcmp(key, race_weapon_tuning_catalog[i].key)) {
        return race_weapon_tuning_catalog + i;
      }
    }
  }
  return NULL;
}

static uint32_t Race_WeaponTuning_ScalarBits(
    const race_weapon_tuning_type_t type,
    const race_weapon_tuning_scalar_t value) {
  uint32_t bits;
  if (type == RACE_WEAPON_TUNING_TYPE_INT32) {
    memcpy(&bits, &value.integer, sizeof(bits));
  } else if (type == RACE_WEAPON_TUNING_TYPE_UINT32) {
    bits = value.unsigned_integer;
  } else {
    memcpy(&bits, &value.real, sizeof(bits));
  }
  return bits;
}

static void Race_WeaponTuning_HashBytes(uint64_t *hash,
                                       const void *data,
                                       const size_t length) {
  const uint8_t *bytes = data;
  for (size_t i = 0u; i < length; i++) {
    *hash ^= bytes[i];
    *hash *= UINT64_C(1099511628211);
  }
}

static void Race_WeaponTuning_Hash32(uint64_t *hash, const uint32_t value) {
  uint8_t bytes[4] = {
    (uint8_t) value, (uint8_t) (value >> 8u),
    (uint8_t) (value >> 16u), (uint8_t) (value >> 24u)
  };
  Race_WeaponTuning_HashBytes(hash, bytes, sizeof(bytes));
}

uint64_t Race_WeaponTuning_CatalogHash(void) {
  uint64_t hash = UINT64_C(14695981039346656037);
  Race_WeaponTuning_Hash32(&hash, RACE_WEAPON_TUNING_CATALOG_VERSION);
  Race_WeaponTuning_Hash32(&hash, RACE_WEAPON_TUNING_VALUE_COUNT);
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
    const race_weapon_tuning_descriptor_t *descriptor =
      race_weapon_tuning_catalog + i;
    Race_WeaponTuning_Hash32(&hash, (uint32_t) descriptor->id);
    Race_WeaponTuning_Hash32(&hash, (uint32_t) descriptor->group);
    Race_WeaponTuning_Hash32(&hash, (uint32_t) descriptor->type);
    Race_WeaponTuning_HashBytes(&hash, descriptor->key,
                                strlen(descriptor->key) + 1u);
    Race_WeaponTuning_HashBytes(&hash, descriptor->group_key,
                                strlen(descriptor->group_key) + 1u);
    Race_WeaponTuning_HashBytes(&hash, descriptor->group_label,
                                strlen(descriptor->group_label) + 1u);
    Race_WeaponTuning_HashBytes(&hash, descriptor->label,
                                strlen(descriptor->label) + 1u);
    Race_WeaponTuning_HashBytes(&hash, descriptor->unit,
                                strlen(descriptor->unit) + 1u);
    Race_WeaponTuning_Hash32(&hash, Race_WeaponTuning_ScalarBits(
      descriptor->type, descriptor->compiled_default));
    Race_WeaponTuning_Hash32(&hash, Race_WeaponTuning_ScalarBits(
      descriptor->type, descriptor->minimum));
    Race_WeaponTuning_Hash32(&hash, Race_WeaponTuning_ScalarBits(
      descriptor->type, descriptor->maximum));
    Race_WeaponTuning_Hash32(&hash, Race_WeaponTuning_ScalarBits(
      descriptor->type, descriptor->step));
  }
  return hash;
}

static bool Race_WeaponTuning_StringBounded(const char *text,
                                           const size_t maximum) {
  if (!text || !*text) {
    return false;
  }
  size_t length = 0u;
  while (length <= maximum && text[length]) {
    const unsigned char c = (unsigned char) text[length++];
    if (c < 0x20u || c > 0x7eu) {
      return false;
    }
  }
  return length <= maximum;
}

bool Race_WeaponTuning_CatalogValid(char *error, const size_t error_size) {
  bool groups[RACE_WEAPON_TUNING_GROUP_TOTAL] = { false };
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
    const race_weapon_tuning_descriptor_t *descriptor =
      race_weapon_tuning_catalog + i;
    if (descriptor->id != (race_weapon_tuning_id_t) i ||
        descriptor->group < 0 ||
        descriptor->group >= RACE_WEAPON_TUNING_GROUP_TOTAL ||
        descriptor->type < 0 ||
        descriptor->type >= RACE_WEAPON_TUNING_TYPE_TOTAL ||
        !Race_WeaponTuning_StringBounded(descriptor->key,
                                         RACE_WEAPON_TUNING_KEY_MAX) ||
        !Race_WeaponTuning_StringBounded(descriptor->group_key,
                                         RACE_WEAPON_TUNING_GROUP_KEY_MAX) ||
        !Race_WeaponTuning_StringBounded(descriptor->group_label,
                                         RACE_WEAPON_TUNING_LABEL_MAX) ||
        !Race_WeaponTuning_StringBounded(descriptor->label,
                                         RACE_WEAPON_TUNING_LABEL_MAX) ||
        !Race_WeaponTuning_StringBounded(descriptor->unit,
                                         RACE_WEAPON_TUNING_UNIT_MAX)) {
      Race_WeaponTuning_Error(error, error_size,
                              "invalid descriptor index=%zu", i);
      return false;
    }
    groups[descriptor->group] = true;
    for (size_t j = 0u; j < i; j++) {
      if (!strcmp(descriptor->key, race_weapon_tuning_catalog[j].key)) {
        Race_WeaponTuning_Error(error, error_size,
                                "duplicate key=%s", descriptor->key);
        return false;
      }
    }
    race_weapon_tuning_scalar_t ignored;
    char value[RACE_WEAPON_TUNING_VALUE_TEXT_SIZE];
    if (!Race_WeaponTuning_FormatValue(descriptor,
                                       descriptor->compiled_default,
                                       value, sizeof(value)) ||
        !Race_WeaponTuning_ParseValue(descriptor, value, &ignored,
                                      error, error_size)) {
      Race_WeaponTuning_Error(error, error_size,
                              "invalid default key=%s", descriptor->key);
      return false;
    }
  }
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_GROUP_TOTAL; i++) {
    if (!groups[i]) {
      Race_WeaponTuning_Error(error, error_size,
                              "empty group=%zu", i);
      return false;
    }
  }
  if (!Race_WeaponTuning_CatalogHash()) {
    Race_WeaponTuning_Error(error, error_size, "zero catalog hash");
    return false;
  }
  if (error && error_size) {
    *error = '\0';
  }
  return true;
}

static bool Race_WeaponTuning_ParseUnsigned(const char *text,
                                           const uint64_t maximum,
                                           uint64_t *value) {
  if (!text || !*text || !value || (text[0] == '0' && text[1])) {
    return false;
  }
  uint64_t parsed = 0u;
  for (const unsigned char *c = (const unsigned char *) text; *c; c++) {
    if (!isdigit(*c)) {
      return false;
    }
    const uint64_t digit = (uint64_t) (*c - '0');
    if (parsed > (maximum - digit) / 10u) {
      return false;
    }
    parsed = parsed * 10u + digit;
  }
  *value = parsed;
  return true;
}

static bool Race_WeaponTuning_ParseSigned(const char *text,
                                         int64_t *value) {
  if (!text || !*text || !value || text[0] == '+' ||
      (text[0] == '0' && text[1]) ||
      (text[0] == '-' && (!text[1] || text[1] == '0'))) {
    return false;
  }
  const bool negative = text[0] == '-';
  uint64_t magnitude;
  const uint64_t maximum = negative
    ? (uint64_t) INT32_MAX + 1u : (uint64_t) INT32_MAX;
  if (!Race_WeaponTuning_ParseUnsigned(text + (negative ? 1u : 0u),
                                       maximum, &magnitude)) {
    return false;
  }
  *value = negative ? -(int64_t) magnitude : (int64_t) magnitude;
  return true;
}

static bool Race_WeaponTuning_ParseReal(const char *text, float *value) {
  if (!text || !*text || !value || text[0] == '+' ||
      (text[0] == '0' && text[1] && text[1] != '.')) {
    return false;
  }
  const bool negative = text[0] == '-';
  const unsigned char *cursor = (const unsigned char *) text +
                                (negative ? 1u : 0u);
  if (!isdigit(*cursor)) {
    return false;
  }
  double parsed = 0.0;
  while (isdigit(*cursor)) {
    parsed = parsed * 10.0 + (double) (*cursor++ - '0');
    if (parsed > 1000000.0) {
      return false;
    }
  }
  if (*cursor == '.') {
    cursor++;
    if (!isdigit(*cursor)) {
      return false;
    }
    double place = .1;
    size_t digits = 0u;
    while (isdigit(*cursor)) {
      if (digits++ >= 6u) {
        return false;
      }
      parsed += (double) (*cursor++ - '0') * place;
      place *= .1;
    }
  }
  if (*cursor) {
    return false;
  }
  parsed = negative ? -parsed : parsed;
  if (!isfinite(parsed) || parsed < -FLT_MAX || parsed > FLT_MAX) {
    return false;
  }
  *value = (float) parsed;
  return isfinite(*value);
}

static bool Race_WeaponTuning_ValueValid(
    const race_weapon_tuning_descriptor_t *descriptor,
    const race_weapon_tuning_scalar_t value,
    char *error, const size_t error_size) {
  if (descriptor->type == RACE_WEAPON_TUNING_TYPE_INT32) {
    if (value.integer < descriptor->minimum.integer ||
        value.integer > descriptor->maximum.integer ||
        (value.integer - descriptor->minimum.integer) %
          descriptor->step.integer) {
      Race_WeaponTuning_Error(error, error_size,
                              "out of range or step key=%s",
                              descriptor->key);
      return false;
    }
  } else if (descriptor->type == RACE_WEAPON_TUNING_TYPE_UINT32) {
    if (value.unsigned_integer < descriptor->minimum.unsigned_integer ||
        value.unsigned_integer > descriptor->maximum.unsigned_integer ||
        (value.unsigned_integer - descriptor->minimum.unsigned_integer) %
          descriptor->step.unsigned_integer) {
      Race_WeaponTuning_Error(error, error_size,
                              "out of range or step key=%s",
                              descriptor->key);
      return false;
    }
  } else {
    if (!isfinite(value.real) || value.real < descriptor->minimum.real ||
        value.real > descriptor->maximum.real) {
      Race_WeaponTuning_Error(error, error_size,
                              "out of range key=%s", descriptor->key);
      return false;
    }
    const float minimum_steps =
      (value.real - descriptor->minimum.real) / descriptor->step.real;
    const float default_steps =
      (value.real - descriptor->compiled_default.real) /
        descriptor->step.real;
    const bool on_minimum_grid = isfinite(minimum_steps) &&
      fabsf(minimum_steps - roundf(minimum_steps)) <= .0001f;
    const bool on_default_grid = isfinite(default_steps) &&
      fabsf(default_steps - roundf(default_steps)) <= .0001f;
    if (!on_minimum_grid && !on_default_grid) {
      Race_WeaponTuning_Error(error, error_size,
                              "off step key=%s", descriptor->key);
      return false;
    }
  }
  return true;
}

bool Race_WeaponTuning_ParseValue(
    const race_weapon_tuning_descriptor_t *descriptor, const char *text,
    race_weapon_tuning_scalar_t *value, char *error,
    const size_t error_size) {
  if (!descriptor || !text || !value ||
      strlen(text) > RACE_WEAPON_TUNING_VALUE_TEXT_MAX) {
    Race_WeaponTuning_Error(error, error_size, "malformed value");
    return false;
  }
  race_weapon_tuning_scalar_t parsed = { 0 };
  if (descriptor->type == RACE_WEAPON_TUNING_TYPE_INT32) {
    int64_t integer;
    if (!Race_WeaponTuning_ParseSigned(text, &integer)) {
      Race_WeaponTuning_Error(error, error_size,
                              "invalid integer key=%s", descriptor->key);
      return false;
    }
    parsed.integer = (int32_t) integer;
  } else if (descriptor->type == RACE_WEAPON_TUNING_TYPE_UINT32) {
    uint64_t integer;
    if (!Race_WeaponTuning_ParseUnsigned(text, UINT32_MAX, &integer)) {
      Race_WeaponTuning_Error(error, error_size,
                              "invalid unsigned key=%s", descriptor->key);
      return false;
    }
    parsed.unsigned_integer = (uint32_t) integer;
  } else if (descriptor->type == RACE_WEAPON_TUNING_TYPE_FLOAT) {
    if (!Race_WeaponTuning_ParseReal(text, &parsed.real)) {
      Race_WeaponTuning_Error(error, error_size,
                              "invalid finite float key=%s", descriptor->key);
      return false;
    }
  } else {
    Race_WeaponTuning_Error(error, error_size, "invalid value type");
    return false;
  }
  if (!Race_WeaponTuning_ValueValid(descriptor, parsed,
                                    error, error_size)) {
    return false;
  }
  *value = parsed;
  if (error && error_size) {
    *error = '\0';
  }
  return true;
}

bool Race_WeaponTuning_FormatValue(
    const race_weapon_tuning_descriptor_t *descriptor,
    const race_weapon_tuning_scalar_t value,
    char *output, const size_t capacity) {
  if (!descriptor || !output || !capacity ||
      !Race_WeaponTuning_ValueValid(descriptor, value, NULL, 0u)) {
    return false;
  }
  int32_t written;
  if (descriptor->type == RACE_WEAPON_TUNING_TYPE_INT32) {
    written = snprintf(output, capacity, "%d", value.integer);
  } else if (descriptor->type == RACE_WEAPON_TUNING_TYPE_UINT32) {
    written = snprintf(output, capacity, "%u", value.unsigned_integer);
  } else if (descriptor->step.real < 1.f) {
    written = snprintf(output, capacity, "%.2f", value.real);
    if (written > 0 && (size_t) written < capacity) {
      while (written > 0 && output[written - 1] == '0') {
        output[--written] = '\0';
      }
      if (written > 0 && output[written - 1] == '.') {
        output[--written] = '\0';
      }
    }
  } else {
    written = snprintf(output, capacity, "%.0f", value.real);
  }
  return written > 0 && (size_t) written < capacity;
}

void Race_WeaponTuning_DefaultSnapshot(
    race_weapon_tuning_snapshot_t *snapshot) {
  if (!snapshot) {
    return;
  }
  memset(snapshot, 0, sizeof(*snapshot));
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
    snapshot->values[i] = race_weapon_tuning_catalog[i].compiled_default;
  }
}

bool Race_WeaponTuning_SnapshotValid(
    const race_weapon_tuning_snapshot_t *snapshot,
    char *error, const size_t error_size) {
  if (!snapshot) {
    Race_WeaponTuning_Error(error, error_size, "missing snapshot");
    return false;
  }
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
    if (!Race_WeaponTuning_ValueValid(race_weapon_tuning_catalog + i,
                                      snapshot->values[i],
                                      error, error_size)) {
      return false;
    }
  }
  const float climb_up = snapshot->values[
    RACE_WEAPON_TUNING_HYPER_CLIMB_IMPULSE_Z].real;
  const float climb_in = snapshot->values[
    RACE_WEAPON_TUNING_HYPER_CLIMB_IN].real;
  const float velocity_boost = snapshot->values[
    RACE_WEAPON_TUNING_HYPER_CLIMB_VELOCITY_BOOST].real;
  const float climb_3d = snapshot->values[
    RACE_WEAPON_TUNING_HYPER_CLIMB_3D].real;
  const float climb_3d_in = snapshot->values[
    RACE_WEAPON_TUNING_HYPER_CLIMB_3D_IN].real;
  const uint32_t refire = snapshot->values[
    RACE_WEAPON_TUNING_HYPER_REFIRE_MS].unsigned_integer;
  const float per_shot = climb_up + climb_in *
    (1.f + climb_3d * climb_3d_in) +
    velocity_boost * RACE_WEAPON_TUNING_HYPER_VELOCITY_SOURCE_CAP;
  if (!isfinite(per_shot) || per_shot * 1000.f / (float) refire >
      RACE_WEAPON_TUNING_MAX_HYPER_IMPULSE_PER_SECOND) {
    Race_WeaponTuning_Error(error, error_size,
                            "hyper impulse rate exceeds %.0f UPS/s",
                            RACE_WEAPON_TUNING_MAX_HYPER_IMPULSE_PER_SECOND);
    return false;
  }
  if (error && error_size) {
    *error = '\0';
  }
  return true;
}

bool Race_WeaponTuning_SnapshotEqual(
    const race_weapon_tuning_snapshot_t *left,
    const race_weapon_tuning_snapshot_t *right) {
  if (!left || !right) {
    return false;
  }
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
    if (Race_WeaponTuning_ScalarBits(race_weapon_tuning_catalog[i].type,
                                     left->values[i]) !=
        Race_WeaponTuning_ScalarBits(race_weapon_tuning_catalog[i].type,
                                     right->values[i])) {
      return false;
    }
  }
  return true;
}

uint64_t Race_WeaponTuning_SnapshotHash(
    const race_weapon_tuning_snapshot_t *snapshot) {
  if (!Race_WeaponTuning_SnapshotValid(snapshot, NULL, 0u)) {
    return 0u;
  }
  uint64_t hash = UINT64_C(14695981039346656037);
  Race_WeaponTuning_Hash32(&hash, RACE_WEAPON_TUNING_CATALOG_VERSION);
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
    Race_WeaponTuning_Hash32(&hash, Race_WeaponTuning_ScalarBits(
      race_weapon_tuning_catalog[i].type, snapshot->values[i]));
  }
  return hash;
}

bool Race_WeaponTuning_SetText(
    race_weapon_tuning_snapshot_t *snapshot, const char *key,
    const char *text, char *error, const size_t error_size) {
  const race_weapon_tuning_descriptor_t *descriptor =
    Race_WeaponTuning_DescriptorForKey(key);
  race_weapon_tuning_scalar_t value;
  if (!snapshot || !descriptor ||
      !Race_WeaponTuning_ParseValue(descriptor, text, &value,
                                    error, error_size)) {
    if (!descriptor) {
      Race_WeaponTuning_Error(error, error_size,
                              "unknown key=%s", key ? key : "<none>");
    }
    return false;
  }
  const race_weapon_tuning_scalar_t previous =
    snapshot->values[descriptor->id];
  snapshot->values[descriptor->id] = value;
  if (!Race_WeaponTuning_SnapshotValid(snapshot, error, error_size)) {
    snapshot->values[descriptor->id] = previous;
    return false;
  }
  return true;
}

int32_t Race_WeaponTuning_Int(
    const race_weapon_tuning_snapshot_t *snapshot,
    const race_weapon_tuning_id_t id) {
  const race_weapon_tuning_descriptor_t *descriptor =
    Race_WeaponTuning_Descriptor(id);
  return snapshot && descriptor &&
         descriptor->type == RACE_WEAPON_TUNING_TYPE_INT32
    ? snapshot->values[id].integer : 0;
}

uint32_t Race_WeaponTuning_Uint(
    const race_weapon_tuning_snapshot_t *snapshot,
    const race_weapon_tuning_id_t id) {
  const race_weapon_tuning_descriptor_t *descriptor =
    Race_WeaponTuning_Descriptor(id);
  return snapshot && descriptor &&
         descriptor->type == RACE_WEAPON_TUNING_TYPE_UINT32
    ? snapshot->values[id].unsigned_integer : 0u;
}

float Race_WeaponTuning_Float(
    const race_weapon_tuning_snapshot_t *snapshot,
    const race_weapon_tuning_id_t id) {
  const race_weapon_tuning_descriptor_t *descriptor =
    Race_WeaponTuning_Descriptor(id);
  return snapshot && descriptor &&
         descriptor->type == RACE_WEAPON_TUNING_TYPE_FLOAT
    ? snapshot->values[id].real : 0.f;
}
