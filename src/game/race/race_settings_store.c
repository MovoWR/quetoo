/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_settings_store.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "race_persistence.h"

typedef struct {
  char *data;
  size_t capacity;
  size_t length;
  bool failed;
} race_gset_writer_t;

static bool Race_SettingsStore_NameValid(const char *name) {
  if (!name || !*name || strlen(name) > RACE_SETTING_NAME_MAX) {
    return false;
  }
  for (const unsigned char *c = (const unsigned char *) name; *c; c++) {
    if (!(isalnum(*c) || *c == '_' || *c == '.' || *c == '-')) {
      return false;
    }
  }
  return true;
}

static bool Race_SettingsStore_ValueValid(const char *value) {
  if (!value || strlen(value) > RACE_SETTING_VALUE_MAX) {
    return false;
  }
  for (const unsigned char *c = (const unsigned char *) value; *c; c++) {
    if (*c < 32u || *c == 127u) {
      return false;
    }
  }
  return true;
}

void Race_SettingsStore_DocumentInit(race_gset_document_t *document) {
  if (document) {
    memset(document, 0, sizeof(*document));
  }
}

static size_t Race_SettingsStore_LowerBound(
  const race_gset_document_t *document, const char *name) {
  size_t first = 0;
  size_t last = document ? document->count : 0;
  while (first < last) {
    const size_t middle = first + (last - first) / 2u;
    if (strcmp(document->assignments[middle].name, name) < 0) {
      first = middle + 1u;
    } else {
      last = middle;
    }
  }
  return first;
}

const race_gset_assignment_t *Race_SettingsStore_Find(
  const race_gset_document_t *document, const char *name) {
  if (!document || !Race_SettingsStore_NameValid(name) ||
      document->count > RACE_GSET_MAX_ASSIGNMENTS) {
    return NULL;
  }
  const size_t index = Race_SettingsStore_LowerBound(document, name);
  return index < document->count &&
         !strcmp(document->assignments[index].name, name)
    ? document->assignments + index
    : NULL;
}

static bool Race_SettingsStore_DocumentValid(
  const race_gset_document_t *document) {
  if (!document || document->count > RACE_GSET_MAX_ASSIGNMENTS) {
    return false;
  }
  for (size_t i = 0; i < document->count; i++) {
    if (!Race_SettingsStore_NameValid(document->assignments[i].name) ||
        !Race_SettingsStore_ValueValid(document->assignments[i].value) ||
        (i && strcmp(document->assignments[i - 1u].name,
                     document->assignments[i].name) >= 0)) {
      return false;
    }
  }
  return true;
}

bool Race_SettingsStore_Set(const race_gset_document_t *current,
                            const char *name, const char *value,
                            race_gset_document_t *candidate) {
  if (!Race_SettingsStore_DocumentValid(current) || !candidate ||
      !Race_SettingsStore_NameValid(name) ||
      !Race_SettingsStore_ValueValid(value)) {
    return false;
  }

  *candidate = *current;
  const size_t index = Race_SettingsStore_LowerBound(candidate, name);
  if (index < candidate->count &&
      !strcmp(candidate->assignments[index].name, name)) {
    snprintf(candidate->assignments[index].value,
             sizeof(candidate->assignments[index].value), "%s", value);
    return true;
  }
  if (candidate->count == RACE_GSET_MAX_ASSIGNMENTS) {
    return false;
  }

  memmove(candidate->assignments + index + 1u,
          candidate->assignments + index,
          (candidate->count - index) * sizeof(candidate->assignments[0]));
  memset(candidate->assignments + index, 0,
         sizeof(candidate->assignments[index]));
  snprintf(candidate->assignments[index].name,
           sizeof(candidate->assignments[index].name), "%s", name);
  snprintf(candidate->assignments[index].value,
           sizeof(candidate->assignments[index].value), "%s", value);
  candidate->count++;
  return true;
}

bool Race_SettingsStore_Remove(const race_gset_document_t *current,
                               const char *name,
                               race_gset_document_t *candidate) {
  if (!Race_SettingsStore_DocumentValid(current) || !candidate ||
      !Race_SettingsStore_NameValid(name)) {
    return false;
  }
  *candidate = *current;
  const size_t index = Race_SettingsStore_LowerBound(candidate, name);
  if (index == candidate->count ||
      strcmp(candidate->assignments[index].name, name)) {
    return true;
  }
  memmove(candidate->assignments + index,
          candidate->assignments + index + 1u,
          (candidate->count - index - 1u) * sizeof(candidate->assignments[0]));
  candidate->count--;
  memset(candidate->assignments + candidate->count, 0,
         sizeof(candidate->assignments[0]));
  return true;
}

