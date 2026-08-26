/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_replay_record.h"

#include <stdlib.h>
#include <string.h>

#define RACE_REPLAY_INITIAL_SAMPLE_CAPACITY 64u
#define RACE_REPLAY_INITIAL_PROJECTILE_CAPACITY 16u

size_t Race_ReplayRecording_AllocationBytes(
    const race_replay_recording_t *recording) {
  return recording
    ? recording->replay.sample_capacity * sizeof(race_replay_sample_t) +
      recording->replay.projectile_event_capacity *
        sizeof(race_replay_projectile_event_t)
    : 0u;
}

static size_t Race_ReplayRecording_NextCapacity(
    const size_t current, const size_t required, const size_t maximum,
    const size_t initial) {
  size_t capacity = current ? current : initial;
  while (capacity < required && capacity < maximum) {
    capacity = capacity > maximum / 2u ? maximum : capacity * 2u;
  }
  return capacity;
}

static bool Race_ReplayRecording_GrowSamples(
    race_replay_recording_t *recording, const size_t required,
    const size_t allocation_limit) {
  if (required <= recording->replay.sample_capacity) {
    return true;
  }
  const size_t capacity = Race_ReplayRecording_NextCapacity(
    recording->replay.sample_capacity, required, RACE_REPLAY_MAX_FRAMES,
    RACE_REPLAY_INITIAL_SAMPLE_CAPACITY);
  const size_t projectile_bytes =
    recording->replay.projectile_event_capacity *
      sizeof(race_replay_projectile_event_t);
  const size_t sample_bytes = capacity * sizeof(race_replay_sample_t);
  if (capacity < required || sample_bytes > allocation_limit ||
      projectile_bytes > allocation_limit - sample_bytes) {
    return false;
  }

  const size_t previous = recording->replay.sample_capacity;
  race_replay_sample_t *samples = realloc(
    recording->replay.samples, sample_bytes);
  if (!samples) {
    return false;
  }
  memset(samples + previous, 0,
         (capacity - previous) * sizeof(*samples));
  recording->replay.samples = samples;
  recording->replay.sample_capacity = capacity;
  return true;
}

static bool Race_ReplayRecording_GrowProjectileEvents(
    race_replay_recording_t *recording, const size_t required,
    const size_t allocation_limit) {
  if (required <= recording->replay.projectile_event_capacity) {
    return true;
  }
  const size_t capacity = Race_ReplayRecording_NextCapacity(
    recording->replay.projectile_event_capacity, required,
    RACE_REPLAY_MAX_PROJECTILE_EVENTS,
    RACE_REPLAY_INITIAL_PROJECTILE_CAPACITY);
  const size_t sample_bytes = recording->replay.sample_capacity *
                              sizeof(race_replay_sample_t);
  const size_t projectile_bytes = capacity *
                                  sizeof(race_replay_projectile_event_t);
  if (capacity < required || sample_bytes > allocation_limit ||
      projectile_bytes > allocation_limit - sample_bytes) {
    return false;
  }

  const size_t previous = recording->replay.projectile_event_capacity;
  race_replay_projectile_event_t *events = realloc(
    recording->replay.projectile_events, projectile_bytes);
  if (!events) {
    return false;
  }
  memset(events + previous, 0,
         (capacity - previous) * sizeof(*events));
  recording->replay.projectile_events = events;
  recording->replay.projectile_event_capacity = capacity;
  return true;
}

bool Race_ReplayRecording_Start(race_replay_recording_t *recording,
                                const char *map, const char *profile_uid,
                                const char *player_name, int32_t player_uid,
                                uint8_t physics_mode, uint32_t start_time,
                                const size_t allocation_limit) {
  if (!recording) {
    return false;
  }

  Race_ReplayRecording_Destroy(recording);
  if (!Race_Replay_Init(&recording->replay, NULL, 0u, NULL, 0u,
                        map, profile_uid, player_name, player_uid,
                        physics_mode) ||
      !Race_ReplayRecording_GrowSamples(recording, 1u,
                                        allocation_limit)) {
    free(recording->replay.projectile_events);
    free(recording->replay.samples);
    memset(recording, 0, sizeof(*recording));
    return false;
  }

  recording->start_time = start_time;
  recording->active = true;
  return true;
}

