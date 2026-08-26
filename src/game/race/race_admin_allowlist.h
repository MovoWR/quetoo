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

#define RACE_ADMIN_MAX_OPERATOR_CVARS 32
#define RACE_ADMIN_CVAR_NAME_MAX 63
#define RACE_ADMIN_CVAR_NAME_SIZE (RACE_ADMIN_CVAR_NAME_MAX + 1)
#define RACE_ADMIN_ALLOWLIST_SERIALIZED_MAX 16384

typedef struct {
  size_t count;
  char names[RACE_ADMIN_MAX_OPERATOR_CVARS][RACE_ADMIN_CVAR_NAME_SIZE];
} race_admin_allowlist_t;

typedef enum {
  RACE_ADMIN_ALLOWLIST_PARSE_OK,
  RACE_ADMIN_ALLOWLIST_PARSE_MALFORMED,
  RACE_ADMIN_ALLOWLIST_PARSE_TOO_LARGE,
  RACE_ADMIN_ALLOWLIST_PARSE_DUPLICATE,
  RACE_ADMIN_ALLOWLIST_PARSE_TOO_MANY
} race_admin_allowlist_parse_result_t;

typedef enum {
  RACE_ADMIN_ALLOWLIST_STORE_OK,
  RACE_ADMIN_ALLOWLIST_STORE_MISSING,
  RACE_ADMIN_ALLOWLIST_STORE_CORRUPT,
  RACE_ADMIN_ALLOWLIST_STORE_IO_ERROR,
  RACE_ADMIN_ALLOWLIST_STORE_INVALID_ARGUMENT
} race_admin_allowlist_store_result_t;

bool Race_AdminAllowlist_NameValid(const char *name);
bool Race_AdminAllowlist_Init(race_admin_allowlist_t *allowlist);
bool Race_AdminAllowlist_Valid(const race_admin_allowlist_t *allowlist);
bool Race_AdminAllowlist_Equals(const race_admin_allowlist_t *left,
                                const race_admin_allowlist_t *right);
bool Race_AdminAllowlist_Contains(const race_admin_allowlist_t *allowlist,
                                  const char *name);
bool Race_AdminAllowlist_Add(const race_admin_allowlist_t *current,
                             const char *name,
                             race_admin_allowlist_t *candidate);
bool Race_AdminAllowlist_Remove(const race_admin_allowlist_t *current,
                                const char *name,
                                race_admin_allowlist_t *candidate);
bool Race_AdminAllowlist_Serialize(const race_admin_allowlist_t *allowlist,
                                   char *output, size_t output_size,
                                   size_t *output_length);
race_admin_allowlist_parse_result_t Race_AdminAllowlist_Parse(
  const void *data, size_t length, race_admin_allowlist_t *allowlist);
const char *Race_AdminAllowlist_ParseResultName(
  race_admin_allowlist_parse_result_t result);

race_admin_allowlist_store_result_t Race_AdminAllowlistStore_Load(
  const char *committed, race_admin_allowlist_t *allowlist,
  race_admin_allowlist_parse_result_t *parse_result);
race_admin_allowlist_store_result_t Race_AdminAllowlistStore_Commit(
  const char *committed, const char *candidate,
  const race_admin_allowlist_t *allowlist,
  race_admin_allowlist_parse_result_t *parse_result);
const char *Race_AdminAllowlistStore_ResultName(
  race_admin_allowlist_store_result_t result);
