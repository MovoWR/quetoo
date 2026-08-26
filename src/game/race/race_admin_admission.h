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

#include "race_admin_types.h"

#define RACE_ADMIN_ADMISSION_KEY_SIZE 64u
#define RACE_ADMIN_ADMISSION_ENTRY_COUNT 128u
#define RACE_ADMIN_ADMISSION_GLOBAL_FAILURE_LIMIT 64u
#define RACE_ADMIN_ADMISSION_GLOBAL_CHALLENGE_LIMIT 64u
#define RACE_ADMIN_ADMISSION_GLOBAL_WINDOW 60000u
#define RACE_ADMIN_ADMISSION_GLOBAL_COOLDOWN 30000u
#define RACE_ADMIN_ADMISSION_CHALLENGE_WINDOW 1000u
#define RACE_ADMIN_ADMISSION_CHALLENGE_COOLDOWN 1000u

typedef struct {
  char key[RACE_ADMIN_ADMISSION_KEY_SIZE];
  race_admin_login_throttle_t throttle;
  uint64_t last_used_at;
  bool in_use;
} race_admin_admission_entry_t;

typedef struct {
  race_admin_admission_entry_t accounts[RACE_ADMIN_ADMISSION_ENTRY_COUNT];
  race_admin_admission_entry_t addresses[RACE_ADMIN_ADMISSION_ENTRY_COUNT];
  uint64_t global_window_started_at;
  uint64_t global_blocked_until;
  uint64_t challenge_window_started_at;
  uint64_t challenge_blocked_until;
  uint16_t global_failures;
  uint16_t challenges;
} race_admin_admission_t;

void Race_AdminAdmission_Init(race_admin_admission_t *admission);
bool Race_AdminAdmission_BeginChallenge(
  race_admin_admission_t *admission, const char *account,
  const char *address, uint64_t now);
bool Race_AdminAdmission_ProofAllowed(
  race_admin_admission_t *admission, const char *account,
  const char *address, uint64_t now);
void Race_AdminAdmission_RecordFailure(
  race_admin_admission_t *admission, const char *account,
  const char *address, uint64_t now);
void Race_AdminAdmission_RecordSuccess(
  race_admin_admission_t *admission, const char *account, uint64_t now);
