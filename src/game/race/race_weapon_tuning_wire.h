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

#include "race_weapon_tuning_types.h"

#define RACE_WEAPON_TUNING_WIRE_VERSION 1u
#define RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD 255u
#define RACE_WEAPON_TUNING_STATUS_MAX 159u
#define RACE_WEAPON_TUNING_STATUS_SIZE \
  (RACE_WEAPON_TUNING_STATUS_MAX + 1u)
#define RACE_WEAPON_TUNING_RESULT_TEXT_MAX 95u
#define RACE_WEAPON_TUNING_RESULT_TEXT_SIZE \
  (RACE_WEAPON_TUNING_RESULT_TEXT_MAX + 1u)

typedef enum {
  RACE_WEAPON_TUNING_MESSAGE_SYNC_BEGIN = 1,
  RACE_WEAPON_TUNING_MESSAGE_CATALOG_ENTRY,
  RACE_WEAPON_TUNING_MESSAGE_SNAPSHOT,
  RACE_WEAPON_TUNING_MESSAGE_SYNC_END,
  RACE_WEAPON_TUNING_MESSAGE_RESULT,

  RACE_WEAPON_TUNING_MESSAGE_TOTAL
} race_weapon_tuning_message_op_t;

typedef enum {
  RACE_WEAPON_TUNING_OPERATION_NONE,
  RACE_WEAPON_TUNING_OPERATION_STATUS,
  RACE_WEAPON_TUNING_OPERATION_LIST,
  RACE_WEAPON_TUNING_OPERATION_GET,
  RACE_WEAPON_TUNING_OPERATION_SYNC,
  RACE_WEAPON_TUNING_OPERATION_BEGIN,
  RACE_WEAPON_TUNING_OPERATION_END,
  RACE_WEAPON_TUNING_OPERATION_APPLY,
  RACE_WEAPON_TUNING_OPERATION_RESET_KEY,
  RACE_WEAPON_TUNING_OPERATION_RESET_GROUP,
  RACE_WEAPON_TUNING_OPERATION_RESET_ALL,
  RACE_WEAPON_TUNING_OPERATION_UNDO,
  RACE_WEAPON_TUNING_OPERATION_SLOT_SAVE_A,
  RACE_WEAPON_TUNING_OPERATION_SLOT_SAVE_B,
  RACE_WEAPON_TUNING_OPERATION_SLOT_LOAD_A,
  RACE_WEAPON_TUNING_OPERATION_SLOT_LOAD_B,
  RACE_WEAPON_TUNING_OPERATION_EXPORT,
  RACE_WEAPON_TUNING_OPERATION_LOAD_NAMED,
  RACE_WEAPON_TUNING_OPERATION_ABORT,

  RACE_WEAPON_TUNING_OPERATION_TOTAL
} race_weapon_tuning_operation_t;

typedef enum {
  RACE_WEAPON_TUNING_RESULT_OK,
  RACE_WEAPON_TUNING_RESULT_NOOP,
  RACE_WEAPON_TUNING_RESULT_DENIED,
  RACE_WEAPON_TUNING_RESULT_INVALID,
  RACE_WEAPON_TUNING_RESULT_STALE,
  RACE_WEAPON_TUNING_RESULT_INACTIVE,
  RACE_WEAPON_TUNING_RESULT_ACTIVE,
  RACE_WEAPON_TUNING_RESULT_UNAVAILABLE,
  RACE_WEAPON_TUNING_RESULT_NOT_FOUND,
  RACE_WEAPON_TUNING_RESULT_INTERNAL,

  RACE_WEAPON_TUNING_RESULT_TOTAL
} race_weapon_tuning_result_t;

typedef enum {
  RACE_WEAPON_TUNING_SNAPSHOT_BASELINE = 1,
  RACE_WEAPON_TUNING_SNAPSHOT_CURRENT,
  RACE_WEAPON_TUNING_SNAPSHOT_PREVIOUS,
  RACE_WEAPON_TUNING_SNAPSHOT_SLOT_A,
  RACE_WEAPON_TUNING_SNAPSHOT_SLOT_B,

  RACE_WEAPON_TUNING_SNAPSHOT_TOTAL
} race_weapon_tuning_snapshot_kind_t;

