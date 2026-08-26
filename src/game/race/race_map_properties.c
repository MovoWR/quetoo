/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_map_properties.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef enum {
  RACE_MAP_TOKEN_WORD,
  RACE_MAP_TOKEN_OPEN,
  RACE_MAP_TOKEN_CLOSE
} race_map_token_kind_t;

typedef struct {
  race_map_token_kind_t kind;
  size_t begin;
  size_t end;
  char text[RACE_SETTING_VALUE_SIZE];
} race_map_token_t;

typedef struct {
  const char *contents;
  size_t length;
  size_t position;
} race_map_lexer_t;

typedef struct {
  size_t open_begin;
  size_t close_begin;
  size_t first_key_begin;
  size_t property_key_begin;
  size_t property_value_begin;
  size_t property_value_end;
  size_t property_occurrences;
} race_map_edit_span_t;

static void Race_MapProperties_Error(char *error, const size_t error_size,
                                     const char *message) {
  if (error && error_size) {
    snprintf(error, error_size, "%s",
             message ? message : "Map properties error");
  }
}

static race_map_properties_result_t Race_MapProperties_Fail(
  const race_map_properties_result_t result,
  char *error, const size_t error_size, const char *message) {
  Race_MapProperties_Error(error, error_size, message);
  return result;
}

static bool Race_MapProperties_Whitespace(const char c) {
  return (unsigned char) c <= 32u;
}

static race_map_properties_result_t Race_MapProperties_SkipTrivia(
  race_map_lexer_t *lexer, char *error, const size_t error_size) {
  while (lexer->position < lexer->length) {
    const char c = lexer->contents[lexer->position];
    if (Race_MapProperties_Whitespace(c)) {
      lexer->position++;
      continue;
    }

    if (c == '#') {
      while (lexer->position < lexer->length &&
             lexer->contents[lexer->position] != '\n' &&
             lexer->contents[lexer->position] != '\r') {
        lexer->position++;
      }
      continue;
    }

    if (c == '/' && lexer->position + 1u < lexer->length) {
      const char next = lexer->contents[lexer->position + 1u];
      if (next == '/') {
        lexer->position += 2u;
        while (lexer->position < lexer->length &&
               lexer->contents[lexer->position] != '\n' &&
               lexer->contents[lexer->position] != '\r') {
          lexer->position++;
        }
        continue;
      }

      if (next == '*') {
        lexer->position += 2u;
        bool closed = false;
        while (lexer->position + 1u < lexer->length) {
          if (lexer->contents[lexer->position] == '*' &&
              lexer->contents[lexer->position + 1u] == '/') {
            lexer->position += 2u;
            closed = true;
            break;
          }
          lexer->position++;
        }
        if (!closed) {
          return Race_MapProperties_Fail(
            RACE_MAP_PROPERTIES_MALFORMED, error, error_size,
            "Unterminated block comment in map catalog");
        }
        continue;
      }
    }
    break;
  }

  return RACE_MAP_PROPERTIES_OK;
}

static race_map_properties_result_t Race_MapProperties_AppendTokenChar(
  race_map_token_t *token, size_t *length, const char c,
  char *error, const size_t error_size) {
  if (*length + 1u >= sizeof(token->text)) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_TOKEN_TOO_LONG, error, error_size,
      "Map catalog token exceeds the shared setting value bound");
  }
  token->text[(*length)++] = c;
  return RACE_MAP_PROPERTIES_OK;
}

