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

#define RACE_VOTE_ADMISSION_ENTRY_COUNT 128u

typedef struct {
  const char *profile_uid;
  const char *address;
} race_vote_admission_identity_t;

typedef enum {
  RACE_VOTE_ADMISSION_AVAILABLE,
  RACE_VOTE_ADMISSION_COOLDOWN,
  RACE_VOTE_ADMISSION_LIMIT,
  RACE_VOTE_ADMISSION_CAPACITY,
  RACE_VOTE_ADMISSION_INVALID
} race_vote_admission_result_t;

void Race_VoteAdmission_Reset(void);
race_vote_admission_result_t Race_VoteAdmission_Check(
  const race_vote_admission_identity_t *identity, uint32_t now,
  uint8_t maximum_starts, uint32_t *cooldown_remaining);
bool Race_VoteAdmission_Record(
  const race_vote_admission_identity_t *identity, uint32_t now,
  uint32_t next_start_time, uint8_t maximum_starts);
