/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_admin.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char *data;
  size_t capacity;
  size_t length;
  bool failed;
} race_admin_writer_t;

static bool Race_Admin_BoundedLength(const char *text, size_t maximum,
                                     size_t *length) {
  if (!text) {
    return false;
  }

  const size_t bounded = strnlen(text, maximum + 1u);
  if (!bounded || bounded > maximum) {
    return false;
  }

  if (length) {
    *length = bounded;
  }
  return true;
}

static bool Race_Admin_CanonicalizeName(const char *input, size_t maximum,
                                        char *output, size_t output_size) {
  size_t length;
  if (!output || output_size <= maximum ||
      !Race_Admin_BoundedLength(input, maximum, &length)) {
    return false;
  }

  for (size_t i = 0; i < length; i++) {
    const unsigned char c = (unsigned char) input[i];
    if (!(isalnum(c) || c == '_' || c == '-')) {
      return false;
    }
    output[i] = (char) tolower(c);
  }
  output[length] = '\0';
  return true;
}

bool Race_Admin_CanonicalizeAccountId(const char *input,
                                      char output[RACE_ADMIN_ACCOUNT_ID_SIZE]) {
  return Race_Admin_CanonicalizeName(input, RACE_ADMIN_ACCOUNT_ID_MAX,
                                     output, RACE_ADMIN_ACCOUNT_ID_SIZE);
}

bool Race_Admin_CanonicalizeHandle(const char *input,
                                   char output[RACE_ADMIN_HANDLE_SIZE]) {
  return Race_Admin_CanonicalizeName(input, RACE_ADMIN_HANDLE_MAX,
                                     output, RACE_ADMIN_HANDLE_SIZE);
}

const char *Race_Admin_RoleName(race_admin_role_t role) {
  switch (role) {
    case RACE_ADMIN_ROLE_MODERATOR:
      return "moderator";
    case RACE_ADMIN_ROLE_OPERATOR:
      return "operator";
    case RACE_ADMIN_ROLE_OWNER:
      return "owner";
    case RACE_ADMIN_ROLE_NONE:
    case RACE_ADMIN_ROLE_TOTAL:
      break;
  }
  return "invalid";
}

race_admin_role_t Race_Admin_RoleForName(const char *name) {
  if (name) {
    if (!strcmp(name, "moderator")) {
      return RACE_ADMIN_ROLE_MODERATOR;
    }
    if (!strcmp(name, "operator")) {
      return RACE_ADMIN_ROLE_OPERATOR;
    }
    if (!strcmp(name, "owner")) {
      return RACE_ADMIN_ROLE_OWNER;
    }
  }
  return RACE_ADMIN_ROLE_NONE;
}

uint32_t Race_Admin_RoleCapabilities(race_admin_role_t role) {
  const uint32_t moderator = RACE_ADMIN_CAP_PLAYER_KICK |
                             RACE_ADMIN_CAP_PLAYER_BAN |
                             RACE_ADMIN_CAP_VOTE_ADMIN;
  const uint32_t operator = moderator |
                            RACE_ADMIN_CAP_SETTINGS_MUTATE |
                            RACE_ADMIN_CAP_MAP_CHANGE;

  switch (role) {
    case RACE_ADMIN_ROLE_MODERATOR:
      return moderator;
    case RACE_ADMIN_ROLE_OPERATOR:
      return operator;
    case RACE_ADMIN_ROLE_OWNER:
      return operator | RACE_ADMIN_CAP_ACCOUNT_MANAGE;
    case RACE_ADMIN_ROLE_NONE:
    case RACE_ADMIN_ROLE_TOTAL:
      return 0;
  }
  return 0;
}

