/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_profiles.h"

#include "race_admin_auth.h"
#include "race_admin_password.h"
#include "race_connection_address.h"
#include "race_persistence.h"
#include "race_profile.h"
#include "race_profile_auth.h"

#define RACE_PROFILE_CHALLENGE_LIFETIME 15000u
#define RACE_PROFILE_ENROLLMENT_ENTRY_COUNT 128u
#define RACE_PROFILE_ENROLLMENT_ADDRESS_LIMIT 4u
#define RACE_PROFILE_ENROLLMENT_ADDRESS_WINDOW 60000u
#define RACE_PROFILE_ENROLLMENT_GLOBAL_LIMIT 8u
#define RACE_PROFILE_ENROLLMENT_GLOBAL_WINDOW 1000u

typedef struct {
  char address[RACE_CONNECTION_ADDRESS_SIZE];
  uint64_t window_started_at;
  uint64_t last_used_at;
  uint8_t enrollments;
  bool in_use;
} race_profile_enrollment_entry_t;

static race_profile_enrollment_entry_t
  race_profile_enrollments[RACE_PROFILE_ENROLLMENT_ENTRY_COUNT];
static uint64_t race_profile_global_window_started_at;
static uint8_t race_profile_global_enrollments;

typedef enum {
  RACE_PROFILE_LOAD_OK,
  RACE_PROFILE_LOAD_MISSING,
  RACE_PROFILE_LOAD_INVALID,
  RACE_PROFILE_LOAD_ERROR
} race_profile_load_result_t;

static bool Race_Profiles_RealPaths(const char *uid,
                                    char committed[MAX_OS_PATH],
                                    char candidate[MAX_OS_PATH]) {
  char committed_virtual[MAX_OS_PATH];
  char candidate_virtual[MAX_OS_PATH];

  if (!Race_Profile_Paths(uid,
                          committed_virtual, sizeof(committed_virtual),
                          candidate_virtual, sizeof(candidate_virtual))) {
    return false;
  }

  if (!Race_Persistence_CopyRealPath(committed_virtual,
                                     gi.RealPath(committed_virtual),
                                     committed, MAX_OS_PATH)) {
    return false;
  }

  if (!Race_Persistence_CopyRealPath(candidate_virtual,
                                     gi.RealPath(candidate_virtual),
                                     candidate, MAX_OS_PATH)) {
    return false;
  }

  return true;
}

static bool Race_Profiles_Save(const race_profile_t *profile,
                               const char *committed, const char *candidate) {
  char serialized[RACE_PROFILE_SERIALIZED_MAX];
  size_t serialized_length;

  if (!Race_Profile_Serialize(profile, serialized, sizeof(serialized), &serialized_length)) {
    G_Warn("Could not serialize Race profile metadata\n");
    return false;
  }

  race_persistence_result_t result = Race_Persistence_WriteCandidate(candidate,
                                                                     serialized,
                                                                     serialized_length);
  if (result != RACE_PERSISTENCE_OK) {
    G_Warn("Could not write Race profile candidate: %s\n",
           Race_Persistence_ResultName(result));
    return false;
  }

  result = Race_Persistence_Promote(candidate, committed);
  if (result != RACE_PERSISTENCE_OK) {
    G_Warn("Could not promote Race profile candidate: %s\n",
           Race_Persistence_ResultName(result));
    return false;
  }

  return true;
}

static race_profile_load_result_t Race_Profiles_Load(const char *expected_uid,
                                                     const char *committed,
                                                     race_profile_t *profile) {
  char serialized[RACE_PROFILE_SERIALIZED_MAX];
  size_t serialized_length;

  const race_persistence_result_t read_result = Race_Persistence_Read(committed,
                                                                     serialized,
                                                                     sizeof(serialized),
                                                                     &serialized_length);
  if (read_result == RACE_PERSISTENCE_NOT_FOUND) {
    return RACE_PROFILE_LOAD_MISSING;
  }

  if (read_result != RACE_PERSISTENCE_OK) {
    G_Warn("Could not read Race profile: %s\n",
           Race_Persistence_ResultName(read_result));
    return read_result == RACE_PERSISTENCE_TOO_LARGE
      ? RACE_PROFILE_LOAD_INVALID
      : RACE_PROFILE_LOAD_ERROR;
  }

  const race_profile_parse_result_t parse_result = Race_Profile_Parse(serialized,
                                                                      serialized_length,
                                                                      profile);
  if (parse_result != RACE_PROFILE_PARSE_OK || q_strcmp(profile->uid, expected_uid)) {
    if (parse_result == RACE_PROFILE_PARSE_UNKNOWN_VERSION) {
      G_Warn("Race profile uses an unsupported version; committed data was left unchanged\n");
    } else {
      G_Warn("Race profile is malformed; committed data was left unchanged\n");
    }
    return RACE_PROFILE_LOAD_INVALID;
  }

  return RACE_PROFILE_LOAD_OK;
}