bool Race_SettingsStore_Equals(const race_gset_document_t *left,
                               const race_gset_document_t *right) {
  if (!Race_SettingsStore_DocumentValid(left) ||
      !Race_SettingsStore_DocumentValid(right) ||
      left->count != right->count) {
    return false;
  }
  for (size_t i = 0; i < left->count; i++) {
    if (strcmp(left->assignments[i].name, right->assignments[i].name) ||
        strcmp(left->assignments[i].value, right->assignments[i].value)) {
      return false;
    }
  }
  return true;
}

static void Race_SettingsStore_Write(race_gset_writer_t *writer,
                                     const void *data, size_t length) {
  if (writer->failed || length > writer->capacity - writer->length) {
    writer->failed = true;
    return;
  }
  memcpy(writer->data + writer->length, data, length);
  writer->length += length;
}

static void Race_SettingsStore_WriteString(race_gset_writer_t *writer,
                                           const char *string) {
  Race_SettingsStore_Write(writer, string, strlen(string));
}

bool Race_SettingsStore_Serialize(const race_gset_document_t *document,
                                  char *output, size_t output_size,
                                  size_t *output_length) {
  if (!Race_SettingsStore_DocumentValid(document) || !output || !output_size ||
      !output_length) {
    return false;
  }
  race_gset_writer_t writer = {
    .data = output,
    .capacity = output_size - 1u
  };
  Race_SettingsStore_WriteString(&writer,
                                 "# Race global cvar overrides\n");
  for (size_t i = 0; i < document->count; i++) {
    Race_SettingsStore_WriteString(&writer, "set ");
    Race_SettingsStore_WriteString(&writer, document->assignments[i].name);
    Race_SettingsStore_WriteString(&writer, " \"");
    for (const char *c = document->assignments[i].value; *c; c++) {
      if (*c == '\\' || *c == '"') {
        Race_SettingsStore_WriteString(&writer, "\\");
      }
      Race_SettingsStore_Write(&writer, c, 1u);
    }
    Race_SettingsStore_WriteString(&writer, "\"\n");
  }
  if (writer.failed || writer.length > RACE_SETTINGS_MAX_FILE_BYTES) {
    return false;
  }
  writer.data[writer.length] = '\0';
  *output_length = writer.length;
  return true;
}

static const char *Race_SettingsStore_SkipSpace(const char *cursor,
                                                const char *end) {
  while (cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r')) {
    cursor++;
  }
  return cursor;
}

static bool Race_SettingsStore_ParseLine(const char *line, size_t length,
                                         char name[RACE_SETTING_NAME_SIZE],
                                         char value[RACE_SETTING_VALUE_SIZE],
                                         bool *assignment) {
  const char *cursor = line;
  const char *end = line + length;
  cursor = Race_SettingsStore_SkipSpace(cursor, end);
  if (cursor == end || *cursor == '#') {
    *assignment = false;
    return true;
  }
  if ((size_t) (end - cursor) < 4u || memcmp(cursor, "set ", 4u)) {
    return false;
  }
  cursor += 4;
  const char *name_start = cursor;
  while (cursor < end && *cursor != ' ' && *cursor != '\t') {
    cursor++;
  }
  const size_t name_length = (size_t) (cursor - name_start);
  if (!name_length || name_length > RACE_SETTING_NAME_MAX) {
    return false;
  }
  memcpy(name, name_start, name_length);
  name[name_length] = '\0';
  if (!Race_SettingsStore_NameValid(name)) {
    return false;
  }

  cursor = Race_SettingsStore_SkipSpace(cursor, end);
  if (cursor == end || *cursor++ != '"') {
    return false;
  }
  size_t value_length = 0;
  bool closed = false;
  while (cursor < end) {
    char c = *cursor++;
    if (c == '"') {
      closed = true;
      break;
    }
    if (c == '\\') {
      if (cursor == end || (*cursor != '\\' && *cursor != '"')) {
        return false;
      }
      c = *cursor++;
    }
    if ((unsigned char) c < 32u || (unsigned char) c == 127u ||
        value_length == RACE_SETTING_VALUE_MAX) {
      return false;
    }
    value[value_length++] = c;
  }
  value[value_length] = '\0';
  cursor = Race_SettingsStore_SkipSpace(cursor, end);
  if (!closed || cursor != end || !Race_SettingsStore_ValueValid(value)) {
    return false;
  }
  *assignment = true;
  return true;
}

