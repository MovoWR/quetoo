/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_leaderboard_wire.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(RACE_LEADERBOARD_CONFIG_MAX_BYTES < MAX_STRING_CHARS,
               "Race leaderboard configstring exceeds MAX_STRING_CHARS");

static bool Race_LeaderboardWire_Unsigned(const char *value,
                                          const uint64_t maximum,
                                          uint64_t *parsed) {
  if (!value || !*value || !parsed) {
    return false;
  }

  for (const char *c = value; *c; c++) {
    if (*c < '0' || *c > '9') {
      return false;
    }
  }

  errno = 0;
  char *end = NULL;
  const unsigned long long number = strtoull(value, &end, 10);
  if (errno == ERANGE || !end || *end || number > maximum) {
    return false;
  }

  *parsed = (uint64_t) number;
  return true;
}

static char *Race_LeaderboardWire_Token(char **cursor) {
  if (!cursor || !*cursor) {
    return NULL;
  }

  char *token = *cursor;
  char *delimiter = strchr(token, '\\');
  if (delimiter) {
    *delimiter = '\0';
    *cursor = delimiter + 1;
  } else {
    *cursor = NULL;
  }
  return token;
}

static bool Race_LeaderboardWire_Name(const char *source,
                                      char output[RACE_LEADERBOARD_MAX_NAME_BYTES + 1u]) {
  if (!source || !output) {
    return false;
  }

  size_t length = 0;
  while (*source && length < RACE_LEADERBOARD_MAX_NAME_BYTES) {
    char value = *source++;
    if (value == '\\' || value == '\r' || value == '\n') {
      value = ' ';
    }
    output[length++] = value;
  }
  output[length] = '\0';
  return length != 0;
}

bool Race_LeaderboardWire_Encode(
  const race_leaderboard_wire_entry_t *entries, const size_t count,
  char *output, const size_t output_size) {
  if (!output || !output_size || count > RACE_LEADERBOARD_TOP_MAX ||
      (count && !entries)) {
    return false;
  }

  int32_t written = snprintf(output, output_size, "%s\\%zu",
                             RACE_LEADERBOARD_CONFIG_VERSION, count);
  if (written <= 0 || (size_t) written >= output_size) {
    output[0] = '\0';
    return false;
  }

  size_t offset = (size_t) written;
  uint32_t previous_time = 0;
  for (size_t i = 0; i < count; i++) {
    const race_leaderboard_wire_entry_t *entry = entries + i;
    char safe_name[RACE_LEADERBOARD_MAX_NAME_BYTES + 1u];
    if (!Race_LeaderboardWire_Name(entry->name, safe_name) ||
        !entry->time_ms || entry->time_ms > RACE_LEADERBOARD_MAX_TIME_MS ||
        entry->date_unix_s > RACE_LEADERBOARD_MAX_DATE_UNIX_S ||
        (i && entry->time_ms < previous_time)) {
      output[0] = '\0';
      return false;
    }

    written = snprintf(output + offset, output_size - offset,
                       "\\%s\\%u\\%" PRIu64, safe_name,
                       entry->time_ms, entry->date_unix_s);
    if (written <= 0 || (size_t) written >= output_size - offset) {
      output[0] = '\0';
      return false;
    }
    offset += (size_t) written;
    previous_time = entry->time_ms;
  }

  return true;
}

bool Race_LeaderboardWire_Decode(
  const char *source, race_leaderboard_wire_entry_t *entries,
  const size_t capacity, size_t *count) {
  if (!count || capacity > RACE_LEADERBOARD_TOP_MAX ||
      (capacity && !entries)) {
    return false;
  }
  *count = 0;

  if (!source || !*source) {
    return true;
  }

  char copy[MAX_STRING_CHARS];
  if (strlen(source) >= sizeof(copy)) {
    return false;
  }
  memcpy(copy, source, strlen(source) + 1u);

  char *cursor = copy;
  const char *version = Race_LeaderboardWire_Token(&cursor);
  const char *count_text = Race_LeaderboardWire_Token(&cursor);
  uint64_t parsed_count;
  if (!version || strcmp(version, RACE_LEADERBOARD_CONFIG_VERSION) ||
      !Race_LeaderboardWire_Unsigned(count_text, RACE_LEADERBOARD_TOP_MAX,
                                     &parsed_count) ||
      parsed_count > capacity) {
    return false;
  }

  uint32_t previous_time = 0;
  for (size_t i = 0; i < (size_t) parsed_count; i++) {
    const char *name = Race_LeaderboardWire_Token(&cursor);
    const char *time_text = Race_LeaderboardWire_Token(&cursor);
    const char *date_text = Race_LeaderboardWire_Token(&cursor);
    const size_t name_length = name ? strlen(name) : 0u;
    uint64_t time_ms;
    uint64_t date_unix_s;
    if (!name_length || name_length > RACE_LEADERBOARD_MAX_NAME_BYTES ||
        !Race_LeaderboardWire_Unsigned(time_text,
                                       RACE_LEADERBOARD_MAX_TIME_MS, &time_ms) ||
        !time_ms ||
        !Race_LeaderboardWire_Unsigned(date_text,
                                       RACE_LEADERBOARD_MAX_DATE_UNIX_S,
                                       &date_unix_s) ||
        (i && time_ms < previous_time)) {
      return false;
    }

    race_leaderboard_wire_entry_t *entry = entries + i;
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, name, name_length + 1u);
    entry->time_ms = (uint32_t) time_ms;
    entry->date_unix_s = date_unix_s;
    previous_time = entry->time_ms;
  }

  if (cursor) {
    return false;
  }

  *count = (size_t) parsed_count;
  return true;
}
