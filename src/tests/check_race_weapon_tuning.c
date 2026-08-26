/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgame.h"
#include "cg_race_weapon_tuning.h"
#include "race_physics.h"
#include "race_weapon_tuning.h"
#include "race_weapon_tuning_wire.h"

static size_t assertions;
cg_import_t cgi;

static char cgame_command[1024];

bool WeaponTuningService_Lifecycle(size_t *assertion_count);

static void CgameCaptureCommand(const char *command) {
  snprintf(cgame_command, sizeof(cgame_command), "%s", command ? command : "");
}

#define REQUIRE(expression_) \
  do { \
    assertions++; \
    if (!(expression_)) { \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
              #expression_); \
      return false; \
    } \
  } while (0)

typedef struct {
  const char *key;
  const char *group_key;
  race_weapon_tuning_group_t group;
  race_weapon_tuning_type_t type;
  const char *unit;
  double compiled_default;
  double minimum;
  double maximum;
  double step;
} weapon_tuning_expected_t;

static const weapon_tuning_expected_t weapon_tuning_expected[] = {
  { "hyper.knockback", "hyper", RACE_WEAPON_TUNING_GROUP_HYPER,
    RACE_WEAPON_TUNING_TYPE_INT32, "scalar", 4, 0, 300, 1 },
  { "hyper.refire_ms", "hyper", RACE_WEAPON_TUNING_GROUP_HYPER,
    RACE_WEAPON_TUNING_TYPE_UINT32, "ms", 100, 25, 1000, 25 },
  { "hyper.speed", "hyper", RACE_WEAPON_TUNING_GROUP_HYPER,
    RACE_WEAPON_TUNING_TYPE_INT32, "UPS", 1800, 100, 2400, 25 },
  { "hyper.climb_impulse_z", "hyper", RACE_WEAPON_TUNING_GROUP_HYPER,
    RACE_WEAPON_TUNING_TYPE_FLOAT, "UPS", 68, 0, 200, 1 },
  { "hyper.climb_in", "hyper", RACE_WEAPON_TUNING_GROUP_HYPER,
    RACE_WEAPON_TUNING_TYPE_FLOAT, "UPS", 0, 0, 200, 1 },
  { "hyper.climb_velocity_boost", "hyper", RACE_WEAPON_TUNING_GROUP_HYPER,
    RACE_WEAPON_TUNING_TYPE_FLOAT, "x", 0, 0, .5, .01 },
  { "hyper.climb_3d", "hyper", RACE_WEAPON_TUNING_GROUP_HYPER,
    RACE_WEAPON_TUNING_TYPE_FLOAT, "weight", 0, 0, 1, .05 },
  { "hyper.climb_3d_up", "hyper", RACE_WEAPON_TUNING_GROUP_HYPER,
    RACE_WEAPON_TUNING_TYPE_FLOAT, "weight", 1, 0, 1, .05 },
  { "hyper.climb_3d_side", "hyper", RACE_WEAPON_TUNING_GROUP_HYPER,
    RACE_WEAPON_TUNING_TYPE_FLOAT, "weight", 1, 0, 1, .05 },
  { "hyper.climb_3d_in", "hyper", RACE_WEAPON_TUNING_GROUP_HYPER,
    RACE_WEAPON_TUNING_TYPE_FLOAT, "weight", 0, 0, 1, .05 },
  { "hyper.climb_range", "hyper", RACE_WEAPON_TUNING_GROUP_HYPER,
    RACE_WEAPON_TUNING_TYPE_FLOAT, "u", 32, 0, 128, 1 },
  { "standard_rocket.speed", "standard_rocket",
    RACE_WEAPON_TUNING_GROUP_STANDARD_ROCKET,
    RACE_WEAPON_TUNING_TYPE_INT32, "UPS", 1000, 100, 2400, 25 },
  { "standard_rocket.knockback", "standard_rocket",
    RACE_WEAPON_TUNING_GROUP_STANDARD_ROCKET,
    RACE_WEAPON_TUNING_TYPE_INT32, "scalar", 75, 0, 300, 1 },
  { "standard_rocket.refire_ms", "standard_rocket",
    RACE_WEAPON_TUNING_GROUP_STANDARD_ROCKET,
    RACE_WEAPON_TUNING_TYPE_UINT32, "ms", 1000, 100, 3000, 25 },
  { "standard_grenade.speed", "standard_grenade",
    RACE_WEAPON_TUNING_GROUP_STANDARD_GRENADE,
    RACE_WEAPON_TUNING_TYPE_INT32, "UPS", 800, 100, 2000, 25 },
  { "standard_grenade.knockback", "standard_grenade",
    RACE_WEAPON_TUNING_GROUP_STANDARD_GRENADE,
    RACE_WEAPON_TUNING_TYPE_INT32, "scalar", 120, 0, 300, 1 },
  { "standard_grenade.fuse_ms", "standard_grenade",
    RACE_WEAPON_TUNING_GROUP_STANDARD_GRENADE,
    RACE_WEAPON_TUNING_TYPE_UINT32, "ms", 2500, 100, 10000, 100 },
  { "standard_grenade.refire_ms", "standard_grenade",
    RACE_WEAPON_TUNING_GROUP_STANDARD_GRENADE,
    RACE_WEAPON_TUNING_TYPE_UINT32, "ms", 1000, 100, 3000, 25 },
  { "hand_grenade.knockback", "hand_grenade",
    RACE_WEAPON_TUNING_GROUP_HAND_GRENADE,
    RACE_WEAPON_TUNING_TYPE_INT32, "scalar", 120, 0, 300, 1 },
  { "hand_grenade.refire_ms", "hand_grenade",
    RACE_WEAPON_TUNING_GROUP_HAND_GRENADE,
    RACE_WEAPON_TUNING_TYPE_UINT32, "ms", 2000, 100, 3000, 25 },
  { "global.self_knockback", "global", RACE_WEAPON_TUNING_GROUP_GLOBAL,
    RACE_WEAPON_TUNING_TYPE_FLOAT, "x", 1, 0, 4, .05 },
  { "grenade.inherit_fraction", "global", RACE_WEAPON_TUNING_GROUP_GLOBAL,
    RACE_WEAPON_TUNING_TYPE_FLOAT, "fraction", .33, 0, 1, .05 }
};