static race_map_properties_result_t Race_MapProperties_NextToken(
  race_map_lexer_t *lexer, race_map_token_t *token, bool *available,
  char *error, const size_t error_size) {
  if (!lexer || !token || !available) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_INVALID_ARGUMENT, error, error_size,
      "Missing map catalog lexer argument");
  }

  *available = false;
  race_map_properties_result_t result =
    Race_MapProperties_SkipTrivia(lexer, error, error_size);
  if (result != RACE_MAP_PROPERTIES_OK || lexer->position == lexer->length) {
    return result;
  }

  memset(token, 0, sizeof(*token));
  token->begin = lexer->position;
  size_t output_length = 0u;
  bool quoted = false;

  if (lexer->contents[lexer->position] == '"') {
    quoted = true;
    lexer->position++;
    bool closed = false;
    while (lexer->position < lexer->length) {
      char c = lexer->contents[lexer->position++];
      if (c == '"') {
        closed = true;
        break;
      }

      if (c == '\\' && lexer->position < lexer->length) {
        const char escaped = lexer->contents[lexer->position++];
        switch (escaped) {
          case 'n':
            c = '\n';
            break;
          case 't':
            c = '\t';
            break;
          case '"':
          case '\'':
          case '\\':
            c = escaped;
            break;
          default:
            result = Race_MapProperties_AppendTokenChar(
              token, &output_length, '\\', error, error_size);
            if (result != RACE_MAP_PROPERTIES_OK) {
              return result;
            }
            c = escaped;
            break;
        }
      }

      result = Race_MapProperties_AppendTokenChar(
        token, &output_length, c, error, error_size);
      if (result != RACE_MAP_PROPERTIES_OK) {
        return result;
      }
    }

    if (!closed) {
      return Race_MapProperties_Fail(
        RACE_MAP_PROPERTIES_MALFORMED, error, error_size,
        "Unterminated quoted value in map catalog");
    }
  } else {
    while (lexer->position < lexer->length &&
           !Race_MapProperties_Whitespace(
             lexer->contents[lexer->position])) {
      result = Race_MapProperties_AppendTokenChar(
        token, &output_length, lexer->contents[lexer->position++],
        error, error_size);
      if (result != RACE_MAP_PROPERTIES_OK) {
        return result;
      }
    }
  }

  token->text[output_length] = '\0';
  token->end = lexer->position;
  token->kind = !quoted && output_length == 1u && token->text[0] == '{'
    ? RACE_MAP_TOKEN_OPEN
    : !quoted && output_length == 1u && token->text[0] == '}'
      ? RACE_MAP_TOKEN_CLOSE
      : RACE_MAP_TOKEN_WORD;
  *available = true;
  return RACE_MAP_PROPERTIES_OK;
}

static bool Race_MapProperties_CanonicalMap(
  const char *map, char canonical[RACE_MAP_IDENTITY_SIZE]) {
  return map && Race_MapState_CanonicalizeMap(map, canonical) &&
         !strcmp(map, canonical);
}

static bool Race_MapProperties_DescriptorSupported(
  const race_setting_descriptor_t *descriptor) {
  return descriptor && descriptor->id < RACE_SETTING_TOTAL &&
         descriptor->map_overridable && descriptor->map_key &&
         *descriptor->map_key;
}

static void Race_MapProperties_AddValue(
  race_map_properties_t *row,
  const race_setting_descriptor_t *descriptor,
  const char *text) {
  if (!row || !Race_MapProperties_DescriptorSupported(descriptor) || !text) {
    return;
  }

  race_map_property_t *property = row->properties + descriptor->id;
  property->present = true;
  if (property->occurrences < UINT16_MAX) {
    property->occurrences++;
  }

  if (property->occurrences > 1u) {
    property->duplicate = true;
    property->valid = false;
    property->canonical[0] = '\0';
    return;
  }

  char canonical[RACE_SETTING_VALUE_SIZE];
  if (Race_Settings_CanonicalizeValue(
        descriptor, text, canonical, sizeof(canonical), NULL, 0)) {
    property->valid = true;
    snprintf(property->canonical, sizeof(property->canonical), "%s",
             canonical);
  } else {
    property->valid = false;
    property->canonical[0] = '\0';
  }
}