const char *Race_Admin_CapabilityName(race_admin_capability_t capability) {
  switch (capability) {
    case RACE_ADMIN_CAP_SETTINGS_MUTATE:
      return "settings-mutate";
    case RACE_ADMIN_CAP_MAP_CHANGE:
      return "map-change";
    case RACE_ADMIN_CAP_PLAYER_KICK:
      return "player-kick";
    case RACE_ADMIN_CAP_PLAYER_BAN:
      return "player-ban";
    case RACE_ADMIN_CAP_VOTE_ADMIN:
      return "vote-admin";
    case RACE_ADMIN_CAP_ACCOUNT_MANAGE:
      return "account-manage";
    default:
      return "invalid";
  }
}

const char *Race_Admin_ActionName(race_admin_action_t action) {
  switch (action) {
    case RACE_ADMIN_ACTION_SETTINGS_MUTATE:
      return "settings";
    case RACE_ADMIN_ACTION_MAP_CHANGE:
      return "map";
    case RACE_ADMIN_ACTION_PLAYER_KICK:
      return "kick";
    case RACE_ADMIN_ACTION_VOTE_CANCEL:
      return "vote-cancel";
    case RACE_ADMIN_ACTION_TOTAL:
      break;
  }
  return "invalid";
}

race_admin_capability_t Race_Admin_ActionCapability(race_admin_action_t action) {
  switch (action) {
    case RACE_ADMIN_ACTION_SETTINGS_MUTATE:
      return RACE_ADMIN_CAP_SETTINGS_MUTATE;
    case RACE_ADMIN_ACTION_MAP_CHANGE:
      return RACE_ADMIN_CAP_MAP_CHANGE;
    case RACE_ADMIN_ACTION_PLAYER_KICK:
      return RACE_ADMIN_CAP_PLAYER_KICK;
    case RACE_ADMIN_ACTION_VOTE_CANCEL:
      return RACE_ADMIN_CAP_VOTE_ADMIN;
    case RACE_ADMIN_ACTION_TOTAL:
      break;
  }
  return 0;
}

bool Race_Admin_DocumentInit(race_admin_document_t *document) {
  if (!document) {
    return false;
  }
  memset(document, 0, sizeof(*document));
  return true;
}

bool Race_Admin_DocumentValid(const race_admin_document_t *document,
                              bool require_generation) {
  if (!document || document->count > RACE_ADMIN_MAX_ACCOUNTS ||
      (require_generation && !document->generation)) {
    return false;
  }

  for (size_t i = 0; i < document->count; i++) {
    const race_admin_account_t *account = document->accounts + i;
    char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
    char handle[RACE_ADMIN_HANDLE_SIZE];
    if (!Race_Admin_CanonicalizeAccountId(account->id, id) ||
        strcmp(id, account->id) ||
        !Race_Admin_CanonicalizeHandle(account->handle, handle) ||
        strcmp(handle, account->handle) ||
        account->role <= RACE_ADMIN_ROLE_NONE ||
        account->role >= RACE_ADMIN_ROLE_TOTAL ||
        !Race_Admin_RoleCapabilities(account->role) || !account->revision) {
      return false;
    }

    if (i && strcmp(document->accounts[i - 1u].id, account->id) >= 0) {
      return false;
    }
    for (size_t j = 0; j < i; j++) {
      if (!strcmp(document->accounts[j].handle, account->handle)) {
        return false;
      }
    }
  }
  return true;
}

bool Race_Admin_DocumentEquals(const race_admin_document_t *left,
                               const race_admin_document_t *right) {
  if (!Race_Admin_DocumentValid(left, false) ||
      !Race_Admin_DocumentValid(right, false) ||
      left->generation != right->generation || left->count != right->count) {
    return false;
  }

  for (size_t i = 0; i < left->count; i++) {
    const race_admin_account_t *left_account = left->accounts + i;
    const race_admin_account_t *right_account = right->accounts + i;
    if (strcmp(left_account->id, right_account->id) ||
        strcmp(left_account->handle, right_account->handle) ||
        left_account->role != right_account->role ||
        left_account->enabled != right_account->enabled ||
        left_account->revision != right_account->revision) {
      return false;
    }
  }
  return true;
}

