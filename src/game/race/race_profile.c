/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_profile.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RACE_PROFILE_MAGIC "RACE_PROFILE_V1"
#define RACE_PROFILE_MAGIC_PREFIX "RACE_PROFILE_V"

static bool Race_Profile_IsHex(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static char Race_Profile_LowerHex(char c) {
  return c >= 'A' && c <= 'F' ? (char) (c - 'A' + 'a') : c;
}

static int32_t Race_Profile_HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }

  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }

  return -1;
}

static bool Race_Profile_BoundedLength(const char *string, size_t maximum, size_t *length) {
  if (!string) {
    return false;
  }

  size_t len = 0;
  while (len <= maximum && string[len]) {
    len++;
  }

  if (len > maximum) {
    return false;
  }

  if (length) {
    *length = len;
  }

  return true;
}

bool Race_Profile_CanonicalizeUid(const char *input, char output[RACE_PROFILE_UID_SIZE]) {
  if (!input || !output || strlen(input) != RACE_PROFILE_UID_LENGTH) {
    return false;
  }

  for (size_t i = 0; i < RACE_PROFILE_UID_LENGTH; i++) {
    const bool separator = i == 8 || i == 13 || i == 18 || i == 23;

    if (separator) {
      if (input[i] != '-') {
        return false;
      }
      output[i] = '-';
    } else {
      if (!Race_Profile_IsHex(input[i])) {
        return false;
      }
      output[i] = Race_Profile_LowerHex(input[i]);
    }
  }

  output[RACE_PROFILE_UID_LENGTH] = '\0';

  if (output[14] != '4' ||
      (output[19] != '8' && output[19] != '9' &&
       output[19] != 'a' && output[19] != 'b')) {
    output[0] = '\0';
    return false;
  }

  return true;
}

bool Race_Profile_SetDisplayName(race_profile_t *profile, const char *display_name) {
  size_t length;

  if (!profile || !Race_Profile_BoundedLength(display_name, RACE_PROFILE_NAME_MAX, &length)) {
    return false;
  }

  memcpy(profile->display_name, display_name, length + 1);
  return true;
}

bool Race_Profile_Init(race_profile_t *profile, const char *uid, const char *display_name) {
  if (!profile) {
    return false;
  }

  memset(profile, 0, sizeof(*profile));

  if (!Race_Profile_CanonicalizeUid(uid, profile->uid) ||
      !Race_Profile_SetDisplayName(profile, display_name)) {
    memset(profile, 0, sizeof(*profile));
    return false;
  }

  return true;
}

bool Race_Profile_Paths(const char *uid,
                        char *committed, size_t committed_size,
                        char *candidate, size_t candidate_size) {
  char canonical[RACE_PROFILE_UID_SIZE];

  if (!committed || !committed_size || !candidate || !candidate_size ||
      !Race_Profile_CanonicalizeUid(uid, canonical)) {
    return false;
  }

  const int32_t committed_length = snprintf(committed, committed_size,
                                            RACE_PROFILE_DIRECTORY "/%s.profile", canonical);
  const int32_t candidate_length = snprintf(candidate, candidate_size,
                                            RACE_PROFILE_DIRECTORY "/%s.candidate", canonical);

  return committed_length >= 0 && (size_t) committed_length < committed_size &&
         candidate_length >= 0 && (size_t) candidate_length < candidate_size;
}