static double WeaponTuning_Scalar(
    const race_weapon_tuning_type_t type,
    const race_weapon_tuning_scalar_t scalar) {
  switch (type) {
    case RACE_WEAPON_TUNING_TYPE_INT32:
      return scalar.integer;
    case RACE_WEAPON_TUNING_TYPE_UINT32:
      return scalar.unsigned_integer;
    case RACE_WEAPON_TUNING_TYPE_FLOAT:
      return scalar.real;
    default:
      return NAN;
  }
}

static bool WeaponTuning_Catalog(void) {
  char error[RACE_WEAPON_TUNING_ERROR_SIZE];
  REQUIRE(Race_WeaponTuning_CatalogValid(error, sizeof(error)));
  REQUIRE(!*error);
  size_t count = 0u;
  const race_weapon_tuning_descriptor_t *catalog =
    Race_WeaponTuning_Catalog(&count);
  REQUIRE(catalog != NULL);
  REQUIRE(count == 22u);
  REQUIRE(RACE_WEAPON_TUNING_CATALOG_VERSION == 3u);
  REQUIRE(RACE_WEAPON_TUNING_GROUP_TOTAL == 5u);
  REQUIRE(sizeof(weapon_tuning_expected) /
          sizeof(weapon_tuning_expected[0]) == count);
  for (size_t i = 0u; i < count; i++) {
    const weapon_tuning_expected_t *expected = weapon_tuning_expected + i;
    REQUIRE(catalog[i].id == (race_weapon_tuning_id_t) i);
    REQUIRE(!strcmp(catalog[i].key, expected->key));
    REQUIRE(!strcmp(catalog[i].group_key, expected->group_key));
    REQUIRE(catalog[i].group == expected->group);
    REQUIRE(catalog[i].type == expected->type);
    REQUIRE(!strcmp(catalog[i].unit, expected->unit));
    REQUIRE(fabs(WeaponTuning_Scalar(catalog[i].type,
                                    catalog[i].compiled_default) -
                 expected->compiled_default) < .00001);
    REQUIRE(fabs(WeaponTuning_Scalar(catalog[i].type, catalog[i].minimum) -
                 expected->minimum) < .00001);
    REQUIRE(fabs(WeaponTuning_Scalar(catalog[i].type, catalog[i].maximum) -
                 expected->maximum) < .00001);
    REQUIRE(fabs(WeaponTuning_Scalar(catalog[i].type, catalog[i].step) -
                 expected->step) < .00001);
  }
  REQUIRE(!strcmp(catalog[RACE_WEAPON_TUNING_HYPER_SPEED].group_label,
                  "Hyperblaster"));
  REQUIRE(!strcmp(catalog[
    RACE_WEAPON_TUNING_STANDARD_ROCKET_SPEED].group_label,
    "Quake II Rocket Launcher"));
  REQUIRE(!strcmp(catalog[
    RACE_WEAPON_TUNING_STANDARD_GRENADE_SPEED].group_label,
    "Quake II Grenade Launcher"));
  REQUIRE(!strcmp(catalog[
    RACE_WEAPON_TUNING_HAND_GRENADE_KNOCKBACK].group_label,
    "Quake II Hand Grenade"));
  REQUIRE(!strcmp(catalog[
    RACE_WEAPON_TUNING_GLOBAL_SELF_KNOCKBACK].group_label, "Global"));
  REQUIRE(Race_WeaponTuning_DescriptorForKey("quake_rocket.speed") == NULL);
  REQUIRE(Race_WeaponTuning_DescriptorForKey("quake_grenade.speed") == NULL);
  REQUIRE(Race_WeaponTuning_CatalogHash() != 0u);

  race_weapon_tuning_snapshot_t defaults;
  Race_WeaponTuning_DefaultSnapshot(&defaults);
  REQUIRE(Race_WeaponTuning_SnapshotValid(&defaults, error, sizeof(error)));
  REQUIRE(defaults.values[RACE_WEAPON_TUNING_HYPER_SPEED].integer == 1800);
  REQUIRE(defaults.values[
    RACE_WEAPON_TUNING_STANDARD_ROCKET_REFIRE_MS].unsigned_integer == 1000u);
  REQUIRE(fabsf(defaults.values[
    RACE_WEAPON_TUNING_GRENADE_INHERIT_FRACTION].real - .33f) < .00001f);
  REQUIRE(Race_WeaponTuning_SnapshotHash(&defaults) != 0u);
  const uint64_t default_hash = Race_WeaponTuning_SnapshotHash(&defaults);
  race_weapon_tuning_snapshot_t changed = defaults;
  changed.values[RACE_WEAPON_TUNING_HYPER_SPEED].integer = 1825;
  REQUIRE(Race_WeaponTuning_SnapshotValid(&changed, error, sizeof(error)));
  REQUIRE(Race_WeaponTuning_SnapshotHash(&changed) != default_hash);
  changed = defaults;
  changed.values[RACE_WEAPON_TUNING_GLOBAL_SELF_KNOCKBACK].real = NAN;
  REQUIRE(!Race_WeaponTuning_SnapshotValid(&changed, error, sizeof(error)));
  REQUIRE(Race_WeaponTuning_SnapshotHash(&changed) == 0u);
  return true;
}

