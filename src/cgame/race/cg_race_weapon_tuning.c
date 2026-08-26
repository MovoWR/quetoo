/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"
#include "cg_race_weapon_tuning.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CG_RACE_TUNING_STAGE_TIMEOUT 3000u
#define CG_RACE_TUNING_REQUEST_TIMEOUT 8000u
#define CG_RACE_TUNING_RESYNC_INTERVAL 1500u
#define CG_RACE_TUNING_MAX_COMMAND_CHARS 768u
#define CG_RACE_TUNING_ALL_DESCRIPTORS \
  ((1u << RACE_WEAPON_TUNING_VALUE_COUNT) - 1u)

typedef struct {
  bool active;
  uint32_t started;
  uint32_t descriptor_mask;
  uint16_t next_descriptor;
  race_weapon_tuning_sync_begin_t begin;
  race_weapon_tuning_descriptor_t
    descriptors[RACE_WEAPON_TUNING_VALUE_COUNT];
  bool snapshots[RACE_WEAPON_TUNING_SNAPSHOT_TOTAL];
  race_weapon_tuning_snapshot_message_t
    snapshot[RACE_WEAPON_TUNING_SNAPSHOT_TOTAL];
} cg_race_weapon_tuning_stage_t;

typedef struct {
  bool pending;
  bool awaiting_authoritative;
  uint32_t request_id;
  uint32_t sent;
  uint64_t expected_generation;
  uint64_t accepted_generation;
  uint64_t accepted_hash;
} cg_race_weapon_tuning_pending_t;

static struct {
  cg_race_weapon_tuning_cache_t cache;
  cg_race_weapon_tuning_stage_t stage;
  cg_race_weapon_tuning_pending_t
    pending[RACE_WEAPON_TUNING_OPERATION_TOTAL];
  uint32_t next_request_id;
  uint32_t last_resync;
  bool resync_required;
} cg_race_weapon_tuning;

static uint32_t Cg_RaceWeaponTuning_Now(void) {
  return cgi.client ? (uint32_t) cgi.client->unclamped_time : 0u;
}

static uint32_t Cg_RaceWeaponTuning_ScalarBits(
    const race_weapon_tuning_type_t type,
    const race_weapon_tuning_scalar_t scalar) {
  uint32_t bits;
  if (type == RACE_WEAPON_TUNING_TYPE_INT32) {
    memcpy(&bits, &scalar.integer, sizeof(bits));
  } else if (type == RACE_WEAPON_TUNING_TYPE_UINT32) {
    bits = scalar.unsigned_integer;
  } else {
    memcpy(&bits, &scalar.real, sizeof(bits));
  }
  return bits;
}

static void Cg_RaceWeaponTuning_HashBytes(uint64_t *hash, const void *data,
                                           const size_t length) {
  const uint8_t *bytes = data;
  for (size_t i = 0u; i < length; i++) {
    *hash ^= bytes[i];
    *hash *= UINT64_C(1099511628211);
  }
}

static void Cg_RaceWeaponTuning_Hash32(uint64_t *hash,
                                       const uint32_t value) {
  const uint8_t bytes[4] = {
    (uint8_t) value, (uint8_t) (value >> 8u),
    (uint8_t) (value >> 16u), (uint8_t) (value >> 24u)
  };
  Cg_RaceWeaponTuning_HashBytes(hash, bytes, sizeof(bytes));
}

static uint64_t Cg_RaceWeaponTuning_CatalogHash(
    const cg_race_weapon_tuning_stage_t *stage) {
  uint64_t hash = UINT64_C(14695981039346656037);
  Cg_RaceWeaponTuning_Hash32(&hash, RACE_WEAPON_TUNING_CATALOG_VERSION);
  Cg_RaceWeaponTuning_Hash32(&hash, RACE_WEAPON_TUNING_VALUE_COUNT);
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
    const race_weapon_tuning_descriptor_t *descriptor =
      stage->descriptors + i;
    Cg_RaceWeaponTuning_Hash32(&hash, (uint32_t) descriptor->id);
    Cg_RaceWeaponTuning_Hash32(&hash, (uint32_t) descriptor->group);
    Cg_RaceWeaponTuning_Hash32(&hash, (uint32_t) descriptor->type);
    Cg_RaceWeaponTuning_HashBytes(
      &hash, descriptor->key, strlen(descriptor->key) + 1u);
    Cg_RaceWeaponTuning_HashBytes(
      &hash, descriptor->group_key, strlen(descriptor->group_key) + 1u);
    Cg_RaceWeaponTuning_HashBytes(
      &hash, descriptor->group_label,
      strlen(descriptor->group_label) + 1u);
    Cg_RaceWeaponTuning_HashBytes(
      &hash, descriptor->label, strlen(descriptor->label) + 1u);
    Cg_RaceWeaponTuning_HashBytes(
      &hash, descriptor->unit, strlen(descriptor->unit) + 1u);
    Cg_RaceWeaponTuning_Hash32(&hash, Cg_RaceWeaponTuning_ScalarBits(
      descriptor->type, descriptor->compiled_default));
    Cg_RaceWeaponTuning_Hash32(&hash, Cg_RaceWeaponTuning_ScalarBits(
      descriptor->type, descriptor->minimum));
    Cg_RaceWeaponTuning_Hash32(&hash, Cg_RaceWeaponTuning_ScalarBits(
      descriptor->type, descriptor->maximum));
    Cg_RaceWeaponTuning_Hash32(&hash, Cg_RaceWeaponTuning_ScalarBits(
      descriptor->type, descriptor->step));
  }
  return hash;
}