bool Race_Profile_Serialize(const race_profile_t *profile,
                            char *output, size_t output_size, size_t *output_length) {
  static const char hex[] = "0123456789abcdef";
  char canonical[RACE_PROFILE_UID_SIZE];
  char encoded_name[RACE_PROFILE_NAME_MAX * 2 + 1];
  size_t name_length;

  if (!profile || !output || !output_size ||
      !Race_Profile_CanonicalizeUid(profile->uid, canonical) ||
      strcmp(canonical, profile->uid) ||
      !Race_Profile_BoundedLength(profile->display_name, RACE_PROFILE_NAME_MAX, &name_length)) {
    return false;
  }

  for (size_t i = 0; i < name_length; i++) {
    const unsigned char c = (unsigned char) profile->display_name[i];
    encoded_name[i * 2] = hex[c >> 4];
    encoded_name[i * 2 + 1] = hex[c & 0xf];
  }
  encoded_name[name_length * 2] = '\0';

  const int32_t length = snprintf(output, output_size,
                                  RACE_PROFILE_MAGIC "\nuid=%s\nname=%s\n",
                                  profile->uid, encoded_name);

  if (length < 0 || (size_t) length >= output_size ||
      (size_t) length > RACE_PROFILE_SERIALIZED_MAX) {
    return false;
  }

  if (output_length) {
    *output_length = (size_t) length;
  }

  return true;
}

race_profile_parse_result_t Race_Profile_Parse(const void *data, size_t length,
                                               race_profile_t *profile) {
  if (!data || !profile) {
    return RACE_PROFILE_PARSE_MALFORMED;
  }

  if (length > RACE_PROFILE_SERIALIZED_MAX) {
    return RACE_PROFILE_PARSE_TOO_LARGE;
  }

  if (!length) {
    return RACE_PROFILE_PARSE_MALFORMED;
  }

  char text[RACE_PROFILE_SERIALIZED_MAX + 1];
  memcpy(text, data, length);
  text[length] = '\0';

  if (memchr(text, '\0', length)) {
    return RACE_PROFILE_PARSE_MALFORMED;
  }

  char *line1 = text;
  char *line2 = strchr(line1, '\n');
  if (!line2) {
    return RACE_PROFILE_PARSE_MALFORMED;
  }
  *line2++ = '\0';

  if (strcmp(line1, RACE_PROFILE_MAGIC)) {
    if (!strncmp(line1, RACE_PROFILE_MAGIC_PREFIX,
                 sizeof(RACE_PROFILE_MAGIC_PREFIX) - 1)) {
      return RACE_PROFILE_PARSE_UNKNOWN_VERSION;
    }
    return RACE_PROFILE_PARSE_MALFORMED;
  }

  char *line3 = strchr(line2, '\n');
  if (!line3) {
    return RACE_PROFILE_PARSE_MALFORMED;
  }
  *line3++ = '\0';

  char *end = strchr(line3, '\n');
  if (!end || end[1] != '\0') {
    return RACE_PROFILE_PARSE_MALFORMED;
  }
  *end = '\0';

  if (strncmp(line2, "uid=", 4) || strncmp(line3, "name=", 5)) {
    return RACE_PROFILE_PARSE_MALFORMED;
  }

  const char *uid = line2 + 4;
  char canonical[RACE_PROFILE_UID_SIZE];
  if (!Race_Profile_CanonicalizeUid(uid, canonical) || strcmp(uid, canonical)) {
    return RACE_PROFILE_PARSE_MALFORMED;
  }

  const char *encoded_name = line3 + 5;
  const size_t encoded_length = strlen(encoded_name);
  if ((encoded_length & 1) || encoded_length > RACE_PROFILE_NAME_MAX * 2) {
    return RACE_PROFILE_PARSE_MALFORMED;
  }

  race_profile_t parsed = { 0 };
  memcpy(parsed.uid, canonical, sizeof(parsed.uid));

  const size_t name_length = encoded_length / 2;
  for (size_t i = 0; i < name_length; i++) {
    const int32_t high = Race_Profile_HexValue(encoded_name[i * 2]);
    const int32_t low = Race_Profile_HexValue(encoded_name[i * 2 + 1]);

    if (high < 0 || low < 0) {
      return RACE_PROFILE_PARSE_MALFORMED;
    }

    const char c = (char) ((high << 4) | low);
    if (!c) {
      return RACE_PROFILE_PARSE_MALFORMED;
    }
    parsed.display_name[i] = c;
  }
  parsed.display_name[name_length] = '\0';

  *profile = parsed;
  return RACE_PROFILE_PARSE_OK;
}