static bool Race_MapProperties_HasDuplicate(
  const race_map_properties_t *properties) {
  if (!properties) {
    return false;
  }
  for (size_t i = 0; i < RACE_SETTING_TOTAL; i++) {
    if (properties->properties[i].duplicate) {
      return true;
    }
  }
  return false;
}

static race_map_properties_result_t Race_MapProperties_ParseInternal(
  const char *contents, const size_t length, const char *map,
  race_map_properties_t *properties,
  const race_setting_descriptor_t *edit_descriptor,
  race_map_edit_span_t *edit_span,
  char *error, const size_t error_size) {
  if (!contents || !map || !properties ||
      (edit_descriptor && !edit_span)) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_INVALID_ARGUMENT, error, error_size,
      "Missing map catalog parse argument");
  }
  if (length > RACE_MAP_PROPERTIES_MAX_FILE_BYTES) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_TOO_LARGE, error, error_size,
      "Map catalog exceeds the bounded file size");
  }
  if (memchr(contents, '\0', length)) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_MALFORMED, error, error_size,
      "Map catalog contains an embedded NUL byte");
  }

  char canonical_map[RACE_MAP_IDENTITY_SIZE];
  if (!Race_MapProperties_CanonicalMap(map, canonical_map)) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_INVALID_MAP, error, error_size,
      "Map identity is not canonical");
  }

  memset(properties, 0, sizeof(*properties));
  snprintf(properties->map, sizeof(properties->map), "%s", canonical_map);
  if (edit_span) {
    memset(edit_span, 0, sizeof(*edit_span));
  }

  race_map_lexer_t lexer = {
    .contents = contents,
    .length = length
  };
  bool in_row = false;
  char row_name[RACE_SETTING_VALUE_SIZE] = { 0 };
  race_map_properties_t row_properties;
  memset(&row_properties, 0, sizeof(row_properties));
  race_map_edit_span_t row_span;
  memset(&row_span, 0, sizeof(row_span));

  while (true) {
    race_map_token_t key;
    bool available;
    race_map_properties_result_t result = Race_MapProperties_NextToken(
      &lexer, &key, &available, error, error_size);
    if (result != RACE_MAP_PROPERTIES_OK) {
      return result;
    }
    if (!available) {
      break;
    }

    if (!in_row) {
      if (key.kind == RACE_MAP_TOKEN_CLOSE) {
        return Race_MapProperties_Fail(
          RACE_MAP_PROPERTIES_MALFORMED, error, error_size,
          "Map catalog has an unmatched closing brace");
      }
      if (key.kind != RACE_MAP_TOKEN_OPEN) {
        continue;
      }

      in_row = true;
      row_name[0] = '\0';
      memset(&row_properties, 0, sizeof(row_properties));
      memset(&row_span, 0, sizeof(row_span));
      row_span.open_begin = key.begin;
      continue;
    }

    if (key.kind == RACE_MAP_TOKEN_OPEN) {
      return Race_MapProperties_Fail(
        RACE_MAP_PROPERTIES_MALFORMED, error, error_size,
        "Map catalog rows cannot be nested");
    }

    if (key.kind == RACE_MAP_TOKEN_CLOSE) {
      in_row = false;
      properties->rows++;
      if (properties->rows > RACE_MAP_PROPERTIES_MAX_ROWS) {
        return Race_MapProperties_Fail(
          RACE_MAP_PROPERTIES_TOO_MANY_ROWS, error, error_size,
          "Map catalog exceeds the bounded row count");
      }

      char canonical_row[RACE_MAP_IDENTITY_SIZE];
      if (Race_MapState_CanonicalizeMap(row_name, canonical_row) &&
          !strcmp(row_name, canonical_row) &&
          !strcmp(canonical_row, canonical_map)) {
        properties->map_matches++;
        if (properties->map_matches == 1u) {
          memcpy(properties->properties, row_properties.properties,
                 sizeof(properties->properties));
          if (edit_span) {
            row_span.close_begin = key.begin;
            *edit_span = row_span;
          }
        }
      }
      continue;
    }

    race_map_token_t value;
    result = Race_MapProperties_NextToken(
      &lexer, &value, &available, error, error_size);
    if (result != RACE_MAP_PROPERTIES_OK) {
      return result;
    }
    if (!available || value.kind != RACE_MAP_TOKEN_WORD) {
      return Race_MapProperties_Fail(
        RACE_MAP_PROPERTIES_MALFORMED, error, error_size,
        "Map catalog key is missing its value");
    }

    if (!row_span.first_key_begin) {
      row_span.first_key_begin = key.begin;
    }

    if (!strcmp(key.text, "name")) {
      snprintf(row_name, sizeof(row_name), "%s", value.text);
      continue;
    }

    const race_setting_descriptor_t *descriptor =
      Race_Settings_DescriptorForMapKey(key.text);
    if (Race_MapProperties_DescriptorSupported(descriptor)) {
      Race_MapProperties_AddValue(&row_properties, descriptor, value.text);
    }

    if (edit_descriptor && descriptor &&
        descriptor->id == edit_descriptor->id) {
      row_span.property_occurrences++;
      if (row_span.property_occurrences == 1u) {
        row_span.property_key_begin = key.begin;
        row_span.property_value_begin = value.begin;
        row_span.property_value_end = value.end;
      }
    }
  }

  if (in_row) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_MALFORMED, error, error_size,
      "Map catalog has an unclosed row");
  }

  return RACE_MAP_PROPERTIES_OK;
}