static uint64_t Cg_RaceWeaponTuning_SnapshotHash(
    const cg_race_weapon_tuning_stage_t *stage,
    const race_weapon_tuning_snapshot_t *snapshot) {
  uint64_t hash = UINT64_C(14695981039346656037);
  Cg_RaceWeaponTuning_Hash32(&hash, RACE_WEAPON_TUNING_CATALOG_VERSION);
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
    Cg_RaceWeaponTuning_Hash32(&hash, Cg_RaceWeaponTuning_ScalarBits(
      stage->descriptors[i].type, snapshot->values[i]));
  }
  return hash;
}

bool Cg_RaceWeaponTuning_ScalarValue(
    const race_weapon_tuning_descriptor_t *descriptor,
    const race_weapon_tuning_scalar_t scalar, double *value) {
  if (!descriptor || !value || descriptor->type < 0 ||
      descriptor->type >= RACE_WEAPON_TUNING_TYPE_TOTAL) {
    return false;
  }
  switch (descriptor->type) {
    case RACE_WEAPON_TUNING_TYPE_INT32:
      *value = scalar.integer;
      return true;
    case RACE_WEAPON_TUNING_TYPE_UINT32:
      *value = scalar.unsigned_integer;
      return true;
    case RACE_WEAPON_TUNING_TYPE_FLOAT:
      if (!isfinite(scalar.real)) {
        return false;
      }
      *value = scalar.real;
      return true;
    default:
      return false;
  }
}

bool Cg_RaceWeaponTuning_SnapshotValue(
    const race_weapon_tuning_snapshot_t *snapshot,
    const race_weapon_tuning_id_t id, double *value) {
  const cg_race_weapon_tuning_cache_t *cache = &cg_race_weapon_tuning.cache;
  if (!snapshot || !value || !cache->complete || id < 0 ||
      id >= RACE_WEAPON_TUNING_VALUE_TOTAL) {
    return false;
  }
  return Cg_RaceWeaponTuning_ScalarValue(cache->descriptors + id,
                                         snapshot->values[id], value);
}

static bool Cg_RaceWeaponTuning_DescriptorValid(
    const race_weapon_tuning_descriptor_t *descriptor) {
  double base, minimum, maximum, step;
  if (!descriptor || descriptor->id < 0 ||
      descriptor->id >= RACE_WEAPON_TUNING_VALUE_TOTAL ||
      descriptor->group < 0 ||
      descriptor->group >= RACE_WEAPON_TUNING_GROUP_TOTAL ||
      !Cg_RaceWeaponTuning_ScalarValue(
        descriptor, descriptor->compiled_default, &base) ||
      !Cg_RaceWeaponTuning_ScalarValue(
        descriptor, descriptor->minimum, &minimum) ||
      !Cg_RaceWeaponTuning_ScalarValue(
        descriptor, descriptor->maximum, &maximum) ||
      !Cg_RaceWeaponTuning_ScalarValue(
        descriptor, descriptor->step, &step)) {
    return false;
  }
  return isfinite(base) && isfinite(minimum) && isfinite(maximum) &&
         isfinite(step) && minimum <= base && base <= maximum && step > 0.0;
}

static bool Cg_RaceWeaponTuning_ScalarValid(
    const race_weapon_tuning_descriptor_t *descriptor,
    const race_weapon_tuning_scalar_t scalar) {
  if (!Cg_RaceWeaponTuning_DescriptorValid(descriptor)) {
    return false;
  }
  if (descriptor->type == RACE_WEAPON_TUNING_TYPE_INT32) {
    return scalar.integer >= descriptor->minimum.integer &&
           scalar.integer <= descriptor->maximum.integer &&
           ((int64_t) scalar.integer - descriptor->minimum.integer) %
             descriptor->step.integer == 0;
  }
  if (descriptor->type == RACE_WEAPON_TUNING_TYPE_UINT32) {
    return scalar.unsigned_integer >= descriptor->minimum.unsigned_integer &&
           scalar.unsigned_integer <= descriptor->maximum.unsigned_integer &&
           (scalar.unsigned_integer - descriptor->minimum.unsigned_integer) %
             descriptor->step.unsigned_integer == 0u;
  }
  if (!isfinite(scalar.real) || scalar.real < descriptor->minimum.real ||
      scalar.real > descriptor->maximum.real) {
    return false;
  }
  const float minimum_steps =
    (scalar.real - descriptor->minimum.real) / descriptor->step.real;
  const float default_steps =
    (scalar.real - descriptor->compiled_default.real) / descriptor->step.real;
  const bool on_minimum_grid = isfinite(minimum_steps) &&
    fabsf(minimum_steps - roundf(minimum_steps)) <= .0001f;
  const bool on_default_grid = isfinite(default_steps) &&
    fabsf(default_steps - roundf(default_steps)) <= .0001f;
  return on_minimum_grid || on_default_grid;
}

