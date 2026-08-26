/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_admin_auth.h"

#include <stdio.h>
#include <string.h>

#include "argon2.h"
#include "blake2/blake2.h"
#include "race_admin_password.h"

#if !defined(ARGON2_NO_THREADS)
#error "Race administrator authentication requires ARGON2_NO_THREADS"
#endif

#define RACE_ADMIN_AUTH_TAG_SIZE RACE_ADMIN_PASSWORD_TAG_SIZE

_Static_assert(RACE_ADMIN_AUTH_SALT_SIZE == RACE_ADMIN_PASSWORD_SALT_SIZE,
               "Race administrator authentication salt policy mismatch");

static int32_t Race_AdminAuth_Base64Value(const char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '+') {
    return 62;
  }
  if (c == '/') {
    return 63;
  }
  return -1;
}

static bool Race_AdminAuth_DecodeBase64(const char *input,
                                        const size_t input_length,
                                        uint8_t *output,
                                        const size_t output_length) {
  if (!input || !output) {
    return false;
  }

  uint32_t accumulator = 0u;
  uint32_t bits = 0u;
  size_t written = 0u;
  for (size_t i = 0u; i < input_length; i++) {
    const int32_t value = Race_AdminAuth_Base64Value(input[i]);
    if (value < 0) {
      return false;
    }
    accumulator = (accumulator << 6u) | (uint32_t) value;
    bits += 6u;
    if (bits >= 8u) {
      bits -= 8u;
      if (written >= output_length) {
        return false;
      }
      output[written++] = (uint8_t) (accumulator >> bits);
      accumulator &= bits ? (1u << bits) - 1u : 0u;
    }
  }
  return written == output_length && accumulator == 0u;
}

static bool Race_AdminAuth_CredentialParts(
  const char *credential, uint8_t salt[RACE_ADMIN_AUTH_SALT_SIZE],
  uint8_t tag[RACE_ADMIN_AUTH_TAG_SIZE]) {
  if (!credential ||
      strnlen(credential, RACE_ADMIN_CREDENTIAL_SIZE) !=
        RACE_ADMIN_PASSWORD_ENCODED_LENGTH) {
    return false;
  }

  const size_t prefix_length = sizeof(RACE_ADMIN_PASSWORD_PREFIX) - 1u;
  if (memcmp(credential, RACE_ADMIN_PASSWORD_PREFIX, prefix_length)) {
    return false;
  }
  const char *encoded_salt = credential + prefix_length;
  if (encoded_salt[RACE_ADMIN_PASSWORD_SALT_ENCODED_SIZE] != '$') {
    return false;
  }
  const char *encoded_tag = encoded_salt +
    RACE_ADMIN_PASSWORD_SALT_ENCODED_SIZE + 1u;
  return Race_AdminAuth_DecodeBase64(
           encoded_salt, RACE_ADMIN_PASSWORD_SALT_ENCODED_SIZE,
           salt, RACE_ADMIN_AUTH_SALT_SIZE) &&
         Race_AdminAuth_DecodeBase64(
           encoded_tag, RACE_ADMIN_PASSWORD_TAG_ENCODED_SIZE,
           tag, RACE_ADMIN_AUTH_TAG_SIZE);
}

bool Race_AdminAuth_PasswordValid(const char *password) {
  if (!password) {
    return false;
  }

  const size_t length = strnlen(password, RACE_ADMIN_PASSWORD_MAX + 1u);
  if (length < RACE_ADMIN_PASSWORD_MIN ||
      length > RACE_ADMIN_PASSWORD_MAX) {
    return false;
  }
  for (size_t i = 0u; i < length; i++) {
    const uint8_t c = (uint8_t) password[i];
    if (c < 0x21u || c > 0x7eu || c == ';' || c == '"' || c == '\\' ||
        c == '$' || c == '/') {
      return false;
    }
  }
  return true;
}

bool Race_AdminAuth_AccountValid(const char *account) {
  if (!account) {
    return false;
  }
  const size_t length = strnlen(account, RACE_ADMIN_ACCOUNT_ID_SIZE);
  if (!length || length > RACE_ADMIN_ACCOUNT_ID_MAX) {
    return false;
  }
  for (size_t i = 0u; i < length; i++) {
    const char c = account[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
          c == '_' || c == '-')) {
      return false;
    }
  }
  return true;
}

bool Race_AdminAuth_CredentialSalt(
  const char *credential, uint8_t salt[RACE_ADMIN_AUTH_SALT_SIZE]) {
  uint8_t tag[RACE_ADMIN_AUTH_TAG_SIZE];
  const bool valid = salt && Race_AdminAuth_CredentialParts(
    credential, salt, tag);
  Race_AdminAuth_ClearSecret(tag, sizeof(tag));
  return valid;
}