static bool Race_Profiles_DuplicateIdentity(const g_client_t *cl, const char *uid) {
  bool duplicate = false;

  G_ForEachClient(other, {
    if (other != cl && other->persistent.race_profile.ready &&
        !q_strcmp(other->persistent.race_profile.uid, uid)) {
      duplicate = true;
      break;
    }
  });

  return duplicate;
}

static void Race_Profiles_ClearAssociation(g_client_t *cl) {
  if (cl) {
    Race_AdminAuth_ClearSecret(&cl->persistent.race_profile,
                               sizeof(cl->persistent.race_profile));
  }
}

static void Race_Profiles_ClientAddress(
  const g_client_t *cl, char address[RACE_CONNECTION_ADDRESS_SIZE]) {
  const char *source = cl
    ? InfoString_Get(cl->persistent.user_info, "ip")
    : NULL;
  if (!Race_ConnectionAddressKey(source, address)) {
    q_strlcpy(address, "unknown", RACE_CONNECTION_ADDRESS_SIZE);
  }
}

static bool Race_Profiles_EnrollmentAllowed(const g_client_t *cl,
                                            const uint64_t now) {
  if (!race_profile_global_enrollments ||
      now < race_profile_global_window_started_at ||
      now - race_profile_global_window_started_at >=
        RACE_PROFILE_ENROLLMENT_GLOBAL_WINDOW) {
    race_profile_global_window_started_at = now;
    race_profile_global_enrollments = 0u;
  }
  if (race_profile_global_enrollments >=
      RACE_PROFILE_ENROLLMENT_GLOBAL_LIMIT) {
    return false;
  }

  char address[RACE_CONNECTION_ADDRESS_SIZE];
  Race_Profiles_ClientAddress(cl, address);
  race_profile_enrollment_entry_t *entry = NULL;
  race_profile_enrollment_entry_t *available = NULL;
  for (size_t i = 0u; i < RACE_PROFILE_ENROLLMENT_ENTRY_COUNT; i++) {
    race_profile_enrollment_entry_t *candidate = race_profile_enrollments + i;
    if (candidate->in_use && !strcmp(candidate->address, address)) {
      entry = candidate;
      break;
    }
    if (!available && (!candidate->in_use ||
        now < candidate->window_started_at ||
        now - candidate->window_started_at >=
          RACE_PROFILE_ENROLLMENT_ADDRESS_WINDOW)) {
      available = candidate;
    }
  }
  if (!entry) {
    entry = available;
    if (!entry) {
      return false;
    }
    memset(entry, 0, sizeof(*entry));
    q_strlcpy(entry->address, address, sizeof(entry->address));
    entry->window_started_at = now;
    entry->in_use = true;
  } else if (now < entry->window_started_at ||
             now - entry->window_started_at >=
               RACE_PROFILE_ENROLLMENT_ADDRESS_WINDOW) {
    entry->window_started_at = now;
    entry->enrollments = 0u;
  }
  if (entry->enrollments >= RACE_PROFILE_ENROLLMENT_ADDRESS_LIMIT) {
    return false;
  }
  entry->enrollments++;
  entry->last_used_at = now;
  race_profile_global_enrollments++;
  return true;
}

static bool Race_Profiles_NewUid(char uid[RACE_PROFILE_UID_SIZE]) {
  uint8_t bytes[16];
  if (!Race_AdminPassword_RandomBytes(bytes, sizeof(bytes))) {
    return false;
  }
  bytes[6] = (uint8_t) ((bytes[6] & 0x0fu) | 0x40u);
  bytes[8] = (uint8_t) ((bytes[8] & 0x3fu) | 0x80u);
  const int32_t length = q_snprintf(
    uid, RACE_PROFILE_UID_SIZE,
    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
    "%02x%02x%02x%02x%02x%02x",
    bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
    bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
    bytes[12], bytes[13], bytes[14], bytes[15]);
  Race_AdminAuth_ClearSecret(bytes, sizeof(bytes));
  return length == RACE_PROFILE_UID_LENGTH;
}

