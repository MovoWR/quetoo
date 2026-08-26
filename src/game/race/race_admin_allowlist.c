/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_admin_allowlist.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "race_persistence.h"

static bool Race_AdminAllowlist_NameEqualsIgnoreCase(const char *left,
                                                      const char *right) {
  if (!left || !right) {
    return false;
  }

  while (*left && *right) {
    if (tolower((unsigned char) *left) !=
        tolower((unsigned char) *right)) {
      return false;
    }
    left++;
    right++;
  }
  return *left == *right;
}

bool Race_AdminAllowlist_NameValid(const char *name) {
  if (!name) {
    return false;
  }

  const size_t length = strnlen(name, RACE_ADMIN_CVAR_NAME_SIZE);
  if (!length || length > RACE_ADMIN_CVAR_NAME_MAX) {
    return false;
  }

  for (size_t i = 0; i < length; i++) {
    const unsigned char c = (unsigned char) name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
      return false;
    }
  }
  return !Race_AdminAllowlist_NameEqualsIgnoreCase(name, "rcon_password");
}

bool Race_AdminAllowlist_Init(race_admin_allowlist_t *allowlist) {
  if (!allowlist) {
    return false;
  }
  memset(allowlist, 0, sizeof(*allowlist));
  return true;
}

bool Race_AdminAllowlist_Valid(const race_admin_allowlist_t *allowlist) {
  if (!allowlist || allowlist->count > RACE_ADMIN_MAX_OPERATOR_CVARS) {
    return false;
  }

  for (size_t i = 0; i < allowlist->count; i++) {
    if (!Race_AdminAllowlist_NameValid(allowlist->names[i]) ||
        (i && strcmp(allowlist->names[i - 1u], allowlist->names[i]) >= 0)) {
      return false;
    }
  }
  return true;
}

bool Race_AdminAllowlist_Equals(const race_admin_allowlist_t *left,
                                const race_admin_allowlist_t *right) {
  if (!Race_AdminAllowlist_Valid(left) ||
      !Race_AdminAllowlist_Valid(right) || left->count != right->count) {
    return false;
  }

  for (size_t i = 0; i < left->count; i++) {
    if (strcmp(left->names[i], right->names[i])) {
      return false;
    }
  }
  return true;
}

static size_t Race_AdminAllowlist_LowerBound(
  const race_admin_allowlist_t *allowlist, const char *name) {
  size_t begin = 0;
  size_t end = allowlist->count;
  while (begin < end) {
    const size_t middle = begin + (end - begin) / 2u;
    if (strcmp(allowlist->names[middle], name) < 0) {
      begin = middle + 1u;
    } else {
      end = middle;
    }
  }
  return begin;
}

bool Race_AdminAllowlist_Contains(const race_admin_allowlist_t *allowlist,
                                  const char *name) {
  if (!Race_AdminAllowlist_Valid(allowlist) ||
      !Race_AdminAllowlist_NameValid(name)) {
    return false;
  }

  const size_t index = Race_AdminAllowlist_LowerBound(allowlist, name);
  return index < allowlist->count && !strcmp(allowlist->names[index], name);
}

bool Race_AdminAllowlist_Add(const race_admin_allowlist_t *current,
                             const char *name,
                             race_admin_allowlist_t *candidate) {
  if (!Race_AdminAllowlist_Valid(current) ||
      !Race_AdminAllowlist_NameValid(name) || !candidate ||
      current->count >= RACE_ADMIN_MAX_OPERATOR_CVARS ||
      Race_AdminAllowlist_Contains(current, name)) {
    return false;
  }

  *candidate = *current;
  const size_t index = Race_AdminAllowlist_LowerBound(candidate, name);
  memmove(candidate->names + index + 1u, candidate->names + index,
          (candidate->count - index) * sizeof(candidate->names[0]));
  memcpy(candidate->names[index], name, strlen(name) + 1u);
  candidate->count++;
  return Race_AdminAllowlist_Valid(candidate);
}

bool Race_AdminAllowlist_Remove(const race_admin_allowlist_t *current,
                                const char *name,
                                race_admin_allowlist_t *candidate) {
  if (!Race_AdminAllowlist_Valid(current) ||
      !Race_AdminAllowlist_NameValid(name) || !candidate ||
      !Race_AdminAllowlist_Contains(current, name)) {
    return false;
  }

  *candidate = *current;
  const size_t index = Race_AdminAllowlist_LowerBound(candidate, name);
  memmove(candidate->names + index, candidate->names + index + 1u,
          (candidate->count - index - 1u) * sizeof(candidate->names[0]));
  candidate->count--;
  memset(candidate->names[candidate->count], 0,
         sizeof(candidate->names[0]));
  return Race_AdminAllowlist_Valid(candidate);
}

