/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <stddef.h>

#include "race_admin_types.h"

#define RACE_ADMIN_MAGIC "QUETOO_RACE_ADMINS_V1"
#define RACE_ADMIN_MAGIC_PREFIX "QUETOO_RACE_ADMINS_"
#define RACE_ADMIN_CREDENTIAL_MODE "disabled"

#define RACE_ADMIN_MAX_ACCOUNTS 64
#define RACE_ADMIN_HANDLE_MAX 32
#define RACE_ADMIN_HANDLE_SIZE (RACE_ADMIN_HANDLE_MAX + 1)
#define RACE_ADMIN_SERIALIZED_MAX 16384

typedef struct {
  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  char handle[RACE_ADMIN_HANDLE_SIZE];
  race_admin_role_t role;
  bool enabled;
  uint64_t revision;
} race_admin_account_t;

typedef struct {
  uint64_t generation;
  size_t count;
  race_admin_account_t accounts[RACE_ADMIN_MAX_ACCOUNTS];
} race_admin_document_t;

typedef enum {
  RACE_ADMIN_PARSE_OK,
  RACE_ADMIN_PARSE_MALFORMED,
  RACE_ADMIN_PARSE_TOO_LARGE,
  RACE_ADMIN_PARSE_UNKNOWN_VERSION,
  RACE_ADMIN_PARSE_LEGACY,
  RACE_ADMIN_PARSE_CHECKSUM
} race_admin_parse_result_t;

bool Race_Admin_CanonicalizeAccountId(const char *input,
                                      char output[RACE_ADMIN_ACCOUNT_ID_SIZE]);
bool Race_Admin_CanonicalizeHandle(const char *input,
                                   char output[RACE_ADMIN_HANDLE_SIZE]);

const char *Race_Admin_RoleName(race_admin_role_t role);
race_admin_role_t Race_Admin_RoleForName(const char *name);
uint32_t Race_Admin_RoleCapabilities(race_admin_role_t role);
const char *Race_Admin_CapabilityName(race_admin_capability_t capability);
const char *Race_Admin_ActionName(race_admin_action_t action);
race_admin_capability_t Race_Admin_ActionCapability(race_admin_action_t action);

bool Race_Admin_DocumentInit(race_admin_document_t *document);
bool Race_Admin_DocumentValid(const race_admin_document_t *document,
                              bool require_generation);
bool Race_Admin_DocumentEquals(const race_admin_document_t *left,
                               const race_admin_document_t *right);

const race_admin_account_t *Race_Admin_AccountById(
  const race_admin_document_t *document, const char *id);
const race_admin_account_t *Race_Admin_AccountByHandle(
  const race_admin_document_t *document, const char *handle);

bool Race_Admin_AddAccount(const race_admin_document_t *current,
                           const char *id, const char *handle,
                           race_admin_role_t role,
                           race_admin_document_t *candidate);
bool Race_Admin_RemoveAccount(const race_admin_document_t *current,
                              const char *id,
                              race_admin_document_t *candidate);
bool Race_Admin_SetAccountRole(const race_admin_document_t *current,
                               const char *id, race_admin_role_t role,
                               race_admin_document_t *candidate);
bool Race_Admin_SetAccountEnabled(const race_admin_document_t *current,
                                  const char *id, bool enabled,
                                  race_admin_document_t *candidate);
bool Race_Admin_SetAccountHandle(const race_admin_document_t *current,
                                 const char *id, const char *handle,
                                 race_admin_document_t *candidate);

void Race_Admin_SessionClear(race_admin_session_t *session);
bool Race_Admin_SessionGrant(race_admin_session_t *session,
                             const race_admin_document_t *document,
                             const char *account_id);
bool Race_Admin_SessionAuthenticated(const race_admin_session_t *session,
                                     const race_admin_document_t *document);
bool Race_Admin_SessionHasCapability(const race_admin_session_t *session,
                                     const race_admin_document_t *document,
                                     race_admin_capability_t capability);
bool Race_Admin_SessionCanPerform(const race_admin_session_t *session,
                                  const race_admin_document_t *document,
                                  race_admin_action_t action);

bool Race_Admin_ParseClientSlot(const char *text, int32_t max_clients,
                                int32_t *slot);
race_admin_kick_target_result_t Race_Admin_ValidateKickTarget(
  int32_t requester_slot, int32_t target_slot, int32_t max_clients,
  bool target_connected);

bool Race_Admin_Serialize(const race_admin_document_t *document,
                          char *output, size_t output_size,
                          size_t *output_length);
race_admin_parse_result_t Race_Admin_Parse(const void *data, size_t length,
                                           race_admin_document_t *document);
const char *Race_Admin_ParseResultName(race_admin_parse_result_t result);
