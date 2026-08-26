/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_weapon_tuning_wire.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RACE_WEAPON_TUNING_WIRE_HEADER_BYTES 16u
#define RACE_WEAPON_TUNING_SYNC_BEGIN_BYTES 77u
#define RACE_WEAPON_TUNING_CATALOG_HEADER_BYTES 42u
#define RACE_WEAPON_TUNING_SNAPSHOT_BYTES \
  (28u + RACE_WEAPON_TUNING_VALUE_COUNT * 4u)
#define RACE_WEAPON_TUNING_SYNC_END_BYTES 28u
#define RACE_WEAPON_TUNING_RESULT_HEADER_BYTES 28u

static void Race_WeaponTuningWire_Write16(uint8_t *output,
                                         const uint16_t value) {
  output[0] = (uint8_t) value;
  output[1] = (uint8_t) (value >> 8u);
}

static void Race_WeaponTuningWire_Write32(uint8_t *output,
                                         const uint32_t value) {
  output[0] = (uint8_t) value;
  output[1] = (uint8_t) (value >> 8u);
  output[2] = (uint8_t) (value >> 16u);
  output[3] = (uint8_t) (value >> 24u);
}

static void Race_WeaponTuningWire_Write64(uint8_t *output,
                                         const uint64_t value) {
  Race_WeaponTuningWire_Write32(output, (uint32_t) value);
  Race_WeaponTuningWire_Write32(output + 4u, (uint32_t) (value >> 32u));
}

static uint16_t Race_WeaponTuningWire_Read16(const uint8_t *input) {
  return (uint16_t) input[0] | (uint16_t) input[1] << 8u;
}

static uint32_t Race_WeaponTuningWire_Read32(const uint8_t *input) {
  return (uint32_t) input[0] |
         (uint32_t) input[1] << 8u |
         (uint32_t) input[2] << 16u |
         (uint32_t) input[3] << 24u;
}

static uint64_t Race_WeaponTuningWire_Read64(const uint8_t *input) {
  return (uint64_t) Race_WeaponTuningWire_Read32(input) |
         (uint64_t) Race_WeaponTuningWire_Read32(input + 4u) << 32u;
}

static bool Race_WeaponTuningWire_String(const char *string,
                                        const size_t maximum,
                                        size_t *length) {
  if (!string) {
    return false;
  }
  size_t len = 0u;
  while (len <= maximum && string[len]) {
    const unsigned char c = (unsigned char) string[len];
    if (c < 0x20u || c > 0x7eu || c == '\\') {
      return false;
    }
    len++;
  }
  if (!len || len > maximum) {
    return false;
  }
  if (length) {
    *length = len;
  }
  return true;
}

static bool Race_WeaponTuningWire_Header(
    const race_weapon_tuning_message_op_t op, const uint32_t request_id,
    const uint64_t generation, void *output, const size_t capacity,
    const size_t length) {
  if (!output || op <= 0 || op >= RACE_WEAPON_TUNING_MESSAGE_TOTAL ||
      length < RACE_WEAPON_TUNING_WIRE_HEADER_BYTES ||
      length > capacity || length > RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD) {
    return false;
  }
  uint8_t *bytes = output;
  memset(bytes, 0, length);
  bytes[0] = RACE_WEAPON_TUNING_WIRE_VERSION;
  bytes[1] = (uint8_t) op;
  Race_WeaponTuningWire_Write32(bytes + 4u, request_id);
  Race_WeaponTuningWire_Write64(bytes + 8u, generation);
  return true;
}

static bool Race_WeaponTuningWire_ReadHeader(
    const void *data, const size_t length,
    const race_weapon_tuning_message_op_t op,
    uint32_t *request_id, uint64_t *generation) {
  if (!data || !request_id || !generation ||
      length < RACE_WEAPON_TUNING_WIRE_HEADER_BYTES ||
      length > RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD) {
    return false;
  }
  const uint8_t *bytes = data;
  if (bytes[0] != RACE_WEAPON_TUNING_WIRE_VERSION ||
      bytes[1] != (uint8_t) op || bytes[2] || bytes[3]) {
    return false;
  }
  *request_id = Race_WeaponTuningWire_Read32(bytes + 4u);
  *generation = Race_WeaponTuningWire_Read64(bytes + 8u);
  return true;
}