bool Race_AdminAllowlist_Serialize(const race_admin_allowlist_t *allowlist,
                                   char *output, size_t output_size,
                                   size_t *output_length) {
  if (!Race_AdminAllowlist_Valid(allowlist) || !output || !output_size) {
    return false;
  }

  size_t length = 0;
  output[0] = '\0';
  for (size_t i = 0; i < allowlist->count; i++) {
    const int written = snprintf(output + length, output_size - length,
                                 "%s\n", allowlist->names[i]);
    if (written < 0 || (size_t) written >= output_size - length) {
      return false;
    }
    length += (size_t) written;
  }

  if (output_length) {
    *output_length = length;
  }
  return true;
}

race_admin_allowlist_parse_result_t Race_AdminAllowlist_Parse(
  const void *data, size_t length, race_admin_allowlist_t *allowlist) {
  if ((!data && length) || !allowlist) {
    return RACE_ADMIN_ALLOWLIST_PARSE_MALFORMED;
  }
  if (length > RACE_ADMIN_ALLOWLIST_SERIALIZED_MAX) {
    return RACE_ADMIN_ALLOWLIST_PARSE_TOO_LARGE;
  }

  char text[RACE_ADMIN_ALLOWLIST_SERIALIZED_MAX + 1u];
  if (length) {
    memcpy(text, data, length);
  }
  text[length] = '\0';
  if (length && memchr(text, '\0', length)) {
    return RACE_ADMIN_ALLOWLIST_PARSE_MALFORMED;
  }

  race_admin_allowlist_t parsed;
  Race_AdminAllowlist_Init(&parsed);
  char *cursor = text;
  while (*cursor) {
    char *line = cursor;
    char *newline = strchr(cursor, '\n');
    if (newline) {
      *newline = '\0';
      cursor = newline + 1;
    } else {
      cursor += strlen(cursor);
    }

    const size_t line_length = strlen(line);
    if (line_length && line[line_length - 1u] == '\r') {
      line[line_length - 1u] = '\0';
    }
    if (strchr(line, '\r')) {
      return RACE_ADMIN_ALLOWLIST_PARSE_MALFORMED;
    }
    if (!*line || *line == '#') {
      continue;
    }
    if (!Race_AdminAllowlist_NameValid(line)) {
      return RACE_ADMIN_ALLOWLIST_PARSE_MALFORMED;
    }
    if (parsed.count) {
      const int order = strcmp(parsed.names[parsed.count - 1u], line);
      if (!order) {
        return RACE_ADMIN_ALLOWLIST_PARSE_DUPLICATE;
      }
      if (order > 0) {
        return RACE_ADMIN_ALLOWLIST_PARSE_MALFORMED;
      }
    }
    if (Race_AdminAllowlist_Contains(&parsed, line)) {
      return RACE_ADMIN_ALLOWLIST_PARSE_DUPLICATE;
    }
    if (parsed.count >= RACE_ADMIN_MAX_OPERATOR_CVARS) {
      return RACE_ADMIN_ALLOWLIST_PARSE_TOO_MANY;
    }

    race_admin_allowlist_t candidate;
    if (!Race_AdminAllowlist_Add(&parsed, line, &candidate)) {
      return RACE_ADMIN_ALLOWLIST_PARSE_MALFORMED;
    }
    parsed = candidate;
  }

  *allowlist = parsed;
  return RACE_ADMIN_ALLOWLIST_PARSE_OK;
}

const char *Race_AdminAllowlist_ParseResultName(
  race_admin_allowlist_parse_result_t result) {
  switch (result) {
    case RACE_ADMIN_ALLOWLIST_PARSE_OK:
      return "ok";
    case RACE_ADMIN_ALLOWLIST_PARSE_MALFORMED:
      return "malformed";
    case RACE_ADMIN_ALLOWLIST_PARSE_TOO_LARGE:
      return "too large";
    case RACE_ADMIN_ALLOWLIST_PARSE_DUPLICATE:
      return "duplicate";
    case RACE_ADMIN_ALLOWLIST_PARSE_TOO_MANY:
      return "too many entries";
  }
  return "unknown";
}