static size_t Race_Admin_LowerBound(const race_admin_document_t *document,
                                    const char *id) {
  size_t begin = 0;
  size_t end = document->count;
  while (begin < end) {
    const size_t middle = begin + (end - begin) / 2u;
    if (strcmp(document->accounts[middle].id, id) < 0) {
      begin = middle + 1u;
    } else {
      end = middle;
    }
  }
  return begin;
}

const race_admin_account_t *Race_Admin_AccountById(
  const race_admin_document_t *document, const char *id) {
  char canonical[RACE_ADMIN_ACCOUNT_ID_SIZE];
  if (!Race_Admin_DocumentValid(document, false) ||
      !Race_Admin_CanonicalizeAccountId(id, canonical)) {
    return NULL;
  }

  const size_t index = Race_Admin_LowerBound(document, canonical);
  return index < document->count &&
         !strcmp(document->accounts[index].id, canonical)
    ? document->accounts + index
    : NULL;
}

const race_admin_account_t *Race_Admin_AccountByHandle(
  const race_admin_document_t *document, const char *handle) {
  char canonical[RACE_ADMIN_HANDLE_SIZE];
  if (!Race_Admin_DocumentValid(document, false) ||
      !Race_Admin_CanonicalizeHandle(handle, canonical)) {
    return NULL;
  }

  for (size_t i = 0; i < document->count; i++) {
    if (!strcmp(document->accounts[i].handle, canonical)) {
      return document->accounts + i;
    }
  }
  return NULL;
}

static bool Race_Admin_NextGeneration(const race_admin_document_t *current,
                                      race_admin_document_t *candidate) {
  if (!Race_Admin_DocumentValid(current, false) || !candidate ||
      current->generation == UINT64_MAX) {
    return false;
  }
  *candidate = *current;
  candidate->generation++;
  return true;
}

bool Race_Admin_AddAccount(const race_admin_document_t *current,
                           const char *id, const char *handle,
                           race_admin_role_t role,
                           race_admin_document_t *candidate) {
  char canonical_id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  char canonical_handle[RACE_ADMIN_HANDLE_SIZE];
  if (!Race_Admin_DocumentValid(current, false) ||
      current->count >= RACE_ADMIN_MAX_ACCOUNTS ||
      !Race_Admin_CanonicalizeAccountId(id, canonical_id) ||
      !Race_Admin_CanonicalizeHandle(handle, canonical_handle) ||
      role <= RACE_ADMIN_ROLE_NONE || role >= RACE_ADMIN_ROLE_TOTAL ||
      Race_Admin_AccountById(current, canonical_id) ||
      Race_Admin_AccountByHandle(current, canonical_handle) ||
      !Race_Admin_NextGeneration(current, candidate)) {
    return false;
  }

  const size_t index = Race_Admin_LowerBound(candidate, canonical_id);
  memmove(candidate->accounts + index + 1u,
          candidate->accounts + index,
          (candidate->count - index) * sizeof(*candidate->accounts));
  candidate->accounts[index] = (race_admin_account_t) {
    .role = role,
    .enabled = true,
    .revision = 1u
  };
  memcpy(candidate->accounts[index].id, canonical_id, sizeof(canonical_id));
  memcpy(candidate->accounts[index].handle, canonical_handle,
         sizeof(canonical_handle));
  candidate->count++;
  return true;
}

bool Race_Admin_RemoveAccount(const race_admin_document_t *current,
                              const char *id,
                              race_admin_document_t *candidate) {
  const race_admin_account_t *account = Race_Admin_AccountById(current, id);
  if (!account || !Race_Admin_NextGeneration(current, candidate)) {
    return false;
  }

  const size_t index = (size_t) (account - current->accounts);
  memmove(candidate->accounts + index, candidate->accounts + index + 1u,
          (candidate->count - index - 1u) * sizeof(*candidate->accounts));
  candidate->count--;
  memset(candidate->accounts + candidate->count, 0,
         sizeof(*candidate->accounts));
  return true;
}

