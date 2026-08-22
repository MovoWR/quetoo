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

size_t Race_ReplayRecording_ReservationBytes(void) {
  return RACE_REPLAY_MAX_FRAMES * sizeof(race_replay_sample_t) +
         RACE_REPLAY_MAX_PROJECTILE_EVENTS *
           sizeof(race_replay_projectile_event_t);
}

bool Race_ReplayRecording_Start(race_replay_recording_t *recording,
                                const char *map, const char *profile_uid,
                                const char *player_name, int32_t player_uid,
                                uint8_t physics_mode, uint32_t start_time) {
  if (!recording) {
    return false;
  }

  Race_ReplayRecording_Destroy(recording);
  race_replay_sample_t *samples = calloc(RACE_REPLAY_MAX_FRAMES,
                                          sizeof(*samples));
  race_replay_projectile_event_t *projectile_events = calloc(
    RACE_REPLAY_MAX_PROJECTILE_EVENTS, sizeof(*projectile_events));
  if (!samples || !projectile_events ||
      !Race_Replay_Init(&recording->replay, samples,
                        RACE_REPLAY_MAX_FRAMES, projectile_events,
                        RACE_REPLAY_MAX_PROJECTILE_EVENTS, map, profile_uid,
                        player_name, player_uid, physics_mode)) {
    free(projectile_events);
    free(samples);
    memset(recording, 0, sizeof(*recording));
    return false;
  }

  recording->start_time = start_time;
  recording->active = true;
  return true;
}

bool Race_ReplayRecording_Capture(race_replay_recording_t *recording,
                                  uint32_t sample_time,
                                  const race_replay_sample_t *sample) {
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

  if (recording->replay.sample_count == recording->replay.sample_capacity) {
    recording->invalid = true;
    return false;
  }
  recording->replay.samples[recording->replay.sample_count++] = captured;
  return true;
}

bool Race_ReplayRecording_CaptureProjectile(
    race_replay_recording_t *recording, const uint32_t event_time,
    const race_replay_projectile_event_t *event) {
  if (!recording || !recording->active || recording->invalid ||
      !Race_Replay_ProjectileEventValid(event)) {
    if (recording) {
      recording->invalid = true;
    }
    return false;
  }

  const uint32_t relative = event_time - recording->start_time;
  if (relative > RACE_REPLAY_MAX_TIME_MS ||
      recording->replay.projectile_event_count ==
        recording->replay.projectile_event_capacity ||
      (recording->replay.projectile_event_count &&
       relative < recording->replay.projectile_events[
         recording->replay.projectile_event_count - 1u].time_ms)) {
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
                                 const race_replay_sample_t *sample) {
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