bool Cg_RaceWeaponTuning_CanonicalValue(
    const race_weapon_tuning_descriptor_t *descriptor, const double value,
    race_weapon_tuning_scalar_t *scalar) {
  if (!descriptor || !scalar || !isfinite(value) ||
      !Cg_RaceWeaponTuning_DescriptorValid(descriptor)) {
    return false;
  }
  double minimum, maximum;
  if (!Cg_RaceWeaponTuning_ScalarValue(
        descriptor, descriptor->minimum, &minimum) ||
      !Cg_RaceWeaponTuning_ScalarValue(
        descriptor, descriptor->maximum, &maximum) ||
      value < minimum || value > maximum) {
    return false;
  }
  *scalar = (race_weapon_tuning_scalar_t) { 0 };
  switch (descriptor->type) {
    case RACE_WEAPON_TUNING_TYPE_INT32:
      if (trunc(value) != value || value < INT32_MIN || value > INT32_MAX) {
        return false;
      }
      scalar->integer = (int32_t) value;
      break;
    case RACE_WEAPON_TUNING_TYPE_UINT32:
      if (trunc(value) != value || value < 0.0 || value > UINT32_MAX) {
        return false;
      }
      scalar->unsigned_integer = (uint32_t) value;
      break;
    case RACE_WEAPON_TUNING_TYPE_FLOAT:
      scalar->real = (float) value;
      break;
    default:
      return false;
  }
  return Cg_RaceWeaponTuning_ScalarValid(descriptor, *scalar);
}

