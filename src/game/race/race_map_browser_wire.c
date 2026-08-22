/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_map_browser_wire.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RACE_MAP_BROWSER_PAGE_TOKENS 7
#define RACE_MAP_BROWSER_ROW_TOKENS 5
#define RACE_MAP_BROWSER_DETAIL_TOKENS 11
#define RACE_MAP_BROWSER_TIME_TOKENS 2

static bool Race_MapBrowserWire_FieldValid(const char *field,
                                           const size_t capacity) {
  if (!field || !capacity) {
    return false;
  }
  const size_t length = strnlen(field, capacity);
  return length < capacity &&
         !strpbrk(field, "\\\r\n");
}

static bool Race_MapBrowserWire_Append(char *output, const size_t output_size,
                                       const char *format, ...) {
  if (!output || !output_size || !format) {
    return false;
  }
  const size_t length = strnlen(output, output_size);
  if (length >= output_size) {
    return false;
  }

  va_list args;
  va_start(args, format);
  const int32_t written = vsnprintf(output + length, output_size - length,
                                    format, args);
  va_end(args);
  return written >= 0 && (size_t) written < output_size - length;
}

static bool Race_MapBrowserWire_ParseInt(const char *string, int32_t *value) {
  if (!string || !*string || !value) {
    return false;
  }

  char *end;
  const long parsed = strtol(string, &end, 10);
  if (!end || *end || parsed < INT32_MIN || parsed > INT32_MAX) {
    return false;
  }
  *value = (int32_t) parsed;
  return true;
}

/**
 * @brief Record dates outrun int32, so they parse unsigned - and reject the
 * leading sign that strtoull would otherwise fold away.
 */
static bool Race_MapBrowserWire_ParseDate(const char *string,
                                          uint64_t *value) {
  if (!string || !*string || !value || *string == '-' || *string == '+') {
    return false;
  }

  char *end;
  const unsigned long long parsed = strtoull(string, &end, 10);
  if (!end || *end || parsed > RACE_MAP_BROWSER_MAX_DATE_UNIX_S) {
    return false;
  }
  *value = (uint64_t) parsed;
  return true;
}

static bool Race_MapBrowserWire_Tokens(char *copy, const char **tokens,
                                       const size_t capacity,
                                       size_t *num_tokens) {
  if (!copy || !tokens || !capacity || !num_tokens) {
    return false;
  }

  size_t count = 0;
  char *cursor = copy;
  while (true) {
    if (count == capacity) {
      return false;
    }
    tokens[count++] = cursor;
    char *separator = strchr(cursor, '\\');
    if (!separator) {
      break;
    }
    *separator = '\0';
    cursor = separator + 1;
  }
  *num_tokens = count;
  return true;
}

static bool Race_MapBrowserWire_RowValid(const race_map_browser_row_t *row) {
  return row && *row->name &&
         Race_MapBrowserWire_FieldValid(row->name, sizeof(row->name)) &&
         Race_MapBrowserWire_FieldValid(row->title, sizeof(row->title)) &&
         Race_MapBrowserWire_FieldValid(row->author, sizeof(row->author)) &&
         row->best_ms >= 0 && row->pb_ms >= 0;
}

bool Race_MapBrowserWire_EncodePage(const race_map_browser_page_t *page,
                                    char *output, const size_t output_size) {
  if (!page || !output || !output_size || page->page < 1 ||
      page->pages < 1 || page->page > page->pages ||
      page->num_rows < 0 || page->num_rows > RACE_MAP_BROWSER_MAX_ROWS ||
      page->total < page->num_rows ||
      !Race_MapBrowserWire_FieldValid(page->prefix, sizeof(page->prefix)) ||
      !Race_MapBrowserWire_FieldValid(page->scope, sizeof(page->scope))) {
    return false;
  }

  output[0] = '\0';
  if (!Race_MapBrowserWire_Append(output, output_size,
                                  "%s\\%d\\%d\\%d\\%s\\%s\\%d",
                                  RACE_MAP_BROWSER_WIRE_VERSION,
                                  page->page, page->pages, page->total,
                                  page->prefix, page->scope, page->num_rows)) {
    return false;
  }

  for (int32_t i = 0; i < page->num_rows; i++) {
    const race_map_browser_row_t *row = page->rows + i;
    if (!Race_MapBrowserWire_RowValid(row) ||
        !Race_MapBrowserWire_Append(output, output_size,
                                    "\\%s\\%s\\%s\\%d\\%d",
                                    row->name, row->title, row->author,
                                    row->best_ms, row->pb_ms)) {
      output[0] = '\0';
      return false;
    }
  }
  return true;
}

