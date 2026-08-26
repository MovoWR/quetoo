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

#include "race_admin_types.h"

#define RACE_ADMIN_AUTH_SALT_SIZE 16u
#define RACE_ADMIN_AUTH_NONCE_SIZE RACE_ADMIN_CHALLENGE_NONCE_SIZE
#define RACE_ADMIN_AUTH_PROOF_SIZE 32u
#define RACE_ADMIN_AUTH_NONCE_HEX_SIZE \
  (RACE_ADMIN_AUTH_NONCE_SIZE * 2u + 1u)
#define RACE_ADMIN_AUTH_PROOF_HEX_SIZE \
  (RACE_ADMIN_AUTH_PROOF_SIZE * 2u + 1u)
#define RACE_ADMIN_AUTH_PROOF_COMMAND_SIZE 192u

bool Race_AdminAuth_PasswordValid(const char *password);
bool Race_AdminAuth_AccountValid(const char *account);

bool Race_AdminAuth_CredentialSalt(
  const char *credential, uint8_t salt[RACE_ADMIN_AUTH_SALT_SIZE]);
bool Race_AdminAuth_DeriveDummySalt(
  const char *credential, const char *account,
  uint8_t salt[RACE_ADMIN_AUTH_SALT_SIZE]);

bool Race_AdminAuth_CreatePasswordProof(
  const char *account, const char *password,
  const uint8_t salt[RACE_ADMIN_AUTH_SALT_SIZE],
  const uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE],
  uint8_t proof[RACE_ADMIN_AUTH_PROOF_SIZE]);

bool Race_AdminAuth_VerifyCredentialProof(
  const char *account, const char *credential,
  const uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE],
  const uint8_t proof[RACE_ADMIN_AUTH_PROOF_SIZE]);

bool Race_AdminAuth_HexEncode(const uint8_t *bytes, size_t length,
                              char *output, size_t output_size);
bool Race_AdminAuth_HexDecode(const char *input, uint8_t *bytes,
                              size_t length);

bool Race_AdminAuth_FormatProofCommand(
  const char *account, const uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE],
  const uint8_t proof[RACE_ADMIN_AUTH_PROOF_SIZE],
  char output[RACE_ADMIN_AUTH_PROOF_COMMAND_SIZE]);

bool Race_AdminAuth_IssueChallenge(
  race_admin_challenge_t *challenge, const char *account,
  const uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE], uint64_t now);
bool Race_AdminAuth_ConsumeChallenge(
  race_admin_challenge_t *challenge, const char *account,
  const uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE], uint64_t now,
  uint64_t lifetime);

void Race_AdminAuth_ClearSecret(void *secret, size_t length);