bool Race_AdminAuth_DeriveDummySalt(
  const char *credential, const char *account,
  uint8_t salt[RACE_ADMIN_AUTH_SALT_SIZE]) {
  static const uint8_t domain[] = "quetoo-race-admin-dummy-salt-v1";
  if (!salt || !Race_AdminAuth_AccountValid(account)) {
    return false;
  }

  uint8_t credential_salt[RACE_ADMIN_AUTH_SALT_SIZE] = { 0 };
  uint8_t key[RACE_ADMIN_AUTH_TAG_SIZE] = { 0 };
  blake2b_state state = { 0 };
  const uint8_t account_length = (uint8_t) strlen(account);
  const bool valid = Race_AdminAuth_CredentialParts(
      credential, credential_salt, key) &&
    blake2b_init_key(&state, RACE_ADMIN_AUTH_SALT_SIZE,
                     key, sizeof(key)) == 0 &&
    blake2b_update(&state, domain, sizeof(domain)) == 0 &&
    blake2b_update(&state, &account_length, sizeof(account_length)) == 0 &&
    blake2b_update(&state, account, account_length) == 0 &&
    blake2b_final(&state, salt, RACE_ADMIN_AUTH_SALT_SIZE) == 0;
  Race_AdminAuth_ClearSecret(credential_salt, sizeof(credential_salt));
  Race_AdminAuth_ClearSecret(key, sizeof(key));
  Race_AdminAuth_ClearSecret(&state, sizeof(state));
  return valid;
}

static bool Race_AdminAuth_CreateProof(
  const char *account, const uint8_t key[RACE_ADMIN_AUTH_TAG_SIZE],
  const uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE],
  uint8_t proof[RACE_ADMIN_AUTH_PROOF_SIZE]) {
  static const uint8_t domain[] = "quetoo-race-admin-proof-v1";
  if (!Race_AdminAuth_AccountValid(account) || !key || !nonce || !proof) {
    return false;
  }

  const uint8_t account_length = (uint8_t) strlen(account);
  blake2b_state state = { 0 };
  const bool valid = blake2b_init_key(
      &state, RACE_ADMIN_AUTH_PROOF_SIZE,
      key, RACE_ADMIN_AUTH_TAG_SIZE) == 0 &&
    blake2b_update(&state, domain, sizeof(domain)) == 0 &&
    blake2b_update(&state, &account_length, sizeof(account_length)) == 0 &&
    blake2b_update(&state, account, account_length) == 0 &&
    blake2b_update(&state, nonce, RACE_ADMIN_AUTH_NONCE_SIZE) == 0 &&
    blake2b_final(&state, proof, RACE_ADMIN_AUTH_PROOF_SIZE) == 0;
  Race_AdminAuth_ClearSecret(&state, sizeof(state));
  return valid;
}

bool Race_AdminAuth_CreatePasswordProof(
  const char *account, const char *password,
  const uint8_t salt[RACE_ADMIN_AUTH_SALT_SIZE],
  const uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE],
  uint8_t proof[RACE_ADMIN_AUTH_PROOF_SIZE]) {
  if (!Race_AdminAuth_PasswordValid(password) || !salt) {
    return false;
  }

  uint8_t key[RACE_ADMIN_AUTH_TAG_SIZE];
  const int32_t result = argon2id_hash_raw(
    RACE_ADMIN_PASSWORD_ITERATIONS, RACE_ADMIN_PASSWORD_MEMORY_KIB,
    RACE_ADMIN_PASSWORD_LANES, password, strlen(password), salt,
    RACE_ADMIN_AUTH_SALT_SIZE, key, sizeof(key));
  const bool valid = result == ARGON2_OK &&
    Race_AdminAuth_CreateProof(account, key, nonce, proof);
  Race_AdminAuth_ClearSecret(key, sizeof(key));
  return valid;
}

bool Race_AdminAuth_VerifyCredentialProof(
  const char *account, const char *credential,
  const uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE],
  const uint8_t proof[RACE_ADMIN_AUTH_PROOF_SIZE]) {
  if (!proof) {
    return false;
  }

  uint8_t salt[RACE_ADMIN_AUTH_SALT_SIZE] = { 0 };
  uint8_t key[RACE_ADMIN_AUTH_TAG_SIZE] = { 0 };
  uint8_t expected[RACE_ADMIN_AUTH_PROOF_SIZE] = { 0 };
  bool valid = Race_AdminAuth_CredentialParts(credential, salt, key) &&
    Race_AdminAuth_CreateProof(account, key, nonce, expected);
  uint8_t difference = 0u;
  for (size_t i = 0u; i < sizeof(expected); i++) {
    difference |= expected[i] ^ proof[i];
  }
  valid = valid && difference == 0u;
  Race_AdminAuth_ClearSecret(salt, sizeof(salt));
  Race_AdminAuth_ClearSecret(key, sizeof(key));
  Race_AdminAuth_ClearSecret(expected, sizeof(expected));
  return valid;
}

