/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_replay_playback_service.h"

#include <stdlib.h>
#include <string.h>

#include "race.h"
#include "race_map_state_service.h"
#include "race_modes.h"
#include "race_persistence.h"
#include "race_replay_playback.h"
#include "race_replay_store.h"
#include "race_physics.h"
#include "race_vote_service.h"

#define RACE_REPLAY_PLAYBACK_MAX_SESSIONS 8u
#define RACE_REPLAY_STATE_SEND_MSEC RACE_REPLAY_TICK_MSEC

typedef struct {
  race_replay_sample_t *samples;
  race_replay_projectile_event_t *projectile_events;
  race_replay_t replay;
  race_leaderboard_record_t record;
  race_replay_clock_t clock;
  race_replay_source_t source;
  uint64_t connection_id;
  uint32_t generation;
  uint32_t state_sequence;
  uint32_t projectile_sequence;
  uint32_t raceline_sequence;
  uint32_t last_state_time;
  uint32_t inactive_since;
  uint32_t last_control_time;
  uint16_t raceline_next_point;
  uint16_t raceline_point_count;
  size_t projectile_cursor;
  race_mode_t return_mode;
  uint8_t rank;
  bool loaded;
  bool playback_active;
  bool raceline_active;
  bool raceline_begin_sent;
  bool raceline_end_sent;
  bool return_spectator;
  bool attack_released;
  bool control_time_initialized;
  bool inactive_time_initialized;
} race_replay_playback_session_t;

static race_replay_playback_session_t race_replay_sessions[MAX_CLIENTS];
static uint32_t race_replay_generations[MAX_CLIENTS];
static uint64_t race_replay_load_connection_ids[MAX_CLIENTS];
static uint32_t race_replay_load_times[MAX_CLIENTS];
static bool race_replay_load_time_initialized[MAX_CLIENTS];
static size_t race_replay_loaded_sessions;

static uint32_t Race_ReplayPlaybackService_Next(uint32_t *value) {
  (*value)++;
  if (!*value) {
    *value = 1u;
  }
  return *value;
}

static bool Race_ReplayPlaybackService_ClientSlot(
  const g_client_t *cl, int32_t *slot) {
  if (!cl || !cl->in_use || !slot || cl->ps.client >= MAX_CLIENTS) {
    return false;
  }
  *slot = cl->ps.client;
  return true;
}

bool Race_ReplayPlaybackService_ClientActive(const g_client_t *cl) {
  int32_t slot;
  return Race_ReplayPlaybackService_ClientSlot(cl, &slot) &&
         race_replay_sessions[slot].loaded &&
         race_replay_sessions[slot].playback_active &&
         race_replay_sessions[slot].connection_id ==
           cl->persistent.race_vote_connection_id;
}

static void Race_ReplayPlaybackService_Free(
  race_replay_playback_session_t *session) {
  if (!session) {
    return;
  }
  if (session->samples || session->projectile_events) {
    gi.Free(session->samples);
    gi.Free(session->projectile_events);
    if (race_replay_loaded_sessions) {
      race_replay_loaded_sessions--;
    }
  }
  memset(session, 0, sizeof(*session));
}

static void Race_ReplayPlaybackService_Write(
  const g_client_t *cl, const int32_t command,
  const void *payload, const size_t length, const bool reliable) {
  if (!cl || !cl->in_use || !payload || !length ||
      length > RACE_REPLAY_TRANSPORT_MAX_PAYLOAD) {
    return;
  }
  gi.WriteByte(command);
  gi.WriteByte((int32_t) length);
  gi.WriteData(payload, length);
  gi.Unicast(cl, reliable);
}

static bool Race_ReplayPlaybackService_SendProjectileMessage(
    g_client_t *cl, race_replay_playback_session_t *session,
    race_replay_projectile_message_t *message) {
  message->generation = session->generation;
  message->sequence = Race_ReplayPlaybackService_Next(
    &session->projectile_sequence);
  message->playhead_ms = session->clock.playhead_ms;
  uint8_t payload[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD];
  const size_t length = Race_ReplayProjectiles_Encode(
    message, payload, sizeof(payload));
  if (!length) {
    return false;
  }
  Race_ReplayPlaybackService_Write(
    cl, SV_CMD_RACE_REPLAY_PROJECTILES, payload, length, true);
  return true;
}

static bool Race_ReplayPlaybackService_SendProjectileReset(
    g_client_t *cl, race_replay_playback_session_t *session) {
  race_replay_projectile_message_t message = {
    .op = RACE_REPLAY_PROJECTILE_MESSAGE_RESET
  };
  return Race_ReplayPlaybackService_SendProjectileMessage(
    cl, session, &message);
}

static bool Race_ReplayPlaybackService_SendProjectileEvents(
    g_client_t *cl, race_replay_playback_session_t *session,
    const uint32_t previous_playhead, const uint32_t playhead,
    const bool include_time_zero) {
  while (session->projectile_cursor <
           session->replay.projectile_event_count) {
    const race_replay_projectile_event_t *event =
      session->replay.projectile_events + session->projectile_cursor;
    if (event->time_ms > playhead) {
      break;
    }
    if ((!include_time_zero && event->time_ms <= previous_playhead) ||
        (include_time_zero && event->time_ms < previous_playhead)) {
      session->projectile_cursor++;
      continue;
    }

    race_replay_projectile_message_t message = {
      .op = RACE_REPLAY_PROJECTILE_MESSAGE_EVENTS
    };
    while (session->projectile_cursor <
             session->replay.projectile_event_count &&
           message.event_count <
             RACE_REPLAY_PROJECTILE_MAX_MESSAGE_EVENTS) {
      event = session->replay.projectile_events +
              session->projectile_cursor;
      if (event->time_ms > playhead) {
        break;
      }
      if ((!include_time_zero && event->time_ms <= previous_playhead) ||
          (include_time_zero && event->time_ms < previous_playhead)) {
        session->projectile_cursor++;
        continue;
      }
      message.events[message.event_count++] = *event;
      session->projectile_cursor++;
    }
    if (message.event_count &&
        !Race_ReplayPlaybackService_SendProjectileMessage(
          cl, session, &message)) {
      return false;
    }
  }
  return true;
}