static bool WeaponTuning_StrictParser(void) {
  const race_weapon_tuning_descriptor_t *inherit =
    Race_WeaponTuning_Descriptor(
      RACE_WEAPON_TUNING_GRENADE_INHERIT_FRACTION);
  const race_weapon_tuning_descriptor_t *speed =
    Race_WeaponTuning_Descriptor(RACE_WEAPON_TUNING_STANDARD_ROCKET_SPEED);
  race_weapon_tuning_scalar_t value;
  char error[RACE_WEAPON_TUNING_ERROR_SIZE];

  // Float steps intentionally accept both the range-minimum lattice and the
  // compiled-default lattice. This preserves the exact legacy .33 baseline
  // while making its neighboring .05 UI increments usable.
  REQUIRE(Race_WeaponTuning_ParseValue(inherit, ".28", &value,
                                        error, sizeof(error)) == false);
  REQUIRE(Race_WeaponTuning_ParseValue(inherit, "0.28", &value,
                                        error, sizeof(error)));
  REQUIRE(fabsf(value.real - .28f) < .00001f);
  REQUIRE(Race_WeaponTuning_ParseValue(inherit, "0.33", &value,
                                        error, sizeof(error)));
  REQUIRE(Race_WeaponTuning_ParseValue(inherit, "0.38", &value,
                                        error, sizeof(error)));
  REQUIRE(!Race_WeaponTuning_ParseValue(inherit, "0.34", &value,
                                         error, sizeof(error)));
  REQUIRE(Race_WeaponTuning_ParseValue(inherit, "0", &value,
                                        error, sizeof(error)));
  REQUIRE(Race_WeaponTuning_ParseValue(inherit, "0.05", &value,
                                        error, sizeof(error)));
  REQUIRE(!Race_WeaponTuning_ParseValue(inherit, "nan", &value,
                                         error, sizeof(error)));
  REQUIRE(!Race_WeaponTuning_ParseValue(inherit, "inf", &value,
                                         error, sizeof(error)));
  REQUIRE(!Race_WeaponTuning_ParseValue(inherit, "1e-1", &value,
                                         error, sizeof(error)));
  REQUIRE(!Race_WeaponTuning_ParseValue(inherit, " 0.33", &value,
                                         error, sizeof(error)));
  REQUIRE(!Race_WeaponTuning_ParseValue(inherit, "+0.33", &value,
                                         error, sizeof(error)));
  REQUIRE(Race_WeaponTuning_ParseValue(speed, "100", &value,
                                        error, sizeof(error)));
  REQUIRE(Race_WeaponTuning_ParseValue(speed, "2400", &value,
                                        error, sizeof(error)));
  REQUIRE(!Race_WeaponTuning_ParseValue(speed, "101", &value,
                                         error, sizeof(error)));
  REQUIRE(!Race_WeaponTuning_ParseValue(speed, "0100", &value,
                                         error, sizeof(error)));

  race_weapon_tuning_snapshot_t candidate;
  Race_WeaponTuning_DefaultSnapshot(&candidate);
  candidate.values[RACE_WEAPON_TUNING_HYPER_CLIMB_IMPULSE_Z].real = 200.f;
  candidate.values[RACE_WEAPON_TUNING_HYPER_REFIRE_MS].unsigned_integer = 25u;
  REQUIRE(!Race_WeaponTuning_SnapshotValid(&candidate,
                                           error, sizeof(error)));
  candidate.values[RACE_WEAPON_TUNING_HYPER_REFIRE_MS].unsigned_integer = 200u;
  REQUIRE(Race_WeaponTuning_SnapshotValid(&candidate,
                                          error, sizeof(error)));
  return true;
}

static bool WeaponTuning_Status(void) {
  race_weapon_tuning_status_t status = {
    .state = RACE_WEAPON_TUNING_STATE_ACTIVE,
    .generation = 19u,
    .hash = UINT64_C(0xe72bb6c10dd482af),
    .preset_key = RACE_PHYSICS_PRESET_Q2_V1_KEY,
    .identity = "custom",
    .hyper_climb_range = 48.f
  };
  char wire[RACE_WEAPON_TUNING_STATUS_SIZE];
  REQUIRE(Race_WeaponTuning_StatusEncode(&status, wire, sizeof(wire)));
  REQUIRE(!strcmp(wire,
    "v2\\active\\19\\e72bb6c10dd482af\\q2-v1\\custom\\48"));
  race_weapon_tuning_status_t decoded;
  REQUIRE(Race_WeaponTuning_StatusDecode(wire, &decoded));
  REQUIRE(decoded.state == status.state);
  REQUIRE(decoded.generation == status.generation);
  REQUIRE(decoded.hash == status.hash);
  REQUIRE(!strcmp(decoded.preset_key, status.preset_key));
  REQUIRE(!strcmp(decoded.identity, status.identity));
  REQUIRE(decoded.hyper_climb_range == status.hyper_climb_range);
  REQUIRE(!Race_WeaponTuning_StatusDecode(
    "v2\\active\\019\\e72bb6c10dd482af\\q2-v1\\custom\\48", &decoded));
  REQUIRE(!Race_WeaponTuning_StatusDecode(
    "v1\\active\\19\\e72bb6c10dd482af\\q2-v1\\custom\\48", &decoded));

  status = (race_weapon_tuning_status_t) {
    .state = RACE_WEAPON_TUNING_STATE_INACTIVE,
    .generation = 0u,
    .hash = UINT64_C(0xe72bb6c10dd482af),
    .preset_key = RACE_PHYSICS_PRESET_Q2_V1_KEY,
    .identity = "baseline",
    .hyper_climb_range = 32.f
  };
  REQUIRE(Race_WeaponTuning_StatusEncode(&status, wire, sizeof(wire)));
  REQUIRE(!strcmp(wire,
    "v2\\inactive\\0\\e72bb6c10dd482af\\q2-v1\\baseline\\32"));
  REQUIRE(Race_WeaponTuning_StatusDecode(wire, &decoded));
  return true;
}