static bool Race_Profiles_NewSecret(
  char secret[RACE_PROFILE_AUTH_SECRET_SIZE]) {
  uint8_t bytes[RACE_PROFILE_AUTH_SECRET_BYTES];
  const bool valid = Race_AdminPassword_RandomBytes(bytes, sizeof(bytes)) &&
    Race_AdminAuth_HexEncode(bytes, sizeof(bytes), secret,
                             RACE_PROFILE_AUTH_SECRET_SIZE);
  Race_AdminAuth_ClearSecret(bytes, sizeof(bytes));
  return valid;
}

static bool Race_Profiles_Create(g_client_t *cl, race_profile_t *profile,
                                 char secret[RACE_PROFILE_AUTH_SECRET_SIZE]) {
  for (size_t attempt = 0u; attempt < 8u; attempt++) {
    char uid[RACE_PROFILE_UID_SIZE];
    char committed[MAX_OS_PATH];
    char candidate[MAX_OS_PATH];
    if (!Race_Profiles_NewUid(uid) ||
        !Race_Profiles_NewSecret(secret) ||
        !Race_Profiles_RealPaths(uid, committed, candidate)) {
      return false;
    }

    race_profile_t existing;
    const race_profile_load_result_t load_result = Race_Profiles_Load(
      uid, committed, &existing);
    if (load_result == RACE_PROFILE_LOAD_OK) {
      continue;
    }
    if (load_result != RACE_PROFILE_LOAD_MISSING) {
      return false;
    }

    char credential[RACE_PROFILE_CREDENTIAL_SIZE];
    if (!Race_AdminPassword_Hash(secret, credential) ||
        !Race_Profile_Init(profile, uid, cl->persistent.net_name) ||
        !Race_Profile_SetCredential(profile, credential)) {
      Race_AdminAuth_ClearSecret(credential, sizeof(credential));
      return false;
    }
    Race_AdminAuth_ClearSecret(credential, sizeof(credential));
    if (!Race_Profiles_Save(profile, committed, candidate)) {
      return false;
    }
    return true;
  }
  return false;
}

static void Race_Profiles_SendRequest(g_client_t *cl) {
  gi.WriteByte(SV_CMD_RACE_PROFILE_REQUEST);
  gi.Unicast(cl, true);
}

static void Race_Profiles_Enroll(g_client_t *cl) {
  race_profile_association_t *association = &cl->persistent.race_profile;
  if (association->ready || association->enrollment_issued) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race profile enrollment rejected: this connection already has a profile request\n");
    return;
  }
  const uint64_t now = g_level.time;
  if (!Race_Profiles_EnrollmentAllowed(cl, now)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race profile enrollment temporarily throttled\n");
    return;
  }

  race_profile_t profile;
  char secret[RACE_PROFILE_AUTH_SECRET_SIZE] = { 0 };
  if (!Race_Profiles_Create(cl, &profile, secret)) {
    G_Warn("Could not create an authenticated Race profile for %s\n",
           cl->persistent.net_name);
    Race_AdminAuth_ClearSecret(secret, sizeof(secret));
    return;
  }

  association->enrollment_issued = true;
  gi.WriteByte(SV_CMD_RACE_PROFILE_ENROLLMENT);
  gi.WriteData(profile.uid, RACE_PROFILE_UID_LENGTH);
  gi.WriteData(secret, RACE_PROFILE_AUTH_SECRET_SIZE - 1u);
  gi.Unicast(cl, true);
  Race_AdminAuth_ClearSecret(secret, sizeof(secret));
  G_Debug("client=%s profile=enrollment-issued uid=%s\n",
          cl->persistent.net_name, profile.uid);
}