bool Race_AdminAuth_HexEncode(const uint8_t *bytes, const size_t length,
                              char *output, const size_t output_size) {
  static const char hex[] = "0123456789abcdef";
  if (!bytes || !output || length > (SIZE_MAX - 1u) / 2u ||
      output_size < length * 2u + 1u) {
    return false;
  }
  for (size_t i = 0u; i < length; i++) {
    output[i * 2u] = hex[bytes[i] >> 4u];
    output[i * 2u + 1u] = hex[bytes[i] & 0x0fu];
  }
  output[length * 2u] = '\0';
  return true;
}

static int32_t Race_AdminAuth_HexValue(const char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

bool Race_AdminAuth_HexDecode(const char *input, uint8_t *bytes,
                              const size_t length) {
  if (!input || !bytes || length > (SIZE_MAX - 1u) / 2u ||
      strnlen(input, length * 2u + 1u) != length * 2u) {
    return false;
  }
  for (size_t i = 0u; i < length; i++) {
    const int32_t high = Race_AdminAuth_HexValue(input[i * 2u]);
    const int32_t low = Race_AdminAuth_HexValue(input[i * 2u + 1u]);
    if (high < 0 || low < 0) {
      return false;
    }
    bytes[i] = (uint8_t) (high << 4 | low);
  }
  return true;
}

bool Race_AdminAuth_FormatProofCommand(
  const char *account, const uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE],
  const uint8_t proof[RACE_ADMIN_AUTH_PROOF_SIZE],
  char output[RACE_ADMIN_AUTH_PROOF_COMMAND_SIZE]) {
  if (!Race_AdminAuth_AccountValid(account) || !nonce || !proof || !output) {
    return false;
  }

  char nonce_hex[RACE_ADMIN_AUTH_NONCE_HEX_SIZE];
  char proof_hex[RACE_ADMIN_AUTH_PROOF_HEX_SIZE];
  if (!Race_AdminAuth_HexEncode(nonce, RACE_ADMIN_AUTH_NONCE_SIZE,
                                nonce_hex, sizeof(nonce_hex)) ||
      !Race_AdminAuth_HexEncode(proof, RACE_ADMIN_AUTH_PROOF_SIZE,
                                proof_hex, sizeof(proof_hex))) {
    return false;
  }
  const int32_t length = snprintf(output, RACE_ADMIN_AUTH_PROOF_COMMAND_SIZE,
                                  "radmin proof %s %s %s\n",
                                  account, nonce_hex, proof_hex);
  return length > 0 &&
         (size_t) length < RACE_ADMIN_AUTH_PROOF_COMMAND_SIZE;
}

bool Race_AdminAuth_IssueChallenge(
  race_admin_challenge_t *challenge, const char *account,
  const uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE], const uint64_t now) {
  if (!challenge || !Race_AdminAuth_AccountValid(account) || !nonce) {
    return false;
  }

  uint8_t nonce_copy[RACE_ADMIN_AUTH_NONCE_SIZE];
  memcpy(nonce_copy, nonce, sizeof(nonce_copy));
  Race_AdminAuth_ClearSecret(challenge, sizeof(*challenge));
  const size_t account_length = strlen(account);
  memcpy(challenge->account_id, account, account_length + 1u);
  memcpy(challenge->nonce, nonce_copy, sizeof(challenge->nonce));
  challenge->issued_at = now;
  challenge->issued = true;
  Race_AdminAuth_ClearSecret(nonce_copy, sizeof(nonce_copy));
  return true;
}

bool Race_AdminAuth_ConsumeChallenge(
  race_admin_challenge_t *challenge, const char *account,
  const uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE], const uint64_t now,
  const uint64_t lifetime) {
  if (!challenge) {
    return false;
  }

  race_admin_challenge_t issued = *challenge;
  Race_AdminAuth_ClearSecret(challenge, sizeof(*challenge));
  const bool valid = issued.issued &&
    Race_AdminAuth_AccountValid(account) && nonce &&
    !strcmp(issued.account_id, account) &&
    !memcmp(issued.nonce, nonce, sizeof(issued.nonce)) &&
    now >= issued.issued_at && now - issued.issued_at <= lifetime;
  Race_AdminAuth_ClearSecret(&issued, sizeof(issued));
  return valid;
}

void Race_AdminAuth_ClearSecret(void *secret, const size_t length) {
  volatile uint8_t *bytes = secret;
  if (bytes) {
    for (size_t i = 0u; i < length; i++) {
      bytes[i] = 0u;
    }
  }
}