static race_admin_account_t *Race_Admin_MutableAccount(
  const race_admin_document_t *current, const char *id,
  race_admin_document_t *candidate) {
  const race_admin_account_t *account = Race_Admin_AccountById(current, id);
  if (!account || account->revision == UINT64_MAX ||
      !Race_Admin_NextGeneration(current, candidate)) {
    return NULL;
  }
  return candidate->accounts + (account - current->accounts);
}

bool Race_Admin_SetAccountRole(const race_admin_document_t *current,
                               const char *id, race_admin_role_t role,
                               race_admin_document_t *candidate) {
  const race_admin_account_t *existing = Race_Admin_AccountById(current, id);
  if (!existing || role <= RACE_ADMIN_ROLE_NONE ||
      role >= RACE_ADMIN_ROLE_TOTAL || !candidate) {
    return false;
  }
  if (existing->role == role) {
    *candidate = *current;
    return true;
  }

  race_admin_account_t *account = Race_Admin_MutableAccount(current, id, candidate);
  if (!account) {
    return false;
  }
  account->role = role;
  account->revision++;
  return true;
}

bool Race_Admin_SetAccountEnabled(const race_admin_document_t *current,
                                  const char *id, bool enabled,
                                  race_admin_document_t *candidate) {
  const race_admin_account_t *existing = Race_Admin_AccountById(current, id);
  if (!existing || !candidate) {
    return false;
  }
  if (existing->enabled == enabled) {
    *candidate = *current;
    return true;
  }

  race_admin_account_t *account = Race_Admin_MutableAccount(current, id, candidate);
  if (!account) {
    return false;
  }
  account->enabled = enabled;
  account->revision++;
  return true;
}

bool Race_Admin_SetAccountHandle(const race_admin_document_t *current,
                                 const char *id, const char *handle,
                                 race_admin_document_t *candidate) {
  char canonical[RACE_ADMIN_HANDLE_SIZE];
  const race_admin_account_t *existing = Race_Admin_AccountById(current, id);
  if (!existing || !candidate ||
      !Race_Admin_CanonicalizeHandle(handle, canonical)) {
    return false;
  }
  const race_admin_account_t *duplicate = Race_Admin_AccountByHandle(current,
                                                                      canonical);
  if (duplicate && duplicate != existing) {
    return false;
  }
  if (!strcmp(existing->handle, canonical)) {
    *candidate = *current;
    return true;
  }

  race_admin_account_t *account = Race_Admin_MutableAccount(current, id, candidate);
  if (!account) {
    return false;
  }
  memcpy(account->handle, canonical, sizeof(canonical));
  account->revision++;
  return true;
}

void Race_Admin_SessionClear(race_admin_session_t *session) {
  if (session) {
    memset(session, 0, sizeof(*session));
  }
}

bool Race_Admin_SessionGrant(race_admin_session_t *session,
                             const race_admin_document_t *document,
                             const char *account_id) {
  if (!session) {
    return false;
  }
  Race_Admin_SessionClear(session);

  const race_admin_account_t *account = Race_Admin_AccountById(document,
                                                               account_id);
  if (!account || !account->enabled) {
    return false;
  }

  session->authenticated = true;
  memcpy(session->account_id, account->id, sizeof(session->account_id));
  session->account_revision = account->revision;
  session->capabilities = Race_Admin_RoleCapabilities(account->role);
  return true;
}

bool Race_Admin_SessionAuthenticated(const race_admin_session_t *session,
                                     const race_admin_document_t *document) {
  if (!session || !session->authenticated) {
    return false;
  }

  const race_admin_account_t *account = Race_Admin_AccountById(
    document, session->account_id);
  return account && account->enabled &&
         account->revision == session->account_revision &&
         session->capabilities == Race_Admin_RoleCapabilities(account->role);
}

bool Race_Admin_SessionHasCapability(const race_admin_session_t *session,
                                     const race_admin_document_t *document,
                                     race_admin_capability_t capability) {
  const uint32_t requested = (uint32_t) capability;
  return requested && !(requested & ~RACE_ADMIN_CAP_ALL) &&
         Race_Admin_SessionAuthenticated(session, document) &&
         (session->capabilities & requested) == requested;
}