bool Race_ReplayRecording_Capture(race_replay_recording_t *recording,
                                  uint32_t sample_time,
                                  const race_replay_sample_t *sample,
                                  const size_t allocation_limit) {
  if (!recording || !recording->active || recording->invalid ||
      !Race_Replay_SampleValid(sample)) {
    if (recording) {
      recording->invalid = true;
    }
    return false;
  }

  const uint32_t relative = sample_time - recording->start_time;
  if (relative > RACE_REPLAY_MAX_TIME_MS) {
    recording->invalid = true;
    return false;
  }

  race_replay_sample_t captured = *sample;
  captured.time_ms = relative;
  if (recording->replay.sample_count) {
    race_replay_sample_t *last = recording->replay.samples +
                                 recording->replay.sample_count - 1u;
    if (relative < last->time_ms || relative - last->time_ms > UINT16_MAX) {
      recording->invalid = true;
      return false;
    }
    captured.delta_time_ms = (uint16_t) (relative - last->time_ms);
    if (relative == last->time_ms) {
      captured.delta_time_ms = last->delta_time_ms;
      *last = captured;
      return true;
    }
  } else {
    if (relative > UINT16_MAX) {
      recording->invalid = true;
      return false;
    }
    captured.delta_time_ms = (uint16_t) relative;
  }

  if (!Race_ReplayRecording_GrowSamples(
        recording, recording->replay.sample_count + 1u,
        allocation_limit)) {
    recording->invalid = true;
    return false;
  }
  recording->replay.samples[recording->replay.sample_count++] = captured;
  return true;
}

bool Race_ReplayRecording_CaptureProjectile(
    race_replay_recording_t *recording, const uint32_t event_time,
    const race_replay_projectile_event_t *event,
    const size_t allocation_limit) {
  if (!recording || !recording->active || recording->invalid ||
      !Race_Replay_ProjectileEventValid(event)) {
    if (recording) {
      recording->invalid = true;
    }
    return false;
  }

  const uint32_t relative = event_time - recording->start_time;
  if (relative > RACE_REPLAY_MAX_TIME_MS ||
      (recording->replay.projectile_event_count &&
       relative < recording->replay.projectile_events[
         recording->replay.projectile_event_count - 1u].time_ms)) {
    recording->invalid = true;
    return false;
  }
  if (!Race_ReplayRecording_GrowProjectileEvents(
        recording, recording->replay.projectile_event_count + 1u,
        allocation_limit)) {
    recording->invalid = true;
    return false;
  }

  race_replay_projectile_event_t captured = *event;
  captured.time_ms = relative;
  recording->replay.projectile_events[
    recording->replay.projectile_event_count++] = captured;
  return true;
}

bool Race_ReplayRecording_Finish(race_replay_recording_t *recording,
                                 uint32_t finish_time, uint32_t elapsed_time,
                                 const race_replay_sample_t *sample,
                                 const size_t allocation_limit) {
  if (!recording || !recording->active || recording->invalid ||
      finish_time - recording->start_time != elapsed_time ||
      !elapsed_time || elapsed_time > RACE_REPLAY_MAX_TIME_MS ||
      !Race_Replay_SampleValid(sample) ||
      !recording->replay.sample_count) {
    if (recording) {
      recording->invalid = true;
    }
    return false;
  }

  race_replay_sample_t captured = *sample;
  captured.time_ms = elapsed_time;
  race_replay_sample_t *last = recording->replay.samples +
                               recording->replay.sample_count - 1u;
  if (last->time_ms < elapsed_time &&
      recording->replay.sample_count == recording->replay.sample_capacity &&
      recording->replay.sample_capacity < RACE_REPLAY_MAX_FRAMES) {
    Race_ReplayRecording_GrowSamples(
      recording, recording->replay.sample_count + 1u, allocation_limit);
    last = recording->replay.samples + recording->replay.sample_count - 1u;
  }
  if (last->time_ms == elapsed_time) {
    captured.delta_time_ms = last->delta_time_ms;
    *last = captured;
  } else if (last->time_ms < elapsed_time &&
             elapsed_time - last->time_ms <= UINT16_MAX &&
             recording->replay.sample_count <
               recording->replay.sample_capacity) {
    captured.delta_time_ms = (uint16_t) (elapsed_time - last->time_ms);
    recording->replay.samples[recording->replay.sample_count++] = captured;
  } else if (recording->replay.sample_count >= 2u) {
    const race_replay_sample_t *previous = last - 1u;
    if (previous->time_ms >= elapsed_time ||
        elapsed_time - previous->time_ms > UINT16_MAX) {
      recording->invalid = true;
      return false;
    }
    captured.delta_time_ms = (uint16_t) (elapsed_time - previous->time_ms);
    *last = captured;
  } else {
    recording->invalid = true;
    return false;
  }

  recording->replay.elapsed_time = elapsed_time;
  recording->active = false;
  return Race_Replay_Valid(&recording->replay);
}

void Race_ReplayRecording_Destroy(race_replay_recording_t *recording) {
  if (!recording) {
    return;
  }
  free(recording->replay.samples);
  free(recording->replay.projectile_events);
  memset(recording, 0, sizeof(*recording));
}
