/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_finish_report.h"

#include <math.h>
#include <string.h>

#define RACE_FINISH_REPORT_HEADER_BYTES 38u
#define RACE_FINISH_REPORT_FLAG_NEW_WORLD_RECORD (1u << 0u)
#define RACE_FINISH_REPORT_FLAG_PUBLICATION_COMMITTED (1u << 1u)
#define RACE_FINISH_REPORT_FLAGS_MASK \
  (RACE_FINISH_REPORT_FLAG_NEW_WORLD_RECORD | \
   RACE_FINISH_REPORT_FLAG_PUBLICATION_COMMITTED)

static void Race_FinishReport_Write16(uint8_t *output, const uint16_t value) {
  output[0] = (uint8_t) value;
  output[1] = (uint8_t) (value >> 8u);
}

static uint16_t Race_FinishReport_Read16(const uint8_t *input) {
  return (uint16_t) input[0] | (uint16_t) input[1] << 8u;
}

static void Race_FinishReport_Write32(uint8_t *output, const uint32_t value) {
  output[0] = (uint8_t) value;
  output[1] = (uint8_t) (value >> 8u);
  output[2] = (uint8_t) (value >> 16u);
  output[3] = (uint8_t) (value >> 24u);
}

static uint32_t Race_FinishReport_Read32(const uint8_t *input) {
  return (uint32_t) input[0] | (uint32_t) input[1] << 8u |
         (uint32_t) input[2] << 16u | (uint32_t) input[3] << 24u;
}

static void Race_FinishReport_WriteFloat(uint8_t *output, const float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  Race_FinishReport_Write32(output, bits);
}

static float Race_FinishReport_ReadFloat(const uint8_t *input) {
  const uint32_t bits = Race_FinishReport_Read32(input);
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

static bool Race_FinishReport_Valid(const race_finish_report_t *report) {
  if (!report || report->mode < RACE_MODE_RACE ||
      report->mode >= RACE_MODE_TOTAL ||
      report->checkpoint_count > RACE_MAX_CHECKPOINTS ||
      !report->elapsed_time ||
      report->elapsed_time > RACE_FINISH_REPORT_MAX_TIME_MS ||
      report->previous_pb > RACE_FINISH_REPORT_MAX_TIME_MS ||
      report->world_record > RACE_FINISH_REPORT_MAX_TIME_MS ||
      !isfinite(report->start_speed) ||
      !isfinite(report->end_speed) || !isfinite(report->top_speed) ||
      !isfinite(report->average_speed) || report->start_speed < 0.f ||
      report->end_speed < 0.f || report->top_speed < 0.f ||
      report->average_speed < 0.f) {
    return false;
  }
  if (report->publication_committed &&
      (report->mode != RACE_MODE_RACE || report->invalid_flags ||
       !report->world_record ||
       (report->previous_pb && report->elapsed_time >= report->previous_pb) ||
       (!report->new_world_record &&
        report->elapsed_time < report->world_record))) {
    return false;
  }
  if (report->new_world_record &&
      (!report->publication_committed ||
       report->elapsed_time != report->world_record)) {
    return false;
  }
  uint32_t previous = 0u;
  for (size_t i = 0; i < report->checkpoint_count; i++) {
    const uint32_t split = report->checkpoint_times[i];
    if (!split || split < previous || split > report->elapsed_time) {
      return false;
    }
    previous = split;
  }
  return true;
}

size_t Race_FinishReport_Encode(const race_finish_report_t *report,
                                void *output, const size_t capacity) {
  if (!Race_FinishReport_Valid(report) || !output) {
    return 0u;
  }
  const size_t length = RACE_FINISH_REPORT_HEADER_BYTES +
                        report->checkpoint_count * sizeof(uint32_t);
  if (length > capacity || length > RACE_FINISH_REPORT_MAX_BYTES) {
    return 0u;
  }

  uint8_t *bytes = output;
  bytes[0] = RACE_FINISH_REPORT_VERSION;
  bytes[1] = (uint8_t) report->mode;
  bytes[2] = report->invalid_flags;
  bytes[3] = (report->new_world_record
                ? RACE_FINISH_REPORT_FLAG_NEW_WORLD_RECORD : 0u) |
             (report->publication_committed
                ? RACE_FINISH_REPORT_FLAG_PUBLICATION_COMMITTED : 0u);
  Race_FinishReport_Write32(bytes + 4u, report->elapsed_time);
  Race_FinishReport_Write32(bytes + 8u, report->previous_pb);
  Race_FinishReport_Write32(bytes + 12u, report->world_record);
  Race_FinishReport_Write16(bytes + 16u, report->checkpoint_count);
  Race_FinishReport_WriteFloat(bytes + 18u, report->start_speed);
  Race_FinishReport_WriteFloat(bytes + 22u, report->end_speed);
  Race_FinishReport_WriteFloat(bytes + 26u, report->top_speed);
  Race_FinishReport_WriteFloat(bytes + 30u, report->average_speed);
  Race_FinishReport_Write32(bytes + 34u, (uint32_t) length);
  for (size_t i = 0; i < report->checkpoint_count; i++) {
    Race_FinishReport_Write32(bytes + RACE_FINISH_REPORT_HEADER_BYTES + i * 4u,
                              report->checkpoint_times[i]);
  }
  return length;
}

bool Race_FinishReport_Decode(const void *input, const size_t length,
                              race_finish_report_t *report) {
  if (!input || !report || length < RACE_FINISH_REPORT_HEADER_BYTES ||
      length > RACE_FINISH_REPORT_MAX_BYTES) {
    return false;
  }
  const uint8_t *bytes = input;
  if (bytes[0] != RACE_FINISH_REPORT_VERSION ||
      (bytes[3] & ~RACE_FINISH_REPORT_FLAGS_MASK) ||
      Race_FinishReport_Read32(bytes + 34u) != length) {
    return false;
  }

  race_finish_report_t parsed = {
    .mode = (race_mode_t) bytes[1],
    .invalid_flags = bytes[2],
    .publication_committed =
      (bytes[3] & RACE_FINISH_REPORT_FLAG_PUBLICATION_COMMITTED) != 0u,
    .new_world_record =
      (bytes[3] & RACE_FINISH_REPORT_FLAG_NEW_WORLD_RECORD) != 0u,
    .elapsed_time = Race_FinishReport_Read32(bytes + 4u),
    .previous_pb = Race_FinishReport_Read32(bytes + 8u),
    .world_record = Race_FinishReport_Read32(bytes + 12u),
    .checkpoint_count = Race_FinishReport_Read16(bytes + 16u),
    .start_speed = Race_FinishReport_ReadFloat(bytes + 18u),
    .end_speed = Race_FinishReport_ReadFloat(bytes + 22u),
    .top_speed = Race_FinishReport_ReadFloat(bytes + 26u),
    .average_speed = Race_FinishReport_ReadFloat(bytes + 30u)
  };
  if (length != RACE_FINISH_REPORT_HEADER_BYTES +
                parsed.checkpoint_count * sizeof(uint32_t)) {
    return false;
  }
  for (size_t i = 0; i < parsed.checkpoint_count; i++) {
    parsed.checkpoint_times[i] = Race_FinishReport_Read32(
      bytes + RACE_FINISH_REPORT_HEADER_BYTES + i * 4u);
  }
  if (!Race_FinishReport_Valid(&parsed)) {
    return false;
  }
  *report = parsed;
  return true;
}
