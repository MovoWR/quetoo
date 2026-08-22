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

#define RACE_PROFILE_UID_LENGTH 36
#define RACE_PROFILE_UID_SIZE (RACE_PROFILE_UID_LENGTH + 1)
#define RACE_PROFILE_NAME_MAX 63
#define RACE_PROFILE_NAME_SIZE (RACE_PROFILE_NAME_MAX + 1)
#define RACE_PROFILE_SERIALIZED_MAX 256

#define RACE_PROFILE_DIRECTORY "profiles"

typedef struct {
  char uid[RACE_PROFILE_UID_SIZE];
  char display_name[RACE_PROFILE_NAME_SIZE];
} race_profile_t;

/**
 * @brief Connection-local reference to a durable Race profile.
 */
typedef struct {
  bool ready;
  char uid[RACE_PROFILE_UID_SIZE];
} race_profile_association_t;

typedef enum {
  RACE_PROFILE_PARSE_OK,
  RACE_PROFILE_PARSE_MALFORMED,
  RACE_PROFILE_PARSE_UNKNOWN_VERSION,
  RACE_PROFILE_PARSE_TOO_LARGE
} race_profile_parse_result_t;

bool Race_Profile_CanonicalizeUid(const char *input, char output[RACE_PROFILE_UID_SIZE]);
bool Race_Profile_Init(race_profile_t *profile, const char *uid, const char *display_name);
bool Race_Profile_SetDisplayName(race_profile_t *profile, const char *display_name);
bool Race_Profile_Paths(const char *uid,
                        char *committed, size_t committed_size,
                        char *candidate, size_t candidate_size);
bool Race_Profile_Serialize(const race_profile_t *profile,
                            char *output, size_t output_size, size_t *output_length);
race_profile_parse_result_t Race_Profile_Parse(const void *data, size_t length,
                                               race_profile_t *profile);