static bool WeaponTuning_Wire(void) {
  uint8_t payload[RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD];
  race_weapon_tuning_snapshot_t defaults;
  Race_WeaponTuning_DefaultSnapshot(&defaults);
  const uint64_t hash = Race_WeaponTuning_SnapshotHash(&defaults);

  race_weapon_tuning_sync_begin_t begin = {
    .request_id = 41u,
    .generation = 19u,
    .session_generation = 0u,
    .catalog_hash = Race_WeaponTuning_CatalogHash(),
    .baseline_hash = hash,
    .current_hash = hash,
    .flags = RACE_WEAPON_TUNING_SYNC_HAS_BASELINE |
             RACE_WEAPON_TUNING_SYNC_HAS_CURRENT |
             RACE_WEAPON_TUNING_SYNC_CAN_MUTATE,
    .state = RACE_WEAPON_TUNING_STATE_INACTIVE,
    .preset_key = RACE_PHYSICS_PRESET_Q2_V1_KEY,
    .identity = "baseline"
  };
  size_t length = Race_WeaponTuningWire_EncodeSyncBegin(
    &begin, payload, sizeof(payload));
  REQUIRE(length > 77u);
  race_weapon_tuning_sync_begin_t decoded_begin;
  REQUIRE(!Race_WeaponTuningWire_DecodeSyncBegin(
    NULL, length, &decoded_begin));
  REQUIRE(Race_WeaponTuningWire_DecodeSyncBegin(
    payload, length, &decoded_begin));
  REQUIRE(decoded_begin.request_id == begin.request_id);
  REQUIRE(decoded_begin.session_generation == 0u);
  REQUIRE(decoded_begin.current_hash == begin.current_hash);
  REQUIRE(decoded_begin.previous_hash == 0u);
  REQUIRE(decoded_begin.slot_a_hash == 0u);
  REQUIRE(decoded_begin.slot_b_hash == 0u);
  REQUIRE(!strcmp(decoded_begin.preset_key, begin.preset_key));
  REQUIRE(!strcmp(decoded_begin.identity, begin.identity));

  race_weapon_tuning_catalog_entry_t entry = {
    .request_id = 41u,
    .generation = 19u,
    .index = (uint16_t) RACE_WEAPON_TUNING_GRENADE_INHERIT_FRACTION,
    .descriptor = *Race_WeaponTuning_Descriptor(
      RACE_WEAPON_TUNING_GRENADE_INHERIT_FRACTION)
  };
  length = Race_WeaponTuningWire_EncodeCatalogEntry(
    &entry, payload, sizeof(payload));
  REQUIRE(length > 42u);
  race_weapon_tuning_catalog_entry_t decoded_entry;
  REQUIRE(!Race_WeaponTuningWire_DecodeCatalogEntry(
    NULL, length, &decoded_entry));
  REQUIRE(Race_WeaponTuningWire_DecodeCatalogEntry(
    payload, length, &decoded_entry));
  REQUIRE(decoded_entry.index == entry.index);
  REQUIRE(!strcmp(decoded_entry.descriptor.unit, "fraction"));
  REQUIRE(decoded_entry.descriptor.compiled_default.real ==
          entry.descriptor.compiled_default.real);

  race_weapon_tuning_snapshot_message_t snapshot = {
    .request_id = 41u,
    .generation = 19u,
    .hash = hash,
    .kind = RACE_WEAPON_TUNING_SNAPSHOT_CURRENT,
    .snapshot = defaults
  };
  length = Race_WeaponTuningWire_EncodeSnapshot(
    &snapshot, payload, sizeof(payload));
  REQUIRE(length == 116u);
  race_weapon_tuning_snapshot_message_t decoded_snapshot;
  REQUIRE(!Race_WeaponTuningWire_DecodeSnapshot(
    NULL, length, &decoded_snapshot));
  REQUIRE(Race_WeaponTuningWire_DecodeSnapshot(
    payload, length, &decoded_snapshot));
  REQUIRE(decoded_snapshot.hash == hash);
  REQUIRE(Race_WeaponTuning_SnapshotEqual(
    &decoded_snapshot.snapshot, &defaults));

  race_weapon_tuning_sync_end_t end = {
    .request_id = 41u,
    .generation = 19u,
    .catalog_hash = Race_WeaponTuning_CatalogHash(),
    .descriptor_count = RACE_WEAPON_TUNING_VALUE_COUNT
  };
  length = Race_WeaponTuningWire_EncodeSyncEnd(
    &end, payload, sizeof(payload));
  REQUIRE(length == 28u);
  race_weapon_tuning_sync_end_t decoded_end;
  REQUIRE(!Race_WeaponTuningWire_DecodeSyncEnd(NULL, length, &decoded_end));
  REQUIRE(Race_WeaponTuningWire_DecodeSyncEnd(
    payload, length, &decoded_end));
  REQUIRE(decoded_end.descriptor_count == RACE_WEAPON_TUNING_VALUE_COUNT);

  race_weapon_tuning_result_message_t result = {
    .request_id = 41u,
    .generation = 19u,
    .hash = hash,
    .operation = RACE_WEAPON_TUNING_OPERATION_APPLY,
    .result = RACE_WEAPON_TUNING_RESULT_OK,
    .preset_key = RACE_PHYSICS_PRESET_Q2_V1_KEY,
    .text = "batch applied; runs reset"
  };
  length = Race_WeaponTuningWire_EncodeResult(
    &result, payload, sizeof(payload));
  REQUIRE(length > 28u);
  race_weapon_tuning_result_message_t decoded_result;
  REQUIRE(!Race_WeaponTuningWire_DecodeResult(
    NULL, length, &decoded_result));
  REQUIRE(Race_WeaponTuningWire_DecodeResult(
    payload, length, &decoded_result));
  REQUIRE(decoded_result.operation == result.operation);
  REQUIRE(decoded_result.result == result.result);
  REQUIRE(!strcmp(decoded_result.preset_key, result.preset_key));
  REQUIRE(!strcmp(decoded_result.text, result.text));

  payload[2] = 1u;
  REQUIRE(!Race_WeaponTuningWire_DecodeResult(
    payload, length, &decoded_result));
  return true;
}

