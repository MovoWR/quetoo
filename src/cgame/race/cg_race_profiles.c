/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"
#include "cg_race_profiles.h"

#include "blake2/blake2.h"
#include "race_admin_auth.h"
#include "race_admin_password.h"
#include "race_profile_auth.h"

#define CG_RACE_PROFILE_CVAR_PREFIX "race_profile_credential_"
#define CG_RACE_PROFILE_SERVER_HASH_SIZE 8u
#define CG_RACE_PROFILE_VALUE_SIZE \
  (RACE_PROFILE_UID_SIZE + RACE_PROFILE_AUTH_SECRET_SIZE)

static cvar_t *cg_race_profile_credential;

static bool Cg_RaceProfiles_Credential(
  char uid[RACE_PROFILE_UID_SIZE],
  char secret[RACE_PROFILE_AUTH_SECRET_SIZE]) {
  if (!cg_race_profile_credential ||
      !cg_race_profile_credential->string) {
    return false;
  }
  const char *value = cg_race_profile_credential->string;
  if (strnlen(value, CG_RACE_PROFILE_VALUE_SIZE) !=
      RACE_PROFILE_UID_LENGTH + 1u + RACE_PROFILE_AUTH_SECRET_SIZE - 1u ||
      value[RACE_PROFILE_UID_LENGTH] != ':') {
    return false;
  }
  memcpy(uid, value, RACE_PROFILE_UID_LENGTH);
  uid[RACE_PROFILE_UID_LENGTH] = '\0';
  memcpy(secret, value + RACE_PROFILE_UID_LENGTH + 1u,
         RACE_PROFILE_AUTH_SECRET_SIZE);
  char canonical[RACE_PROFILE_UID_SIZE];
  return Race_Profile_CanonicalizeUid(uid, canonical) &&
         !strcmp(uid, canonical) && Race_ProfileAuth_SecretValid(secret);
}

static void Cg_RaceProfiles_Request(void) {
  char uid[RACE_PROFILE_UID_SIZE] = { 0 };
  char secret[RACE_PROFILE_AUTH_SECRET_SIZE] = { 0 };
  if (Cg_RaceProfiles_Credential(uid, secret)) {
    cgi.Cbuf(va("race_profile challenge %s\n", uid));
  } else {
    if (cg_race_profile_credential &&
        cg_race_profile_credential->string &&
        *cg_race_profile_credential->string) {
      Cg_Warn("Clearing malformed local Race profile credential\n");
      cgi.ForceSetCvarString(cg_race_profile_credential->name, "");
    }
    cgi.Cbuf("race_profile enroll\n");
  }
  Race_AdminAuth_ClearSecret(secret, sizeof(secret));
}

static void Cg_RaceProfiles_Reset_f(void) {
  if (!cg_race_profile_credential) {
    return;
  }
  cgi.ForceSetCvarString(cg_race_profile_credential->name, "");
  cgi.Print("Local Race profile credential reset for %s. Requesting a new profile.\n",
            cgi.server_name && *cgi.server_name ? cgi.server_name : "this server");
  Cg_RaceProfiles_Request();
}

void Cg_RaceProfiles_Init(void) {
  uint8_t digest[CG_RACE_PROFILE_SERVER_HASH_SIZE] = { 0 };
  char digest_hex[CG_RACE_PROFILE_SERVER_HASH_SIZE * 2u + 1u] = { 0 };
  const char *server = cgi.server_name && *cgi.server_name
    ? cgi.server_name : "unknown";
  if (blake2b(digest, sizeof(digest), server, strlen(server), NULL, 0u) != 0 ||
      !Race_AdminAuth_HexEncode(digest, sizeof(digest),
                                digest_hex, sizeof(digest_hex))) {
    q_strlcpy(digest_hex, "0000000000000000", sizeof(digest_hex));
  }
  char name[sizeof(CG_RACE_PROFILE_CVAR_PREFIX) + sizeof(digest_hex)];
  q_snprintf(name, sizeof(name), "%s%s",
             CG_RACE_PROFILE_CVAR_PREFIX, digest_hex);
  cg_race_profile_credential = cgi.AddCvar(
    name, "", CVAR_ARCHIVE,
    "Server-specific Race ranked-profile credential. Keep this value private.");
  cgi.AddCmd("race_profile_reset", Cg_RaceProfiles_Reset_f, CMD_CGAME,
             "Replace this server's ranked Race profile credential.");
  Race_AdminAuth_ClearSecret(digest, sizeof(digest));
}