static bool Race_WeaponTuningWire_Identity(const char *identity,
                                          size_t *length) {
  if (!Race_WeaponTuningWire_String(identity,
                                    RACE_WEAPON_TUNING_IDENTITY_MAX,
                                    length)) {
    return false;
  }
  for (size_t i = 0u; identity[i]; i++) {
    const unsigned char c = (unsigned char) identity[i];
    if (!isalnum(c) && c != '-' && c != '_' && c != '.' && c != ':') {
      return false;
    }
  }
  return true;
}

static bool Race_WeaponTuningWire_PresetKey(const char *key,
                                            size_t *length) {
  if (!Race_WeaponTuningWire_String(key,
                                    RACE_WEAPON_TUNING_PRESET_KEY_MAX,
                                    length)) {
    return false;
  }
  for (size_t i = 0u; key[i]; i++) {
    const unsigned char c = (unsigned char) key[i];
    if (!isalnum(c) && c != '-' && c != '_' && c != '.') {
      return false;
    }
  }
  return true;
}

const char *Race_WeaponTuning_StateToken(
    const race_weapon_tuning_state_t state) {
  switch (state) {
    case RACE_WEAPON_TUNING_STATE_INACTIVE:
      return "inactive";
    case RACE_WEAPON_TUNING_STATE_ACTIVE:
      return "active";
    case RACE_WEAPON_TUNING_STATE_TRANSITION:
      return "transition";
    case RACE_WEAPON_TUNING_STATE_RECOVERY:
      return "recovery";
    case RACE_WEAPON_TUNING_STATE_ERROR:
      return "error";
    default:
      return NULL;
  }
}

static bool Race_WeaponTuning_ParseDecimal(const char *text,
                                          uint64_t *value) {
  if (!text || !*text || !value || (text[0] == '0' && text[1])) {
    return false;
  }
  uint64_t parsed = 0u;
  for (const unsigned char *c = (const unsigned char *) text; *c; c++) {
    if (*c < '0' || *c > '9') {
      return false;
    }
    const uint64_t digit = (uint64_t) (*c - '0');
    if (parsed > (UINT64_MAX - digit) / 10u) {
      return false;
    }
    parsed = parsed * 10u + digit;
  }
  *value = parsed;
  return true;
}

