/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_admin_admission.h"

#include <string.h>

#include "race_admin.h"

static uint64_t Race_AdminAdmission_SaturatingAdd(const uint64_t value,
                                                  const uint64_t increment) {
  return increment > UINT64_MAX - value ? UINT64_MAX : value + increment;
}

static race_admin_admission_entry_t *Race_AdminAdmission_Find(
  race_admin_admission_entry_t entries[RACE_ADMIN_ADMISSION_ENTRY_COUNT],
  const char *key) {
  if (!key || !*key) {
    return NULL;
  }
  for (size_t i = 0u; i < RACE_ADMIN_ADMISSION_ENTRY_COUNT; i++) {
    if (entries[i].in_use && !strcmp(entries[i].key, key)) {
      return &entries[i];
    }
  }
  return NULL;
}

static race_admin_admission_entry_t *Race_AdminAdmission_Get(
  race_admin_admission_entry_t entries[RACE_ADMIN_ADMISSION_ENTRY_COUNT],
  const char *key, const uint64_t now) {
  race_admin_admission_entry_t *entry = Race_AdminAdmission_Find(entries, key);
  if (entry) {
    entry->last_used_at = now;
    return entry;
  }

  size_t selected = 0u;
  for (size_t i = 0u; i < RACE_ADMIN_ADMISSION_ENTRY_COUNT; i++) {
    if (!entries[i].in_use) {
      selected = i;
      break;
    }
    if (entries[i].last_used_at < entries[selected].last_used_at) {
      selected = i;
    }
  }
  entry = &entries[selected];
  memset(entry, 0, sizeof(*entry));
  const size_t length = strnlen(key, sizeof(entry->key));
  if (!length || length >= sizeof(entry->key)) {
    return NULL;
  }
  memcpy(entry->key, key, length + 1u);
  entry->last_used_at = now;
  entry->in_use = true;
  return entry;
}

static bool Race_AdminAdmission_EntryAllowed(
  race_admin_admission_entry_t entries[RACE_ADMIN_ADMISSION_ENTRY_COUNT],
  const char *key, const uint64_t now) {
  race_admin_admission_entry_t *entry = Race_AdminAdmission_Find(entries, key);
  return !entry || Race_Admin_LoginThrottleAllowed(&entry->throttle, now);
}

static bool Race_AdminAdmission_GlobalAllowed(race_admin_admission_t *admission,
                                              const uint64_t now) {
  if (admission->global_blocked_until) {
    if (now < admission->global_blocked_until) {
      return false;
    }
    admission->global_blocked_until = 0u;
    admission->global_failures = 0u;
    admission->global_window_started_at = now;
  }
  if (!admission->global_failures ||
      now < admission->global_window_started_at ||
      now - admission->global_window_started_at >=
        RACE_ADMIN_ADMISSION_GLOBAL_WINDOW) {
    admission->global_failures = 0u;
    admission->global_window_started_at = now;
  }
  return true;
}

static bool Race_AdminAdmission_ChallengeAllowed(
  race_admin_admission_t *admission, const uint64_t now) {
  if (admission->challenge_blocked_until) {
    if (now < admission->challenge_blocked_until) {
      return false;
    }
    admission->challenge_blocked_until = 0u;
    admission->challenges = 0u;
    admission->challenge_window_started_at = now;
  }
  if (!admission->challenges || now < admission->challenge_window_started_at ||
      now - admission->challenge_window_started_at >=
        RACE_ADMIN_ADMISSION_CHALLENGE_WINDOW) {
    admission->challenges = 0u;
    admission->challenge_window_started_at = now;
  }
  if (admission->challenges >=
      RACE_ADMIN_ADMISSION_GLOBAL_CHALLENGE_LIMIT) {
    admission->challenge_blocked_until =
      Race_AdminAdmission_SaturatingAdd(
        now, RACE_ADMIN_ADMISSION_CHALLENGE_COOLDOWN);
    return false;
  }
  admission->challenges++;
  return true;
}

void Race_AdminAdmission_Init(race_admin_admission_t *admission) {
  if (admission) {
    memset(admission, 0, sizeof(*admission));
  }
}

bool Race_AdminAdmission_BeginChallenge(
  race_admin_admission_t *admission, const char *account,
  const char *address, const uint64_t now) {
  return admission && account && address &&
         Race_AdminAdmission_GlobalAllowed(admission, now) &&
         Race_AdminAdmission_EntryAllowed(admission->accounts, account, now) &&
         Race_AdminAdmission_EntryAllowed(admission->addresses, address, now) &&
         Race_AdminAdmission_ChallengeAllowed(admission, now);
}

bool Race_AdminAdmission_ProofAllowed(
  race_admin_admission_t *admission, const char *account,
  const char *address, const uint64_t now) {
  return admission && account && address &&
         Race_AdminAdmission_GlobalAllowed(admission, now) &&
         Race_AdminAdmission_EntryAllowed(admission->accounts, account, now) &&
         Race_AdminAdmission_EntryAllowed(admission->addresses, address, now);
}

void Race_AdminAdmission_RecordFailure(
  race_admin_admission_t *admission, const char *account,
  const char *address, const uint64_t now) {
  if (!admission || !Race_AdminAdmission_GlobalAllowed(admission, now)) {
    return;
  }

  race_admin_admission_entry_t *account_entry = Race_AdminAdmission_Get(
    admission->accounts, account, now);
  race_admin_admission_entry_t *address_entry = Race_AdminAdmission_Get(
    admission->addresses, address, now);
  if (account_entry) {
    Race_Admin_LoginThrottleRecordFailure(&account_entry->throttle, now);
  }
  if (address_entry) {
    Race_Admin_LoginThrottleRecordFailure(&address_entry->throttle, now);
  }

  admission->global_failures++;
  if (admission->global_failures >=
      RACE_ADMIN_ADMISSION_GLOBAL_FAILURE_LIMIT) {
    admission->global_failures = RACE_ADMIN_ADMISSION_GLOBAL_FAILURE_LIMIT;
    admission->global_blocked_until =
      Race_AdminAdmission_SaturatingAdd(
        now, RACE_ADMIN_ADMISSION_GLOBAL_COOLDOWN);
  }
}

void Race_AdminAdmission_RecordSuccess(
  race_admin_admission_t *admission, const char *account,
  const uint64_t now) {
  if (!admission) {
    return;
  }
  race_admin_admission_entry_t *entry = Race_AdminAdmission_Find(
    admission->accounts, account);
  if (entry) {
    Race_Admin_LoginThrottleClear(&entry->throttle);
    entry->last_used_at = now;
  }
}