static bool Cg_RaceWeaponTuning_SnapshotValid(
    const cg_race_weapon_tuning_stage_t *stage,
    const race_weapon_tuning_snapshot_t *snapshot) {
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
    const race_weapon_tuning_descriptor_t *descriptor = stage->descriptors + i;
    double value, minimum, maximum;
    if (!Cg_RaceWeaponTuning_ScalarValid(
          descriptor, snapshot->values[i]) ||
        !Cg_RaceWeaponTuning_ScalarValue(
          descriptor, snapshot->values[i], &value) ||
        !Cg_RaceWeaponTuning_ScalarValue(
          descriptor, descriptor->minimum, &minimum) ||
        !Cg_RaceWeaponTuning_ScalarValue(
          descriptor, descriptor->maximum, &maximum) ||
        !isfinite(value) || value < minimum || value > maximum) {
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
  if (!refire || !isfinite(per_shot) ||
      per_shot * 1000.f / (float) refire >
        RACE_WEAPON_TUNING_MAX_HYPER_IMPULSE_PER_SECOND) {
    return false;
  }
  return true;
}

static uint64_t Cg_RaceWeaponTuning_ExpectedSnapshotHash(
    const race_weapon_tuning_sync_begin_t *begin,
    const race_weapon_tuning_snapshot_kind_t kind) {
  switch (kind) {
    case RACE_WEAPON_TUNING_SNAPSHOT_BASELINE:
      return begin->baseline_hash;
    case RACE_WEAPON_TUNING_SNAPSHOT_CURRENT:
      return begin->current_hash;
    case RACE_WEAPON_TUNING_SNAPSHOT_PREVIOUS:
      return begin->previous_hash;
    case RACE_WEAPON_TUNING_SNAPSHOT_SLOT_A:
      return begin->slot_a_hash;
    case RACE_WEAPON_TUNING_SNAPSHOT_SLOT_B:
      return begin->slot_b_hash;
    default:
      return 0u;
  }
}

static uint16_t Cg_RaceWeaponTuning_SnapshotFlag(
    const race_weapon_tuning_snapshot_kind_t kind) {
  switch (kind) {
    case RACE_WEAPON_TUNING_SNAPSHOT_BASELINE:
      return RACE_WEAPON_TUNING_SYNC_HAS_BASELINE;
    case RACE_WEAPON_TUNING_SNAPSHOT_CURRENT:
      return RACE_WEAPON_TUNING_SYNC_HAS_CURRENT;
    case RACE_WEAPON_TUNING_SNAPSHOT_PREVIOUS:
      return RACE_WEAPON_TUNING_SYNC_HAS_PREVIOUS;
    case RACE_WEAPON_TUNING_SNAPSHOT_SLOT_A:
      return RACE_WEAPON_TUNING_SYNC_HAS_SLOT_A;
    case RACE_WEAPON_TUNING_SNAPSHOT_SLOT_B:
      return RACE_WEAPON_TUNING_SYNC_HAS_SLOT_B;
    default:
      return 0u;
  }
}

static bool Cg_RaceWeaponTuning_BeginValid(
    const race_weapon_tuning_sync_begin_t *begin) {
  if (!begin || !begin->catalog_hash ||
      begin->state >= RACE_WEAPON_TUNING_STATE_TOTAL ||
      begin->flags & ~RACE_WEAPON_TUNING_SYNC_FLAGS_MASK) {
    return false;
  }
  for (race_weapon_tuning_snapshot_kind_t kind =
         RACE_WEAPON_TUNING_SNAPSHOT_BASELINE;
       kind < RACE_WEAPON_TUNING_SNAPSHOT_TOTAL; kind++) {
    const bool present =
      (begin->flags & Cg_RaceWeaponTuning_SnapshotFlag(kind)) != 0u;
    if (present !=
        (Cg_RaceWeaponTuning_ExpectedSnapshotHash(begin, kind) != 0u)) {
      return false;
    }
  }
  const uint16_t supported = RACE_WEAPON_TUNING_SYNC_HAS_BASELINE |
    RACE_WEAPON_TUNING_SYNC_HAS_CURRENT |
    RACE_WEAPON_TUNING_SYNC_CAN_MUTATE;
  if (begin->flags & ~supported ||
      (begin->flags & (RACE_WEAPON_TUNING_SYNC_HAS_BASELINE |
                       RACE_WEAPON_TUNING_SYNC_HAS_CURRENT)) !=
        (RACE_WEAPON_TUNING_SYNC_HAS_BASELINE |
         RACE_WEAPON_TUNING_SYNC_HAS_CURRENT)) {
    return false;
  }
  return true;
}

static void Cg_RaceWeaponTuning_StageFailed(void) {
  memset(&cg_race_weapon_tuning.stage, 0,
         sizeof(cg_race_weapon_tuning.stage));
  cg_race_weapon_tuning.resync_required = true;
}

static bool Cg_RaceWeaponTuning_StreamMatches(const uint32_t request_id,
                                              const uint64_t generation) {
  const cg_race_weapon_tuning_stage_t *stage = &cg_race_weapon_tuning.stage;
  return stage->active && request_id == stage->begin.request_id &&
         generation == stage->begin.generation;
}

static bool Cg_RaceWeaponTuning_StartStage(const void *data,
                                           const size_t length) {
  race_weapon_tuning_sync_begin_t begin;
  if (!Race_WeaponTuningWire_DecodeSyncBegin(data, length, &begin) ||
      !Cg_RaceWeaponTuning_BeginValid(&begin)) {
    Cg_RaceWeaponTuning_StageFailed();
    return false;
  }
  if (cg_race_weapon_tuning.cache.complete &&
      begin.generation < cg_race_weapon_tuning.cache.metadata.generation) {
    Cg_RaceWeaponTuning_StageFailed();
    return false;
  }
  if (begin.request_id) {
    const cg_race_weapon_tuning_pending_t *sync =
      cg_race_weapon_tuning.pending + RACE_WEAPON_TUNING_OPERATION_SYNC;
    if (!sync->pending || sync->request_id != begin.request_id) {
      return false;
    }
  }
  cg_race_weapon_tuning_stage_t stage = { 0 };
  stage.active = true;
  stage.started = Cg_RaceWeaponTuning_Now();
  stage.begin = begin;
  cg_race_weapon_tuning.stage = stage;
  return true;
}

static bool Cg_RaceWeaponTuning_AddDescriptor(const void *data,
                                              const size_t length) {
  race_weapon_tuning_catalog_entry_t entry;
  cg_race_weapon_tuning_stage_t *stage = &cg_race_weapon_tuning.stage;
  if (!Race_WeaponTuningWire_DecodeCatalogEntry(data, length, &entry) ||
      !Cg_RaceWeaponTuning_StreamMatches(entry.request_id, entry.generation) ||
      entry.index != stage->next_descriptor ||
      !Cg_RaceWeaponTuning_DescriptorValid(&entry.descriptor) ||
      stage->descriptor_mask & (1u << entry.index)) {
    Cg_RaceWeaponTuning_StageFailed();
    return false;
  }
  for (size_t i = 0u; i < entry.index; i++) {
    const race_weapon_tuning_descriptor_t *prior = stage->descriptors + i;
    if (!strcmp(prior->key, entry.descriptor.key) ||
        (prior->group == entry.descriptor.group &&
         (strcmp(prior->group_key, entry.descriptor.group_key) ||
          strcmp(prior->group_label, entry.descriptor.group_label)))) {
      Cg_RaceWeaponTuning_StageFailed();
      return false;
    }
  }
  stage->descriptors[entry.index] = entry.descriptor;
  stage->descriptor_mask |= 1u << entry.index;
  stage->next_descriptor++;
  return true;
}

static bool Cg_RaceWeaponTuning_AddSnapshot(const void *data,
                                            const size_t length) {
  race_weapon_tuning_snapshot_message_t snapshot;
  cg_race_weapon_tuning_stage_t *stage = &cg_race_weapon_tuning.stage;
  if (!Race_WeaponTuningWire_DecodeSnapshot(data, length, &snapshot) ||
      !Cg_RaceWeaponTuning_StreamMatches(snapshot.request_id,
                                         snapshot.generation) ||
      stage->descriptor_mask != CG_RACE_TUNING_ALL_DESCRIPTORS ||
      !(stage->begin.flags & Cg_RaceWeaponTuning_SnapshotFlag(snapshot.kind)) ||
      snapshot.hash != Cg_RaceWeaponTuning_ExpectedSnapshotHash(
        &stage->begin, snapshot.kind) || stage->snapshots[snapshot.kind] ||
      !Cg_RaceWeaponTuning_SnapshotValid(stage, &snapshot.snapshot) ||
      snapshot.hash != Cg_RaceWeaponTuning_SnapshotHash(
        stage, &snapshot.snapshot)) {
    Cg_RaceWeaponTuning_StageFailed();
    return false;
  }
  stage->snapshots[snapshot.kind] = true;
  stage->snapshot[snapshot.kind] = snapshot;
  return true;
}

static bool Cg_RaceWeaponTuning_CatalogGroupsValid(
    const cg_race_weapon_tuning_stage_t *stage) {
  uint32_t groups = 0u;
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
    groups |= 1u << stage->descriptors[i].group;
  }
  return groups == (1u << RACE_WEAPON_TUNING_GROUP_TOTAL) - 1u;
}