static bool Race_WeaponTuning_ParseHash(const char *text, uint64_t *value) {
  if (!text || !value || strlen(text) != 16u) {
    return false;
  }
  uint64_t parsed = 0u;
  for (size_t i = 0u; i < 16u; i++) {
    const unsigned char c = (unsigned char) text[i];
    uint64_t digit;
    if (c >= '0' && c <= '9') {
      digit = (uint64_t) (c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = (uint64_t) (c - 'a' + 10u);
    } else {
      return false;
    }
    parsed = parsed << 4u | digit;
  }
  *value = parsed;
  return true;
}

bool Race_WeaponTuning_StatusEncode(
    const race_weapon_tuning_status_t *status,
    char *output, const size_t capacity) {
  if (!status || !output || !capacity ||
      status->state >= RACE_WEAPON_TUNING_STATE_TOTAL ||
      !Race_WeaponTuningWire_PresetKey(status->preset_key, NULL) ||
      !Race_WeaponTuningWire_Identity(status->identity, NULL) ||
      !isfinite(status->hyper_climb_range) ||
      status->hyper_climb_range < 0.f ||
      status->hyper_climb_range > 128.f ||
      floorf(status->hyper_climb_range) != status->hyper_climb_range) {
    return false;
  }
  if (status->state == RACE_WEAPON_TUNING_STATE_INACTIVE &&
      (!status->hash || strcmp(status->identity, "baseline"))) {
    return false;
  }
  if (status->state == RACE_WEAPON_TUNING_STATE_ACTIVE &&
      (!status->generation || !status->hash ||
       strcmp(status->identity, "custom"))) {
    return false;
  }
  const char *state = Race_WeaponTuning_StateToken(status->state);
  const int32_t written = state ? snprintf(
    output, capacity, "v2\\%s\\%llu\\%016llx\\%s\\%s\\%.0f",
    state, (unsigned long long) status->generation,
    (unsigned long long) status->hash, status->preset_key, status->identity,
    status->hyper_climb_range) : -1;
  return written >= 0 && (size_t) written < capacity &&
         (size_t) written <= RACE_WEAPON_TUNING_STATUS_MAX;
}

bool Race_WeaponTuning_StatusDecode(
    const char *input, race_weapon_tuning_status_t *status) {
  if (!input || !status || strlen(input) > RACE_WEAPON_TUNING_STATUS_MAX) {
    return false;
  }
  char copy[RACE_WEAPON_TUNING_STATUS_SIZE];
  memcpy(copy, input, strlen(input) + 1u);
  char *fields[7] = { 0 };
  size_t field_count = 0u;
  fields[field_count++] = copy;
  for (char *c = copy; *c; c++) {
    if (*c == '\\') {
      *c = '\0';
      if (field_count == 7u) {
        return false;
      }
      fields[field_count++] = c + 1;
    }
  }
  if (field_count != 7u || strcmp(fields[0], "v2")) {
    return false;
  }

  race_weapon_tuning_status_t parsed = { 0 };
  for (parsed.state = RACE_WEAPON_TUNING_STATE_INACTIVE;
       parsed.state < RACE_WEAPON_TUNING_STATE_TOTAL; parsed.state++) {
    if (!strcmp(fields[1], Race_WeaponTuning_StateToken(parsed.state))) {
      break;
    }
  }
  uint64_t climb;
  if (parsed.state == RACE_WEAPON_TUNING_STATE_TOTAL ||
      !Race_WeaponTuning_ParseDecimal(fields[2], &parsed.generation) ||
      !Race_WeaponTuning_ParseHash(fields[3], &parsed.hash) ||
      !Race_WeaponTuningWire_PresetKey(fields[4], NULL) ||
      !Race_WeaponTuningWire_Identity(fields[5], NULL) ||
      !Race_WeaponTuning_ParseDecimal(fields[6], &climb) || climb > 128u) {
    return false;
  }
  memcpy(parsed.preset_key, fields[4], strlen(fields[4]) + 1u);
  memcpy(parsed.identity, fields[5], strlen(fields[5]) + 1u);
  parsed.hyper_climb_range = (float) climb;

  char canonical[RACE_WEAPON_TUNING_STATUS_SIZE];
  if (!Race_WeaponTuning_StatusEncode(&parsed, canonical,
                                      sizeof(canonical)) ||
      strcmp(canonical, input)) {
    return false;
  }
  *status = parsed;
  return true;
}

size_t Race_WeaponTuningWire_EncodeSyncBegin(
    const race_weapon_tuning_sync_begin_t *message,
    void *output, const size_t capacity) {
  size_t identity_length;
  size_t preset_length;
  if (!message || message->state >= RACE_WEAPON_TUNING_STATE_TOTAL ||
      message->flags & ~RACE_WEAPON_TUNING_SYNC_FLAGS_MASK ||
      !Race_WeaponTuningWire_PresetKey(message->preset_key, &preset_length) ||
      !Race_WeaponTuningWire_Identity(message->identity, &identity_length)) {
    return 0u;
  }
  const size_t length = RACE_WEAPON_TUNING_SYNC_BEGIN_BYTES + preset_length +
                        identity_length;
  if (!Race_WeaponTuningWire_Header(
        RACE_WEAPON_TUNING_MESSAGE_SYNC_BEGIN, message->request_id,
        message->generation, output, capacity, length)) {
    return 0u;
  }
  uint8_t *bytes = output;
  Race_WeaponTuningWire_Write64(bytes + 16u, message->session_generation);
  Race_WeaponTuningWire_Write64(bytes + 24u, message->catalog_hash);
  Race_WeaponTuningWire_Write64(bytes + 32u, message->baseline_hash);
  Race_WeaponTuningWire_Write64(bytes + 40u, message->current_hash);
  Race_WeaponTuningWire_Write64(bytes + 48u, message->previous_hash);
  Race_WeaponTuningWire_Write64(bytes + 56u, message->slot_a_hash);
  Race_WeaponTuningWire_Write64(bytes + 64u, message->slot_b_hash);
  Race_WeaponTuningWire_Write16(bytes + 72u, message->flags);
  bytes[74] = (uint8_t) message->state;
  bytes[75] = (uint8_t) preset_length;
  bytes[76] = (uint8_t) identity_length;
  uint8_t *cursor = bytes + RACE_WEAPON_TUNING_SYNC_BEGIN_BYTES;
  memcpy(cursor, message->preset_key, preset_length);
  memcpy(cursor + preset_length, message->identity, identity_length);
  return length;
}

bool Race_WeaponTuningWire_DecodeSyncBegin(
    const void *data, const size_t length,
    race_weapon_tuning_sync_begin_t *message) {
  if (!data || !message || length < RACE_WEAPON_TUNING_SYNC_BEGIN_BYTES) {
    return false;
  }
  const uint8_t *bytes = data;
  const size_t preset_length = bytes[75];
  const size_t identity_length = bytes[76];
  if (!preset_length || preset_length > RACE_WEAPON_TUNING_PRESET_KEY_MAX ||
      !identity_length || identity_length > RACE_WEAPON_TUNING_IDENTITY_MAX ||
      length != RACE_WEAPON_TUNING_SYNC_BEGIN_BYTES + preset_length +
                identity_length) {
    return false;
  }
  race_weapon_tuning_sync_begin_t parsed = { 0 };
  if (!Race_WeaponTuningWire_ReadHeader(
        data, length, RACE_WEAPON_TUNING_MESSAGE_SYNC_BEGIN,
        &parsed.request_id, &parsed.generation)) {
    return false;
  }
  parsed.session_generation = Race_WeaponTuningWire_Read64(bytes + 16u);
  parsed.catalog_hash = Race_WeaponTuningWire_Read64(bytes + 24u);
  parsed.baseline_hash = Race_WeaponTuningWire_Read64(bytes + 32u);
  parsed.current_hash = Race_WeaponTuningWire_Read64(bytes + 40u);
  parsed.previous_hash = Race_WeaponTuningWire_Read64(bytes + 48u);
  parsed.slot_a_hash = Race_WeaponTuningWire_Read64(bytes + 56u);
  parsed.slot_b_hash = Race_WeaponTuningWire_Read64(bytes + 64u);
  parsed.flags = Race_WeaponTuningWire_Read16(bytes + 72u);
  parsed.state = (race_weapon_tuning_state_t) bytes[74];
  const uint8_t *cursor = bytes + RACE_WEAPON_TUNING_SYNC_BEGIN_BYTES;
  memcpy(parsed.preset_key, cursor, preset_length);
  memcpy(parsed.identity, cursor + preset_length, identity_length);
  if (parsed.state >= RACE_WEAPON_TUNING_STATE_TOTAL ||
      parsed.flags & ~RACE_WEAPON_TUNING_SYNC_FLAGS_MASK ||
      !Race_WeaponTuningWire_PresetKey(parsed.preset_key, NULL) ||
      !Race_WeaponTuningWire_Identity(parsed.identity, NULL)) {
    return false;
  }
  *message = parsed;
  return true;
}

static bool Race_WeaponTuningWire_ScalarValid(
    const race_weapon_tuning_type_t type,
    const race_weapon_tuning_scalar_t scalar) {
  return type != RACE_WEAPON_TUNING_TYPE_FLOAT || isfinite(scalar.real);
}

static uint32_t Race_WeaponTuningWire_ScalarBits(
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

static race_weapon_tuning_scalar_t Race_WeaponTuningWire_Scalar(
    const race_weapon_tuning_type_t type, const uint32_t bits) {
  race_weapon_tuning_scalar_t scalar = { 0 };
  if (type == RACE_WEAPON_TUNING_TYPE_INT32) {
    memcpy(&scalar.integer, &bits, sizeof(bits));
  } else if (type == RACE_WEAPON_TUNING_TYPE_UINT32) {
    scalar.unsigned_integer = bits;
  } else {
    memcpy(&scalar.real, &bits, sizeof(bits));
  }
  return scalar;
}

static bool Race_WeaponTuningWire_Descriptor(
    const race_weapon_tuning_descriptor_t *descriptor,
    size_t lengths[5]) {
  if (!descriptor || descriptor->id < 0 ||
      descriptor->id >= RACE_WEAPON_TUNING_VALUE_TOTAL ||
      descriptor->group < 0 ||
      descriptor->group >= RACE_WEAPON_TUNING_GROUP_TOTAL ||
      descriptor->type < 0 ||
      descriptor->type >= RACE_WEAPON_TUNING_TYPE_TOTAL ||
      !Race_WeaponTuningWire_String(descriptor->key,
                                    RACE_WEAPON_TUNING_KEY_MAX,
                                    lengths) ||
      !Race_WeaponTuningWire_String(descriptor->group_key,
                                    RACE_WEAPON_TUNING_GROUP_KEY_MAX,
                                    lengths + 1u) ||
      !Race_WeaponTuningWire_String(descriptor->group_label,
                                    RACE_WEAPON_TUNING_LABEL_MAX,
                                    lengths + 2u) ||
      !Race_WeaponTuningWire_String(descriptor->label,
                                    RACE_WEAPON_TUNING_LABEL_MAX,
                                    lengths + 3u) ||
      !Race_WeaponTuningWire_String(descriptor->unit,
                                    RACE_WEAPON_TUNING_UNIT_MAX,
                                    lengths + 4u) ||
      !Race_WeaponTuningWire_ScalarValid(
        descriptor->type, descriptor->compiled_default) ||
      !Race_WeaponTuningWire_ScalarValid(
        descriptor->type, descriptor->minimum) ||
      !Race_WeaponTuningWire_ScalarValid(
        descriptor->type, descriptor->maximum) ||
      !Race_WeaponTuningWire_ScalarValid(
        descriptor->type, descriptor->step)) {
    return false;
  }
  return true;
}

size_t Race_WeaponTuningWire_EncodeCatalogEntry(
    const race_weapon_tuning_catalog_entry_t *message,
    void *output, const size_t capacity) {
  size_t lengths[5];
  if (!message || message->index >= RACE_WEAPON_TUNING_VALUE_COUNT ||
      message->descriptor.id != (race_weapon_tuning_id_t) message->index ||
      !Race_WeaponTuningWire_Descriptor(&message->descriptor, lengths)) {
    return 0u;
  }
  size_t string_bytes = 0u;
  for (size_t i = 0u; i < 5u; i++) {
    string_bytes += lengths[i];
  }
  const size_t length = RACE_WEAPON_TUNING_CATALOG_HEADER_BYTES +
                        string_bytes;
  if (!Race_WeaponTuningWire_Header(
        RACE_WEAPON_TUNING_MESSAGE_CATALOG_ENTRY, message->request_id,
        message->generation, output, capacity, length)) {
    return 0u;
  }
  uint8_t *bytes = output;
  Race_WeaponTuningWire_Write16(bytes + 16u, message->index);
  bytes[18] = (uint8_t) message->descriptor.id;
  bytes[19] = (uint8_t) message->descriptor.group;
  bytes[20] = (uint8_t) message->descriptor.type;
  for (size_t i = 0u; i < 5u; i++) {
    bytes[21u + i] = (uint8_t) lengths[i];
  }
  Race_WeaponTuningWire_Write32(
    bytes + 26u, Race_WeaponTuningWire_ScalarBits(
      message->descriptor.type, message->descriptor.compiled_default));
  Race_WeaponTuningWire_Write32(
    bytes + 30u, Race_WeaponTuningWire_ScalarBits(
      message->descriptor.type, message->descriptor.minimum));
  Race_WeaponTuningWire_Write32(
    bytes + 34u, Race_WeaponTuningWire_ScalarBits(
      message->descriptor.type, message->descriptor.maximum));
  Race_WeaponTuningWire_Write32(
    bytes + 38u, Race_WeaponTuningWire_ScalarBits(
      message->descriptor.type, message->descriptor.step));
  const char *strings[5] = {
    message->descriptor.key, message->descriptor.group_key,
    message->descriptor.group_label, message->descriptor.label,
    message->descriptor.unit
  };
  uint8_t *cursor = bytes + RACE_WEAPON_TUNING_CATALOG_HEADER_BYTES;
  for (size_t i = 0u; i < 5u; i++) {
    memcpy(cursor, strings[i], lengths[i]);
    cursor += lengths[i];
  }
  return length;
}

bool Race_WeaponTuningWire_DecodeCatalogEntry(
    const void *data, const size_t length,
    race_weapon_tuning_catalog_entry_t *message) {
  if (!data || !message ||
      length < RACE_WEAPON_TUNING_CATALOG_HEADER_BYTES) {
    return false;
  }
  const uint8_t *bytes = data;
  size_t lengths[5];
  size_t string_bytes = 0u;
  for (size_t i = 0u; i < 5u; i++) {
    lengths[i] = bytes[21u + i];
    string_bytes += lengths[i];
  }
  if (length != RACE_WEAPON_TUNING_CATALOG_HEADER_BYTES + string_bytes ||
      lengths[0] > RACE_WEAPON_TUNING_KEY_MAX ||
      lengths[1] > RACE_WEAPON_TUNING_GROUP_KEY_MAX ||
      lengths[2] > RACE_WEAPON_TUNING_LABEL_MAX ||
      lengths[3] > RACE_WEAPON_TUNING_LABEL_MAX ||
      lengths[4] > RACE_WEAPON_TUNING_UNIT_MAX) {
    return false;
  }
  race_weapon_tuning_catalog_entry_t parsed = { 0 };
  if (!Race_WeaponTuningWire_ReadHeader(
        data, length, RACE_WEAPON_TUNING_MESSAGE_CATALOG_ENTRY,
        &parsed.request_id, &parsed.generation)) {
    return false;
  }
  parsed.index = Race_WeaponTuningWire_Read16(bytes + 16u);
  parsed.descriptor.id = (race_weapon_tuning_id_t) bytes[18];
  parsed.descriptor.group = (race_weapon_tuning_group_t) bytes[19];
  parsed.descriptor.type = (race_weapon_tuning_type_t) bytes[20];
  parsed.descriptor.compiled_default = Race_WeaponTuningWire_Scalar(
    parsed.descriptor.type, Race_WeaponTuningWire_Read32(bytes + 26u));
  parsed.descriptor.minimum = Race_WeaponTuningWire_Scalar(
    parsed.descriptor.type, Race_WeaponTuningWire_Read32(bytes + 30u));
  parsed.descriptor.maximum = Race_WeaponTuningWire_Scalar(
    parsed.descriptor.type, Race_WeaponTuningWire_Read32(bytes + 34u));
  parsed.descriptor.step = Race_WeaponTuningWire_Scalar(
    parsed.descriptor.type, Race_WeaponTuningWire_Read32(bytes + 38u));
  char *strings[5] = {
    parsed.descriptor.key, parsed.descriptor.group_key,
    parsed.descriptor.group_label, parsed.descriptor.label,
    parsed.descriptor.unit
  };
  const uint8_t *cursor = bytes + RACE_WEAPON_TUNING_CATALOG_HEADER_BYTES;
  for (size_t i = 0u; i < 5u; i++) {
    if (!lengths[i]) {
      return false;
    }
    memcpy(strings[i], cursor, lengths[i]);
    cursor += lengths[i];
  }
  if (parsed.index >= RACE_WEAPON_TUNING_VALUE_COUNT ||
      parsed.descriptor.id != (race_weapon_tuning_id_t) parsed.index ||
      !Race_WeaponTuningWire_Descriptor(&parsed.descriptor, lengths)) {
    return false;
  }
  *message = parsed;
  return true;
}

size_t Race_WeaponTuningWire_EncodeSnapshot(
    const race_weapon_tuning_snapshot_message_t *message,
    void *output, const size_t capacity) {
  if (!message || message->kind <= 0 ||
      message->kind >= RACE_WEAPON_TUNING_SNAPSHOT_TOTAL || !message->hash ||
      !Race_WeaponTuningWire_Header(
        RACE_WEAPON_TUNING_MESSAGE_SNAPSHOT, message->request_id,
        message->generation, output, capacity,
        RACE_WEAPON_TUNING_SNAPSHOT_BYTES)) {
    return 0u;
  }
  uint8_t *bytes = output;
  bytes[16] = (uint8_t) message->kind;
  bytes[17] = RACE_WEAPON_TUNING_VALUE_COUNT;
  Race_WeaponTuningWire_Write64(bytes + 20u, message->hash);
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
    uint32_t bits;
    memcpy(&bits, message->snapshot.values + i, sizeof(bits));
    Race_WeaponTuningWire_Write32(bytes + 28u + i * 4u, bits);
  }
  return RACE_WEAPON_TUNING_SNAPSHOT_BYTES;
}

bool Race_WeaponTuningWire_DecodeSnapshot(
    const void *data, const size_t length,
    race_weapon_tuning_snapshot_message_t *message) {
  if (!message || length != RACE_WEAPON_TUNING_SNAPSHOT_BYTES) {
    return false;
  }
  const uint8_t *bytes = data;
  race_weapon_tuning_snapshot_message_t parsed = { 0 };
  if (!Race_WeaponTuningWire_ReadHeader(
        data, length, RACE_WEAPON_TUNING_MESSAGE_SNAPSHOT,
        &parsed.request_id, &parsed.generation) || bytes[17] !=
          RACE_WEAPON_TUNING_VALUE_COUNT || bytes[18] || bytes[19]) {
    return false;
  }
  parsed.kind = (race_weapon_tuning_snapshot_kind_t) bytes[16];
  parsed.hash = Race_WeaponTuningWire_Read64(bytes + 20u);
  if (parsed.kind <= 0 || parsed.kind >= RACE_WEAPON_TUNING_SNAPSHOT_TOTAL ||
      !parsed.hash) {
    return false;
  }
  for (size_t i = 0u; i < RACE_WEAPON_TUNING_VALUE_COUNT; i++) {
    const uint32_t bits = Race_WeaponTuningWire_Read32(
      bytes + 28u + i * 4u);
    memcpy(parsed.snapshot.values + i, &bits, sizeof(bits));
  }
  *message = parsed;
  return true;
}

size_t Race_WeaponTuningWire_EncodeSyncEnd(
    const race_weapon_tuning_sync_end_t *message,
    void *output, const size_t capacity) {
  if (!message || !message->catalog_hash ||
      message->descriptor_count != RACE_WEAPON_TUNING_VALUE_COUNT ||
      !Race_WeaponTuningWire_Header(
        RACE_WEAPON_TUNING_MESSAGE_SYNC_END, message->request_id,
        message->generation, output, capacity,
        RACE_WEAPON_TUNING_SYNC_END_BYTES)) {
    return 0u;
  }
  uint8_t *bytes = output;
  Race_WeaponTuningWire_Write64(bytes + 16u, message->catalog_hash);
  Race_WeaponTuningWire_Write16(bytes + 24u, message->descriptor_count);
  return RACE_WEAPON_TUNING_SYNC_END_BYTES;
}

bool Race_WeaponTuningWire_DecodeSyncEnd(
    const void *data, const size_t length,
    race_weapon_tuning_sync_end_t *message) {
  if (!message || length != RACE_WEAPON_TUNING_SYNC_END_BYTES) {
    return false;
  }
  const uint8_t *bytes = data;
  race_weapon_tuning_sync_end_t parsed = { 0 };
  if (!Race_WeaponTuningWire_ReadHeader(
        data, length, RACE_WEAPON_TUNING_MESSAGE_SYNC_END,
        &parsed.request_id, &parsed.generation) || bytes[26] || bytes[27]) {
    return false;
  }
  parsed.catalog_hash = Race_WeaponTuningWire_Read64(bytes + 16u);
  parsed.descriptor_count = Race_WeaponTuningWire_Read16(bytes + 24u);
  if (!parsed.catalog_hash ||
      parsed.descriptor_count != RACE_WEAPON_TUNING_VALUE_COUNT) {
    return false;
  }
  *message = parsed;
  return true;
}

size_t Race_WeaponTuningWire_EncodeResult(
    const race_weapon_tuning_result_message_t *message,
    void *output, const size_t capacity) {
  size_t preset_length;
  size_t text_length;
  if (!message || message->operation <= RACE_WEAPON_TUNING_OPERATION_NONE ||
      message->operation >= RACE_WEAPON_TUNING_OPERATION_TOTAL ||
      message->result >= RACE_WEAPON_TUNING_RESULT_TOTAL ||
      !Race_WeaponTuningWire_PresetKey(message->preset_key, &preset_length) ||
      !Race_WeaponTuningWire_String(message->text,
                                    RACE_WEAPON_TUNING_RESULT_TEXT_MAX,
                                    &text_length)) {
    return 0u;
  }
  const size_t length = RACE_WEAPON_TUNING_RESULT_HEADER_BYTES +
                        preset_length + text_length;
  if (!Race_WeaponTuningWire_Header(
        RACE_WEAPON_TUNING_MESSAGE_RESULT, message->request_id,
        message->generation, output, capacity, length)) {
    return 0u;
  }
  uint8_t *bytes = output;
  Race_WeaponTuningWire_Write64(bytes + 16u, message->hash);
  bytes[24] = (uint8_t) message->operation;
  bytes[25] = (uint8_t) message->result;
  bytes[26] = (uint8_t) text_length;
  bytes[27] = (uint8_t) preset_length;
  memcpy(bytes + RACE_WEAPON_TUNING_RESULT_HEADER_BYTES,
         message->preset_key, preset_length);
  memcpy(bytes + RACE_WEAPON_TUNING_RESULT_HEADER_BYTES + preset_length,
         message->text, text_length);
  return length;
}

bool Race_WeaponTuningWire_DecodeResult(
    const void *data, const size_t length,
    race_weapon_tuning_result_message_t *message) {
  if (!data || !message || length < RACE_WEAPON_TUNING_RESULT_HEADER_BYTES) {
    return false;
  }
  const uint8_t *bytes = data;
  const size_t text_length = bytes[26];
  const size_t preset_length = bytes[27];
  if (!text_length || text_length > RACE_WEAPON_TUNING_RESULT_TEXT_MAX ||
      !preset_length || preset_length > RACE_WEAPON_TUNING_PRESET_KEY_MAX ||
      length != RACE_WEAPON_TUNING_RESULT_HEADER_BYTES + preset_length +
                text_length) {
    return false;
  }
  race_weapon_tuning_result_message_t parsed = { 0 };
  if (!Race_WeaponTuningWire_ReadHeader(
        data, length, RACE_WEAPON_TUNING_MESSAGE_RESULT,
        &parsed.request_id, &parsed.generation)) {
    return false;
  }
  parsed.hash = Race_WeaponTuningWire_Read64(bytes + 16u);
  parsed.operation = (race_weapon_tuning_operation_t) bytes[24];
  parsed.result = (race_weapon_tuning_result_t) bytes[25];
  const uint8_t *cursor = bytes + RACE_WEAPON_TUNING_RESULT_HEADER_BYTES;
  memcpy(parsed.preset_key, cursor, preset_length);
  memcpy(parsed.text, cursor + preset_length, text_length);
  if (parsed.operation <= RACE_WEAPON_TUNING_OPERATION_NONE ||
      parsed.operation >= RACE_WEAPON_TUNING_OPERATION_TOTAL ||
      parsed.result >= RACE_WEAPON_TUNING_RESULT_TOTAL ||
      !Race_WeaponTuningWire_PresetKey(parsed.preset_key, NULL) ||
      !Race_WeaponTuningWire_String(parsed.text,
                                    RACE_WEAPON_TUNING_RESULT_TEXT_MAX,
                                    NULL)) {
    return false;
  }
  *message = parsed;
  return true;
}