bool Race_Admin_SessionCanPerform(const race_admin_session_t *session,
                                  const race_admin_document_t *document,
                                  race_admin_action_t action) {
  const race_admin_capability_t capability = Race_Admin_ActionCapability(action);
  return capability && Race_Admin_SessionHasCapability(session, document,
                                                        capability);
}

bool Race_Admin_ParseClientSlot(const char *text, int32_t max_clients,
                                int32_t *slot) {
  if (!text || !*text || max_clients <= 0 || !slot) {
    return false;
  }

  uint32_t value = 0;
  for (const char *cursor = text; *cursor; cursor++) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    const uint32_t digit = (uint32_t) (*cursor - '0');
    if (value > ((uint32_t) INT32_MAX - digit) / 10u) {
      return false;
    }
    value = value * 10u + digit;
  }

  if (value >= (uint32_t) max_clients) {
    return false;
  }

  *slot = (int32_t) value;
  return true;
}

race_admin_kick_target_result_t Race_Admin_ValidateKickTarget(
  int32_t requester_slot, int32_t target_slot, int32_t max_clients,
  bool target_connected) {
  if (requester_slot < 0 || requester_slot >= max_clients ||
      target_slot < 0 || target_slot >= max_clients) {
    return RACE_ADMIN_KICK_TARGET_INVALID;
  }
  if (requester_slot == target_slot) {
    return RACE_ADMIN_KICK_TARGET_SELF;
  }
  if (!target_connected) {
    return RACE_ADMIN_KICK_TARGET_UNAVAILABLE;
  }
  return RACE_ADMIN_KICK_TARGET_OK;
}

static void Race_Admin_Write(race_admin_writer_t *writer,
                             const char *format, ...) {
  if (writer->failed) {
    return;
  }

  va_list args;
  va_start(args, format);
  const int32_t length = vsnprintf(writer->data + writer->length,
                                   writer->capacity - writer->length,
                                   format, args);
  va_end(args);
  if (length < 0 || (size_t) length >= writer->capacity - writer->length) {
    writer->failed = true;
    return;
  }
  writer->length += (size_t) length;
}

static uint32_t Race_Admin_Crc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; i++) {
    crc ^= bytes[i];
    for (size_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                          (uint32_t) -(int32_t) (crc & 1u));
    }
  }
  return ~crc;
}

bool Race_Admin_Serialize(const race_admin_document_t *document,
                          char *output, size_t output_size,
                          size_t *output_length) {
  if (!Race_Admin_DocumentValid(document, true) || !output || !output_size) {
    return false;
  }

  race_admin_writer_t writer = {
    .data = output,
    .capacity = output_size
  };
  Race_Admin_Write(&writer, RACE_ADMIN_MAGIC "\n");
  Race_Admin_Write(&writer, "generation=%" PRIu64 "\n", document->generation);
  Race_Admin_Write(&writer, "credential=" RACE_ADMIN_CREDENTIAL_MODE "\n");
  Race_Admin_Write(&writer, "accounts=%zu\n", document->count);
  for (size_t i = 0; i < document->count; i++) {
    const race_admin_account_t *account = document->accounts + i;
    Race_Admin_Write(&writer, "account=%s|%s|%s|%u|%" PRIu64 "\n",
                     account->id, account->handle,
                     Race_Admin_RoleName(account->role),
                     account->enabled ? 1u : 0u, account->revision);
  }

  if (writer.failed) {
    return false;
  }
  const uint32_t crc = Race_Admin_Crc32(output, writer.length);
  Race_Admin_Write(&writer, "crc=%08" PRIx32 "\n", crc);
  if (writer.failed || writer.length > RACE_ADMIN_SERIALIZED_MAX) {
    return false;
  }

  if (output_length) {
    *output_length = writer.length;
  }
  return true;
}