void Cg_RaceProfiles_Shutdown(void) {
  cg_race_profile_credential = NULL;
}

static void Cg_RaceProfiles_Challenge(void) {
  char requested_uid[RACE_PROFILE_UID_SIZE] = { 0 };
  uint8_t salt[RACE_ADMIN_PASSWORD_SALT_SIZE] = { 0 };
  uint8_t nonce[RACE_PROFILE_AUTH_NONCE_SIZE] = { 0 };
  cgi.ReadData(requested_uid, RACE_PROFILE_UID_LENGTH);
  cgi.ReadData(salt, sizeof(salt));
  cgi.ReadData(nonce, sizeof(nonce));

  char uid[RACE_PROFILE_UID_SIZE] = { 0 };
  char secret[RACE_PROFILE_AUTH_SECRET_SIZE] = { 0 };
  uint8_t proof[RACE_PROFILE_AUTH_PROOF_SIZE] = { 0 };
  char command[RACE_PROFILE_AUTH_PROOF_COMMAND_SIZE];
  const bool valid = Cg_RaceProfiles_Credential(uid, secret) &&
    !strcmp(uid, requested_uid) &&
    Race_ProfileAuth_CreateProof(uid, secret, salt, nonce, proof) &&
    Race_ProfileAuth_FormatProofCommand(uid, nonce, proof, command);
  Race_AdminAuth_ClearSecret(secret, sizeof(secret));
  Race_AdminAuth_ClearSecret(salt, sizeof(salt));
  Race_AdminAuth_ClearSecret(nonce, sizeof(nonce));
  Race_AdminAuth_ClearSecret(proof, sizeof(proof));
  if (!valid) {
    Cg_Warn("Rejected Race profile challenge: local credential does not match\n");
    return;
  }
  cgi.Cbuf(command);
}

static void Cg_RaceProfiles_Enrollment(void) {
  char uid[RACE_PROFILE_UID_SIZE] = { 0 };
  char secret[RACE_PROFILE_AUTH_SECRET_SIZE] = { 0 };
  cgi.ReadData(uid, RACE_PROFILE_UID_LENGTH);
  cgi.ReadData(secret, RACE_PROFILE_AUTH_SECRET_SIZE - 1u);

  char canonical[RACE_PROFILE_UID_SIZE];
  if (!Race_Profile_CanonicalizeUid(uid, canonical) || strcmp(uid, canonical) ||
      !Race_ProfileAuth_SecretValid(secret) ||
      !cg_race_profile_credential) {
    Cg_Warn("Rejected malformed Race profile enrollment\n");
    Race_AdminAuth_ClearSecret(secret, sizeof(secret));
    return;
  }

  char value[CG_RACE_PROFILE_VALUE_SIZE];
  q_snprintf(value, sizeof(value), "%s:%s", uid, secret);
  cgi.ForceSetCvarString(cg_race_profile_credential->name, value);
  Race_AdminAuth_ClearSecret(value, sizeof(value));
  Race_AdminAuth_ClearSecret(secret, sizeof(secret));
  cgi.Cbuf(va("race_profile challenge %s\n", uid));
}

bool Cg_RaceProfiles_ParseMessage(const int32_t command) {
  if (command == SV_CMD_RACE_PROFILE_REQUEST) {
    Cg_RaceProfiles_Request();
    return true;
  }
  if (command == SV_CMD_RACE_PROFILE_CHALLENGE) {
    Cg_RaceProfiles_Challenge();
    return true;
  }
  if (command == SV_CMD_RACE_PROFILE_ENROLLMENT) {
    Cg_RaceProfiles_Enrollment();
    return true;
  }
  return false;
}