bool Race_SettingsStore_Parse(const void *data, size_t length,
                              race_gset_document_t *document) {
  if ((!data && length) || length > RACE_SETTINGS_MAX_FILE_BYTES || !document) {
    return false;
  }
  race_gset_document_t parsed;
  Race_SettingsStore_DocumentInit(&parsed);
  const char *cursor = data ? data : "";
  const char *end = cursor + length;
  while (cursor < end) {
    const char *line_end = memchr(cursor, '\n', (size_t) (end - cursor));
    if (!line_end) {
      line_end = end;
    }
    char name[RACE_SETTING_NAME_SIZE];
    char value[RACE_SETTING_VALUE_SIZE];
    bool assignment;
    if (!Race_SettingsStore_ParseLine(cursor, (size_t) (line_end - cursor),
                                      name, value, &assignment)) {
      return false;
    }
    if (assignment) {
      if (Race_SettingsStore_Find(&parsed, name)) {
        return false;
      }
      race_gset_document_t candidate;
      if (!Race_SettingsStore_Set(&parsed, name, value, &candidate)) {
        return false;
      }
      parsed = candidate;
    }
    cursor = line_end < end ? line_end + 1u : end;
  }
  *document = parsed;
  return true;
}

race_settings_store_result_t Race_SettingsStore_Load(
  const char *committed, race_gset_document_t *document) {
  if (!committed || !*committed || !document) {
    return RACE_SETTINGS_STORE_INVALID_ARGUMENT;
  }
  char data[RACE_SETTINGS_MAX_FILE_BYTES];
  size_t length;
  const race_persistence_result_t result = Race_Persistence_Read(
    committed, data, sizeof(data), &length);
  if (result == RACE_PERSISTENCE_NOT_FOUND) {
    Race_SettingsStore_DocumentInit(document);
    return RACE_SETTINGS_STORE_MISSING;
  }
  if (result == RACE_PERSISTENCE_TOO_LARGE) {
    return RACE_SETTINGS_STORE_TOO_LARGE;
  }
  if (result != RACE_PERSISTENCE_OK) {
    return RACE_SETTINGS_STORE_IO_ERROR;
  }
  return Race_SettingsStore_Parse(data, length, document)
    ? RACE_SETTINGS_STORE_OK
    : RACE_SETTINGS_STORE_CORRUPT;
}

race_settings_store_result_t Race_SettingsStore_Commit(
  const char *committed, const char *candidate,
  const race_gset_document_t *document) {
  if (!committed || !*committed || !candidate || !*candidate ||
      !Race_SettingsStore_DocumentValid(document)) {
    return RACE_SETTINGS_STORE_INVALID_ARGUMENT;
  }
  char serialized[RACE_SETTINGS_MAX_FILE_BYTES + 1u];
  size_t serialized_length;
  if (!Race_SettingsStore_Serialize(document, serialized, sizeof(serialized),
                                    &serialized_length)) {
    return RACE_SETTINGS_STORE_TOO_LARGE;
  }
  if (Race_Persistence_WriteCandidateOwnerOnly(candidate, serialized,
                                                serialized_length) !=
      RACE_PERSISTENCE_OK) {
    return RACE_SETTINGS_STORE_IO_ERROR;
  }

  char verified_data[RACE_SETTINGS_MAX_FILE_BYTES];
  size_t verified_length;
  if (Race_Persistence_Read(candidate, verified_data, sizeof(verified_data),
                            &verified_length) != RACE_PERSISTENCE_OK ||
      verified_length != serialized_length ||
      memcmp(verified_data, serialized, serialized_length)) {
    Race_Persistence_Remove(candidate);
    return RACE_SETTINGS_STORE_IO_ERROR;
  }
  race_gset_document_t verified;
  if (!Race_SettingsStore_Parse(verified_data, verified_length, &verified) ||
      !Race_SettingsStore_Equals(document, &verified)) {
    Race_Persistence_Remove(candidate);
    return RACE_SETTINGS_STORE_CORRUPT;
  }
  if (Race_Persistence_Promote(candidate, committed) != RACE_PERSISTENCE_OK ||
      !Race_Persistence_RestrictOwner(committed)) {
    Race_Persistence_Remove(candidate);
    return RACE_SETTINGS_STORE_IO_ERROR;
  }
  return RACE_SETTINGS_STORE_OK;
}

const char *Race_SettingsStore_ResultName(race_settings_store_result_t result) {
  switch (result) {
    case RACE_SETTINGS_STORE_OK: return "ok";
    case RACE_SETTINGS_STORE_MISSING: return "missing";
    case RACE_SETTINGS_STORE_CORRUPT: return "corrupt";
    case RACE_SETTINGS_STORE_IO_ERROR: return "I/O error";
    case RACE_SETTINGS_STORE_TOO_LARGE: return "too large";
    case RACE_SETTINGS_STORE_INVALID_ARGUMENT: return "invalid argument";
  }
  return "unknown";
}