static bool Race_ReplayPlaybackService_SendProjectileSnapshot(
    g_client_t *cl, race_replay_playback_session_t *session) {
  if (!Race_ReplayPlaybackService_SendProjectileReset(cl, session)) {
    return false;
  }

  race_replay_projectile_event_t
    active[RACE_REPLAY_MAX_ACTIVE_PROJECTILES];
  size_t active_count = 0u;
  session->projectile_cursor = 0u;
  while (session->projectile_cursor <
           session->replay.projectile_event_count &&
         session->replay.projectile_events[
           session->projectile_cursor].time_ms <= session->clock.playhead_ms) {
    const race_replay_projectile_event_t *event =
      session->replay.projectile_events + session->projectile_cursor++;
    size_t active_index = active_count;
    for (size_t i = 0u; i < active_count; i++) {
      if (active[i].id == event->id) {
        active_index = i;
        break;
      }
    }
    if (event->operation == RACE_REPLAY_PROJECTILE_SPAWN) {
      if (active_index != active_count ||
          active_count == RACE_REPLAY_MAX_ACTIVE_PROJECTILES) {
        return false;
      }
      active[active_count++] = *event;
    } else {
      if (active_index == active_count ||
          active[active_index].kind != event->kind) {
        return false;
      }
      active[active_index] = active[--active_count];
    }
  }

  size_t first = 0u;
  while (first < active_count) {
    race_replay_projectile_message_t message = {
      .op = RACE_REPLAY_PROJECTILE_MESSAGE_SNAPSHOT,
      .event_count = (uint8_t) Minz(
        active_count - first, RACE_REPLAY_PROJECTILE_MAX_MESSAGE_EVENTS)
    };
    memcpy(message.events, active + first,
           message.event_count * sizeof(*message.events));
    if (!Race_ReplayPlaybackService_SendProjectileMessage(
          cl, session, &message)) {
      return false;
    }
    first += message.event_count;
  }
  return true;
}

static bool Race_ReplayPlaybackService_SendTelemetry(
    g_client_t *cl, const race_replay_playback_session_t *session,
    const uint32_t sequence, const size_t cursor,
    const bool reliable) {
  if (cursor >= session->replay.sample_count || cursor > UINT32_MAX) {
    return false;
  }
  const race_replay_sample_t *sample = session->replay.samples + cursor;
  const race_replay_telemetry_message_t message = {
    .generation = session->generation,
    .sequence = sequence,
    .playhead_ms = session->clock.playhead_ms,
    .frame_cursor = (uint32_t) cursor,
    .pm_type = sample->pm_state.type,
    .pm_flags = sample->pm_state.flags,
    .origin = sample->pm_state.origin,
    .velocity = sample->pm_state.velocity,
    .input_flags = sample->stats[STAT_RACE_INPUT],
    .strafe_helper = sample->strafe_helper
  };
  uint8_t payload[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD];
  const size_t length = Race_ReplayTelemetry_Encode(
    &message, payload, sizeof(payload));
  if (!length) {
    return false;
  }
  Race_ReplayPlaybackService_Write(
    cl, SV_CMD_RACE_REPLAY_TELEMETRY, payload, length, reliable);
  return true;
}

static bool Race_ReplayPlaybackService_SendState(
  g_client_t *cl, race_replay_playback_session_t *session,
  const bool active, const bool discontinuity, const bool reliable) {
  race_replay_state_message_t message = {
    .speed = RACE_REPLAY_SPEED_NORMAL,
    .source = RACE_REPLAY_SOURCE_NONE,
    .generation = session->generation,
    .sequence = Race_ReplayPlaybackService_Next(&session->state_sequence)
  };
  size_t telemetry_cursor = 0u;
  if (active) {
    message.flags = RACE_REPLAY_STATE_ACTIVE;
    if (session->clock.paused) {
      message.flags |= RACE_REPLAY_STATE_PAUSED;
    }
    if (session->clock.completed) {
      message.flags |= RACE_REPLAY_STATE_COMPLETED;
    }
    if (discontinuity) {
      message.flags |= RACE_REPLAY_STATE_DISCONTINUITY;
    }
    message.speed = session->clock.speed;
    message.source = session->source;
    message.replay_id = session->replay.replay_id;
    message.duration_ms = session->replay.elapsed_time;
    message.playhead_ms = session->clock.playhead_ms;
    message.rank = session->rank;
    q_strlcpy(message.display_name, session->record.display_name,
              sizeof(message.display_name));
    message.sample_count = Race_ReplayPlayback_Window(
      &session->replay, session->clock.playhead_ms,
      message.samples, lengthof(message.samples), &telemetry_cursor);
    if (!message.sample_count) {
      return false;
    }
  }
  uint8_t payload[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD];
  const size_t length = Race_ReplayState_Encode(
    &message, payload, sizeof(payload));
  if (!length) {
    return false;
  }
  Race_ReplayPlaybackService_Write(
    cl, SV_CMD_RACE_REPLAY_STATE, payload, length, reliable);
  if (active && !Race_ReplayPlaybackService_SendTelemetry(
        cl, session, message.sequence, telemetry_cursor, reliable)) {
    return false;
  }
  session->last_state_time = g_level.time;
  return true;
}