static bool Cg_RaceWeaponTuning_StatusMatches(void) {
  const cg_race_weapon_tuning_cache_t *cache = &cg_race_weapon_tuning.cache;
  if (!cache->complete || !cache->status_valid ||
      cache->status.state != cache->metadata.state ||
      cache->status.generation != cache->metadata.generation ||
      strcmp(cache->status.preset_key, cache->metadata.preset_key) ||
      strcmp(cache->status.identity, cache->metadata.identity) ||
      !cache->baseline_valid || !cache->current_valid ||
      cache->status.hash != cache->metadata.current_hash) {
    return false;
  }
  if (cache->status.state == RACE_WEAPON_TUNING_STATE_ACTIVE) {
    double climb;
    if (!cache->baseline_valid ||
        !Cg_RaceWeaponTuning_SnapshotValue(
          &cache->current, RACE_WEAPON_TUNING_HYPER_CLIMB_RANGE, &climb) ||
        !isfinite(climb) || climb != cache->status.hyper_climb_range) {
      return false;
    }
  }
  return true;
}

static void Cg_RaceWeaponTuning_Reconcile(void) {
  cg_race_weapon_tuning.cache.synchronized =
    Cg_RaceWeaponTuning_StatusMatches();
  if (!cg_race_weapon_tuning.cache.synchronized) {
    cg_race_weapon_tuning.resync_required = true;
  }
}

static void Cg_RaceWeaponTuning_ResolveAcceptedRequests(void) {
  const cg_race_weapon_tuning_cache_t *cache = &cg_race_weapon_tuning.cache;
  const race_weapon_tuning_operation_t operations[] = {
    RACE_WEAPON_TUNING_OPERATION_APPLY,
    RACE_WEAPON_TUNING_OPERATION_RESET_ALL
  };
  for (size_t i = 0u; i < lengthof(operations); i++) {
    const race_weapon_tuning_operation_t operation = operations[i];
    cg_race_weapon_tuning_pending_t *pending =
      cg_race_weapon_tuning.pending + operation;
    if (!pending->pending || !pending->awaiting_authoritative ||
        cache->metadata.generation < pending->accepted_generation) {
      continue;
    }
    if (cache->metadata.generation == pending->accepted_generation &&
        pending->accepted_hash &&
        cache->metadata.current_hash != pending->accepted_hash) {
      continue;
    }
    memset(pending, 0, sizeof(*pending));
  }
}

static bool Cg_RaceWeaponTuning_CommitStage(const void *data,
                                            const size_t length) {
  race_weapon_tuning_sync_end_t end;
  cg_race_weapon_tuning_stage_t *stage = &cg_race_weapon_tuning.stage;
  if (!Race_WeaponTuningWire_DecodeSyncEnd(data, length, &end) ||
      !Cg_RaceWeaponTuning_StreamMatches(end.request_id, end.generation) ||
      end.catalog_hash != stage->begin.catalog_hash ||
      end.descriptor_count != RACE_WEAPON_TUNING_VALUE_COUNT ||
      stage->descriptor_mask != CG_RACE_TUNING_ALL_DESCRIPTORS ||
      !Cg_RaceWeaponTuning_CatalogGroupsValid(stage) ||
      end.catalog_hash != Cg_RaceWeaponTuning_CatalogHash(stage)) {
    Cg_RaceWeaponTuning_StageFailed();
    return false;
  }
  for (race_weapon_tuning_snapshot_kind_t kind =
         RACE_WEAPON_TUNING_SNAPSHOT_BASELINE;
       kind < RACE_WEAPON_TUNING_SNAPSHOT_TOTAL; kind++) {
    const bool required =
      (stage->begin.flags & Cg_RaceWeaponTuning_SnapshotFlag(kind)) != 0u;
    if (stage->snapshots[kind] != required) {
      Cg_RaceWeaponTuning_StageFailed();
      return false;
    }
  }

  cg_race_weapon_tuning_cache_t next = { 0 };
  next.complete = true;
  next.revision = cg_race_weapon_tuning.cache.revision + 1u;
  if (!next.revision) {
    next.revision = 1u;
  }
  next.result_revision = cg_race_weapon_tuning.cache.result_revision;
  next.result_valid = cg_race_weapon_tuning.cache.result_valid;
  next.result = cg_race_weapon_tuning.cache.result;
  next.status_valid = cg_race_weapon_tuning.cache.status_valid;
  next.status = cg_race_weapon_tuning.cache.status;
  next.metadata = stage->begin;
  memcpy(next.descriptors, stage->descriptors, sizeof(next.descriptors));

#define CG_RACE_TUNING_COPY_SNAPSHOT(member, kind) do { \
  next.member##_valid = stage->snapshots[kind]; \
  if (next.member##_valid) { \
    next.member = stage->snapshot[kind].snapshot; \
  } \
} while (0)
  CG_RACE_TUNING_COPY_SNAPSHOT(
    baseline, RACE_WEAPON_TUNING_SNAPSHOT_BASELINE);
  CG_RACE_TUNING_COPY_SNAPSHOT(
    current, RACE_WEAPON_TUNING_SNAPSHOT_CURRENT);
