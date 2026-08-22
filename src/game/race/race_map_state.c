/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_map_state.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define RACE_MAP_STATE_VERSION_PREFIX "QUETOO_RACE_MAP_STATE_V"
#define RACE_MAP_STATE_LEGACY_MAGIC "# RACE_MAP_STATE_V1"

typedef struct {
  const char *data;
  size_t length;
  size_t position;
} race_map_state_reader_t;

typedef struct {
  char *data;
  size_t capacity;
  size_t length;
  bool failed;
} race_map_state_writer_t;

typedef struct {
  const char *data;
  size_t length;
} race_map_state_span_t;

static char Race_MapState_Lower(char c) {
  return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

static bool Race_MapState_IsMapCharacter(char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') ||
         c == '_' || c == '-' || c == '.' || c == '/';
}

static bool Race_MapState_BoundedLength(const char *string, size_t maximum,
                                        size_t *length) {
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

bool Race_MapState_CanonicalizeMap(const char *input,
                                   char output[RACE_MAP_IDENTITY_SIZE]) {
  if (!output) {
    return false;
  }

  output[0] = '\0';

  size_t length;
  if (!Race_MapState_BoundedLength(input, RACE_MAP_IDENTITY_MAX, &length) ||
      !length) {
    return false;
  }

  char lowered[RACE_MAP_IDENTITY_SIZE];
  for (size_t i = 0; i < length; i++) {
    lowered[i] = Race_MapState_Lower(input[i]);
  }
  lowered[length] = '\0';

  const char *map = lowered;
  if (length >= 5 && !memcmp(map, "maps/", 5)) {
    map += 5;
    length -= 5;
  }

  if (length >= 4 && !memcmp(map + length - 4, ".bsp", 4)) {
    length -= 4;
  }

  if (!length || length > RACE_MAP_IDENTITY_MAX ||
      map[0] == '/' || map[length - 1] == '/') {
    return false;
  }

  size_t segment_start = 0;
  for (size_t i = 0; i <= length; i++) {
    if (i < length && !Race_MapState_IsMapCharacter(map[i])) {
      return false;
    }

    if (i == length || map[i] == '/') {
      const size_t segment_length = i - segment_start;
      if (!segment_length ||
          (segment_length == 1 && map[segment_start] == '.') ||
          (segment_length == 2 && map[segment_start] == '.' &&
           map[segment_start + 1] == '.')) {
        return false;
      }
      segment_start = i + 1;
    }
  }

  memcpy(output, map, length);
  output[length] = '\0';
  return true;
}

static void Race_MapState_HexEncode(const void *data, size_t length, char *output) {
  static const char hex[] = "0123456789abcdef";
  const uint8_t *bytes = data;

  for (size_t i = 0; i < length; i++) {
    output[i * 2] = hex[bytes[i] >> 4];
    output[i * 2 + 1] = hex[bytes[i] & 0xf];
  }
  output[length * 2] = '\0';
}

bool Race_MapState_EncodeMap(const char *map,
                             char output[RACE_MAP_IDENTITY_ENCODED_SIZE]) {
  if (!output) {
    return false;
  }

  output[0] = '\0';

  char canonical[RACE_MAP_IDENTITY_SIZE];
  if (!Race_MapState_CanonicalizeMap(map, canonical)) {
    return false;
  }

  Race_MapState_HexEncode(canonical, strlen(canonical), output);
  return true;
}

static int32_t Race_MapState_HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

static bool Race_MapState_HexDecode(race_map_state_span_t encoded,
                                    void *output, size_t capacity,
                                    size_t *output_length) {
  if ((encoded.length & 1u) || encoded.length / 2u > capacity) {
    return false;
  }

  uint8_t *bytes = output;
  const size_t length = encoded.length / 2u;
  for (size_t i = 0; i < length; i++) {
    const int32_t high = Race_MapState_HexValue(encoded.data[i * 2]);
    const int32_t low = Race_MapState_HexValue(encoded.data[i * 2 + 1]);
    if (high < 0 || low < 0) {
      return false;
    }

    bytes[i] = (uint8_t) ((high << 4) | low);
    if (!bytes[i]) {
      return false;
    }
  }

  if (output_length) {
    *output_length = length;
  }
  return true;
}

bool Race_MapState_RulesetValid(const char *ruleset) {
  size_t length;
  if (!Race_MapState_BoundedLength(ruleset, RACE_RULESET_ID_MAX, &length) ||
      !length) {
    return false;
  }

  for (size_t i = 0; i < length; i++) {
    const char c = ruleset[i];
    if (!((c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')) {
      return false;
    }
  }
  return true;
}

bool Race_MapState_Paths(const char *ruleset, const char *map,
                          char *committed, size_t committed_size,
                          char *candidate, size_t candidate_size) {
  if (!committed || !committed_size || !candidate || !candidate_size) {
    return false;
  }

  char encoded[RACE_MAP_IDENTITY_ENCODED_SIZE];
  if (!Race_MapState_RulesetValid(ruleset) ||
      !Race_MapState_EncodeMap(map, encoded)) {
    return false;
  }

  const int32_t committed_length = snprintf(committed, committed_size,
                                             RACE_MAP_STATE_ROOT_DIRECTORY
                                             "/%s/%s.state",
                                             ruleset, encoded);
  const int32_t candidate_length = snprintf(candidate, candidate_size,
                                             RACE_MAP_STATE_ROOT_DIRECTORY
                                             "/%s/%s.candidate",
                                             ruleset, encoded);

  return committed_length >= 0 && (size_t) committed_length < committed_size &&
         candidate_length >= 0 && (size_t) candidate_length < candidate_size;
}

bool Race_MapState_Init(race_map_state_t *state,
                        race_leaderboard_record_t *records, size_t capacity,
                        const char *map, const char *ruleset) {
  if (!state || (!records && capacity) || capacity > RACE_MAP_STATE_MAX_RECORDS) {
    return false;
  }

  char canonical[RACE_MAP_IDENTITY_SIZE];
  if (!Race_MapState_CanonicalizeMap(map, canonical) ||
      !Race_MapState_RulesetValid(ruleset)) {
    return false;
  }

  memset(state, 0, sizeof(*state));
  memcpy(state->map, canonical, strlen(canonical) + 1);
  memcpy(state->ruleset, ruleset, strlen(ruleset) + 1u);
  state->format = RACE_MAP_STATE_FORMAT_V1;
  state->records = records;
  state->record_capacity = capacity;
  if (records && capacity) {
    memset(records, 0, capacity * sizeof(*records));
  }
  return true;
}

bool Race_MapState_Valid(const race_map_state_t *state) {
  if (!state || state->record_count > state->record_capacity ||
      state->record_capacity > RACE_MAP_STATE_MAX_RECORDS ||
      (state->record_capacity && !state->records) ||
      (state->record_count && !state->generation) ||
      !Race_MapState_RulesetValid(state->ruleset) ||
      (state->format != RACE_MAP_STATE_FORMAT_V1 &&
       state->format != RACE_MAP_STATE_FORMAT_V2 &&
       state->format != RACE_MAP_STATE_FORMAT_V3 &&
       state->format != RACE_MAP_STATE_FORMAT_V4)) {
    return false;
  }

  char canonical[RACE_MAP_IDENTITY_SIZE];
  if (!Race_MapState_CanonicalizeMap(state->map, canonical) ||
      strcmp(state->map, canonical) ||
      !Race_Leaderboard_RecordsValid(state->records, state->record_count)) {
    return false;
  }

  for (size_t i = 0; i < state->record_count; i++) {
    const bool has_replay = state->records[i].replay_id != 0;
    const bool replay_backed = state->format == RACE_MAP_STATE_FORMAT_V2 ||
                               state->format == RACE_MAP_STATE_FORMAT_V3 ||
                               state->format == RACE_MAP_STATE_FORMAT_V4;
    if (has_replay != replay_backed ||
        (state->records[i].date_unix_s &&
         state->format != RACE_MAP_STATE_FORMAT_V3 &&
         state->format != RACE_MAP_STATE_FORMAT_V4) ||
        (state->records[i].split_count &&
         state->format != RACE_MAP_STATE_FORMAT_V4)) {
      return false;
    }
  }

  return true;
}

bool Race_MapState_ReplayBacked(const race_map_state_t *state) {
  return Race_MapState_Valid(state) &&
         (state->format == RACE_MAP_STATE_FORMAT_V2 ||
          state->format == RACE_MAP_STATE_FORMAT_V3 ||
          state->format == RACE_MAP_STATE_FORMAT_V4);
}

bool Race_MapState_CanPublishReplay(const race_map_state_t *state) {
  return Race_MapState_Valid(state) &&
         (state->format == RACE_MAP_STATE_FORMAT_V2 ||
          state->format == RACE_MAP_STATE_FORMAT_V3 ||
          state->format == RACE_MAP_STATE_FORMAT_V4 ||
          !state->record_count);
}

bool Race_MapState_Equals(const race_map_state_t *left,
                          const race_map_state_t *right) {
  if (!Race_MapState_Valid(left) || !Race_MapState_Valid(right) ||
      strcmp(left->map, right->map) ||
      strcmp(left->ruleset, right->ruleset) ||
      left->format != right->format ||
      left->generation != right->generation ||
      left->record_count != right->record_count) {
    return false;
  }

  for (size_t i = 0; i < left->record_count; i++) {
    const race_leaderboard_record_t *left_record = left->records + i;
    const race_leaderboard_record_t *right_record = right->records + i;
    if (strcmp(left_record->uid, right_record->uid) ||
        strcmp(left_record->display_name, right_record->display_name) ||
        left_record->elapsed_time != right_record->elapsed_time ||
        left_record->date_unix_s != right_record->date_unix_s ||
        left_record->checkpoint_count != right_record->checkpoint_count ||
        left_record->split_count != right_record->split_count ||
        left_record->split_layout != right_record->split_layout ||
        left_record->replay_id != right_record->replay_id ||
        memcmp(left_record->checkpoint_times, right_record->checkpoint_times,
               sizeof(left_record->checkpoint_times)) ||
        memcmp(left_record->split_times, right_record->split_times,
               sizeof(left_record->split_times))) {
      return false;
    }
  }

  return true;
}

bool Race_MapState_EvaluateCandidate(const race_map_state_t *state,
                                     const race_leaderboard_record_t *candidate,
                                     race_leaderboard_evaluation_t *evaluation) {
  if (!Race_MapState_Valid(state)) {
    if (evaluation) {
      memset(evaluation, 0, sizeof(*evaluation));
    }
    return false;
  }

  return Race_Leaderboard_Evaluate(state->records, state->record_count,
                                   state->record_capacity, candidate, evaluation);
}

bool Race_MapState_ApplyCandidate(const race_map_state_t *current,
                                  const race_leaderboard_record_t *candidate,
                                  race_map_state_t *next,
                                  race_leaderboard_evaluation_t *evaluation) {
  if (!Race_MapState_Valid(current) || !next ||
      !next->records || next->records == current->records ||
      next->record_capacity < current->record_count ||
      next->record_capacity > RACE_MAP_STATE_MAX_RECORDS ||
      current->generation == UINT64_MAX) {
    if (evaluation) {
      memset(evaluation, 0, sizeof(*evaluation));
    }
    return false;
  }


  const bool replay_backed_candidate = candidate && candidate->replay_id;
  if (((current->format == RACE_MAP_STATE_FORMAT_V2 ||
        current->format == RACE_MAP_STATE_FORMAT_V3 ||
        current->format == RACE_MAP_STATE_FORMAT_V4) &&
       !replay_backed_candidate) ||
      (current->format == RACE_MAP_STATE_FORMAT_V1 && replay_backed_candidate &&
       current->record_count)) {
    if (evaluation) {
      memset(evaluation, 0, sizeof(*evaluation));
    }
    return false;
  }

  race_leaderboard_evaluation_t local;
  race_leaderboard_evaluation_t *result = evaluation ? evaluation : &local;
  if (!Race_MapState_EvaluateCandidate(current, candidate, result) ||
      !result->would_accept) {
    return false;
  }

  race_leaderboard_record_t *next_records = next->records;
  const size_t next_capacity = next->record_capacity;
  memset(next, 0, sizeof(*next));
  next->records = next_records;
  next->record_capacity = next_capacity;
  memcpy(next->map, current->map, sizeof(next->map));
  memcpy(next->ruleset, current->ruleset, sizeof(next->ruleset));
  next->format = replay_backed_candidate
    ? RACE_MAP_STATE_FORMAT_V4
    : current->format;
  next->generation = current->generation + 1u;
  next->record_count = current->record_count;
  memcpy(next->records, current->records,
         current->record_count * sizeof(*current->records));

  return Race_Leaderboard_Apply(next->records, &next->record_count,
                                next->record_capacity, candidate, result);
}

static void Race_MapState_Write(race_map_state_writer_t *writer,
                                const char *format, ...) {
  if (writer->failed) {
    return;
  }

  va_list args;
  va_start(args, format);
  const int32_t length = vsnprintf(writer->data + writer->length,
                                   writer->capacity - writer->length,
                                   format, args);
  va_end(args);

  if (length < 0 || (size_t) length >= writer->capacity - writer->length) {
    writer->failed = true;
    return;
  }

  writer->length += (size_t) length;
}

static uint32_t Race_MapState_Crc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = UINT32_MAX;

  for (size_t i = 0; i < length; i++) {
    crc ^= bytes[i];
    for (size_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                          (uint32_t) -(int32_t) (crc & 1u));
    }
  }

  return ~crc;
}

bool Race_MapState_Serialize(const race_map_state_t *state,
                             char *output, size_t output_size,
                             size_t *output_length) {
  if (!Race_MapState_Valid(state) || !state->generation ||
      !output || !output_size) {
    return false;
  }

  const size_t capacity = output_size < RACE_MAP_STATE_MAX_FILE_BYTES + 1u
    ? output_size
    : RACE_MAP_STATE_MAX_FILE_BYTES + 1u;

  race_map_state_writer_t writer = {
    .data = output,
    .capacity = capacity
  };

  char encoded_map[RACE_MAP_IDENTITY_ENCODED_SIZE];
  Race_MapState_HexEncode(state->map, strlen(state->map), encoded_map);

  const char *magic = state->format == RACE_MAP_STATE_FORMAT_V4
    ? RACE_MAP_STATE_MAGIC_V4
    : state->format == RACE_MAP_STATE_FORMAT_V3
      ? RACE_MAP_STATE_MAGIC_V3
    : state->format == RACE_MAP_STATE_FORMAT_V2
      ? RACE_MAP_STATE_MAGIC_V2
      : RACE_MAP_STATE_MAGIC_V1;
  const char *publication = state->format == RACE_MAP_STATE_FORMAT_V1
    ? RACE_MAP_STATE_PUBLICATION_V1
    : RACE_MAP_STATE_PUBLICATION_V2;

  Race_MapState_Write(&writer,
                      "%s\n"
                      "map=%s\n"
                      "ruleset=%s\n"
                      "publication=%s\n"
                      "generation=%llu\n"
                      "records=%zu\n",
                      magic,
                      encoded_map,
                      state->ruleset,
                      publication,
                      (unsigned long long) state->generation,
                      state->record_count);

  for (size_t i = 0; i < state->record_count && !writer.failed; i++) {
    const race_leaderboard_record_t *record = state->records + i;
    char encoded_name[RACE_PROFILE_NAME_MAX * 2u + 1u];
    Race_MapState_HexEncode(record->display_name,
                            strlen(record->display_name), encoded_name);

    if (state->format == RACE_MAP_STATE_FORMAT_V4) {
      Race_MapState_Write(&writer,
                          "record=%s|%s|%u|%u|%016" PRIx64 "|%llu|",
                          record->uid, encoded_name, record->elapsed_time,
                          record->checkpoint_count, record->replay_id,
                          (unsigned long long) record->date_unix_s);
    } else if (state->format == RACE_MAP_STATE_FORMAT_V3) {
      Race_MapState_Write(&writer,
                          "record=%s|%s|%u|%u|%016" PRIx64 "|%llu|",
                          record->uid, encoded_name, record->elapsed_time,
                          record->checkpoint_count, record->replay_id,
                          (unsigned long long) record->date_unix_s);
    } else if (state->format == RACE_MAP_STATE_FORMAT_V2) {
      Race_MapState_Write(&writer, "record=%s|%s|%u|%u|%016" PRIx64 "|",
                          record->uid, encoded_name, record->elapsed_time,
                          record->checkpoint_count, record->replay_id);
    } else {
      Race_MapState_Write(&writer, "record=%s|%s|%u|%u|",
                          record->uid, encoded_name, record->elapsed_time,
                          record->checkpoint_count);
    }
    for (size_t split = 0; split < record->checkpoint_count; split++) {
      Race_MapState_Write(&writer, "%s%u", split ? "," : "",
                          record->checkpoint_times[split]);
    }
    if (state->format == RACE_MAP_STATE_FORMAT_V4) {
      Race_MapState_Write(&writer, "|%u|%016" PRIx64 "|",
                          record->split_count, record->split_layout);
      for (size_t split = 0; split < record->split_count; split++) {
        Race_MapState_Write(&writer, "%s%u", split ? "," : "",
                            record->split_times[split]);
      }
    }
    Race_MapState_Write(&writer, "\n");
  }

  if (writer.failed || writer.length > RACE_MAP_STATE_MAX_FILE_BYTES) {
    return false;
  }

  const uint32_t checksum = Race_MapState_Crc32(output, writer.length);
  Race_MapState_Write(&writer, "crc=%08x\n", checksum);
  if (writer.failed || writer.length > RACE_MAP_STATE_MAX_FILE_BYTES) {
    return false;
  }

  if (output_length) {
    *output_length = writer.length;
  }
  return true;
}

static bool Race_MapState_NextLine(race_map_state_reader_t *reader,
                                   race_map_state_span_t *line) {
  if (reader->position >= reader->length) {
    return false;
  }

  const size_t start = reader->position;
  while (reader->position < reader->length &&
         reader->data[reader->position] != '\n') {
    reader->position++;
  }

  if (reader->position == reader->length) {
    return false;
  }

  line->data = reader->data + start;
  line->length = reader->position - start;
  reader->position++;
  return true;
}

static bool Race_MapState_SpanEquals(race_map_state_span_t span,
                                     const char *string) {
  const size_t length = strlen(string);
  return span.length == length && !memcmp(span.data, string, length);
}

static bool Race_MapState_SpanPrefix(race_map_state_span_t span,
                                     const char *prefix,
                                     race_map_state_span_t *value) {
  const size_t length = strlen(prefix);
  if (span.length < length || memcmp(span.data, prefix, length)) {
    return false;
  }

  value->data = span.data + length;
  value->length = span.length - length;
  return true;
}

static bool Race_MapState_Decimal(race_map_state_span_t span,
                                  uint64_t maximum, uint64_t *value) {
  if (!span.length || (span.length > 1 && span.data[0] == '0')) {
    return false;
  }

  uint64_t parsed = 0;
  for (size_t i = 0; i < span.length; i++) {
    if (span.data[i] < '0' || span.data[i] > '9') {
      return false;
    }

    const uint64_t digit = (uint64_t) (span.data[i] - '0');
    if (parsed > (maximum - digit) / 10u) {
      return false;
    }
    parsed = parsed * 10u + digit;
  }

  *value = parsed;
  return true;
}

static bool Race_MapState_ParseChecksum(race_map_state_span_t span,
                                        uint32_t *checksum) {
  if (span.length != 8) {
    return false;
  }

  uint32_t parsed = 0;
  for (size_t i = 0; i < span.length; i++) {
    const int32_t digit = Race_MapState_HexValue(span.data[i]);
    if (digit < 0) {
      return false;
    }
    parsed = (parsed << 4) | (uint32_t) digit;
  }

  *checksum = parsed;
  return true;
}

static bool Race_MapState_SplitRecord(race_map_state_span_t value,
                                      race_map_state_span_t *fields,
                                      size_t field_count) {
  size_t field = 0;
  size_t start = 0;

  for (size_t i = 0; i <= value.length; i++) {
    if (i == value.length || value.data[i] == '|') {
      if (field >= field_count) {
        return false;
      }
      fields[field].data = value.data + start;
      fields[field].length = i - start;
      field++;
      start = i + 1;
    }
  }

  return field == field_count;
}

static bool Race_MapState_ParseHex64(race_map_state_span_t span,
                                    uint64_t *value, const bool allow_zero) {
  if (span.length != 16u || !value) {
    return false;
  }

  uint64_t parsed = 0;
  for (size_t i = 0; i < span.length; i++) {
    const int32_t digit = Race_MapState_HexValue(span.data[i]);
    if (digit < 0) {
      return false;
    }
    parsed = (parsed << 4u) | (uint64_t) digit;
  }

  if (!parsed && !allow_zero) {
    return false;
  }
  *value = parsed;
  return true;
}

static bool Race_MapState_ParseReplayId(race_map_state_span_t span,
                                        uint64_t *replay_id) {
  return Race_MapState_ParseHex64(span, replay_id, false);
}

static bool Race_MapState_CopySpan(race_map_state_span_t span,
                                   char *output, size_t capacity) {
  if (!capacity || span.length >= capacity) {
    return false;
  }
  memcpy(output, span.data, span.length);
  output[span.length] = '\0';
  return true;
}

static bool Race_MapState_ParseSplits(race_map_state_span_t span,
                                      size_t count, uint32_t elapsed,
                                      uint32_t splits[RACE_MAX_CHECKPOINTS]) {
  if (!count) {
    return !span.length;
  }

  size_t start = 0;
  size_t parsed_count = 0;
  uint32_t previous = 0;

  for (size_t i = 0; i <= span.length; i++) {
    if (i == span.length || span.data[i] == ',') {
      if (parsed_count >= count) {
        return false;
      }

      const race_map_state_span_t token = {
        .data = span.data + start,
        .length = i - start
      };
      uint64_t parsed;
      if (!Race_MapState_Decimal(token, elapsed, &parsed) ||
          !parsed || parsed <= previous) {
        return false;
      }

      splits[parsed_count++] = (uint32_t) parsed;
      previous = (uint32_t) parsed;
      start = i + 1;
    }
  }

  return parsed_count == count;
}

static race_map_state_parse_result_t Race_MapState_ParseHeader(
  race_map_state_reader_t *reader, race_map_state_span_t *line,
  race_map_state_format_t *format) {
  if (!Race_MapState_NextLine(reader, line)) {
    return RACE_MAP_STATE_PARSE_MALFORMED;
  }

  if (Race_MapState_SpanEquals(*line, RACE_MAP_STATE_MAGIC_V1)) {
    *format = RACE_MAP_STATE_FORMAT_V1;
    return RACE_MAP_STATE_PARSE_OK;
  }
  if (Race_MapState_SpanEquals(*line, RACE_MAP_STATE_MAGIC_V2)) {
    *format = RACE_MAP_STATE_FORMAT_V2;
    return RACE_MAP_STATE_PARSE_OK;
  }
  if (Race_MapState_SpanEquals(*line, RACE_MAP_STATE_MAGIC_V3)) {
    *format = RACE_MAP_STATE_FORMAT_V3;
    return RACE_MAP_STATE_PARSE_OK;
  }
  if (Race_MapState_SpanEquals(*line, RACE_MAP_STATE_MAGIC_V4)) {
    *format = RACE_MAP_STATE_FORMAT_V4;
    return RACE_MAP_STATE_PARSE_OK;
  }
  if (Race_MapState_SpanEquals(*line, RACE_MAP_STATE_LEGACY_MAGIC)) {
    return RACE_MAP_STATE_PARSE_LEGACY_UNSUPPORTED;
  }

  const size_t prefix_length = sizeof(RACE_MAP_STATE_VERSION_PREFIX) - 1u;
  if (line->length >= prefix_length &&
      !memcmp(line->data, RACE_MAP_STATE_VERSION_PREFIX, prefix_length)) {
    return RACE_MAP_STATE_PARSE_UNKNOWN_VERSION;
  }
  return RACE_MAP_STATE_PARSE_MALFORMED;
}

race_map_state_parse_result_t Race_MapState_Parse(
  const void *data, size_t length,
  race_leaderboard_record_t *records, size_t capacity,
  race_map_state_t *state) {
  if (!data || !state || (!records && capacity)) {
    return RACE_MAP_STATE_PARSE_MALFORMED;
  }
  if (length > RACE_MAP_STATE_MAX_FILE_BYTES) {
    return RACE_MAP_STATE_PARSE_TOO_LARGE;
  }
  if (!length || capacity > RACE_MAP_STATE_MAX_RECORDS ||
      memchr(data, '\0', length) || memchr(data, '\r', length)) {
    return RACE_MAP_STATE_PARSE_MALFORMED;
  }

  race_map_state_reader_t reader = {
    .data = data,
    .length = length
  };
  race_map_state_span_t line;
  race_map_state_format_t format;
  race_map_state_parse_result_t header = Race_MapState_ParseHeader(&reader, &line,
                                                                    &format);
  if (header != RACE_MAP_STATE_PARSE_OK) {
    return header;
  }

  race_map_state_span_t value;
  char decoded_map[RACE_MAP_IDENTITY_SIZE];
  size_t decoded_map_length;
  if (!Race_MapState_NextLine(&reader, &line) ||
      !Race_MapState_SpanPrefix(line, "map=", &value) ||
      !Race_MapState_HexDecode(value, decoded_map, RACE_MAP_IDENTITY_MAX,
                               &decoded_map_length)) {
    return RACE_MAP_STATE_PARSE_MALFORMED;
  }
  decoded_map[decoded_map_length] = '\0';

  char canonical_map[RACE_MAP_IDENTITY_SIZE];
  if (!Race_MapState_CanonicalizeMap(decoded_map, canonical_map) ||
      strcmp(decoded_map, canonical_map)) {
    return RACE_MAP_STATE_PARSE_MALFORMED;
  }

  if (!Race_MapState_NextLine(&reader, &line) ||
      !Race_MapState_SpanPrefix(line, "ruleset=", &value) ||
      !value.length || value.length > RACE_RULESET_ID_MAX) {
    return RACE_MAP_STATE_PARSE_MALFORMED;
  }
  char ruleset[RACE_RULESET_ID_SIZE];
  memcpy(ruleset, value.data, value.length);
  ruleset[value.length] = '\0';
  if (!Race_MapState_RulesetValid(ruleset)) {
    return RACE_MAP_STATE_PARSE_UNSUPPORTED_RULESET;
  }

  if (!Race_MapState_NextLine(&reader, &line) ||
      !Race_MapState_SpanPrefix(line, "publication=", &value) ||
      !Race_MapState_SpanEquals(value,
        format == RACE_MAP_STATE_FORMAT_V1
          ? RACE_MAP_STATE_PUBLICATION_V1
          : RACE_MAP_STATE_PUBLICATION_V2)) {
    return RACE_MAP_STATE_PARSE_MALFORMED;
  }

  uint64_t generation;
  if (!Race_MapState_NextLine(&reader, &line) ||
      !Race_MapState_SpanPrefix(line, "generation=", &value) ||
      !Race_MapState_Decimal(value, UINT64_MAX, &generation) || !generation) {
    return RACE_MAP_STATE_PARSE_MALFORMED;
  }

  uint64_t record_count;
  if (!Race_MapState_NextLine(&reader, &line) ||
      !Race_MapState_SpanPrefix(line, "records=", &value) ||
      !Race_MapState_Decimal(value, RACE_MAP_STATE_MAX_RECORDS, &record_count)) {
    return RACE_MAP_STATE_PARSE_MALFORMED;
  }
  if (record_count > capacity) {
    return RACE_MAP_STATE_PARSE_BOUNDS;
  }

  if (!Race_MapState_Init(state, records, capacity, canonical_map, ruleset)) {
    return RACE_MAP_STATE_PARSE_BOUNDS;
  }
  state->generation = generation;
  state->format = format;

  for (size_t row = 0; row < (size_t) record_count; row++) {
    if (!Race_MapState_NextLine(&reader, &line) ||
        !Race_MapState_SpanPrefix(line, "record=", &value)) {
      return RACE_MAP_STATE_PARSE_MALFORMED;
    }

    race_map_state_span_t fields[10];
    const size_t field_count = format == RACE_MAP_STATE_FORMAT_V4
      ? 10u
      : format == RACE_MAP_STATE_FORMAT_V3 ? 7u
      : format == RACE_MAP_STATE_FORMAT_V2 ? 6u : 5u;
    if (!Race_MapState_SplitRecord(value, fields, field_count)) {
      return RACE_MAP_STATE_PARSE_MALFORMED;
    }

    char uid[RACE_PROFILE_UID_SIZE];
    char display_name[RACE_PROFILE_NAME_SIZE];
    size_t display_name_length;
    uint64_t elapsed;
    uint64_t checkpoint_count;
    uint64_t split_count = 0u;
    uint64_t split_layout = 0u;
    uint64_t replay_id = 0;
    uint64_t date_unix_s = 0;
    uint32_t splits[RACE_MAX_CHECKPOINTS] = { 0 };
    uint32_t analytical_splits[RACE_MAX_CHECKPOINTS] = { 0 };
    const size_t checkpoint_times_field = format == RACE_MAP_STATE_FORMAT_V1
      ? 4u
      : format == RACE_MAP_STATE_FORMAT_V2 ? 5u : 6u;

    if (!Race_MapState_CopySpan(fields[0], uid, sizeof(uid)) ||
        !Race_MapState_HexDecode(fields[1], display_name, RACE_PROFILE_NAME_MAX,
                                 &display_name_length) ||
        !Race_MapState_Decimal(fields[2], RACE_LEADERBOARD_MAX_TIME_MS, &elapsed) ||
        !elapsed ||
        !Race_MapState_Decimal(fields[3], RACE_MAX_CHECKPOINTS, &checkpoint_count) ||
        (format != RACE_MAP_STATE_FORMAT_V1 &&
         !Race_MapState_ParseReplayId(fields[4], &replay_id)) ||
        ((format == RACE_MAP_STATE_FORMAT_V3 ||
          format == RACE_MAP_STATE_FORMAT_V4) &&
         !Race_MapState_Decimal(fields[5], RACE_LEADERBOARD_MAX_DATE_UNIX_S,
                                &date_unix_s)) ||
        !Race_MapState_ParseSplits(fields[checkpoint_times_field],
                                   (size_t) checkpoint_count,
                                   (uint32_t) elapsed, splits) ||
        (format == RACE_MAP_STATE_FORMAT_V4 &&
         (!Race_MapState_Decimal(fields[7], RACE_MAX_CHECKPOINTS,
                                 &split_count) ||
          !Race_MapState_ParseHex64(fields[8], &split_layout,
                                    split_count == 0u) ||
          !Race_MapState_ParseSplits(fields[9], (size_t) split_count,
                                     (uint32_t) elapsed,
                                     analytical_splits)))) {
      return RACE_MAP_STATE_PARSE_MALFORMED;
    }
    display_name[display_name_length] = '\0';

    if (!Race_Leaderboard_RecordInit(records + row, uid, display_name,
                                     (uint32_t) elapsed, splits,
                                     (size_t) checkpoint_count) ||
        !Race_Leaderboard_RecordSetSplits(records + row, analytical_splits,
                                          (size_t) split_count,
                                          split_layout) ||
        (date_unix_s &&
         !Race_Leaderboard_RecordSetDate(records + row, date_unix_s)) ||
        (replay_id &&
         !Race_Leaderboard_RecordAttachReplay(records + row, replay_id)) ||
        (row && strcmp(records[row - 1].uid, records[row].uid) >= 0)) {
      return RACE_MAP_STATE_PARSE_MALFORMED;
    }
    state->record_count++;
  }

  const size_t crc_offset = reader.position;
  uint32_t expected_crc;
  if (!Race_MapState_NextLine(&reader, &line) ||
      !Race_MapState_SpanPrefix(line, "crc=", &value) ||
      !Race_MapState_ParseChecksum(value, &expected_crc) ||
      reader.position != reader.length) {
    return RACE_MAP_STATE_PARSE_MALFORMED;
  }

  if (Race_MapState_Crc32(data, crc_offset) != expected_crc) {
    return RACE_MAP_STATE_PARSE_CHECKSUM;
  }

  return Race_MapState_Valid(state)
    ? RACE_MAP_STATE_PARSE_OK
    : RACE_MAP_STATE_PARSE_MALFORMED;
}

const char *Race_MapState_ParseResultName(race_map_state_parse_result_t result) {
  switch (result) {
    case RACE_MAP_STATE_PARSE_OK:
      return "ok";
    case RACE_MAP_STATE_PARSE_MALFORMED:
      return "malformed";
    case RACE_MAP_STATE_PARSE_UNKNOWN_VERSION:
      return "unknown version";
    case RACE_MAP_STATE_PARSE_LEGACY_UNSUPPORTED:
      return "legacy unsupported";
    case RACE_MAP_STATE_PARSE_UNSUPPORTED_RULESET:
      return "unsupported ruleset";
    case RACE_MAP_STATE_PARSE_CHECKSUM:
      return "checksum mismatch";
    case RACE_MAP_STATE_PARSE_TOO_LARGE:
      return "too large";
    case RACE_MAP_STATE_PARSE_BOUNDS:
      return "bounds exceeded";
  }

  return "unknown";
}
