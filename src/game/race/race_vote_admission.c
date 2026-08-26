/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_vote_admission.h"

#include <string.h>

#include "race_connection_address.h"
#include "race_profile.h"
#include "race_vote.h"

typedef struct {
  bool in_use;
  char key[RACE_CONNECTION_ADDRESS_SIZE];
  uint32_t next_start_time;
  uint8_t starts;
} race_vote_admission_entry_t;

static race_vote_admission_entry_t
  race_vote_profile_admission[RACE_VOTE_ADMISSION_ENTRY_COUNT];
static race_vote_admission_entry_t
  race_vote_address_admission[RACE_VOTE_ADMISSION_ENTRY_COUNT];

static bool Race_VoteAdmission_IdentityValid(
  const race_vote_admission_identity_t *identity) {
  if (!identity || !identity->address) {
    return false;
  }

  char canonical_address[RACE_CONNECTION_ADDRESS_SIZE];
  if (!Race_ConnectionAddressKey(identity->address, canonical_address) ||
      strcmp(identity->address, canonical_address)) {
    return false;
  }

  if (identity->profile_uid && *identity->profile_uid) {
    char canonical_uid[RACE_PROFILE_UID_SIZE];
    if (!Race_Profile_CanonicalizeUid(identity->profile_uid, canonical_uid) ||
        strcmp(identity->profile_uid, canonical_uid)) {
      return false;
    }
  }
  return true;
}

static race_vote_admission_entry_t *Race_VoteAdmission_Find(
  race_vote_admission_entry_t entries[RACE_VOTE_ADMISSION_ENTRY_COUNT],
  const char *key) {
  for (size_t i = 0u; i < RACE_VOTE_ADMISSION_ENTRY_COUNT; i++) {
    if (entries[i].in_use && !strcmp(entries[i].key, key)) {
      return entries + i;
    }
  }
  return NULL;
}

static race_vote_admission_entry_t *Race_VoteAdmission_Available(
  race_vote_admission_entry_t entries[RACE_VOTE_ADMISSION_ENTRY_COUNT]) {
  for (size_t i = 0u; i < RACE_VOTE_ADMISSION_ENTRY_COUNT; i++) {
    if (!entries[i].in_use) {
      return entries + i;
    }
  }
  return NULL;
}

static race_vote_admission_result_t Race_VoteAdmission_CheckTable(
  race_vote_admission_entry_t entries[RACE_VOTE_ADMISSION_ENTRY_COUNT],
  const char *key, const uint32_t now, const uint8_t maximum_starts,
  uint32_t *cooldown_remaining) {
  race_vote_admission_entry_t *entry = Race_VoteAdmission_Find(entries, key);
  if (!entry) {
    if (!Race_VoteAdmission_Available(entries)) {
      return RACE_VOTE_ADMISSION_CAPACITY;
    }
    return maximum_starts
      ? RACE_VOTE_ADMISSION_AVAILABLE
      : RACE_VOTE_ADMISSION_LIMIT;
  }

  const race_vote_start_availability_t availability =
    Race_Vote_StartAvailability(now, entry->next_start_time, entry->starts,
                                maximum_starts);
  if (availability == RACE_VOTE_START_LIMIT) {
    return RACE_VOTE_ADMISSION_LIMIT;
  }
  if (availability == RACE_VOTE_START_COOLDOWN) {
    if (cooldown_remaining) {
      *cooldown_remaining = Race_Vote_TimeRemaining(
        now, entry->next_start_time);
    }
    return RACE_VOTE_ADMISSION_COOLDOWN;
  }
  return RACE_VOTE_ADMISSION_AVAILABLE;
}

void Race_VoteAdmission_Reset(void) {
  memset(race_vote_profile_admission, 0,
         sizeof(race_vote_profile_admission));
  memset(race_vote_address_admission, 0,
         sizeof(race_vote_address_admission));
}

race_vote_admission_result_t Race_VoteAdmission_Check(
  const race_vote_admission_identity_t *identity, const uint32_t now,
  const uint8_t maximum_starts, uint32_t *cooldown_remaining) {
  if (cooldown_remaining) {
    *cooldown_remaining = 0u;
  }
  if (!Race_VoteAdmission_IdentityValid(identity)) {
    return RACE_VOTE_ADMISSION_INVALID;
  }

  race_vote_admission_result_t result = Race_VoteAdmission_CheckTable(
    race_vote_address_admission, identity->address, now, maximum_starts,
    cooldown_remaining);
  if (result != RACE_VOTE_ADMISSION_AVAILABLE) {
    return result;
  }
  if (identity->profile_uid && *identity->profile_uid) {
    result = Race_VoteAdmission_CheckTable(
      race_vote_profile_admission, identity->profile_uid, now,
      maximum_starts, cooldown_remaining);
  }
  return result;
}

static race_vote_admission_entry_t *Race_VoteAdmission_Entry(
  race_vote_admission_entry_t entries[RACE_VOTE_ADMISSION_ENTRY_COUNT],
  const char *key) {
  race_vote_admission_entry_t *entry = Race_VoteAdmission_Find(entries, key);
  return entry ? entry : Race_VoteAdmission_Available(entries);
}

static void Race_VoteAdmission_RecordEntry(
  race_vote_admission_entry_t *entry, const char *key,
  const uint32_t next_start_time) {
  if (!entry->in_use) {
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->key, key, strlen(key) + 1u);
    entry->in_use = true;
  }
  entry->starts++;
  entry->next_start_time = next_start_time;
}

bool Race_VoteAdmission_Record(
  const race_vote_admission_identity_t *identity, const uint32_t now,
  const uint32_t next_start_time, const uint8_t maximum_starts) {
  if (Race_VoteAdmission_Check(identity, now, maximum_starts, NULL) !=
      RACE_VOTE_ADMISSION_AVAILABLE) {
    return false;
  }

  race_vote_admission_entry_t *address_entry = Race_VoteAdmission_Entry(
    race_vote_address_admission, identity->address);
  race_vote_admission_entry_t *profile_entry = NULL;
  if (identity->profile_uid && *identity->profile_uid) {
    profile_entry = Race_VoteAdmission_Entry(
      race_vote_profile_admission, identity->profile_uid);
  }
  if (!address_entry || ((identity->profile_uid && *identity->profile_uid) &&
                         !profile_entry)) {
    return false;
  }

  Race_VoteAdmission_RecordEntry(address_entry, identity->address,
                                 next_start_time);
  if (profile_entry) {
    Race_VoteAdmission_RecordEntry(profile_entry, identity->profile_uid,
                                   next_start_time);
  }
  return true;
}