#undef CG_RACE_TUNING_COPY_SNAPSHOT

  cg_race_weapon_tuning.cache = next;
  if (end.request_id) {
    cg_race_weapon_tuning_pending_t *sync =
      cg_race_weapon_tuning.pending + RACE_WEAPON_TUNING_OPERATION_SYNC;
    if (sync->pending && sync->request_id == end.request_id) {
      memset(sync, 0, sizeof(*sync));
    }
  }
  memset(stage, 0, sizeof(*stage));
  Cg_RaceWeaponTuning_Reconcile();
  Cg_RaceWeaponTuning_ResolveAcceptedRequests();
  return true;
}

static bool Cg_RaceWeaponTuning_AcceptResult(const void *data,
                                             const size_t length) {
  race_weapon_tuning_result_message_t result;
  if (!Race_WeaponTuningWire_DecodeResult(data, length, &result) ||
      !result.request_id || result.operation <= RACE_WEAPON_TUNING_OPERATION_NONE ||
      result.operation >= RACE_WEAPON_TUNING_OPERATION_TOTAL) {
    return false;
  }
  cg_race_weapon_tuning_pending_t *pending =
    cg_race_weapon_tuning.pending + result.operation;
  if (!pending->pending || pending->request_id != result.request_id) {
    return false;
  }

  cg_race_weapon_tuning.cache.result = result;
  cg_race_weapon_tuning.cache.result_valid = true;
  cg_race_weapon_tuning.cache.result_revision++;
  if (!cg_race_weapon_tuning.cache.result_revision) {
    cg_race_weapon_tuning.cache.result_revision = 1u;
  }

  if (result.result == RACE_WEAPON_TUNING_RESULT_OK) {
    pending->awaiting_authoritative = true;
    pending->accepted_generation = result.generation;
    pending->accepted_hash = result.hash;
  } else {
    memset(pending, 0, sizeof(*pending));
  }
  if (result.result == RACE_WEAPON_TUNING_RESULT_STALE) {
    cg_race_weapon_tuning.resync_required = true;
  }
  return true;
}

bool Cg_RaceWeaponTuning_ParsePayload(const void *data, const size_t length) {
  if (!data || length < 2u || length > RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD) {
    Cg_RaceWeaponTuning_StageFailed();
    return false;
  }
  const uint8_t *bytes = data;
  if (bytes[0] != RACE_WEAPON_TUNING_WIRE_VERSION) {
    Cg_RaceWeaponTuning_StageFailed();
    return false;
  }
  switch ((race_weapon_tuning_message_op_t) bytes[1]) {
    case RACE_WEAPON_TUNING_MESSAGE_SYNC_BEGIN:
      return Cg_RaceWeaponTuning_StartStage(data, length);
    case RACE_WEAPON_TUNING_MESSAGE_CATALOG_ENTRY:
      return Cg_RaceWeaponTuning_AddDescriptor(data, length);
    case RACE_WEAPON_TUNING_MESSAGE_SNAPSHOT:
      return Cg_RaceWeaponTuning_AddSnapshot(data, length);
    case RACE_WEAPON_TUNING_MESSAGE_SYNC_END:
      return Cg_RaceWeaponTuning_CommitStage(data, length);
    case RACE_WEAPON_TUNING_MESSAGE_RESULT:
      return Cg_RaceWeaponTuning_AcceptResult(data, length);
    default:
      Cg_RaceWeaponTuning_StageFailed();
      return false;
  }
}

bool Cg_RaceWeaponTuning_ParseMessage(const int32_t command) {
  if (command != SV_CMD_RACE_WEAPON_TUNING) {
    return false;
  }
  const int32_t encoded_length = cgi.ReadByte ? cgi.ReadByte() : -1;
  if (encoded_length <= 0 ||
      encoded_length > (int32_t) RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD ||
      !cgi.ReadData) {
    Cg_RaceWeaponTuning_StageFailed();
    return true;
  }
  uint8_t payload[RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD];
  cgi.ReadData(payload, (size_t) encoded_length);
  Cg_RaceWeaponTuning_ParsePayload(payload, (size_t) encoded_length);
  return true;
}

void Cg_RaceWeaponTuning_UpdateStatus(const char *wire) {
  race_weapon_tuning_status_t status;
  if (!Race_WeaponTuning_StatusDecode(wire ? wire : "", &status)) {
    memset(&cg_race_weapon_tuning.cache.status, 0,
           sizeof(cg_race_weapon_tuning.cache.status));
    cg_race_weapon_tuning.cache.status_valid = false;
    cg_race_weapon_tuning.cache.synchronized = false;
    cg_race_weapon_tuning.resync_required = true;
    return;
  }
  cg_race_weapon_tuning.cache.status = status;
  cg_race_weapon_tuning.cache.status_valid = true;
  Cg_RaceWeaponTuning_Reconcile();
}

const cg_race_weapon_tuning_cache_t *Cg_RaceWeaponTuning_Cache(void) {
  return &cg_race_weapon_tuning.cache;
}