static bool Race_ReplayPlaybackService_SendRaceline(
  g_client_t *cl, race_replay_playback_session_t *session,
  race_raceline_message_t *message, const bool reliable) {
  message->source = session->source;
  message->rank = session->rank;
  message->generation = session->generation;
  message->sequence = Race_ReplayPlaybackService_Next(
    &session->raceline_sequence);
  message->replay_id = session->replay.replay_id;
  message->total_points = session->raceline_point_count;
  message->duration_ms = session->replay.elapsed_time;
  uint8_t payload[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD];
  const size_t length = Race_Raceline_Encode(
    message, payload, sizeof(payload));
  if (!length) {
    return false;
  }
  Race_ReplayPlaybackService_Write(
    cl, SV_CMD_RACE_RACELINE, payload, length, reliable);
  return true;
}

static void Race_ReplayPlaybackService_SendRacelineClear(
  g_client_t *cl, race_replay_playback_session_t *session) {
  race_raceline_message_t message = {
    .op = RACE_RACELINE_MESSAGE_CLEAR,
    .generation = session->generation,
    .sequence = Race_ReplayPlaybackService_Next(
      &session->raceline_sequence)
  };
  uint8_t payload[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD];
  const size_t length = Race_Raceline_Encode(
    &message, payload, sizeof(payload));
  if (length) {
    Race_ReplayPlaybackService_Write(
      cl, SV_CMD_RACE_RACELINE, payload, length, true);
  }
}

static void Race_ReplayPlaybackService_StopSlot(
  g_client_t *cl, const int32_t slot, const bool notify) {
  race_replay_playback_session_t *session = race_replay_sessions + slot;
  if (!session->loaded) {
    return;
  }
  if (notify && cl && cl->in_use) {
    if (session->playback_active) {
      Race_ReplayPlaybackService_SendProjectileReset(cl, session);
      Race_ReplayPlaybackService_SendState(cl, session, false, false, true);
    }
    if (session->raceline_active) {
      Race_ReplayPlaybackService_SendRacelineClear(cl, session);
    }
  }
  Race_ReplayPlaybackService_Free(session);
}

static bool Race_ReplayPlaybackService_ExitSlot(
  g_client_t *cl, const int32_t slot) {
  race_replay_playback_session_t *session = race_replay_sessions + slot;
  if (!cl || !cl->in_use || !session->loaded ||
      !session->playback_active) {
    return false;
  }

  const race_mode_t return_mode = session->return_mode;
  const bool return_spectator = session->return_spectator;
  Race_ReplayPlaybackService_StopSlot(cl, slot, true);

  cl->persistent.race_mode = return_mode;
  cl->persistent.spectator = return_spectator;
  cl->ps.stats[STAT_RACE_MODE] = (int16_t) return_mode;
  cl->old_buttons = 0u;
  cl->buttons = 0u;
  cl->latched_buttons = 0u;
  cl->cmd.buttons = 0u;
  Race_Reset(cl);
  G_ClientRespawn(cl, false);
  G_Debug("client=%s replay action=exit mode=%s spectator=%d\n",
          cl->persistent.net_name, Race_ModeName(return_mode),
          return_spectator);
  return true;
}

bool Race_ReplayPlaybackService_ExitClient(g_client_t *cl) {
  int32_t slot;
  return Race_ReplayPlaybackService_ClientSlot(cl, &slot) &&
         Race_ReplayPlaybackService_ExitSlot(cl, slot);
}

bool Race_ReplayPlaybackService_ClientInput(g_client_t *cl,
                                            const pm_cmd_t *cmd) {
  int32_t slot;
  if (!cmd || !Race_ReplayPlaybackService_ClientSlot(cl, &slot)) {
    return false;
  }
  race_replay_playback_session_t *session = race_replay_sessions + slot;
  if (!session->loaded || !session->playback_active ||
      session->connection_id != cl->persistent.race_vote_connection_id) {
    return false;
  }
  if (!Race_ReplayPlayback_AttackExit(
        &session->attack_released,
        (cmd->buttons & BUTTON_ATTACK) != 0u)) {
    return false;
  }
  G_Debug("client=%s replay action=attack-exit\n",
          cl->persistent.net_name);
  return Race_ReplayPlaybackService_ExitSlot(cl, slot);
}

static void Race_ReplayPlaybackService_StopAll(void) {
  for (int32_t slot = 0; slot < MAX_CLIENTS; slot++) {
    Race_ReplayPlaybackService_StopSlot(NULL, slot, false);
  }
}

void Race_ReplayPlaybackService_Init(void) {
  Race_ReplayPlaybackService_StopAll();
  memset(race_replay_generations, 0, sizeof(race_replay_generations));
  memset(race_replay_load_connection_ids, 0,
         sizeof(race_replay_load_connection_ids));
  memset(race_replay_load_times, 0, sizeof(race_replay_load_times));
  memset(race_replay_load_time_initialized, 0,
         sizeof(race_replay_load_time_initialized));
}

void Race_ReplayPlaybackService_Shutdown(void) {
  Race_ReplayPlaybackService_StopAll();
}

void Race_ReplayPlaybackService_ConfigureLevel(void) {
  if (race_replay_loaded_sessions) {
    G_Debug("replay action=map-clear sessions=%zu\n",
            race_replay_loaded_sessions);
  }
  Race_ReplayPlaybackService_StopAll();
  memset(race_replay_load_connection_ids, 0,
         sizeof(race_replay_load_connection_ids));
  memset(race_replay_load_time_initialized, 0,
         sizeof(race_replay_load_time_initialized));
}