race_map_properties_result_t Race_MapProperties_ValidateVirtualPath(
  const char *path, char *error, const size_t error_size) {
  if (!path || !*path) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_UNSAFE_PATH, error, error_size,
      "Map catalog path is empty");
  }

  const size_t length = strlen(path);
  if (length > RACE_MAP_PROPERTIES_MAX_VIRTUAL_PATH ||
      path[0] == '/' || path[0] == '\\' ||
      path[length - 1u] == '/') {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_UNSAFE_PATH, error, error_size,
      "Map catalog path is not a bounded relative virtual path");
  }

  size_t segment_start = 0u;
  for (size_t i = 0; i <= length; i++) {
    const unsigned char c = (unsigned char) path[i];
    if (i < length && c != '/') {
      if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
        return Race_MapProperties_Fail(
          RACE_MAP_PROPERTIES_UNSAFE_PATH, error, error_size,
          "Map catalog path contains an unsafe character");
      }
      continue;
    }

    const size_t segment_length = i - segment_start;
    if (!segment_length ||
        (segment_length == 1u && path[segment_start] == '.') ||
        (segment_length == 2u && path[segment_start] == '.' &&
         path[segment_start + 1u] == '.')) {
      return Race_MapProperties_Fail(
        RACE_MAP_PROPERTIES_UNSAFE_PATH, error, error_size,
        "Map catalog path contains an unsafe segment");
    }
    segment_start = i + 1u;
  }

  return RACE_MAP_PROPERTIES_OK;
}

race_map_properties_result_t Race_MapProperties_Parse(
  const char *contents, const size_t length, const char *map,
  race_map_properties_t *properties, char *error, const size_t error_size) {
  return Race_MapProperties_ParseInternal(
    contents, length, map, properties, NULL, NULL, error, error_size);
}

static bool Race_MapProperties_BareValue(const char *value) {
  if (!value || !*value || value[0] == '#' ||
      (value[0] == '/' && (value[1] == '/' || value[1] == '*'))) {
    return false;
  }
  for (const unsigned char *c = (const unsigned char *) value; *c; c++) {
    if (*c <= 32u || *c == '"' || *c == '{' || *c == '}') {
      return false;
    }
  }
  return true;
}

