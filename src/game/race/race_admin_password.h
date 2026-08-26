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
#include <stdint.h>

#include "race_admin.h"

#define RACE_ADMIN_PASSWORD_MIN 8
#define RACE_ADMIN_PASSWORD_MAX 128
#define RACE_ADMIN_PASSWORD_SALT_SIZE 16
#define RACE_ADMIN_PASSWORD_TAG_SIZE 32
#define RACE_ADMIN_PASSWORD_MEMORY_KIB 19456
#define RACE_ADMIN_PASSWORD_ITERATIONS 2
#define RACE_ADMIN_PASSWORD_LANES 1
#define RACE_ADMIN_PASSWORD_PREFIX \
  "$argon2id$v=19$m=19456,t=2,p=1$"
#define RACE_ADMIN_PASSWORD_SALT_ENCODED_SIZE 22
#define RACE_ADMIN_PASSWORD_TAG_ENCODED_SIZE 43
#define RACE_ADMIN_PASSWORD_ENCODED_LENGTH 97

typedef enum {
  RACE_ADMIN_PASSWORD_VERIFY_MATCH,
  RACE_ADMIN_PASSWORD_VERIFY_MISMATCH,
  RACE_ADMIN_PASSWORD_VERIFY_ERROR
} race_admin_password_verify_result_t;

bool Race_AdminPassword_Valid(const char *password);
bool Race_AdminPassword_EncodedValid(const char *encoded);

bool Race_AdminPassword_Hash(
  const char *password, char encoded[RACE_ADMIN_CREDENTIAL_SIZE]);
bool Race_AdminPassword_HashWithSalt(
  const char *password,
  const uint8_t salt[RACE_ADMIN_PASSWORD_SALT_SIZE],
  char encoded[RACE_ADMIN_CREDENTIAL_SIZE]);
bool Race_AdminPassword_RandomBytes(uint8_t *bytes, size_t length);

race_admin_password_verify_result_t Race_AdminPassword_Verify(
  const char *encoded, const char *password);