bool Race_MapBrowserWire_DecodePage(const char *payload,
                                    race_map_browser_page_t *page) {
  if (!payload || !page || q_strlen(payload) >= MAX_STRING_CHARS) {
    return false;
  }

  char copy[MAX_STRING_CHARS];
  q_strlcpy(copy, payload, sizeof(copy));
  const char *tokens[RACE_MAP_BROWSER_PAGE_TOKENS +
                     RACE_MAP_BROWSER_MAX_ROWS * RACE_MAP_BROWSER_ROW_TOKENS];
  size_t num_tokens;
  if (!Race_MapBrowserWire_Tokens(copy, tokens, lengthof(tokens),
                                  &num_tokens) ||
      num_tokens < RACE_MAP_BROWSER_PAGE_TOKENS ||
      q_strcmp(tokens[0], RACE_MAP_BROWSER_WIRE_VERSION)) {
    return false;
  }

  race_map_browser_page_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  if (!Race_MapBrowserWire_ParseInt(tokens[1], &parsed.page) ||
      !Race_MapBrowserWire_ParseInt(tokens[2], &parsed.pages) ||
      !Race_MapBrowserWire_ParseInt(tokens[3], &parsed.total) ||
      !Race_MapBrowserWire_ParseInt(tokens[6], &parsed.num_rows) ||
      parsed.page < 1 || parsed.pages < 1 || parsed.page > parsed.pages ||
      parsed.num_rows < 0 || parsed.num_rows > RACE_MAP_BROWSER_MAX_ROWS ||
      parsed.total < parsed.num_rows ||
      num_tokens != (size_t) RACE_MAP_BROWSER_PAGE_TOKENS +
                    (size_t) parsed.num_rows * RACE_MAP_BROWSER_ROW_TOKENS ||
      q_strlen(tokens[4]) >= sizeof(parsed.prefix) ||
      q_strlen(tokens[5]) >= sizeof(parsed.scope) ||
      !Race_MapBrowserWire_FieldValid(tokens[4], sizeof(parsed.prefix)) ||
      !Race_MapBrowserWire_FieldValid(tokens[5], sizeof(parsed.scope))) {
    return false;
  }

  q_strlcpy(parsed.prefix, tokens[4], sizeof(parsed.prefix));
  q_strlcpy(parsed.scope, tokens[5], sizeof(parsed.scope));
  size_t token = RACE_MAP_BROWSER_PAGE_TOKENS;
  for (int32_t i = 0; i < parsed.num_rows; i++) {
    race_map_browser_row_t *row = parsed.rows + i;
    if (q_strlen(tokens[token]) >= sizeof(row->name) ||
        q_strlen(tokens[token + 1]) >= sizeof(row->title) ||
        q_strlen(tokens[token + 2]) >= sizeof(row->author) ||
        !Race_MapBrowserWire_ParseInt(tokens[token + 3], &row->best_ms) ||
        !Race_MapBrowserWire_ParseInt(tokens[token + 4], &row->pb_ms)) {
      return false;
    }
    q_strlcpy(row->name, tokens[token], sizeof(row->name));
    q_strlcpy(row->title, tokens[token + 1], sizeof(row->title));
    q_strlcpy(row->author, tokens[token + 2], sizeof(row->author));
    token += RACE_MAP_BROWSER_ROW_TOKENS;
    if (!Race_MapBrowserWire_RowValid(row)) {
      return false;
    }
  }

  *page = parsed;
  return true;
}

static bool Race_MapBrowserWire_DetailValid(
  const race_map_browser_detail_t *detail) {
  if (!detail || !detail->valid || !*detail->name ||
      !Race_MapBrowserWire_FieldValid(detail->name, sizeof(detail->name)) ||
      !Race_MapBrowserWire_FieldValid(detail->title, sizeof(detail->title)) ||
      !Race_MapBrowserWire_FieldValid(detail->author,
                                      sizeof(detail->author)) ||
      !Race_MapBrowserWire_FieldValid(detail->record_holder,
                                      sizeof(detail->record_holder)) ||
      detail->best_ms < 0 || detail->pb_ms < 0 || detail->ranked_runs < 0 ||
      detail->record_date_unix_s > RACE_MAP_BROWSER_MAX_DATE_UNIX_S ||
      detail->num_times < 0 ||
      detail->num_times > RACE_MAP_BROWSER_MAX_TIMES ||
      detail->local_rank < 0 || detail->local_rank > detail->num_times ||
      detail->ranked_runs < detail->num_times) {
    return false;
  }
  for (int32_t i = 0; i < detail->num_times; i++) {
    const race_map_browser_time_t *time = detail->times + i;
    if (!*time->player ||
        !Race_MapBrowserWire_FieldValid(time->player,
                                        sizeof(time->player)) ||
        time->time_ms < 0) {
      return false;
    }
  }
  return true;
}