static bool Race_MapProperties_EncodeValue(
  const char *canonical, char *output, const size_t output_size,
  size_t *output_length) {
  if (!canonical || !output || !output_size || !output_length) {
    return false;
  }

  if (Race_MapProperties_BareValue(canonical)) {
    const size_t length = strlen(canonical);
    if (length >= output_size) {
      return false;
    }
    memcpy(output, canonical, length + 1u);
    *output_length = length;
    return true;
  }

  size_t length = 0u;
  if (length + 1u >= output_size) {
    return false;
  }
  output[length++] = '"';
  for (const unsigned char *c = (const unsigned char *) canonical; *c; c++) {
    const char *escaped = NULL;
    if (*c == '"') {
      escaped = "\\\"";
    } else if (*c == '\\') {
      escaped = "\\\\";
    } else if (*c == '\n') {
      escaped = "\\n";
    } else if (*c == '\t') {
      escaped = "\\t";
    } else if (*c < 32u || *c == 127u) {
      return false;
    }

    if (escaped) {
      if (length + 2u >= output_size) {
        return false;
      }
      output[length++] = escaped[0];
      output[length++] = escaped[1];
    } else {
      if (length + 1u >= output_size) {
        return false;
      }
      output[length++] = (char) *c;
    }
  }
  if (length + 1u >= output_size) {
    return false;
  }
  output[length++] = '"';
  output[length] = '\0';
  *output_length = length;
  return true;
}

static race_map_properties_result_t Race_MapProperties_Replace(
  const char *contents, const size_t length,
  const size_t begin, const size_t end,
  const char *replacement, const size_t replacement_length,
  char *output, const size_t output_size,
  size_t *output_length,
  char *error, const size_t error_size) {
  if (begin > end || end > length || !replacement || !output ||
      !output_length) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_INVALID_ARGUMENT, error, error_size,
      "Invalid map catalog replacement span");
  }

  const size_t removed = end - begin;
  if (replacement_length > SIZE_MAX - (length - removed)) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_TOO_LARGE, error, error_size,
      "Edited map catalog length overflowed");
  }
  const size_t candidate_length = length - removed + replacement_length;
  if (candidate_length > RACE_MAP_PROPERTIES_MAX_FILE_BYTES) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_TOO_LARGE, error, error_size,
      "Edited map catalog exceeds the bounded file size");
  }
  if (candidate_length + 1u > output_size) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_OUTPUT_TOO_SMALL, error, error_size,
      "Map catalog candidate buffer is too small");
  }

  memcpy(output, contents, begin);
  memcpy(output + begin, replacement, replacement_length);
  memcpy(output + begin + replacement_length, contents + end, length - end);
  output[candidate_length] = '\0';
  *output_length = candidate_length;
  return RACE_MAP_PROPERTIES_OK;
}

static size_t Race_MapProperties_LineStart(const char *contents,
                                           size_t position) {
  while (position && contents[position - 1u] != '\n' &&
         contents[position - 1u] != '\r') {
    position--;
  }
  return position;
}

static size_t Race_MapProperties_LineEnd(const char *contents,
                                         const size_t length,
                                         size_t position) {
  while (position < length && contents[position] != '\n' &&
         contents[position] != '\r') {
    position++;
  }
  return position;
}

static bool Race_MapProperties_SpanIndent(const char *contents,
                                          const size_t begin,
                                          const size_t end) {
  for (size_t i = begin; i < end; i++) {
    if (contents[i] != ' ' && contents[i] != '\t') {
      return false;
    }
  }
  return true;
}

static const char *Race_MapProperties_LineEnding(const char *contents,
                                                 const size_t length,
                                                 size_t *ending_length) {
  for (size_t i = 0; i < length; i++) {
    if (contents[i] == '\r' && i + 1u < length &&
        contents[i + 1u] == '\n') {
      *ending_length = 2u;
      return "\r\n";
    }
    if (contents[i] == '\n') {
      *ending_length = 1u;
      return "\n";
    }
  }
  *ending_length = 1u;
  return "\n";
}