static void CgameWeaponTuning_HashBytes(uint64_t *hash, const void *data,
                                        const size_t length) {
  const uint8_t *bytes = data;
  for (size_t i = 0u; i < length; i++) {
    *hash ^= bytes[i];
    *hash *= UINT64_C(1099511628211);
  }
}

static void CgameWeaponTuning_Hash32(uint64_t *hash, const uint32_t value) {
  const uint8_t bytes[4] = {
    (uint8_t) value, (uint8_t) (value >> 8u),
    (uint8_t) (value >> 16u), (uint8_t) (value >> 24u)
  };
  CgameWeaponTuning_HashBytes(hash, bytes, sizeof(bytes));
}

static uint64_t CgameWeaponTuning_RawSnapshotHash(
    const race_weapon_tuning_snapshot_t *snapshot) {
  uint64_t hash = UINT64_C(14695981039346656037);
  CgameWeaponTuning_Hash32(&hash, RACE_WEAPON_TUNING_CATALOG_VERSION);
  size_t count = 0u;
  const race_weapon_tuning_descriptor_t *catalog =
    Race_WeaponTuning_Catalog(&count);
  for (size_t i = 0u; i < count; i++) {
    uint32_t bits;
    if (catalog[i].type == RACE_WEAPON_TUNING_TYPE_INT32) {
      memcpy(&bits, &snapshot->values[i].integer, sizeof(bits));
    } else if (catalog[i].type == RACE_WEAPON_TUNING_TYPE_UINT32) {
      bits = snapshot->values[i].unsigned_integer;
    } else {
      memcpy(&bits, &snapshot->values[i].real, sizeof(bits));
    }
    CgameWeaponTuning_Hash32(&hash, bits);
  }
  return hash;
}

static bool CgameWeaponTuning_StartCatalog(
    const race_weapon_tuning_sync_begin_t *begin,
    const bool corrupt_catalog) {
  uint8_t payload[RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD];
  size_t length = Race_WeaponTuningWire_EncodeSyncBegin(
    begin, payload, sizeof(payload));
  REQUIRE(length > 0u);
  REQUIRE(Cg_RaceWeaponTuning_ParsePayload(payload, length));

  size_t count = 0u;
  const race_weapon_tuning_descriptor_t *catalog =
    Race_WeaponTuning_Catalog(&count);
  for (size_t index = 0u; index < count; index++) {
    race_weapon_tuning_catalog_entry_t entry = {
      .request_id = begin->request_id,
      .generation = begin->generation,
      .index = (uint16_t) index,
      .descriptor = catalog[index]
    };
    if (corrupt_catalog && index == 0u) {
      snprintf(entry.descriptor.label, sizeof(entry.descriptor.label),
               "Corrupt speed");
    }
    length = Race_WeaponTuningWire_EncodeCatalogEntry(
      &entry, payload, sizeof(payload));
    REQUIRE(length > 0u);
    REQUIRE(Cg_RaceWeaponTuning_ParsePayload(payload, length));
  }
  return true;
}

static bool CgameWeaponTuning_Status(
    const race_weapon_tuning_state_t state, const uint64_t generation,
    const uint64_t hash, const char *identity, const float climb_range) {
  race_weapon_tuning_status_t status = {
    .state = state,
    .generation = generation,
    .hash = hash,
    .preset_key = RACE_PHYSICS_PRESET_Q2_V1_KEY,
    .hyper_climb_range = climb_range
  };
  snprintf(status.identity, sizeof(status.identity), "%s", identity);
  char wire[RACE_WEAPON_TUNING_STATUS_SIZE];
  REQUIRE(Race_WeaponTuning_StatusEncode(&status, wire, sizeof(wire)));
  Cg_RaceWeaponTuning_UpdateStatus(wire);
  return true;
}

static bool CgameWeaponTuning_Stream(
    const uint32_t request_id, const uint64_t generation,
    const race_weapon_tuning_state_t state, const char *identity,
    const race_weapon_tuning_snapshot_t *baseline,
    const race_weapon_tuning_snapshot_t *current,
    const uint16_t capability_flags) {
  REQUIRE(baseline != NULL);
  REQUIRE(current != NULL);
  uint8_t payload[RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD];
  race_weapon_tuning_sync_begin_t begin = {
    .request_id = request_id,
    .generation = generation,
    .catalog_hash = Race_WeaponTuning_CatalogHash(),
    .baseline_hash = Race_WeaponTuning_SnapshotHash(baseline),
    .current_hash = Race_WeaponTuning_SnapshotHash(current),
    .flags = capability_flags | RACE_WEAPON_TUNING_SYNC_HAS_BASELINE |
      RACE_WEAPON_TUNING_SYNC_HAS_CURRENT,
    .state = state,
    .preset_key = RACE_PHYSICS_PRESET_Q2_V1_KEY
  };
  snprintf(begin.identity, sizeof(begin.identity), "%s", identity);

  size_t length = Race_WeaponTuningWire_EncodeSyncBegin(
    &begin, payload, sizeof(payload));
  REQUIRE(length > 0u);
  REQUIRE(Cg_RaceWeaponTuning_ParsePayload(payload, length));

  size_t count = 0u;
  const race_weapon_tuning_descriptor_t *catalog =
    Race_WeaponTuning_Catalog(&count);
  REQUIRE(count == RACE_WEAPON_TUNING_VALUE_COUNT);
  for (size_t index = 0u; index < count; index++) {
    race_weapon_tuning_catalog_entry_t entry = {
      .request_id = request_id,
      .generation = generation,
      .index = (uint16_t) index,
      .descriptor = catalog[index]
    };
    length = Race_WeaponTuningWire_EncodeCatalogEntry(
      &entry, payload, sizeof(payload));
    REQUIRE(length > 0u);
    REQUIRE(Cg_RaceWeaponTuning_ParsePayload(payload, length));
  }

  const race_weapon_tuning_snapshot_t *snapshots[] = { baseline, current };
  const race_weapon_tuning_snapshot_kind_t kinds[] = {
    RACE_WEAPON_TUNING_SNAPSHOT_BASELINE,
    RACE_WEAPON_TUNING_SNAPSHOT_CURRENT
  };
  for (size_t i = 0u; i < sizeof(snapshots) / sizeof(snapshots[0]); i++) {
    if (!snapshots[i]) {
      continue;
    }
    race_weapon_tuning_snapshot_message_t snapshot = {
      .request_id = request_id,
      .generation = generation,
      .hash = Race_WeaponTuning_SnapshotHash(snapshots[i]),
      .kind = kinds[i],
      .snapshot = *snapshots[i]
    };
    length = Race_WeaponTuningWire_EncodeSnapshot(
      &snapshot, payload, sizeof(payload));
    REQUIRE(length > 0u);
    REQUIRE(Cg_RaceWeaponTuning_ParsePayload(payload, length));
  }

  race_weapon_tuning_sync_end_t end = {
    .request_id = request_id,
    .generation = generation,
    .catalog_hash = Race_WeaponTuning_CatalogHash(),
    .descriptor_count = RACE_WEAPON_TUNING_VALUE_COUNT
  };
  length = Race_WeaponTuningWire_EncodeSyncEnd(
    &end, payload, sizeof(payload));
  REQUIRE(length > 0u);
  REQUIRE(Cg_RaceWeaponTuning_ParsePayload(payload, length));
  return true;
}

