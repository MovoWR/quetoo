/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_profile_auth.h"

#include <stdio.h>
#include <string.h>

#include "argon2.h"
#include "blake2/blake2.h"
#include "race_admin_auth.h"
#include "race_admin_password.h"

#if !defined(ARGON2_NO_THREADS)
#error "Race profile authentication requires ARGON2_NO_THREADS"
#endif

_Static_assert(RACE_PROFILE_CREDENTIAL_SIZE == RACE_ADMIN_CREDENTIAL_SIZE,
               "Race credential buffer policy mismatch");

static bool Race_ProfileAuth_Hex(const char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static int32_t Race_ProfileAuth_Base64Value(const char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  return c == '+' ? 62 : c == '/' ? 63 : -1;
}

static bool Race_ProfileAuth_Base64Valid(const char *text,
                                         const size_t length,
                                         const int32_t trailing_mask) {
  for (size_t i = 0u; i < length; i++) {
    const int32_t value = Race_ProfileAuth_Base64Value(text[i]);
    if (value < 0 || (i + 1u == length && (value & trailing_mask))) {
      return false;
    }
  }
  return true;
}

static bool Race_ProfileAuth_CredentialValid(const char *credential) {
  if (!credential ||
      strnlen(credential, RACE_PROFILE_CREDENTIAL_SIZE) !=
        RACE_ADMIN_PASSWORD_ENCODED_LENGTH) {
    return false;
  }
  const size_t prefix_length = sizeof(RACE_ADMIN_PASSWORD_PREFIX) - 1u;
  if (memcmp(credential, RACE_ADMIN_PASSWORD_PREFIX, prefix_length)) {
    return false;
  }
  const char *salt = credential + prefix_length;
  return Race_ProfileAuth_Base64Valid(
           salt, RACE_ADMIN_PASSWORD_SALT_ENCODED_SIZE, 0x0f) &&
         salt[RACE_ADMIN_PASSWORD_SALT_ENCODED_SIZE] == '$' &&
         Race_ProfileAuth_Base64Valid(
           salt + RACE_ADMIN_PASSWORD_SALT_ENCODED_SIZE + 1u,
           RACE_ADMIN_PASSWORD_TAG_ENCODED_SIZE, 0x03);
}

bool Race_ProfileAuth_SecretValid(const char *secret) {
  if (!secret ||
      strnlen(secret, RACE_PROFILE_AUTH_SECRET_SIZE) !=
        RACE_PROFILE_AUTH_SECRET_SIZE - 1u) {
    return false;
  }
  for (size_t i = 0u; i + 1u < RACE_PROFILE_AUTH_SECRET_SIZE; i++) {
    if (!Race_ProfileAuth_Hex(secret[i])) {
      return false;
    }
  }
  return true;
}

bool Race_ProfileAuth_DeriveCredential(
  const char secret[RACE_PROFILE_AUTH_SECRET_SIZE],
  const uint8_t salt[RACE_ADMIN_PASSWORD_SALT_SIZE],
  char credential[RACE_PROFILE_CREDENTIAL_SIZE]) {
  if (!Race_ProfileAuth_SecretValid(secret) || !salt || !credential) {
    return false;
  }

  char derived[RACE_PROFILE_CREDENTIAL_SIZE] = { 0 };
  const int32_t result = argon2id_hash_encoded(
    RACE_ADMIN_PASSWORD_ITERATIONS, RACE_ADMIN_PASSWORD_MEMORY_KIB,
    RACE_ADMIN_PASSWORD_LANES, secret, strlen(secret), salt,
    RACE_ADMIN_PASSWORD_SALT_SIZE, RACE_ADMIN_PASSWORD_TAG_SIZE,
    derived, sizeof(derived));
  if (result != ARGON2_OK || !Race_ProfileAuth_CredentialValid(derived)) {
    Race_AdminAuth_ClearSecret(derived, sizeof(derived));
    return false;
  }
  memcpy(credential, derived, sizeof(derived));
  Race_AdminAuth_ClearSecret(derived, sizeof(derived));
  return true;
}

static bool Race_ProfileAuth_ProofWithCredential(
  const char *uid, const char *credential,
  const uint8_t nonce[RACE_PROFILE_AUTH_NONCE_SIZE],
  uint8_t proof[RACE_PROFILE_AUTH_PROOF_SIZE]) {
  static const uint8_t domain[] = "quetoo-race-profile-proof-v1";
  char canonical[RACE_PROFILE_UID_SIZE];
  if (!Race_Profile_CanonicalizeUid(uid, canonical) || strcmp(uid, canonical) ||
      !Race_ProfileAuth_CredentialValid(credential) || !nonce || !proof) {
    return false;
  }

  const uint8_t uid_length = (uint8_t) strlen(uid);
  uint8_t key[32] = { 0 };
  blake2b_state state = { 0 };
  const bool valid = blake2b(
      key, sizeof(key), credential, strlen(credential), NULL, 0u) == 0 &&
    blake2b_init_key(
      &state, RACE_PROFILE_AUTH_PROOF_SIZE,
      key, sizeof(key)) == 0 &&
    blake2b_update(&state, domain, sizeof(domain)) == 0 &&
    blake2b_update(&state, &uid_length, sizeof(uid_length)) == 0 &&
    blake2b_update(&state, uid, uid_length) == 0 &&
    blake2b_update(&state, nonce, RACE_PROFILE_AUTH_NONCE_SIZE) == 0 &&
    blake2b_final(&state, proof, RACE_PROFILE_AUTH_PROOF_SIZE) == 0;
  Race_AdminAuth_ClearSecret(key, sizeof(key));
  Race_AdminAuth_ClearSecret(&state, sizeof(state));
  return valid;
}

bool Race_ProfileAuth_CreateProof(
  const char *uid, const char secret[RACE_PROFILE_AUTH_SECRET_SIZE],
  const uint8_t salt[RACE_ADMIN_PASSWORD_SALT_SIZE],
  const uint8_t nonce[RACE_PROFILE_AUTH_NONCE_SIZE],
  uint8_t proof[RACE_PROFILE_AUTH_PROOF_SIZE]) {
  char credential[RACE_PROFILE_CREDENTIAL_SIZE] = { 0 };
  const bool valid = Race_ProfileAuth_DeriveCredential(
      secret, salt, credential) &&
    Race_ProfileAuth_ProofWithCredential(uid, credential, nonce, proof);
  Race_AdminAuth_ClearSecret(credential, sizeof(credential));
  return valid;
}

bool Race_ProfileAuth_VerifyProof(
  const char *uid, const char *credential,
  const uint8_t nonce[RACE_PROFILE_AUTH_NONCE_SIZE],
  const uint8_t proof[RACE_PROFILE_AUTH_PROOF_SIZE]) {
  if (!proof) {
    return false;
  }
  uint8_t expected[RACE_PROFILE_AUTH_PROOF_SIZE] = { 0 };
  bool valid = Race_ProfileAuth_ProofWithCredential(
    uid, credential, nonce, expected);
  uint8_t difference = 0u;
  for (size_t i = 0u; i < sizeof(expected); i++) {
    difference |= expected[i] ^ proof[i];
  }
  valid = valid && difference == 0u;
  Race_AdminAuth_ClearSecret(expected, sizeof(expected));
  return valid;
}

bool Race_ProfileAuth_FormatProofCommand(
  const char *uid, const uint8_t nonce[RACE_PROFILE_AUTH_NONCE_SIZE],
  const uint8_t proof[RACE_PROFILE_AUTH_PROOF_SIZE],
  char output[RACE_PROFILE_AUTH_PROOF_COMMAND_SIZE]) {
  char canonical[RACE_PROFILE_UID_SIZE];
  char nonce_hex[RACE_PROFILE_AUTH_NONCE_HEX_SIZE];
  char proof_hex[RACE_PROFILE_AUTH_PROOF_HEX_SIZE];
  if (!Race_Profile_CanonicalizeUid(uid, canonical) || strcmp(uid, canonical) ||
      !nonce || !proof || !output ||
      !Race_AdminAuth_HexEncode(nonce, RACE_PROFILE_AUTH_NONCE_SIZE,
                                nonce_hex, sizeof(nonce_hex)) ||
      !Race_AdminAuth_HexEncode(proof, RACE_PROFILE_AUTH_PROOF_SIZE,
                                proof_hex, sizeof(proof_hex))) {
    return false;
  }
  const int32_t length = snprintf(
    output, RACE_PROFILE_AUTH_PROOF_COMMAND_SIZE,
    "race_profile proof %s %s %s\n", uid, nonce_hex, proof_hex);
  return length > 0 &&
         (size_t) length < RACE_PROFILE_AUTH_PROOF_COMMAND_SIZE;
}