static bool Race_ReplayPlaybackService_RecordMatches(
  const race_leaderboard_record_t *record, const race_replay_t *replay) {
  int32_t player_uid;
  return record && replay &&
         Race_Replay_ProfilePlayerUid(record->uid, &player_uid) &&
         record->replay_id == replay->replay_id &&
         record->elapsed_time == replay->elapsed_time &&
         player_uid == replay->player_uid;
}

static bool Race_ReplayPlaybackService_Select(
  g_client_t *cl, const char *selector, race_leaderboard_record_t *record,
  race_replay_source_t *source, size_t *rank) {
  if (!selector || !*selector || q_strcmp(selector, "pb") == 0) {
    if (!cl->persistent.race_profile.ready ||
        !Race_MapStateService_ReplayForUid(
          cl->persistent.race_profile.uid, record, rank)) {
      G_Debug("client=%s replay selection=pb result=unavailable\n",
              cl->persistent.net_name);
      gi.ClientPrint(cl, PRINT_HIGH,
                     "No personal-best replay is available.\n");
      return false;
    }
    *source = RACE_REPLAY_SOURCE_PERSONAL_BEST;
    return true;
  }
  if (q_strcmp(selector, "wr") == 0) {
    if (!Race_MapStateService_ReplayForRank(1u, record, rank)) {
      G_Debug("client=%s replay selection=wr result=unavailable\n",
              cl->persistent.net_name);
      gi.ClientPrint(cl, PRINT_HIGH,
                     "No world-record replay is available.\n");
      return false;
    }
    *source = RACE_REPLAY_SOURCE_WORLD_RECORD;
    return true;
  }

  const size_t selector_length = q_strlen(selector);
  if (selector_length <= 2u) {
    size_t selected_rank = 0u;
    bool numeric_rank = true;
    for (size_t i = 0u; i < selector_length; i++) {
      if (selector[i] < '0' || selector[i] > '9') {
        numeric_rank = false;
        break;
      }
      selected_rank = selected_rank * 10u + (size_t) (selector[i] - '0');
    }
    if (numeric_rank) {
      if (!selected_rank || selected_rank > RACE_LEADERBOARD_TOP_MAX ||
          !Race_MapStateService_ReplayForRank(selected_rank, record, rank)) {
        G_Debug("client=%s replay selection=rank rank=%zu result=unavailable\n",
                cl->persistent.net_name, selected_rank);
        gi.ClientPrint(cl, PRINT_HIGH,
                       "No replay is available for rank %zu.\n", selected_rank);
        return false;
      }
      *source = selected_rank == 1u
        ? RACE_REPLAY_SOURCE_WORLD_RECORD : RACE_REPLAY_SOURCE_ID;
      return true;
    }
  }

  G_Debug("client=%s replay selection=unknown\n",
          cl->persistent.net_name);
  gi.ClientPrint(cl, PRINT_HIGH,
                 "Unknown replay selection. Use pb, wr, or rank 1-15.\n");
  return false;
}