static bool CgameWeaponTuning_Result(
    const uint32_t request_id, const uint64_t generation,
    const uint64_t hash, const race_weapon_tuning_operation_t operation,
    const race_weapon_tuning_result_t result, const char *text) {
  race_weapon_tuning_result_message_t message = {
    .request_id = request_id,
    .generation = generation,
    .hash = hash,
    .operation = operation,
    .result = result,
    .preset_key = RACE_PHYSICS_PRESET_Q2_V1_KEY
  };
  snprintf(message.text, sizeof(message.text), "%s", text);
  uint8_t payload[RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD];
  const size_t length = Race_WeaponTuningWire_EncodeResult(
    &message, payload, sizeof(payload));
  return length > 0u && Cg_RaceWeaponTuning_ParsePayload(payload, length);
}

static bool CgameWeaponTuning_Cache(void) {
  memset(&cgi, 0, sizeof(cgi));
  cgi.Cbuf = CgameCaptureCommand;
  Cg_RaceWeaponTuning_Clear();
  REQUIRE(!Cg_RaceWeaponTuning_Cache()->complete);
  REQUIRE(Cg_RaceWeaponTuning_Warning() != NULL);

  race_weapon_tuning_snapshot_t baseline, custom;
  Race_WeaponTuning_DefaultSnapshot(&baseline);
  // The authoritative reset source is captured runtime state, not descriptors.
  baseline.values[RACE_WEAPON_TUNING_HYPER_SPEED].integer = 1775;
  custom = baseline;
  custom.values[RACE_WEAPON_TUNING_HYPER_SPEED].integer = 1825;
  const uint64_t baseline_hash = Race_WeaponTuning_SnapshotHash(&baseline);
  const uint64_t custom_hash = Race_WeaponTuning_SnapshotHash(&custom);

  REQUIRE(CgameWeaponTuning_Status(
    RACE_WEAPON_TUNING_STATE_INACTIVE, 0u, baseline_hash, "baseline", 32.f));
  const uint32_t sync = Cg_RaceWeaponTuning_RequestSync();
  REQUIRE(sync != 0u);
  char expected[128];
  snprintf(expected, sizeof(expected), "race tune sync req=%u\n", sync);
  REQUIRE(!strcmp(cgame_command, expected));
  REQUIRE(CgameWeaponTuning_Stream(
    sync, 0u, RACE_WEAPON_TUNING_STATE_INACTIVE, "baseline",
    &baseline, &baseline, RACE_WEAPON_TUNING_SYNC_CAN_MUTATE));

  const cg_race_weapon_tuning_cache_t *cache =
    Cg_RaceWeaponTuning_Cache();
  REQUIRE(cache->complete && cache->synchronized);
  REQUIRE(cache->baseline_valid && cache->current_valid);
  REQUIRE(Race_WeaponTuning_SnapshotEqual(&cache->baseline, &cache->current));
  REQUIRE(Cg_RaceWeaponTuning_Warning() == NULL);
  REQUIRE(!strcmp(cache->descriptors[
    RACE_WEAPON_TUNING_STANDARD_GRENADE_SPEED].key,
    "standard_grenade.speed"));
  REQUIRE(!strcmp(cache->descriptors[
    RACE_WEAPON_TUNING_HAND_GRENADE_KNOCKBACK].key,
    "hand_grenade.knockback"));

  const race_weapon_tuning_descriptor_t *inherit =
    cache->descriptors + RACE_WEAPON_TUNING_GRENADE_INHERIT_FRACTION;
  race_weapon_tuning_scalar_t scalar;
  REQUIRE(Cg_RaceWeaponTuning_CanonicalValue(inherit, .28, &scalar));
  REQUIRE(Cg_RaceWeaponTuning_CanonicalValue(inherit, .33, &scalar));
  REQUIRE(Cg_RaceWeaponTuning_CanonicalValue(inherit, .38, &scalar));
  REQUIRE(!Cg_RaceWeaponTuning_CanonicalValue(inherit, .34, &scalar));
  REQUIRE(!Cg_RaceWeaponTuning_CanonicalValue(inherit, NAN, &scalar));

  const uint32_t revision = cache->revision;
  uint8_t payload[RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD];

  race_weapon_tuning_sync_begin_t corrupt_catalog = {
    .generation = 1u,
    .catalog_hash = Race_WeaponTuning_CatalogHash(),
    .baseline_hash = baseline_hash,
    .current_hash = custom_hash,
    .flags = RACE_WEAPON_TUNING_SYNC_HAS_BASELINE |
             RACE_WEAPON_TUNING_SYNC_HAS_CURRENT,
    .state = RACE_WEAPON_TUNING_STATE_ACTIVE,
    .preset_key = RACE_PHYSICS_PRESET_Q2_V1_KEY,
    .identity = "custom"
  };
  REQUIRE(CgameWeaponTuning_StartCatalog(&corrupt_catalog, true));
  race_weapon_tuning_sync_end_t corrupt_catalog_end = {
    .generation = corrupt_catalog.generation,
    .catalog_hash = corrupt_catalog.catalog_hash,
    .descriptor_count = RACE_WEAPON_TUNING_VALUE_COUNT
  };
  size_t length = Race_WeaponTuningWire_EncodeSyncEnd(
    &corrupt_catalog_end, payload, sizeof(payload));
  REQUIRE(length > 0u);
  REQUIRE(!Cg_RaceWeaponTuning_ParsePayload(payload, length));
  REQUIRE(Cg_RaceWeaponTuning_Cache()->revision == revision);

  race_weapon_tuning_sync_begin_t corrupt_snapshot = {
    .generation = 1u,
    .catalog_hash = Race_WeaponTuning_CatalogHash(),
    .baseline_hash = baseline_hash,
    .current_hash = custom_hash,
    .flags = RACE_WEAPON_TUNING_SYNC_HAS_BASELINE |
             RACE_WEAPON_TUNING_SYNC_HAS_CURRENT |
             RACE_WEAPON_TUNING_SYNC_CAN_MUTATE,
    .state = RACE_WEAPON_TUNING_STATE_ACTIVE,
    .preset_key = RACE_PHYSICS_PRESET_Q2_V1_KEY,
    .identity = "custom"
  };
  REQUIRE(CgameWeaponTuning_StartCatalog(&corrupt_snapshot, false));
  race_weapon_tuning_snapshot_message_t mislabeled_snapshot = {
    .generation = corrupt_snapshot.generation,
    .hash = custom_hash,
    .kind = RACE_WEAPON_TUNING_SNAPSHOT_CURRENT,
    .snapshot = baseline
  };
  length = Race_WeaponTuningWire_EncodeSnapshot(
    &mislabeled_snapshot, payload, sizeof(payload));
  REQUIRE(length > 0u);
  REQUIRE(!Cg_RaceWeaponTuning_ParsePayload(payload, length));
  REQUIRE(Cg_RaceWeaponTuning_Cache()->revision == revision);

  race_weapon_tuning_snapshot_t unsafe = custom;
  unsafe.values[RACE_WEAPON_TUNING_HYPER_CLIMB_IMPULSE_Z].real = 200.f;
  unsafe.values[RACE_WEAPON_TUNING_HYPER_REFIRE_MS].unsigned_integer = 25u;
  const uint64_t unsafe_hash = CgameWeaponTuning_RawSnapshotHash(&unsafe);
  race_weapon_tuning_sync_begin_t unsafe_snapshot = corrupt_snapshot;
  unsafe_snapshot.current_hash = unsafe_hash;
  REQUIRE(CgameWeaponTuning_StartCatalog(&unsafe_snapshot, false));
  race_weapon_tuning_snapshot_message_t unsafe_message = {
    .generation = unsafe_snapshot.generation,
    .hash = unsafe_hash,
    .kind = RACE_WEAPON_TUNING_SNAPSHOT_CURRENT,
    .snapshot = unsafe
  };
  length = Race_WeaponTuningWire_EncodeSnapshot(
    &unsafe_message, payload, sizeof(payload));
  REQUIRE(length > 0u);
  REQUIRE(!Cg_RaceWeaponTuning_ParsePayload(payload, length));
  REQUIRE(Cg_RaceWeaponTuning_Cache()->revision == revision);

  race_weapon_tuning_sync_begin_t incomplete = {
    .generation = 1u,
    .catalog_hash = Race_WeaponTuning_CatalogHash(),
    .baseline_hash = baseline_hash,
    .current_hash = custom_hash,
    .flags = RACE_WEAPON_TUNING_SYNC_HAS_BASELINE |
             RACE_WEAPON_TUNING_SYNC_HAS_CURRENT |
             RACE_WEAPON_TUNING_SYNC_CAN_MUTATE,
    .state = RACE_WEAPON_TUNING_STATE_ACTIVE,
    .preset_key = RACE_PHYSICS_PRESET_Q2_V1_KEY,
    .identity = "custom"
  };
  length = Race_WeaponTuningWire_EncodeSyncBegin(
    &incomplete, payload, sizeof(payload));
  REQUIRE(Cg_RaceWeaponTuning_ParsePayload(payload, length));
  race_weapon_tuning_sync_end_t premature = {
    .generation = incomplete.generation,
    .catalog_hash = incomplete.catalog_hash,
    .descriptor_count = RACE_WEAPON_TUNING_VALUE_COUNT
  };
  length = Race_WeaponTuningWire_EncodeSyncEnd(
    &premature, payload, sizeof(payload));
  REQUIRE(!Cg_RaceWeaponTuning_ParsePayload(payload, length));
  REQUIRE(Cg_RaceWeaponTuning_Cache()->revision == revision);
  REQUIRE(Cg_RaceWeaponTuning_Cache()->metadata.generation == 0u);

  const uint32_t apply = Cg_RaceWeaponTuning_RequestApply(
    0u, "hyper.speed=1825");
  REQUIRE(apply != 0u);
  snprintf(expected, sizeof(expected),
           "race tune apply req=%u 0 hyper.speed=1825\n", apply);
  REQUIRE(!strcmp(cgame_command, expected));
  REQUIRE(!CgameWeaponTuning_Result(
    apply + 1u, 1u, custom_hash, RACE_WEAPON_TUNING_OPERATION_APPLY,
    RACE_WEAPON_TUNING_RESULT_OK, "wrong request"));
  REQUIRE(Cg_RaceWeaponTuning_RequestState(
    RACE_WEAPON_TUNING_OPERATION_APPLY).pending);
  REQUIRE(CgameWeaponTuning_Result(
    apply, 1u, custom_hash, RACE_WEAPON_TUNING_OPERATION_APPLY,
    RACE_WEAPON_TUNING_RESULT_OK, "batch applied; runs reset"));
  cg_race_weapon_tuning_request_t apply_state =
    Cg_RaceWeaponTuning_RequestState(RACE_WEAPON_TUNING_OPERATION_APPLY);
  REQUIRE(apply_state.pending && apply_state.awaiting_authoritative);
  REQUIRE(Cg_RaceWeaponTuning_Cache()->metadata.generation == 0u);
  REQUIRE(Cg_RaceWeaponTuning_Cache()->current.values[
    RACE_WEAPON_TUNING_HYPER_SPEED].integer == 1775);

  REQUIRE(CgameWeaponTuning_Status(
    RACE_WEAPON_TUNING_STATE_ACTIVE, 1u, custom_hash, "custom", 32.f));
  REQUIRE(CgameWeaponTuning_Stream(
    0u, 1u, RACE_WEAPON_TUNING_STATE_ACTIVE, "custom",
    &baseline, &custom, RACE_WEAPON_TUNING_SYNC_CAN_MUTATE));
  cache = Cg_RaceWeaponTuning_Cache();
  REQUIRE(cache->synchronized && cache->metadata.generation == 1u);
  REQUIRE(cache->current.values[
    RACE_WEAPON_TUNING_HYPER_SPEED].integer == 1825);
  REQUIRE(!Cg_RaceWeaponTuning_RequestState(
    RACE_WEAPON_TUNING_OPERATION_APPLY).pending);
  REQUIRE(Cg_RaceWeaponTuning_Warning() != NULL);

  REQUIRE(Cg_RaceWeaponTuning_RequestApply(1u, "bad\nvalue=1") == 0u);
  REQUIRE(Cg_RaceWeaponTuning_RequestApply(1u, "bad;value=1") == 0u);
  char oversized[800];
  memset(oversized, 'a', sizeof(oversized) - 1u);
  oversized[sizeof(oversized) - 1u] = '\0';
  REQUIRE(Cg_RaceWeaponTuning_RequestApply(1u, oversized) == 0u);

  const uint32_t reset = Cg_RaceWeaponTuning_RequestResetAll();
  REQUIRE(reset != 0u);
  snprintf(expected, sizeof(expected),
           "race tune reset req=%u 1 all\n", reset);
  REQUIRE(!strcmp(cgame_command, expected));
  REQUIRE(CgameWeaponTuning_Result(
    reset, 2u, baseline_hash, RACE_WEAPON_TUNING_OPERATION_RESET_ALL,
    RACE_WEAPON_TUNING_RESULT_OK, "baseline values restored"));
  REQUIRE(Cg_RaceWeaponTuning_RequestState(
    RACE_WEAPON_TUNING_OPERATION_RESET_ALL).awaiting_authoritative);
  REQUIRE(CgameWeaponTuning_Status(
    RACE_WEAPON_TUNING_STATE_INACTIVE, 2u, baseline_hash, "baseline", 32.f));
  REQUIRE(CgameWeaponTuning_Stream(
    0u, 2u, RACE_WEAPON_TUNING_STATE_INACTIVE, "baseline",
    &baseline, &baseline, RACE_WEAPON_TUNING_SYNC_CAN_MUTATE));
  REQUIRE(!Cg_RaceWeaponTuning_RequestState(
    RACE_WEAPON_TUNING_OPERATION_RESET_ALL).pending);
  REQUIRE(Cg_RaceWeaponTuning_Cache()->metadata.generation == 2u);
  REQUIRE(Race_WeaponTuning_SnapshotEqual(
    &Cg_RaceWeaponTuning_Cache()->baseline,
    &Cg_RaceWeaponTuning_Cache()->current));
  REQUIRE(Cg_RaceWeaponTuning_Warning() == NULL);

  // Capability is authoritative even with a complete synchronized cache.
  REQUIRE(CgameWeaponTuning_Stream(
    0u, 2u, RACE_WEAPON_TUNING_STATE_INACTIVE, "baseline",
    &baseline, &baseline, 0u));
  REQUIRE(Cg_RaceWeaponTuning_RequestApply(
    2u, "hyper.speed=1825") == 0u);
  REQUIRE(Cg_RaceWeaponTuning_RequestResetAll() == 0u);

  Cg_RaceWeaponTuning_Clear();
  REQUIRE(!Cg_RaceWeaponTuning_Cache()->complete);
  REQUIRE(!Cg_RaceWeaponTuning_MutationPending());
  return true;
}

#undef main
int main(void) {
  size_t service_assertions = 0u;
  if (!WeaponTuning_Catalog() || !WeaponTuning_StrictParser() ||
      !WeaponTuning_Status() || !WeaponTuning_Wire() ||
      !CgameWeaponTuning_Cache() ||
      !WeaponTuningService_Lifecycle(&service_assertions)) {
    return EXIT_FAILURE;
  }
  assertions += service_assertions;
  printf("RACE_WEAPON_TUNING_TEST PASS assertions=%zu values=%u\n",
         assertions, RACE_WEAPON_TUNING_VALUE_COUNT);
  return EXIT_SUCCESS;
}