race_admin_allowlist_store_result_t Race_AdminAllowlistStore_Load(
  const char *committed, race_admin_allowlist_t *allowlist,
  race_admin_allowlist_parse_result_t *parse_result) {
  if (!committed || !*committed || !allowlist) {
    return RACE_ADMIN_ALLOWLIST_STORE_INVALID_ARGUMENT;
  }

  char serialized[RACE_ADMIN_ALLOWLIST_SERIALIZED_MAX];
  size_t serialized_length;
  const race_persistence_result_t persisted = Race_Persistence_Read(
    committed, serialized, sizeof(serialized), &serialized_length);
  if (persisted == RACE_PERSISTENCE_NOT_FOUND) {
    Race_AdminAllowlist_Init(allowlist);
    if (parse_result) {
      *parse_result = RACE_ADMIN_ALLOWLIST_PARSE_OK;
    }
    return RACE_ADMIN_ALLOWLIST_STORE_MISSING;
  }
  if (persisted != RACE_PERSISTENCE_OK) {
    if (parse_result && persisted == RACE_PERSISTENCE_TOO_LARGE) {
      *parse_result = RACE_ADMIN_ALLOWLIST_PARSE_TOO_LARGE;
    }
    return persisted == RACE_PERSISTENCE_TOO_LARGE
      ? RACE_ADMIN_ALLOWLIST_STORE_CORRUPT
      : RACE_ADMIN_ALLOWLIST_STORE_IO_ERROR;
  }
  if (!Race_Persistence_RestrictOwner(committed)) {
    return RACE_ADMIN_ALLOWLIST_STORE_IO_ERROR;
  }

  race_admin_allowlist_t parsed;
  const race_admin_allowlist_parse_result_t result =
    Race_AdminAllowlist_Parse(serialized, serialized_length, &parsed);
  if (parse_result) {
    *parse_result = result;
  }
  if (result != RACE_ADMIN_ALLOWLIST_PARSE_OK) {
    return RACE_ADMIN_ALLOWLIST_STORE_CORRUPT;
  }

  *allowlist = parsed;
  return RACE_ADMIN_ALLOWLIST_STORE_OK;
}

race_admin_allowlist_store_result_t Race_AdminAllowlistStore_Commit(
  const char *committed, const char *candidate,
  const race_admin_allowlist_t *allowlist,
  race_admin_allowlist_parse_result_t *parse_result) {
  if (!committed || !*committed || !candidate || !*candidate ||
      !strcmp(committed, candidate) ||
      !Race_AdminAllowlist_Valid(allowlist)) {
    return RACE_ADMIN_ALLOWLIST_STORE_INVALID_ARGUMENT;
  }

  char serialized[RACE_ADMIN_ALLOWLIST_SERIALIZED_MAX];
  size_t serialized_length;
  if (!Race_AdminAllowlist_Serialize(allowlist, serialized,
                                     sizeof(serialized),
                                     &serialized_length)) {
    return RACE_ADMIN_ALLOWLIST_STORE_INVALID_ARGUMENT;
  }

  race_persistence_result_t persisted =
    Race_Persistence_WriteCandidateOwnerOnly(candidate, serialized,
                                              serialized_length);
  if (persisted != RACE_PERSISTENCE_OK) {
    return RACE_ADMIN_ALLOWLIST_STORE_IO_ERROR;
  }
  if (!Race_Persistence_RestrictOwner(candidate)) {
    Race_Persistence_Remove(candidate);
    return RACE_ADMIN_ALLOWLIST_STORE_IO_ERROR;
  }

  char verified_bytes[RACE_ADMIN_ALLOWLIST_SERIALIZED_MAX];
  size_t verified_length;
  persisted = Race_Persistence_Read(candidate, verified_bytes,
                                    sizeof(verified_bytes), &verified_length);
  if (persisted != RACE_PERSISTENCE_OK ||
      verified_length != serialized_length ||
      memcmp(verified_bytes, serialized, serialized_length)) {
    return RACE_ADMIN_ALLOWLIST_STORE_IO_ERROR;
  }

  race_admin_allowlist_t verified;
  const race_admin_allowlist_parse_result_t parsed =
    Race_AdminAllowlist_Parse(verified_bytes, verified_length, &verified);
  if (parse_result) {
    *parse_result = parsed;
  }
  if (parsed != RACE_ADMIN_ALLOWLIST_PARSE_OK ||
      !Race_AdminAllowlist_Equals(allowlist, &verified)) {
    return RACE_ADMIN_ALLOWLIST_STORE_CORRUPT;
  }

  persisted = Race_Persistence_Promote(candidate, committed);
  if (persisted != RACE_PERSISTENCE_OK ||
      !Race_Persistence_RestrictOwner(committed)) {
    return RACE_ADMIN_ALLOWLIST_STORE_IO_ERROR;
  }
  return RACE_ADMIN_ALLOWLIST_STORE_OK;
}

const char *Race_AdminAllowlistStore_ResultName(
  race_admin_allowlist_store_result_t result) {
  switch (result) {
    case RACE_ADMIN_ALLOWLIST_STORE_OK:
      return "ok";
    case RACE_ADMIN_ALLOWLIST_STORE_MISSING:
      return "missing";
    case RACE_ADMIN_ALLOWLIST_STORE_CORRUPT:
      return "corrupt";
    case RACE_ADMIN_ALLOWLIST_STORE_IO_ERROR:
      return "I/O error";
    case RACE_ADMIN_ALLOWLIST_STORE_INVALID_ARGUMENT:
      return "invalid argument";
  }
  return "unknown";
}