bool Race_MapBrowserWire_EncodeDetail(const race_map_browser_detail_t *detail,
                                      char *output,
                                      const size_t output_size) {
  if (!output || !output_size || !Race_MapBrowserWire_DetailValid(detail)) {
    return false;
  }

  output[0] = '\0';
  if (!Race_MapBrowserWire_Append(
        output, output_size, "%s\\%s\\%s\\%s\\%s\\%d\\%d\\%d\\%llu\\%d\\%d",
        RACE_MAP_BROWSER_WIRE_VERSION, detail->name, detail->title,
        detail->author, detail->record_holder, detail->best_ms,
        detail->pb_ms, detail->ranked_runs,
        (unsigned long long) detail->record_date_unix_s, detail->local_rank,
        detail->num_times)) {
    return false;
  }

  for (int32_t i = 0; i < detail->num_times; i++) {
    const race_map_browser_time_t *time = detail->times + i;
    if (!Race_MapBrowserWire_Append(output, output_size, "\\%s\\%d",
                                    time->player, time->time_ms)) {
      output[0] = '\0';
      return false;
    }
  }
  return true;
}

bool Race_MapBrowserWire_DecodeDetail(const char *payload,
                                      race_map_browser_detail_t *detail) {
  if (!payload || !detail || q_strlen(payload) >= MAX_STRING_CHARS) {
    return false;
  }

  char copy[MAX_STRING_CHARS];
  q_strlcpy(copy, payload, sizeof(copy));
  const char *tokens[RACE_MAP_BROWSER_DETAIL_TOKENS +
                     RACE_MAP_BROWSER_MAX_TIMES *
                     RACE_MAP_BROWSER_TIME_TOKENS];
  size_t num_tokens;
  if (!Race_MapBrowserWire_Tokens(copy, tokens, lengthof(tokens),
                                  &num_tokens) ||
      num_tokens < RACE_MAP_BROWSER_DETAIL_TOKENS ||
      q_strcmp(tokens[0], RACE_MAP_BROWSER_WIRE_VERSION)) {
    return false;
  }

  race_map_browser_detail_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  if (q_strlen(tokens[1]) >= sizeof(parsed.name) ||
      q_strlen(tokens[2]) >= sizeof(parsed.title) ||
      q_strlen(tokens[3]) >= sizeof(parsed.author) ||
      q_strlen(tokens[4]) >= sizeof(parsed.record_holder) ||
      !Race_MapBrowserWire_ParseInt(tokens[5], &parsed.best_ms) ||
      !Race_MapBrowserWire_ParseInt(tokens[6], &parsed.pb_ms) ||
      !Race_MapBrowserWire_ParseInt(tokens[7], &parsed.ranked_runs) ||
      !Race_MapBrowserWire_ParseDate(tokens[8], &parsed.record_date_unix_s) ||
      !Race_MapBrowserWire_ParseInt(tokens[9], &parsed.local_rank) ||
      !Race_MapBrowserWire_ParseInt(tokens[10], &parsed.num_times) ||
      parsed.num_times < 0 ||
      parsed.num_times > RACE_MAP_BROWSER_MAX_TIMES ||
      num_tokens != (size_t) RACE_MAP_BROWSER_DETAIL_TOKENS +
                    (size_t) parsed.num_times *
                    RACE_MAP_BROWSER_TIME_TOKENS) {
    return false;
  }

  q_strlcpy(parsed.name, tokens[1], sizeof(parsed.name));
  q_strlcpy(parsed.title, tokens[2], sizeof(parsed.title));
  q_strlcpy(parsed.author, tokens[3], sizeof(parsed.author));
  q_strlcpy(parsed.record_holder, tokens[4], sizeof(parsed.record_holder));

  size_t token = RACE_MAP_BROWSER_DETAIL_TOKENS;
  for (int32_t i = 0; i < parsed.num_times; i++) {
    race_map_browser_time_t *time = parsed.times + i;
    if (q_strlen(tokens[token]) >= sizeof(time->player) ||
        !Race_MapBrowserWire_ParseInt(tokens[token + 1], &time->time_ms)) {
      return false;
    }
    q_strlcpy(time->player, tokens[token], sizeof(time->player));
    token += RACE_MAP_BROWSER_TIME_TOKENS;
  }

  parsed.valid = true;
  if (!Race_MapBrowserWire_DetailValid(&parsed)) {
    return false;
  }

  *detail = parsed;
  return true;
}