cg_race_weapon_tuning_request_t Cg_RaceWeaponTuning_RequestState(
    const race_weapon_tuning_operation_t operation) {
  if (operation <= RACE_WEAPON_TUNING_OPERATION_NONE ||
      operation >= RACE_WEAPON_TUNING_OPERATION_TOTAL) {
    return (cg_race_weapon_tuning_request_t) { 0 };
  }
  const cg_race_weapon_tuning_pending_t *pending =
    cg_race_weapon_tuning.pending + operation;
  return (cg_race_weapon_tuning_request_t) {
    .pending = pending->pending,
    .awaiting_authoritative = pending->awaiting_authoritative,
    .request_id = pending->request_id,
    .expected_generation = pending->expected_generation
  };
}

static bool Cg_RaceWeaponTuning_ConflictingOperation(
    const race_weapon_tuning_operation_t operation) {
  return operation == RACE_WEAPON_TUNING_OPERATION_APPLY ||
         operation == RACE_WEAPON_TUNING_OPERATION_RESET_ALL;
}

bool Cg_RaceWeaponTuning_MutationPending(void) {
  return cg_race_weapon_tuning.pending[
           RACE_WEAPON_TUNING_OPERATION_APPLY].pending ||
         cg_race_weapon_tuning.pending[
           RACE_WEAPON_TUNING_OPERATION_RESET_ALL].pending;
}

static uint32_t Cg_RaceWeaponTuning_NextRequestId(void) {
  for (size_t attempt = 0u;
       attempt < RACE_WEAPON_TUNING_OPERATION_TOTAL + 1u; attempt++) {
    uint32_t request_id = ++cg_race_weapon_tuning.next_request_id;
    if (!request_id) {
      request_id = ++cg_race_weapon_tuning.next_request_id;
    }
    bool used = false;
    for (size_t i = 1u; i < RACE_WEAPON_TUNING_OPERATION_TOTAL; i++) {
      used |= cg_race_weapon_tuning.pending[i].pending &&
              cg_race_weapon_tuning.pending[i].request_id == request_id;
    }
    if (!used) {
      return request_id;
    }
  }
  return 0u;
}

static uint32_t Cg_RaceWeaponTuning_Send(
    const race_weapon_tuning_operation_t operation, const char *subcommand,
    const bool generation_required, const uint64_t expected_generation,
    const char *arguments) {
  if (!cgi.Cbuf || !subcommand || !*subcommand ||
      operation <= RACE_WEAPON_TUNING_OPERATION_NONE ||
      operation >= RACE_WEAPON_TUNING_OPERATION_TOTAL ||
      cg_race_weapon_tuning.pending[operation].pending ||
      (Cg_RaceWeaponTuning_ConflictingOperation(operation) &&
       Cg_RaceWeaponTuning_MutationPending())) {
    return 0u;
  }
  const uint32_t request_id = Cg_RaceWeaponTuning_NextRequestId();
  if (!request_id) {
    return 0u;
  }
  char command[MAX_STRING_CHARS];
  const int32_t written = generation_required
    ? snprintf(command, sizeof(command), "race tune %s req=%" PRIu32
               " %" PRIu64 "%s%s\n", subcommand, request_id,
               expected_generation,
               arguments && *arguments ? " " : "",
               arguments ? arguments : "")
    : snprintf(command, sizeof(command), "race tune %s req=%" PRIu32
               "%s%s\n", subcommand, request_id,
               arguments && *arguments ? " " : "", arguments ? arguments : "");
  if (written <= 0 || (size_t) written >= sizeof(command) ||
      (size_t) written >= CG_RACE_TUNING_MAX_COMMAND_CHARS) {
    return 0u;
  }
  cg_race_weapon_tuning.pending[operation] =
    (cg_race_weapon_tuning_pending_t) {
      .pending = true,
      .request_id = request_id,
      .sent = Cg_RaceWeaponTuning_Now(),
      .expected_generation = expected_generation
    };
  cgi.Cbuf(command);
  return request_id;
}

static bool Cg_RaceWeaponTuning_Mutable(uint64_t *generation) {
  const cg_race_weapon_tuning_cache_t *cache = &cg_race_weapon_tuning.cache;
  if (!cache->complete || !cache->synchronized ||
      !cache->baseline_valid || !cache->current_valid ||
      (cache->metadata.state != RACE_WEAPON_TUNING_STATE_INACTIVE &&
       cache->metadata.state != RACE_WEAPON_TUNING_STATE_ACTIVE) ||
      !(cache->metadata.flags & RACE_WEAPON_TUNING_SYNC_CAN_MUTATE)) {
    return false;
  }
  if (generation) {
    *generation = cache->metadata.generation;
  }
  return true;
}

uint32_t Cg_RaceWeaponTuning_RequestSync(void) {
  const uint32_t request_id = Cg_RaceWeaponTuning_Send(
    RACE_WEAPON_TUNING_OPERATION_SYNC, "sync", false, 0u, NULL);
  if (request_id) {
    cg_race_weapon_tuning.last_resync = Cg_RaceWeaponTuning_Now();
    cg_race_weapon_tuning.resync_required = false;
  }
  return request_id;
}

