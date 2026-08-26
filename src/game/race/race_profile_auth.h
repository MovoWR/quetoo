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

#include "race_profile.h"

#define RACE_PROFILE_AUTH_SECRET_BYTES 32u
#define RACE_PROFILE_AUTH_SECRET_SIZE \
  (RACE_PROFILE_AUTH_SECRET_BYTES * 2u + 1u)
#define RACE_PROFILE_AUTH_PROOF_SIZE 32u
#define RACE_PROFILE_AUTH_PROOF_HEX_SIZE \
  (RACE_PROFILE_AUTH_PROOF_SIZE * 2u + 1u)
#define RACE_PROFILE_AUTH_NONCE_HEX_SIZE \
  (RACE_PROFILE_AUTH_NONCE_SIZE * 2u + 1u)
#define RACE_PROFILE_AUTH_PROOF_COMMAND_SIZE 256u

bool Race_ProfileAuth_SecretValid(const char *secret);
bool Race_ProfileAuth_DeriveCredential(
  const char secret[RACE_PROFILE_AUTH_SECRET_SIZE],
  const uint8_t salt[16],
  char credential[RACE_PROFILE_CREDENTIAL_SIZE]);
bool Race_ProfileAuth_CreateProof(
  const char *uid, const char secret[RACE_PROFILE_AUTH_SECRET_SIZE],
  const uint8_t salt[16],
  const uint8_t nonce[RACE_PROFILE_AUTH_NONCE_SIZE],
  uint8_t proof[RACE_PROFILE_AUTH_PROOF_SIZE]);
bool Race_ProfileAuth_VerifyProof(
  const char *uid, const char *credential,
  const uint8_t nonce[RACE_PROFILE_AUTH_NONCE_SIZE],
  const uint8_t proof[RACE_PROFILE_AUTH_PROOF_SIZE]);
bool Race_ProfileAuth_FormatProofCommand(
  const char *uid, const uint8_t nonce[RACE_PROFILE_AUTH_NONCE_SIZE],
  const uint8_t proof[RACE_PROFILE_AUTH_PROOF_SIZE],
  char output[RACE_PROFILE_AUTH_PROOF_COMMAND_SIZE]);