static race_map_properties_result_t Race_MapProperties_CopyUnchanged(
  const char *contents, const size_t length,
  char *output, const size_t output_size,
  race_map_properties_edit_t *edit,
  char *error, const size_t error_size) {
  if (length + 1u > output_size) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_OUTPUT_TOO_SMALL, error, error_size,
      "Map catalog candidate buffer is too small");
  }
  memcpy(output, contents, length);
  output[length] = '\0';
  memset(edit, 0, sizeof(*edit));
  edit->length = length;
  return RACE_MAP_PROPERTIES_OK;
}

race_map_properties_result_t Race_MapProperties_Edit(
  const char *contents, const size_t length, const char *map,
  const race_setting_descriptor_t *descriptor,
  const char *canonical_value,
  char *output, const size_t output_size,
  race_map_properties_edit_t *edit,
  char *error, const size_t error_size) {
  if (!contents || !map || !output || !output_size || !edit) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_INVALID_ARGUMENT, error, error_size,
      "Missing map catalog edit argument");
  }
  if (!Race_MapProperties_DescriptorSupported(descriptor)) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_UNSUPPORTED_PROPERTY, error, error_size,
      "Setting does not support a map override");
  }

  char canonical[RACE_SETTING_VALUE_SIZE];
  if (canonical_value) {
    if (!Race_Settings_CanonicalizeValue(
          descriptor, canonical_value, canonical, sizeof(canonical),
          error, error_size) || strcmp(canonical, canonical_value)) {
      return Race_MapProperties_Fail(
        RACE_MAP_PROPERTIES_INVALID_VALUE, error, error_size,
        "Map property value is not canonical");
    }
  }

  race_map_properties_t properties;
  race_map_edit_span_t span;
  race_map_properties_result_t result = Race_MapProperties_ParseInternal(
    contents, length, map, &properties, descriptor, &span,
    error, error_size);
  if (result != RACE_MAP_PROPERTIES_OK) {
    return result;
  }
  if (properties.map_matches > 1u) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_DUPLICATE_MAP, error, error_size,
      "Map catalog contains multiple target rows");
  }
  if (properties.map_matches == 1u &&
      Race_MapProperties_HasDuplicate(&properties)) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_DUPLICATE_PROPERTY, error, error_size,
      "Target map row contains a duplicate registered property");
  }

  if (!canonical_value &&
      (!properties.map_matches || !span.property_occurrences)) {
    return Race_MapProperties_CopyUnchanged(
      contents, length, output, output_size, edit, error, error_size);
  }

  char encoded[RACE_SETTING_VALUE_SIZE * 2u + 3u];
  size_t encoded_length = 0u;
  if (canonical_value && !Race_MapProperties_EncodeValue(
        canonical, encoded, sizeof(encoded), &encoded_length)) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_INVALID_VALUE, error, error_size,
      "Map property value cannot be encoded safely");
  }

  memset(edit, 0, sizeof(*edit));
  if (properties.map_matches == 1u && span.property_occurrences == 1u) {
    size_t begin = span.property_value_begin;
    size_t end = span.property_value_end;
    const char *replacement = encoded;
    size_t replacement_length = encoded_length;

    if (!canonical_value) {
      begin = span.property_key_begin;
      end = span.property_value_end;
      replacement = "";
      replacement_length = 0u;

      const size_t line_start = Race_MapProperties_LineStart(contents, begin);
      const size_t line_end = Race_MapProperties_LineEnd(contents, length, end);
      if (Race_MapProperties_SpanIndent(contents, line_start, begin) &&
          Race_MapProperties_SpanIndent(contents, end, line_end)) {
        begin = line_start;
        end = line_end;
        if (end < length && contents[end] == '\r') {
          end++;
        }
        if (end < length && contents[end] == '\n') {
          end++;
        }
      }
      edit->removed = true;
    }

    result = Race_MapProperties_Replace(
      contents, length, begin, end, replacement, replacement_length,
      output, output_size, &edit->length, error, error_size);
    edit->changed = result == RACE_MAP_PROPERTIES_OK;
  } else if (properties.map_matches == 1u) {
    char insertion[RACE_SETTING_VALUE_SIZE * 2u + 128u];
    size_t insertion_length;
    size_t ending_length;
    const char *ending = Race_MapProperties_LineEnding(
      contents, length, &ending_length);
    const size_t close_line = Race_MapProperties_LineStart(
      contents, span.close_begin);
    const bool multiline_close = close_line > span.open_begin &&
      Race_MapProperties_SpanIndent(contents, close_line, span.close_begin);

    size_t insert_at = span.close_begin;
    if (multiline_close) {
      char indent[33] = " ";
      if (span.first_key_begin) {
        const size_t key_line = Race_MapProperties_LineStart(
          contents, span.first_key_begin);
        const size_t indent_length = span.first_key_begin - key_line;
        if (indent_length <= 32u && Race_MapProperties_SpanIndent(
              contents, key_line, span.first_key_begin)) {
          memcpy(indent, contents + key_line, indent_length);
          indent[indent_length] = '\0';
        }
      }
      const int written = snprintf(
        insertion, sizeof(insertion), "%s%s %s%.*s",
        indent, descriptor->map_key, encoded,
        (int) ending_length, ending);
      if (written < 0 || (size_t) written >= sizeof(insertion)) {
        return Race_MapProperties_Fail(
          RACE_MAP_PROPERTIES_OUTPUT_TOO_SMALL, error, error_size,
          "Map property insertion exceeds its bounded buffer");
      }
      insertion_length = (size_t) written;
      insert_at = close_line;
    } else {
      const int written = snprintf(
        insertion, sizeof(insertion), " %s %s",
        descriptor->map_key, encoded);
      if (written < 0 || (size_t) written >= sizeof(insertion)) {
        return Race_MapProperties_Fail(
          RACE_MAP_PROPERTIES_OUTPUT_TOO_SMALL, error, error_size,
          "Map property insertion exceeds its bounded buffer");
      }
      insertion_length = (size_t) written;
    }

    result = Race_MapProperties_Replace(
      contents, length, insert_at, insert_at,
      insertion, insertion_length,
      output, output_size, &edit->length, error, error_size);
    edit->changed = result == RACE_MAP_PROPERTIES_OK;
  } else {
    char insertion[RACE_SETTING_VALUE_SIZE * 2u +
                   RACE_MAP_IDENTITY_SIZE + 128u];
    size_t ending_length;
    const char *ending = Race_MapProperties_LineEnding(
      contents, length, &ending_length);
    const bool needs_ending = length && contents[length - 1u] != '\n' &&
                              contents[length - 1u] != '\r';
    const int written = snprintf(
      insertion, sizeof(insertion), "%.*s{%.*s name %s%.*s %s %s%.*s}%.*s",
      needs_ending ? (int) ending_length : 0, ending,
      (int) ending_length, ending,
      map, (int) ending_length, ending,
      descriptor->map_key, encoded,
      (int) ending_length, ending,
      (int) ending_length, ending);
    if (written < 0 || (size_t) written >= sizeof(insertion)) {
      return Race_MapProperties_Fail(
        RACE_MAP_PROPERTIES_OUTPUT_TOO_SMALL, error, error_size,
        "Appended map row exceeds its bounded buffer");
    }

    result = Race_MapProperties_Replace(
      contents, length, length, length,
      insertion, (size_t) written,
      output, output_size, &edit->length, error, error_size);
    edit->changed = result == RACE_MAP_PROPERTIES_OK;
    edit->appended_row = result == RACE_MAP_PROPERTIES_OK;
  }

  if (result != RACE_MAP_PROPERTIES_OK) {
    return result;
  }

  result = Race_MapProperties_ValidateCandidate(
    output, edit->length, map, descriptor, canonical_value,
    NULL, error, error_size);
  if (result != RACE_MAP_PROPERTIES_OK) {
    memset(edit, 0, sizeof(*edit));
  }
  return result;
}