uint32_t Cg_RaceWeaponTuning_RequestApply(
    const uint64_t expected_generation, const char *pairs) {
  if (!pairs || !*pairs) {
    return 0u;
  }
  bool previous_space = true;
  for (const unsigned char *c = (const unsigned char *) pairs; *c; c++) {
    if (*c == ' ') {
      if (previous_space || !c[1]) {
        return 0u;
      }
      previous_space = true;
      continue;
    }
    if (*c < 0x21u || *c > 0x7eu || *c == ';' || *c == '\\' || *c == '"') {
      return 0u;
    }
    previous_space = false;
  }
  uint64_t generation;
  return Cg_RaceWeaponTuning_Mutable(&generation) &&
         generation == expected_generation
    ? Cg_RaceWeaponTuning_Send(RACE_WEAPON_TUNING_OPERATION_APPLY, "apply",
                               true, expected_generation, pairs)
    : 0u;
}

uint32_t Cg_RaceWeaponTuning_RequestResetAll(void) {
  uint64_t generation;
  return Cg_RaceWeaponTuning_Mutable(&generation)
    ? Cg_RaceWeaponTuning_Send(RACE_WEAPON_TUNING_OPERATION_RESET_ALL,
                               "reset", true, generation, "all")
    : 0u;
}

bool Cg_RaceWeaponTuning_ClimbPresentation(float *range, bool *overridden) {
  const cg_race_weapon_tuning_cache_t *cache = &cg_race_weapon_tuning.cache;
  if (overridden) {
    *overridden = false;
  }
  if (cache->status_valid &&
      cache->status.state == RACE_WEAPON_TUNING_STATE_INACTIVE) {
    return true;
  }
  if (!cache->status_valid ||
      cache->status.state != RACE_WEAPON_TUNING_STATE_ACTIVE ||
      !cache->synchronized) {
    return false;
  }
  if (!isfinite(cache->status.hyper_climb_range)) {
    return false;
  }
  if (range) {
    *range = cache->status.hyper_climb_range;
  }
  if (overridden) {
    *overridden = true;
  }
  return true;
}

const char *Cg_RaceWeaponTuning_Warning(void) {
  const cg_race_weapon_tuning_cache_t *cache = &cg_race_weapon_tuning.cache;
  if (!cache->status_valid || !cache->synchronized) {
    if (cache->status_valid &&
        cache->status.state != RACE_WEAPON_TUNING_STATE_INACTIVE) {
      return "WEAPON TUNING UNSYNCHRONIZED - UNRANKED";
    }
    return "WEAPON TUNING STATUS UNSYNCHRONIZED - RANKING UNKNOWN";
  }
  switch (cache->status.state) {
    case RACE_WEAPON_TUNING_STATE_INACTIVE:
      return NULL;
    case RACE_WEAPON_TUNING_STATE_ACTIVE:
      return "CUSTOM WEAPON VALUES - UNRANKED";
    case RACE_WEAPON_TUNING_STATE_TRANSITION:
      return "WEAPON TUNING TRANSITION - UNRANKED";
    case RACE_WEAPON_TUNING_STATE_RECOVERY:
      return "WEAPON TUNING RECOVERY BLOCK - UNRANKED";
    case RACE_WEAPON_TUNING_STATE_ERROR:
      return "WEAPON TUNING ERROR BLOCK - UNRANKED";
    default:
      return "WEAPON TUNING STATUS UNSYNCHRONIZED - RANKING UNKNOWN";
  }
}

void Cg_RaceWeaponTuning_Update(void) {
  const uint32_t now = Cg_RaceWeaponTuning_Now();
  if (cg_race_weapon_tuning.stage.active &&
      now - cg_race_weapon_tuning.stage.started >
        CG_RACE_TUNING_STAGE_TIMEOUT) {
    Cg_RaceWeaponTuning_StageFailed();
  }
  for (size_t i = 1u; i < RACE_WEAPON_TUNING_OPERATION_TOTAL; i++) {
    cg_race_weapon_tuning_pending_t *pending =
      cg_race_weapon_tuning.pending + i;
    if (pending->pending &&
        now - pending->sent > CG_RACE_TUNING_REQUEST_TIMEOUT) {
      memset(pending, 0, sizeof(*pending));
      cg_race_weapon_tuning.resync_required = true;
    }
  }
  const bool connected = cgi.state && *cgi.state >= CL_CONNECTED;
  if (connected && cg_race_weapon_tuning.resync_required &&
      !cg_race_weapon_tuning.pending[RACE_WEAPON_TUNING_OPERATION_SYNC].pending &&
      now - cg_race_weapon_tuning.last_resync >=
        CG_RACE_TUNING_RESYNC_INTERVAL) {
    Cg_RaceWeaponTuning_RequestSync();
  }
}

void Cg_RaceWeaponTuning_Clear(void) {
  memset(&cg_race_weapon_tuning, 0, sizeof(cg_race_weapon_tuning));
  cg_race_weapon_tuning.resync_required = true;
}

void Cg_RaceWeaponTuning_Init(void) {
  Cg_RaceWeaponTuning_Clear();
  if (cgi.ConfigString) {
    Cg_RaceWeaponTuning_UpdateStatus(
      cgi.ConfigString(CS_RACE_WEAPON_TUNING_STATUS));
  }
}
