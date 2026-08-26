/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "race_replay_format.h"

#define RACE_REPLAY_RECORDING_MEMORY_BYTES (128u * 1024u * 1024u)

typedef struct {
  race_replay_t replay;
  uint32_t start_time;
  bool active;
  bool invalid;
} race_replay_recording_t;

size_t Race_ReplayRecording_AllocationBytes(
  const race_replay_recording_t *recording);
bool Race_ReplayRecording_Start(race_replay_recording_t *recording,
                                const char *map, const char *profile_uid,
                                const char *player_name, int32_t player_uid,
                                uint8_t physics_mode, uint32_t start_time,
                                size_t allocation_limit);
bool Race_ReplayRecording_Capture(race_replay_recording_t *recording,
                                  uint32_t sample_time,
                                  const race_replay_sample_t *sample,
                                  size_t allocation_limit);
bool Race_ReplayRecording_CaptureProjectile(
  race_replay_recording_t *recording, uint32_t event_time,
  const race_replay_projectile_event_t *event, size_t allocation_limit);
bool Race_ReplayRecording_Finish(race_replay_recording_t *recording,
                                 uint32_t finish_time, uint32_t elapsed_time,
                                 const race_replay_sample_t *sample,
                                 size_t allocation_limit);
void Race_ReplayRecording_Destroy(race_replay_recording_t *recording);