race_map_properties_result_t Race_MapProperties_ValidateCandidate(
  const char *contents, const size_t length, const char *map,
  const race_setting_descriptor_t *descriptor,
  const char *canonical_value,
  race_map_properties_t *properties,
  char *error, const size_t error_size) {
  if (!Race_MapProperties_DescriptorSupported(descriptor)) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_UNSUPPORTED_PROPERTY, error, error_size,
      "Setting does not support a map override");
  }

  race_map_properties_t parsed;
  race_map_properties_result_t result = Race_MapProperties_Parse(
    contents, length, map, &parsed, error, error_size);
  if (result != RACE_MAP_PROPERTIES_OK) {
    return result;
  }
  if (parsed.map_matches > 1u) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_DUPLICATE_MAP, error, error_size,
      "Edited map catalog contains multiple target rows");
  }
  if (parsed.map_matches == 1u && Race_MapProperties_HasDuplicate(&parsed)) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_DUPLICATE_PROPERTY, error, error_size,
      "Edited target row contains a duplicate registered property");
  }

  const race_map_property_t *property =
    parsed.properties + descriptor->id;
  if (canonical_value) {
    char canonical[RACE_SETTING_VALUE_SIZE];
    if (!Race_Settings_CanonicalizeValue(
          descriptor, canonical_value, canonical, sizeof(canonical),
          NULL, 0) || strcmp(canonical, canonical_value) ||
        parsed.map_matches != 1u || !property->present || !property->valid ||
        property->duplicate || strcmp(property->canonical, canonical)) {
      return Race_MapProperties_Fail(
        RACE_MAP_PROPERTIES_EXPECTATION_FAILED, error, error_size,
        "Edited map property does not match the expected value");
    }
  } else if (parsed.map_matches == 1u && property->present) {
    return Race_MapProperties_Fail(
      RACE_MAP_PROPERTIES_EXPECTATION_FAILED, error, error_size,
      "Edited map property was not cleared");
  }

  if (properties) {
    *properties = parsed;
  }
  return RACE_MAP_PROPERTIES_OK;
}

