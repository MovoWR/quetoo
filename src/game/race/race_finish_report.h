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
#include <stdint.h>

#include "race_types.h"

#define RACE_FINISH_REPORT_VERSION 2u
#define RACE_FINISH_REPORT_MAX_TIME_MS 600000u
#define RACE_FINISH_REPORT_MAX_BYTES \
  (38u + RACE_MAX_CHECKPOINTS * sizeof(uint32_t))

typedef struct {
  race_mode_t mode;
  uint8_t invalid_flags;
  bool publication_committed;
  bool new_world_record;
  uint32_t elapsed_time;
  uint32_t previous_pb;
  uint32_t world_record;
  uint16_t checkpoint_count;
  uint32_t checkpoint_times[RACE_MAX_CHECKPOINTS];
  float start_speed;
  float end_speed;
  float top_speed;
  float average_speed;
} race_finish_report_t;

size_t Race_FinishReport_Encode(const race_finish_report_t *report,
                                void *output, size_t capacity);
bool Race_FinishReport_Decode(const void *input, size_t length,
                              race_finish_report_t *report);