static void Race_Profiles_Challenge(g_client_t *cl, const char *input_uid) {
  race_profile_association_t *association = &cl->persistent.race_profile;
  char uid[RACE_PROFILE_UID_SIZE];
  if (association->ready ||
      !Race_Profile_CanonicalizeUid(input_uid, uid) ||
      strcmp(input_uid, uid) || Race_Profiles_DuplicateIdentity(cl, uid)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race profile challenge rejected: invalid or active identity\n");
    return;
  }

  char committed[MAX_OS_PATH];
  char candidate[MAX_OS_PATH];
  race_profile_t profile;
  if (!Race_Profiles_RealPaths(uid, committed, candidate) ||
      Race_Profiles_Load(uid, committed, &profile) != RACE_PROFILE_LOAD_OK ||
      !Race_Profile_HasCredential(&profile)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race profile unavailable or legacy-only. Use race_profile_reset to create a new authenticated profile.\n");
    return;
  }

  uint8_t salt[RACE_ADMIN_PASSWORD_SALT_SIZE];
  uint8_t nonce[RACE_PROFILE_AUTH_NONCE_SIZE];
  if (!Race_AdminAuth_CredentialSalt(profile.credential, salt) ||
      !Race_AdminPassword_RandomBytes(nonce, sizeof(nonce))) {
    G_Warn("Could not issue Race profile proof challenge\n");
    Race_AdminAuth_ClearSecret(salt, sizeof(salt));
    Race_AdminAuth_ClearSecret(nonce, sizeof(nonce));
    return;
  }

  Race_AdminAuth_ClearSecret(association->challenge_uid,
                             sizeof(association->challenge_uid));
  Race_AdminAuth_ClearSecret(association->challenge_nonce,
                             sizeof(association->challenge_nonce));
  q_strlcpy(association->challenge_uid, uid,
            sizeof(association->challenge_uid));
  memcpy(association->challenge_nonce, nonce, sizeof(nonce));
  association->challenge_issued_at = g_level.time;
  association->challenge_issued = true;
  gi.WriteByte(SV_CMD_RACE_PROFILE_CHALLENGE);
  gi.WriteData(uid, RACE_PROFILE_UID_LENGTH);
  gi.WriteData(salt, sizeof(salt));
  gi.WriteData(nonce, sizeof(nonce));
  gi.Unicast(cl, true);
  Race_AdminAuth_ClearSecret(salt, sizeof(salt));
  Race_AdminAuth_ClearSecret(nonce, sizeof(nonce));
}

static bool Race_Profiles_ConsumeChallenge(
  race_profile_association_t *association, const char *uid,
  const uint8_t nonce[RACE_PROFILE_AUTH_NONCE_SIZE], const uint64_t now) {
  const bool valid = association->challenge_issued && uid && nonce &&
    !strcmp(association->challenge_uid, uid) &&
    !memcmp(association->challenge_nonce, nonce,
            sizeof(association->challenge_nonce)) &&
    now >= association->challenge_issued_at &&
    now - association->challenge_issued_at <=
      RACE_PROFILE_CHALLENGE_LIFETIME;
  Race_AdminAuth_ClearSecret(association->challenge_uid,
                             sizeof(association->challenge_uid));
  Race_AdminAuth_ClearSecret(association->challenge_nonce,
                             sizeof(association->challenge_nonce));
  association->challenge_issued_at = 0u;
  association->challenge_issued = false;
  return valid;
}