static char *Race_Admin_NextLine(char **cursor) {
  if (!cursor || !*cursor || !**cursor) {
    return NULL;
  }

  char *line = *cursor;
  char *newline = strchr(line, '\n');
  if (!newline) {
    return NULL;
  }
  *newline = '\0';
  *cursor = newline + 1;
  return line;
}

static bool Race_Admin_ParseUint64(const char *text, uint64_t *value) {
  if (!text || !*text || !value || *text == '+' || *text == '-') {
    return false;
  }
  char *end;
  const uintmax_t parsed = strtoumax(text, &end, 10);
  if (*end || parsed > UINT64_MAX) {
    return false;
  }
  *value = (uint64_t) parsed;
  return true;
}

static bool Race_Admin_ParseSize(const char *text, size_t maximum,
                                 size_t *value) {
  uint64_t parsed;
  if (!Race_Admin_ParseUint64(text, &parsed) || parsed > maximum) {
    return false;
  }
  *value = (size_t) parsed;
  return true;
}

static int32_t Race_Admin_HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

static bool Race_Admin_ParseCrc(const char *text, uint32_t *value) {
  if (!text || strlen(text) != 8u || !value) {
    return false;
  }
  uint32_t parsed = 0;
  for (size_t i = 0; i < 8u; i++) {
    const int32_t digit = Race_Admin_HexValue(text[i]);
    if (digit < 0) {
      return false;
    }
    parsed = (parsed << 4) | (uint32_t) digit;
  }
  *value = parsed;
  return true;
}

static bool Race_Admin_ParseAccount(char *line, race_admin_account_t *account) {
  if (!line || strncmp(line, "account=", 8) || !account) {
    return false;
  }

  char *fields[5];
  fields[0] = line + 8;
  for (size_t i = 1; i < 5u; i++) {
    char *separator = strchr(fields[i - 1u], '|');
    if (!separator) {
      return false;
    }
    *separator = '\0';
    fields[i] = separator + 1;
  }
  if (strchr(fields[4], '|')) {
    return false;
  }

  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  char handle[RACE_ADMIN_HANDLE_SIZE];
  const race_admin_role_t role = Race_Admin_RoleForName(fields[2]);
  uint64_t revision;
  if (!Race_Admin_CanonicalizeAccountId(fields[0], id) ||
      strcmp(id, fields[0]) ||
      !Race_Admin_CanonicalizeHandle(fields[1], handle) ||
      strcmp(handle, fields[1]) || role == RACE_ADMIN_ROLE_NONE ||
      (strcmp(fields[3], "0") && strcmp(fields[3], "1")) ||
      !Race_Admin_ParseUint64(fields[4], &revision) || !revision) {
    return false;
  }

  memset(account, 0, sizeof(*account));
  memcpy(account->id, id, sizeof(id));
  memcpy(account->handle, handle, sizeof(handle));
  account->role = role;
  account->enabled = !strcmp(fields[3], "1");
  account->revision = revision;
  return true;
}