static bool Race_ReplayPlaybackService_Start(
  g_client_t *cl, const char *selector, const bool playback,
  const bool start_race) {
  int32_t slot;
  race_leaderboard_record_t record;
  race_replay_source_t source;
  size_t rank;
  const uint64_t connection_id = Race_VoteService_ConnectionId(cl);
  if (!Race_ReplayPlaybackService_ClientSlot(cl, &slot) || !connection_id ||
      !Race_ReplayPlaybackService_Select(cl, selector, &record,
                                         &source, &rank)) {
    return false;
  }

  if (race_replay_load_connection_ids[slot] != connection_id) {
    race_replay_load_connection_ids[slot] = connection_id;
    race_replay_load_time_initialized[slot] = false;
  }
  if (!Race_ReplayPlayback_LoadAllowed(
        race_replay_load_times[slot],
        race_replay_load_time_initialized[slot], g_level.time)) {
    G_Debug("client=%s replay action=load-cooldown\n",
            cl->persistent.net_name);
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Replay selection is cooling down.\n");
    return false;
  }
  race_replay_load_times[slot] = g_level.time;
  race_replay_load_time_initialized[slot] = true;

  if (race_replay_sessions[slot].loaded) {
    if (!playback && race_replay_sessions[slot].playback_active) {
      Race_ReplayPlaybackService_ExitSlot(cl, slot);
    } else {
      Race_ReplayPlaybackService_StopSlot(cl, slot, true);
    }
  }
  if (race_replay_loaded_sessions >= RACE_REPLAY_PLAYBACK_MAX_SESSIONS) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Replay playback is at its bounded session limit.\n");
    return false;
  }

  race_replay_sample_t *samples = gi.Malloc(
    RACE_REPLAY_MAX_SAMPLES * sizeof(*samples), MEM_TAG_GAME);
  race_replay_projectile_event_t *projectile_events = gi.Malloc(
    RACE_REPLAY_MAX_PROJECTILE_EVENTS * sizeof(*projectile_events),
    MEM_TAG_GAME);
  if (!samples || !projectile_events) {
    gi.Free(projectile_events);
    gi.Free(samples);
    gi.ClientPrint(cl, PRINT_HIGH, "Replay allocation failed.\n");
    return false;
  }
  char committed_virtual[MAX_OS_PATH];
  char candidate_virtual[MAX_OS_PATH];
  const char *ruleset = Race_Physics_ConfigRuleset(Race_Physics_Current());
  if (!Race_Replay_Paths(ruleset, g_level.name, record.replay_id,
                         committed_virtual, sizeof(committed_virtual),
                         candidate_virtual, sizeof(candidate_virtual))) {
    gi.Free(projectile_events);
    gi.Free(samples);
    return false;
  }
  char committed[MAX_OS_PATH];
  if (!Race_Persistence_CopyRealPath(committed_virtual,
                                     gi.RealPath(committed_virtual),
                                     committed, sizeof(committed))) {
    gi.Free(projectile_events);
    gi.Free(samples);
    gi.ClientPrint(cl, PRINT_HIGH, "Replay path resolution failed.\n");
    return false;
  }
  race_replay_t replay;
  race_replay_parse_result_t parse_result = RACE_REPLAY_PARSE_OK;
  const race_replay_store_result_t loaded = Race_ReplayStore_Load(
    committed, g_level.name, record.replay_id,
    samples, RACE_REPLAY_MAX_SAMPLES,
    projectile_events, RACE_REPLAY_MAX_PROJECTILE_EVENTS,
    &replay, &parse_result);
  if (loaded != RACE_REPLAY_STORE_OK ||
      !Race_ReplayPlaybackService_RecordMatches(&record, &replay)) {
    gi.Free(projectile_events);
    gi.Free(samples);
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Replay is unavailable or failed QRPL v1 validation.\n");
    G_Warn("Replay playback rejected id=%016llx result=%s parse=%s\n",
           (unsigned long long) record.replay_id,
           Race_ReplayStore_ResultName(loaded),
           Race_Replay_ParseResultName(parse_result));
    return false;
  }

  if (playback) {
    Race_Reset(cl);
  }

  race_replay_playback_session_t *session = race_replay_sessions + slot;
  *session = (race_replay_playback_session_t) {
    .samples = samples,
    .projectile_events = projectile_events,
    .replay = replay,
    .record = record,
    .source = source,
    .connection_id = connection_id,
    .generation = Race_ReplayPlaybackService_Next(
      race_replay_generations + slot),
    .return_mode = Race_Mode(cl),
    .rank = (uint8_t) (rank < 15u ? rank : 15u),
    .loaded = true,
    .playback_active = playback,
    .raceline_active = !playback,
    .return_spectator = cl->persistent.spectator,
    .attack_released = (cl->buttons & BUTTON_ATTACK) == 0u,
    .raceline_point_count = (uint16_t) Race_ReplayRaceline_PointCount(
      replay.sample_count)
  };
  race_replay_loaded_sessions++;
  Race_ReplayClock_Init(&session->clock, g_level.time);
  if (playback &&
      (!Race_ReplayPlaybackService_SendProjectileReset(cl, session) ||
       !Race_ReplayPlaybackService_SendProjectileEvents(
         cl, session, 0u, 0u, true))) {
    Race_ReplayPlaybackService_Free(session);
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Replay projectile transport initialization failed.\n");
    return false;
  }
  if (!Race_ReplayPlaybackService_SendState(
        cl, session, playback, false, true)) {
    Race_ReplayPlaybackService_Free(session);
    gi.ClientPrint(cl, PRINT_HIGH, "Replay transport initialization failed.\n");
    return false;
  }
  if (!playback) {
    race_raceline_message_t begin = {
      .op = RACE_RACELINE_MESSAGE_BEGIN
    };
    if (!Race_ReplayPlaybackService_SendRaceline(
          cl, session, &begin, true)) {
      Race_ReplayPlaybackService_StopSlot(cl, slot, true);
      gi.ClientPrint(
        cl, PRINT_HIGH, "Raceline transport initialization failed.\n");
      return false;
    }
    session->raceline_begin_sent = true;
    if (start_race) {
      Race_Start(cl);
    }
  }
  G_Debug("client=%s replay=%016llx source=%u generation=%u action=%s start_race=%d duration=%u points=%u\n",
          cl->persistent.net_name,
          (unsigned long long) replay.replay_id, (unsigned) source,
          session->generation,
          playback ? "playback-start" : "raceline-start",
          start_race, replay.elapsed_time, session->raceline_point_count);
  gi.ClientPrint(cl, PRINT_HIGH,
                 playback
                   ? "Replaying %s^7 - %u:%02u.%03u\n"
                   : start_race
                     ? "Racing %s^7 - %u:%02u.%03u\n"
                     : "Raceline: %s^7 - %u:%02u.%03u\n",
                 record.display_name,
                 replay.elapsed_time / 60000u,
                 replay.elapsed_time / 1000u % 60u,
                 replay.elapsed_time % 1000u);
  return true;
}

static void Race_ReplayPlaybackService_SendRacelineFrame(
  g_client_t *cl, race_replay_playback_session_t *session) {
  if (!session->raceline_active || !session->raceline_begin_sent ||
      session->raceline_end_sent) {
    return;
  }
  if (session->raceline_next_point < session->raceline_point_count) {
    race_raceline_message_t chunk = {
      .op = RACE_RACELINE_MESSAGE_CHUNK,
      .first_point = session->raceline_next_point,
      .point_count = (uint8_t) (
        (size_t) session->raceline_point_count -
          session->raceline_next_point <
            RACE_REPLAY_RACELINE_CHUNK_MAX_POINTS
          ? (size_t) session->raceline_point_count -
              session->raceline_next_point
          : RACE_REPLAY_RACELINE_CHUNK_MAX_POINTS)
    };
    for (size_t i = 0u; i < chunk.point_count; i++) {
      if (!Race_ReplayRaceline_Point(
            &session->replay, session->raceline_point_count,
            session->raceline_next_point + i, chunk.points + i)) {
        Race_ReplayPlaybackService_SendRacelineClear(cl, session);
        session->raceline_active = false;
        return;
      }
    }
    if (!Race_ReplayPlaybackService_SendRaceline(
          cl, session, &chunk, true)) {
      Race_ReplayPlaybackService_SendRacelineClear(cl, session);
      session->raceline_active = false;
      return;
    }
    session->raceline_next_point += chunk.point_count;
    return;
  }
  race_raceline_message_t end = {
    .op = RACE_RACELINE_MESSAGE_END,
    .first_point = session->raceline_point_count
  };
  if (Race_ReplayPlaybackService_SendRaceline(cl, session, &end, true)) {
    session->raceline_end_sent = true;
  } else {
    Race_ReplayPlaybackService_SendRacelineClear(cl, session);
    session->raceline_active = false;
  }
}