static void Race_Profiles_Proof(g_client_t *cl, const char *input_uid,
                                const char *nonce_input,
                                const char *proof_input) {
  race_profile_association_t *association = &cl->persistent.race_profile;
  char uid[RACE_PROFILE_UID_SIZE];
  uint8_t nonce[RACE_PROFILE_AUTH_NONCE_SIZE] = { 0 };
  uint8_t proof[RACE_PROFILE_AUTH_PROOF_SIZE] = { 0 };
  const bool input_valid = Race_Profile_CanonicalizeUid(input_uid, uid) &&
    !strcmp(input_uid, uid) &&
    Race_AdminAuth_HexDecode(nonce_input, nonce, sizeof(nonce)) &&
    Race_AdminAuth_HexDecode(proof_input, proof, sizeof(proof));
  const bool challenge_valid = Race_Profiles_ConsumeChallenge(
    association, input_valid ? uid : NULL, nonce, g_level.time);
  if (!input_valid || !challenge_valid || association->ready ||
      Race_Profiles_DuplicateIdentity(cl, uid)) {
    gi.ClientPrint(cl, PRINT_HIGH, "Race profile proof rejected\n");
    Race_AdminAuth_ClearSecret(nonce, sizeof(nonce));
    Race_AdminAuth_ClearSecret(proof, sizeof(proof));
    return;
  }

  char committed[MAX_OS_PATH];
  char candidate[MAX_OS_PATH];
  race_profile_t profile;
  const bool valid = Race_Profiles_RealPaths(uid, committed, candidate) &&
    Race_Profiles_Load(uid, committed, &profile) == RACE_PROFILE_LOAD_OK &&
    Race_Profile_HasCredential(&profile) &&
    Race_ProfileAuth_VerifyProof(uid, profile.credential, nonce, proof);
  Race_AdminAuth_ClearSecret(nonce, sizeof(nonce));
  Race_AdminAuth_ClearSecret(proof, sizeof(proof));
  if (!valid) {
    gi.ClientPrint(cl, PRINT_HIGH, "Race profile proof rejected\n");
    return;
  }

  association->ready = true;
  q_strlcpy(association->uid, uid, sizeof(association->uid));
  if (strcmp(profile.display_name, cl->persistent.net_name)) {
    race_profile_t updated = profile;
    if (Race_Profile_SetDisplayName(&updated, cl->persistent.net_name) &&
        !Race_Profiles_Save(&updated, committed, candidate)) {
      G_Warn("Could not persist updated Race profile name for %s\n",
             cl->persistent.net_name);
    }
  }
  gi.ClientPrint(cl, PRINT_HIGH,
                 "Authenticated Race profile %s; ranked publication enabled\n",
                 uid);
}

void Race_Profiles_Init(void) {
  memset(race_profile_enrollments, 0, sizeof(race_profile_enrollments));
  race_profile_global_window_started_at = 0u;
  race_profile_global_enrollments = 0u;
  if (!gi.Mkdir(RACE_PROFILE_DIRECTORY)) {
    G_Warn("Could not prepare the Race profile storage directory\n");
  }
}

void Race_Profiles_ClientBegin(g_client_t *cl) {
  Race_Profiles_ClearAssociation(cl);
  if (!cl || cl->ai) {
    return;
  }
  Race_Profiles_SendRequest(cl);
}

void Race_Profiles_ClientDisconnect(g_client_t *cl) {
  Race_Profiles_ClearAssociation(cl);
}

void Race_Profiles_ClientUserInfoChanged(g_client_t *cl) {
  if (!cl || cl->ai || !cl->persistent.race_profile.ready) {
    return;
  }

  char committed[MAX_OS_PATH];
  char candidate[MAX_OS_PATH];
  race_profile_t profile;
  const char *uid = cl->persistent.race_profile.uid;
  if (!Race_Profiles_RealPaths(uid, committed, candidate) ||
      Race_Profiles_Load(uid, committed, &profile) != RACE_PROFILE_LOAD_OK ||
      !Race_Profile_HasCredential(&profile)) {
    G_Warn("Authenticated Race profile became unavailable for %s; ranking disabled\n",
           cl->persistent.net_name);
    Race_Profiles_ClearAssociation(cl);
    return;
  }
  if (strcmp(profile.display_name, cl->persistent.net_name)) {
    race_profile_t updated = profile;
    if (!Race_Profile_SetDisplayName(&updated, cl->persistent.net_name) ||
        !Race_Profiles_Save(&updated, committed, candidate)) {
      G_Warn("Could not persist updated Race profile name for %s\n",
             cl->persistent.net_name);
    }
  }
}

const char *Race_Profiles_AuthenticatedUid(const g_client_t *cl) {
  return cl && cl->persistent.race_profile.ready
    ? cl->persistent.race_profile.uid
    : NULL;
}

bool Race_Profiles_ClientCommand(g_client_t *cl, const char *cmd) {
  if (!cl || !cl->in_use || !cmd || strcmp(cmd, "race_profile")) {
    return false;
  }
  if (gi.Argc() == 2 && !strcmp(gi.Argv(1), "enroll")) {
    Race_Profiles_Enroll(cl);
  } else if (gi.Argc() == 3 && !strcmp(gi.Argv(1), "challenge")) {
    Race_Profiles_Challenge(cl, gi.Argv(2));
  } else if (gi.Argc() == 5 && !strcmp(gi.Argv(1), "proof")) {
    Race_Profiles_Proof(cl, gi.Argv(2), gi.Argv(3), gi.Argv(4));
  } else {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race profile protocol command rejected\n");
  }
  return true;
}