#define RACE_WEAPON_TUNING_SYNC_HAS_BASELINE (1u << 0u)
#define RACE_WEAPON_TUNING_SYNC_HAS_CURRENT  (1u << 1u)
#define RACE_WEAPON_TUNING_SYNC_HAS_PREVIOUS (1u << 2u)
#define RACE_WEAPON_TUNING_SYNC_HAS_SLOT_A   (1u << 3u)
#define RACE_WEAPON_TUNING_SYNC_HAS_SLOT_B   (1u << 4u)
#define RACE_WEAPON_TUNING_SYNC_CAN_MUTATE   (1u << 5u)
#define RACE_WEAPON_TUNING_SYNC_CAN_BEGIN    (1u << 6u)
#define RACE_WEAPON_TUNING_SYNC_FLAGS_MASK   \
  (RACE_WEAPON_TUNING_SYNC_HAS_BASELINE | \
   RACE_WEAPON_TUNING_SYNC_HAS_CURRENT | \
   RACE_WEAPON_TUNING_SYNC_HAS_PREVIOUS | \
   RACE_WEAPON_TUNING_SYNC_HAS_SLOT_A | \
   RACE_WEAPON_TUNING_SYNC_HAS_SLOT_B | \
   RACE_WEAPON_TUNING_SYNC_CAN_MUTATE | \
   RACE_WEAPON_TUNING_SYNC_CAN_BEGIN)

typedef struct {
  uint32_t request_id;
  uint64_t generation;
  uint64_t session_generation;
  uint64_t catalog_hash;
  uint64_t baseline_hash;
  uint64_t current_hash;
  uint64_t previous_hash;
  uint64_t slot_a_hash;
  uint64_t slot_b_hash;
  uint16_t flags;
  race_weapon_tuning_state_t state;
  char preset_key[RACE_WEAPON_TUNING_PRESET_KEY_SIZE];
  char identity[RACE_WEAPON_TUNING_IDENTITY_SIZE];
} race_weapon_tuning_sync_begin_t;

typedef struct {
  uint32_t request_id;
  uint64_t generation;
  uint16_t index;
  race_weapon_tuning_descriptor_t descriptor;
} race_weapon_tuning_catalog_entry_t;

typedef struct {
  uint32_t request_id;
  uint64_t generation;
  uint64_t hash;
  race_weapon_tuning_snapshot_kind_t kind;
  race_weapon_tuning_snapshot_t snapshot;
} race_weapon_tuning_snapshot_message_t;

typedef struct {
  uint32_t request_id;
  uint64_t generation;
  uint64_t catalog_hash;
  uint16_t descriptor_count;
} race_weapon_tuning_sync_end_t;

typedef struct {
  uint32_t request_id;
  uint64_t generation;
  uint64_t hash;
  race_weapon_tuning_operation_t operation;
  race_weapon_tuning_result_t result;
  char preset_key[RACE_WEAPON_TUNING_PRESET_KEY_SIZE];
  char text[RACE_WEAPON_TUNING_RESULT_TEXT_SIZE];
} race_weapon_tuning_result_message_t;

const char *Race_WeaponTuning_StateToken(race_weapon_tuning_state_t state);
bool Race_WeaponTuning_StatusEncode(
  const race_weapon_tuning_status_t *status, char *output, size_t capacity);
bool Race_WeaponTuning_StatusDecode(
  const char *input, race_weapon_tuning_status_t *status);

size_t Race_WeaponTuningWire_EncodeSyncBegin(
  const race_weapon_tuning_sync_begin_t *message,
  void *output, size_t capacity);
bool Race_WeaponTuningWire_DecodeSyncBegin(
  const void *data, size_t length, race_weapon_tuning_sync_begin_t *message);
size_t Race_WeaponTuningWire_EncodeCatalogEntry(
  const race_weapon_tuning_catalog_entry_t *message,
  void *output, size_t capacity);
bool Race_WeaponTuningWire_DecodeCatalogEntry(
  const void *data, size_t length, race_weapon_tuning_catalog_entry_t *message);
size_t Race_WeaponTuningWire_EncodeSnapshot(
  const race_weapon_tuning_snapshot_message_t *message,
  void *output, size_t capacity);
bool Race_WeaponTuningWire_DecodeSnapshot(
  const void *data, size_t length,
  race_weapon_tuning_snapshot_message_t *message);
size_t Race_WeaponTuningWire_EncodeSyncEnd(
  const race_weapon_tuning_sync_end_t *message,
  void *output, size_t capacity);
bool Race_WeaponTuningWire_DecodeSyncEnd(
  const void *data, size_t length, race_weapon_tuning_sync_end_t *message);
size_t Race_WeaponTuningWire_EncodeResult(
  const race_weapon_tuning_result_message_t *message,
  void *output, size_t capacity);
bool Race_WeaponTuningWire_DecodeResult(
  const void *data, size_t length, race_weapon_tuning_result_message_t *message);