static bool Race_ReplayPlaybackService_TimedOut(
  const race_replay_playback_session_t *session) {
  if (!session->clock.paused || !session->inactive_time_initialized) {
    return false;
  }
  return g_level.time - session->inactive_since >=
         RACE_REPLAY_PAUSE_TIMEOUT_MSEC;
}

void Race_ReplayPlaybackService_Frame(void) {
  const int32_t clients = Mini(sv_max_clients->integer, MAX_CLIENTS);
  for (int32_t slot = 0; slot < clients; slot++) {
    race_replay_playback_session_t *session = race_replay_sessions + slot;
    if (!session->loaded) {
      continue;
    }
    g_client_t *cl = ge.clients[slot];
    if (!cl || !cl->in_use ||
        !cl->persistent.race_vote_connection_id ||
        cl->persistent.race_vote_connection_id != session->connection_id) {
      G_Debug("replay=%016llx slot=%d action=connection-clear\n",
              (unsigned long long) session->replay.replay_id, slot);
      Race_ReplayPlaybackService_StopSlot(NULL, slot, false);
      continue;
    }
    if (session->playback_active) {
      const uint32_t previous_playhead = session->clock.playhead_ms;
      const race_replay_advance_t advanced = Race_ReplayClock_Advance(
        &session->clock, g_level.time, session->replay.elapsed_time);
      if (advanced != RACE_REPLAY_ADVANCE_NONE &&
          !Race_ReplayPlaybackService_SendProjectileEvents(
            cl, session, previous_playhead,
            session->clock.playhead_ms, false)) {
        G_Warn("client=%s replay=%016llx action=projectile-transport-failed\n",
               cl->persistent.net_name,
               (unsigned long long) session->replay.replay_id);
        Race_ReplayPlaybackService_ExitSlot(cl, slot);
        continue;
      }
      if (advanced == RACE_REPLAY_ADVANCE_COMPLETED ||
          session->clock.completed) {
        G_Debug("client=%s replay=%016llx action=completed playhead=%u\n",
                cl->persistent.net_name,
                (unsigned long long) session->replay.replay_id,
                session->clock.playhead_ms);
        Race_ReplayPlaybackService_ExitSlot(cl, slot);
        continue;
      } else if (!session->clock.paused &&
                 g_level.time - session->last_state_time >=
                   RACE_REPLAY_STATE_SEND_MSEC) {
        Race_ReplayPlaybackService_SendState(
          cl, session, true, false, false);
      }
      if (Race_ReplayPlaybackService_TimedOut(session)) {
        Race_ReplayPlaybackService_ExitSlot(cl, slot);
        continue;
      }
    }
    Race_ReplayPlaybackService_SendRacelineFrame(cl, session);
  }
}

void Race_ReplayPlaybackService_FinalizeClientFrames(void) {
  const int32_t clients = Mini(sv_max_clients->integer, MAX_CLIENTS);
  for (int32_t slot = 0; slot < clients; slot++) {
    race_replay_playback_session_t *session = race_replay_sessions + slot;
    if (!session->loaded || !session->playback_active) {
      continue;
    }

    g_client_t *cl = ge.clients[slot];
    if (!cl || !cl->in_use ||
        cl->persistent.race_vote_connection_id != session->connection_id) {
      continue;
    }

    race_replay_sample_t sample;
    if (!Race_ReplayPlayback_Sample(
          &session->replay, session->clock.playhead_ms, &sample, NULL)) {
      G_Warn("client=%s replay=%016llx action=viewer-state-failed\n",
             cl->persistent.net_name,
             (unsigned long long) session->replay.replay_id);
      Race_ReplayPlaybackService_ExitSlot(cl, slot);
      continue;
    }

    cl->chase_target = NULL;
    cl->old_chase_target = NULL;
    const int16_t voteFlags = cl->ps.stats[STAT_RACE_VOTE_FLAGS];
    Race_ReplayPlayback_ApplyViewerState(
      &cl->ps, &sample, STAT_RACE_ADMIN_CAPABILITIES);
    cl->ps.stats[STAT_RACE_VOTE_FLAGS] = voteFlags;
    cl->angles = sample.pm_state.view_angles;
    Vec3_Vectors(cl->angles, &cl->forward, &cl->right, &cl->up);
  }
}

void Race_ReplayPlaybackService_ClientRunStarted(g_client_t *cl) {
  int32_t slot;
  if (Race_ReplayPlaybackService_ClientSlot(cl, &slot) &&
      race_replay_sessions[slot].loaded) {
    Race_ReplayPlaybackService_StopSlot(cl, slot, true);
  }
}

static race_replay_speed_t Race_ReplayPlaybackService_Speed(
  const char *value) {
  if (value && (!q_strcmp(value, "0.25") || !q_strcmp(value, "0.25x"))) {
    return RACE_REPLAY_SPEED_QUARTER;
  }
  if (value && (!q_strcmp(value, "0.5") || !q_strcmp(value, "0.5x"))) {
    return RACE_REPLAY_SPEED_HALF;
  }
  if (value && (!q_strcmp(value, "1") || !q_strcmp(value, "1x"))) {
    return RACE_REPLAY_SPEED_NORMAL;
  }
  if (value && (!q_strcmp(value, "2") || !q_strcmp(value, "2x"))) {
    return RACE_REPLAY_SPEED_DOUBLE;
  }
  if (value && (!q_strcmp(value, "4") || !q_strcmp(value, "4x"))) {
    return RACE_REPLAY_SPEED_QUADRUPLE;
  }
  return RACE_REPLAY_SPEED_TOTAL;
}