const char *Race_MapProperties_ResultName(
  const race_map_properties_result_t result) {
  switch (result) {
    case RACE_MAP_PROPERTIES_OK:
      return "ok";
    case RACE_MAP_PROPERTIES_INVALID_ARGUMENT:
      return "invalid argument";
    case RACE_MAP_PROPERTIES_UNSAFE_PATH:
      return "unsafe path";
    case RACE_MAP_PROPERTIES_TOO_LARGE:
      return "too large";
    case RACE_MAP_PROPERTIES_MALFORMED:
      return "malformed";
    case RACE_MAP_PROPERTIES_TOKEN_TOO_LONG:
      return "token too long";
    case RACE_MAP_PROPERTIES_TOO_MANY_ROWS:
      return "too many rows";
    case RACE_MAP_PROPERTIES_INVALID_MAP:
      return "invalid map";
    case RACE_MAP_PROPERTIES_UNSUPPORTED_PROPERTY:
      return "unsupported property";
    case RACE_MAP_PROPERTIES_INVALID_VALUE:
      return "invalid value";
    case RACE_MAP_PROPERTIES_DUPLICATE_MAP:
      return "duplicate map";
    case RACE_MAP_PROPERTIES_DUPLICATE_PROPERTY:
      return "duplicate property";
    case RACE_MAP_PROPERTIES_OUTPUT_TOO_SMALL:
      return "output too small";
    case RACE_MAP_PROPERTIES_EXPECTATION_FAILED:
      return "expectation failed";
  }
  return "unknown";
}