race_admin_parse_result_t Race_Admin_Parse(const void *data, size_t length,
                                           race_admin_document_t *document) {
  if (!data || !document || !length) {
    return RACE_ADMIN_PARSE_MALFORMED;
  }
  if (length > RACE_ADMIN_SERIALIZED_MAX) {
    return RACE_ADMIN_PARSE_TOO_LARGE;
  }

  char text[RACE_ADMIN_SERIALIZED_MAX + 1u];
  memcpy(text, data, length);
  text[length] = '\0';
  if (memchr(text, '\0', length)) {
    return RACE_ADMIN_PARSE_MALFORMED;
  }

  const char *first_newline = memchr(text, '\n', length);
  if (!first_newline) {
    return RACE_ADMIN_PARSE_MALFORMED;
  }
  const size_t first_length = (size_t) (first_newline - text);
  if (first_length != sizeof(RACE_ADMIN_MAGIC) - 1u ||
      memcmp(text, RACE_ADMIN_MAGIC, first_length)) {
    if (first_length >= sizeof(RACE_ADMIN_MAGIC_PREFIX) - 1u &&
        !memcmp(text, RACE_ADMIN_MAGIC_PREFIX,
                sizeof(RACE_ADMIN_MAGIC_PREFIX) - 1u)) {
      return RACE_ADMIN_PARSE_UNKNOWN_VERSION;
    }
    if (*text == '#' || strstr(text, "Race admin accounts")) {
      return RACE_ADMIN_PARSE_LEGACY;
    }
    return RACE_ADMIN_PARSE_MALFORMED;
  }

  char *crc_field = NULL;
  for (char *match = strstr(text, "\ncrc="); match;
       match = strstr(match + 1, "\ncrc=")) {
    if (crc_field) {
      return RACE_ADMIN_PARSE_MALFORMED;
    }
    crc_field = match + 1;
  }
  if (!crc_field || strlen(crc_field) != 13u || crc_field[12] != '\n') {
    return RACE_ADMIN_PARSE_MALFORMED;
  }

  uint32_t stored_crc;
  char crc_text[9];
  memcpy(crc_text, crc_field + 4, 8u);
  crc_text[8] = '\0';
  if (!Race_Admin_ParseCrc(crc_text, &stored_crc)) {
    return RACE_ADMIN_PARSE_MALFORMED;
  }
  const size_t prefix_length = (size_t) (crc_field - text);
  if (Race_Admin_Crc32(text, prefix_length) != stored_crc) {
    return RACE_ADMIN_PARSE_CHECKSUM;
  }

  char *cursor = text;
  char *line = Race_Admin_NextLine(&cursor);
  if (!line || strcmp(line, RACE_ADMIN_MAGIC)) {
    return RACE_ADMIN_PARSE_MALFORMED;
  }

  race_admin_document_t parsed;
  Race_Admin_DocumentInit(&parsed);

  line = Race_Admin_NextLine(&cursor);
  if (!line || strncmp(line, "generation=", 11) ||
      !Race_Admin_ParseUint64(line + 11, &parsed.generation) ||
      !parsed.generation) {
    return RACE_ADMIN_PARSE_MALFORMED;
  }

  line = Race_Admin_NextLine(&cursor);
  if (!line || strcmp(line, "credential=" RACE_ADMIN_CREDENTIAL_MODE)) {
    return RACE_ADMIN_PARSE_MALFORMED;
  }

  line = Race_Admin_NextLine(&cursor);
  if (!line || strncmp(line, "accounts=", 9) ||
      !Race_Admin_ParseSize(line + 9, RACE_ADMIN_MAX_ACCOUNTS,
                            &parsed.count)) {
    return RACE_ADMIN_PARSE_MALFORMED;
  }

  for (size_t i = 0; i < parsed.count; i++) {
    line = Race_Admin_NextLine(&cursor);
    if (!Race_Admin_ParseAccount(line, parsed.accounts + i)) {
      return RACE_ADMIN_PARSE_MALFORMED;
    }
  }

  line = Race_Admin_NextLine(&cursor);
  if (!line || strncmp(line, "crc=", 4) || strcmp(line + 4, crc_text) ||
      *cursor || !Race_Admin_DocumentValid(&parsed, true)) {
    return RACE_ADMIN_PARSE_MALFORMED;
  }

  *document = parsed;
  return RACE_ADMIN_PARSE_OK;
}

const char *Race_Admin_ParseResultName(race_admin_parse_result_t result) {
  switch (result) {
    case RACE_ADMIN_PARSE_OK:
      return "ok";
    case RACE_ADMIN_PARSE_MALFORMED:
      return "malformed";
    case RACE_ADMIN_PARSE_TOO_LARGE:
      return "too large";
    case RACE_ADMIN_PARSE_UNKNOWN_VERSION:
      return "unknown version";
    case RACE_ADMIN_PARSE_LEGACY:
      return "legacy reset required";
    case RACE_ADMIN_PARSE_CHECKSUM:
      return "checksum mismatch";
  }
  return "unknown";
}