static void Race_ReplayPlaybackService_Control(g_client_t *cl,
                                                const char *control) {
  int32_t slot;
  if (!Race_ReplayPlaybackService_ClientSlot(cl, &slot) ||
      !race_replay_sessions[slot].loaded ||
      !race_replay_sessions[slot].playback_active) {
    gi.ClientPrint(cl, PRINT_HIGH, "No replay is active.\n");
    return;
  }
  race_replay_playback_session_t *session = race_replay_sessions + slot;
  if (session->control_time_initialized &&
      g_level.time - session->last_control_time <
        RACE_REPLAY_CONTROL_COOLDOWN_MSEC) {
    gi.ClientPrint(cl, PRINT_HIGH, "Replay controls are cooling down.\n");
    return;
  }
  const uint32_t previous_playhead = session->clock.playhead_ms;
  const race_replay_advance_t advanced = Race_ReplayClock_Advance(
    &session->clock, g_level.time, session->replay.elapsed_time);
  if (advanced != RACE_REPLAY_ADVANCE_NONE &&
      !Race_ReplayPlaybackService_SendProjectileEvents(
        cl, session, previous_playhead, session->clock.playhead_ms, false)) {
    Race_ReplayPlaybackService_ExitSlot(cl, slot);
    gi.ClientPrint(cl, PRINT_HIGH, "Replay projectile transport failed.\n");
    return;
  }
  bool changed = false;
  bool discontinuity = false;
  const char *result = "Replay control is already at its limit.\n";
  if (!q_strcmp(control, "pause")) {
    const bool pause = !session->clock.paused;
    changed = Race_ReplayClock_SetPaused(
      &session->clock, pause, g_level.time);
    result = pause ? "Replay paused.\n" : "Replay resumed.\n";
  } else if (!q_strcmp(control, "resume")) {
    changed = Race_ReplayClock_SetPaused(&session->clock, false, g_level.time);
    result = "Replay resumed.\n";
  } else if (!q_strcmp(control, "restart")) {
    changed = Race_ReplayClock_Restart(&session->clock, g_level.time);
    discontinuity = true;
    result = "Replay restarted.\n";
  } else if (!q_strcmp(control, "back") ||
             !q_strcmp(control, "forward")) {
    const int32_t delta = !q_strcmp(control, "back")
      ? -(int32_t) RACE_REPLAY_SEEK_MSEC
      : (int32_t) RACE_REPLAY_SEEK_MSEC;
    changed = Race_ReplayClock_Seek(
      &session->clock,
      Race_ReplayClock_OffsetTarget(session->clock.playhead_ms, delta,
                                    session->replay.elapsed_time),
      session->replay.elapsed_time, g_level.time);
    discontinuity = true;
    result = delta < 0
      ? "Replay moved back five seconds and paused.\n"
      : "Replay moved forward five seconds and paused.\n";
  } else if (!q_strcmp(control, "step") ||
             !q_strcmp(control, "step_forward") ||
             !q_strcmp(control, "step_back")) {
    const int32_t direction = !q_strcmp(control, "step_back") ? -1 : 1;
    uint32_t target;
    if (Race_ReplayPlayback_StepTarget(
          &session->replay, session->clock.playhead_ms,
          direction, &target)) {
      changed = Race_ReplayClock_Seek(
        &session->clock, target, session->replay.elapsed_time, g_level.time);
    }
    discontinuity = true;
    result = direction < 0
      ? "Replay stepped back one sample and paused.\n"
      : "Replay stepped forward one sample and paused.\n";
  } else if (!q_strcmp(control, "speed")) {
    const int32_t speed_argument = !q_strcmp(gi.Argv(0), "replay") ? 2 : 3;
    const race_replay_speed_t speed = Race_ReplayPlaybackService_Speed(
      gi.Argc() > speed_argument ? gi.Argv(speed_argument) : NULL);
    if (speed == RACE_REPLAY_SPEED_TOTAL) {
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Usage: race replay speed <0.25|0.5|1|2|4>\n");
      return;
    }
    changed = Race_ReplayClock_SetSpeed(&session->clock, speed, g_level.time);
    result = "Replay speed changed.\n";
  } else if (!q_strcmp(control, "slower") ||
             !q_strcmp(control, "faster")) {
    changed = Race_ReplayClock_ShiftSpeed(
      &session->clock, !q_strcmp(control, "slower") ? -1 : 1,
      g_level.time);
    result = "Replay speed changed.\n";
  } else {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Usage: race replay <pb|wr|1-15|stop|pause|resume|restart|step|step_back|step_forward|back|forward|slower|faster|speed>\n");
    return;
  }
  if (!changed) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Replay control is already at its limit.\n");
    return;
  }
  session->control_time_initialized = true;
  session->last_control_time = g_level.time;
  session->inactive_since = session->clock.paused ? g_level.time : 0u;
  session->inactive_time_initialized = session->clock.paused;
  if (discontinuity &&
      !Race_ReplayPlaybackService_SendProjectileSnapshot(cl, session)) {
    Race_ReplayPlaybackService_ExitSlot(cl, slot);
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Replay projectile reconstruction failed.\n");
    return;
  }
  Race_ReplayPlaybackService_SendState(
    cl, session, true, discontinuity, true);
  G_Debug("client=%s replay=%016llx control=%s playhead=%u paused=%u speed=%u\n",
          cl->persistent.net_name,
          (unsigned long long) session->replay.replay_id,
          control, session->clock.playhead_ms,
          session->clock.paused, (unsigned) session->clock.speed);
  gi.ClientPrint(cl, PRINT_HIGH, "%s", result);
}

