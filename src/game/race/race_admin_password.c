/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_admin_password.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "argon2.h"
#include "race_admin_auth.h"

#if !defined(ARGON2_NO_THREADS)
#error "Race administrator password hashing requires ARGON2_NO_THREADS"
#endif

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__) || defined(__DragonFly__)
#include <stdlib.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#endif

static int32_t Race_AdminPassword_Base64Value(char c) {
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

static bool Race_AdminPassword_Base64Valid(const char *text, size_t length,
                                            int32_t trailing_mask) {
  for (size_t i = 0; i < length; i++) {
    const int32_t value = Race_AdminPassword_Base64Value(text[i]);
    if (value < 0 || (i == length - 1u && (value & trailing_mask))) {
      return false;
    }
  }
  return true;
}

bool Race_AdminPassword_Valid(const char *password) {
  return Race_AdminAuth_PasswordValid(password);
}

bool Race_AdminPassword_EncodedValid(const char *encoded) {
  if (!encoded ||
      strnlen(encoded, RACE_ADMIN_CREDENTIAL_SIZE) !=
        RACE_ADMIN_PASSWORD_ENCODED_LENGTH) {
    return false;
  }

  const size_t prefix_length = sizeof(RACE_ADMIN_PASSWORD_PREFIX) - 1u;
  if (memcmp(encoded, RACE_ADMIN_PASSWORD_PREFIX, prefix_length)) {
    return false;
  }

  const char *salt = encoded + prefix_length;
  if (!Race_AdminPassword_Base64Valid(
        salt, RACE_ADMIN_PASSWORD_SALT_ENCODED_SIZE, 0x0f) ||
      salt[RACE_ADMIN_PASSWORD_SALT_ENCODED_SIZE] != '$') {
    return false;
  }

  const char *tag = salt + RACE_ADMIN_PASSWORD_SALT_ENCODED_SIZE + 1u;
  return Race_AdminPassword_Base64Valid(
    tag, RACE_ADMIN_PASSWORD_TAG_ENCODED_SIZE, 0x03);
}

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__FreeBSD__) && \
    !defined(__OpenBSD__) && !defined(__NetBSD__) && !defined(__DragonFly__)
static bool Race_AdminPassword_ReadUrandom(uint8_t *bytes, size_t length) {
  const int32_t descriptor = open("/dev/urandom", O_RDONLY);
  if (descriptor < 0) {
    return false;
  }

  size_t offset = 0;
  while (offset < length) {
    const ssize_t result = read(descriptor, bytes + offset, length - offset);
    if (result > 0) {
      offset += (size_t) result;
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    close(descriptor);
    return false;
  }

  close(descriptor);
  return true;
}
#endif

bool Race_AdminPassword_RandomBytes(uint8_t *bytes, size_t length) {
  if (!bytes || !length) {
    return false;
  }
#if defined(_WIN32)
  return BCryptGenRandom(NULL, bytes, (ULONG) length,
                         BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__) || defined(__DragonFly__)
  arc4random_buf(bytes, length);
  return true;
#elif defined(__linux__)
  size_t offset = 0;
  while (offset < length) {
    const ssize_t result = getrandom(bytes + offset, length - offset, 0);
    if (result > 0) {
      offset += (size_t) result;
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    if (result < 0 && errno == ENOSYS) {
      return Race_AdminPassword_ReadUrandom(bytes, length);
    }
    return false;
  }
  return true;
#else
  return Race_AdminPassword_ReadUrandom(bytes, length);
#endif
}

bool Race_AdminPassword_HashWithSalt(
  const char *password,
  const uint8_t salt[RACE_ADMIN_PASSWORD_SALT_SIZE],
  char encoded[RACE_ADMIN_CREDENTIAL_SIZE]) {
  if (!Race_AdminPassword_Valid(password) || !salt || !encoded) {
    return false;
  }

  char candidate[RACE_ADMIN_CREDENTIAL_SIZE] = { 0 };
  const int32_t result = argon2id_hash_encoded(
    RACE_ADMIN_PASSWORD_ITERATIONS, RACE_ADMIN_PASSWORD_MEMORY_KIB,
    RACE_ADMIN_PASSWORD_LANES, password, strlen(password), salt,
    RACE_ADMIN_PASSWORD_SALT_SIZE, RACE_ADMIN_PASSWORD_TAG_SIZE,
    candidate, sizeof(candidate));
  if (result != ARGON2_OK || !Race_AdminPassword_EncodedValid(candidate)) {
    return false;
  }

  memcpy(encoded, candidate, sizeof(candidate));
  return true;
}

bool Race_AdminPassword_Hash(
  const char *password, char encoded[RACE_ADMIN_CREDENTIAL_SIZE]) {
  if (!encoded) {
    return false;
  }

  uint8_t salt[RACE_ADMIN_PASSWORD_SALT_SIZE];
  if (!Race_AdminPassword_RandomBytes(salt, sizeof(salt))) {
    return false;
  }
  return Race_AdminPassword_HashWithSalt(password, salt, encoded);
}

race_admin_password_verify_result_t Race_AdminPassword_Verify(
  const char *encoded, const char *password) {
  if (!Race_AdminPassword_EncodedValid(encoded) || !password ||
      strnlen(password, RACE_ADMIN_PASSWORD_MAX + 1u) >
        RACE_ADMIN_PASSWORD_MAX) {
    return RACE_ADMIN_PASSWORD_VERIFY_ERROR;
  }

  const int32_t result = argon2id_verify(encoded, password, strlen(password));
  if (result == ARGON2_OK) {
    return RACE_ADMIN_PASSWORD_VERIFY_MATCH;
  }
  if (result == ARGON2_VERIFY_MISMATCH) {
    return RACE_ADMIN_PASSWORD_VERIFY_MISMATCH;
  }
  return RACE_ADMIN_PASSWORD_VERIFY_ERROR;
}