void Race_ReplayPlaybackService_ClientCommand(g_client_t *cl) {
  const int32_t argument_index = !q_strcmp(gi.Argv(0), "replay") ? 1 : 2;
  const char *argument = gi.Argc() > argument_index
    ? gi.Argv(argument_index) : "pb";
  if (!q_strcmp(argument, "stop") || !q_strcmp(argument, "off")) {
    int32_t slot;
    if (Race_ReplayPlaybackService_ClientSlot(cl, &slot) &&
        race_replay_sessions[slot].loaded) {
      G_Debug("client=%s replay=%016llx action=stop\n",
              cl->persistent.net_name,
              (unsigned long long) race_replay_sessions[slot].replay.replay_id);
      if (!Race_ReplayPlaybackService_ExitSlot(cl, slot)) {
        Race_ReplayPlaybackService_StopSlot(cl, slot, true);
      }
      gi.ClientPrint(cl, PRINT_HIGH, "Replay stopped.\n");
    } else {
      gi.ClientPrint(cl, PRINT_HIGH, "No replay is active.\n");
    }
    return;
  }
  if (!q_strcmp(argument, "pause") || !q_strcmp(argument, "resume") ||
      !q_strcmp(argument, "restart") || !q_strcmp(argument, "step") ||
      !q_strcmp(argument, "step_forward") ||
      !q_strcmp(argument, "step_back") || !q_strcmp(argument, "back") ||
      !q_strcmp(argument, "forward") || !q_strcmp(argument, "slower") ||
      !q_strcmp(argument, "faster") || !q_strcmp(argument, "speed")) {
    Race_ReplayPlaybackService_Control(cl, argument);
    return;
  }
  Race_ReplayPlaybackService_Start(cl, argument, true, false);
}

void Race_ReplayPlaybackService_ControlCommand(g_client_t *cl) {
  if (gi.Argc() != 2) {
    gi.ClientPrint(
      cl, PRINT_HIGH,
      "Usage: replay_control <pause|restart|back|forward|step_back|step_forward|slower|faster>\n");
    return;
  }
  const char *control = gi.Argv(1);
  if (q_strcmp(control, "pause") && q_strcmp(control, "restart") &&
      q_strcmp(control, "back") && q_strcmp(control, "forward") &&
      q_strcmp(control, "step_back") &&
      q_strcmp(control, "step_forward") &&
      q_strcmp(control, "slower") && q_strcmp(control, "faster")) {
    gi.ClientPrint(
      cl, PRINT_HIGH,
      "Usage: replay_control <pause|restart|back|forward|step_back|step_forward|slower|faster>\n");
    return;
  }
  Race_ReplayPlaybackService_Control(cl, control);
}

void Race_ReplayPlaybackService_CancelCommand(g_client_t *cl) {
  int32_t slot;
  if (Race_ReplayPlaybackService_ClientSlot(cl, &slot) &&
      race_replay_sessions[slot].loaded) {
    G_Debug("client=%s replay=%016llx action=cancel\n",
            cl->persistent.net_name,
            (unsigned long long) race_replay_sessions[slot].replay.replay_id);
    if (!Race_ReplayPlaybackService_ExitSlot(cl, slot)) {
      Race_ReplayPlaybackService_StopSlot(cl, slot, true);
    }
    gi.ClientPrint(cl, PRINT_HIGH, "Replay cancelled.\n");
  } else {
    gi.ClientPrint(cl, PRINT_HIGH, "No replay is active.\n");
  }
}

static void Race_ReplayPlaybackService_RacelineSelect(g_client_t *cl,
                                                       const char *argument) {
  argument = argument && *argument ? argument : "wr";
  int32_t slot;
  if (!q_strcmp(argument, "off") || !q_strcmp(argument, "stop")) {
    if (!Race_ReplayPlaybackService_ClientSlot(cl, &slot) ||
        !race_replay_sessions[slot].loaded ||
        !race_replay_sessions[slot].raceline_active) {
      gi.ClientPrint(cl, PRINT_HIGH, "No raceline is active.\n");
      return;
    }
    race_replay_playback_session_t *session = race_replay_sessions + slot;
    G_Debug("client=%s replay=%016llx action=raceline-off\n",
            cl->persistent.net_name,
            (unsigned long long) session->replay.replay_id);
    Race_ReplayPlaybackService_SendRacelineClear(cl, session);
    session->raceline_active = false;
    if (!session->playback_active) {
      Race_ReplayPlaybackService_Free(session);
    }
    gi.ClientPrint(cl, PRINT_HIGH, "Raceline hidden.\n");
    return;
  }
  Race_ReplayPlaybackService_Start(cl, argument, false, false);
}

void Race_ReplayPlaybackService_RaceSelect(g_client_t *cl,
                                            const char *argument) {
  Race_ReplayPlaybackService_Start(
    cl, argument && *argument ? argument : "wr", false, true);
}

void Race_ReplayPlaybackService_RacelineCommand(g_client_t *cl) {
  const int32_t argument_index = !q_strcmp(gi.Argv(0), "raceline") ? 1 : 2;
  Race_ReplayPlaybackService_RacelineSelect(
    cl, gi.Argc() > argument_index ? gi.Argv(argument_index) : "wr");
}
