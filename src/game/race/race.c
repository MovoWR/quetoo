/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race.h"
#include "race_admin_service.h"
#include "race_finish_report.h"
#include "race_map_state_service.h"
#include "race_modes.h"
#include "race_physics_service.h"
#include "race_replay_service.h"
#include "race_replay_playback_service.h"
#include "race_settings_service.h"
#include "race_trigger.h"
#include "race_vote_service.h"
#include "race_vote_menu_service.h"
#include "race_wire.h"

static ConfigureLevel Race_ConfigureLevel_Previous;
static InitMedia Race_InitMedia_Previous;
static InhibitItem Race_InhibitItem_Previous;
static InitItem Race_InitItem_Previous;
static TossInventory Race_TossInventory_Previous;
static uint16_t race_finish_sound;
static uint16_t race_first_completion_sound;
static uint16_t race_world_record_sound;
static uint32_t race_client_kill_times[MAX_CLIENTS];
static bool race_client_kill_time_initialized[MAX_CLIENTS];

static bool Race_ClientCanRun(const g_client_t *cl) {
  return cl && cl->entity && cl->entity->in_use && !cl->persistent.spectator &&
         !cl->entity->dead && cl->entity->health > 0 &&
         (Race_Mode(cl) == RACE_MODE_RACE || Race_Mode(cl) == RACE_MODE_PRACTICE);
}

static const char *Race_StateName(race_run_state_t state) {
  switch (state) {
    case RACE_RUN_ACTIVE:
      return "active";
    case RACE_RUN_FINISHED:
      return "finished";
    default:
      return "idle";
  }
}

static void Race_PrintTime(g_client_t *cl, const char *label, uint32_t time) {
  const uint32_t minutes = time / 60000;
  const uint32_t seconds = time / 1000 % 60;
  const uint32_t millis = time % 1000;

  gi.ClientPrint(cl, PRINT_HIGH, "%s %u:%02u.%03u\n", label, minutes, seconds, millis);
}

static void Race_InitMedia(void) {

  Race_InitMedia_Previous();

  race_finish_sound = gi.SoundIndex("misc/quake_secret");
  race_first_completion_sound = gi.SoundIndex("misc/quake_secret");
  race_world_record_sound = gi.SoundIndex("ctf/capture");
}

static void Race_ConfigureLevel(void) {

  Race_ConfigureLevel_Previous();

  Race_Trigger_ConfigureLevel();
  Race_Course_Validate(&g_level.race_course);
  Race_Trigger_FinalizeCourse();

  Race_PhysicsService_ConfigureLevel(g_level.name);
  Race_SettingsService_Load(g_level.name);
  Race_MapStateService_Load(g_level.name);
  Race_ReplayService_ConfigureLevel(g_level.name);
  Race_ReplayPlaybackService_ConfigureLevel();
  Race_VoteService_ConfigureLevel();
  Race_VoteMenuService_ConfigureLevel();

  gi.SetConfigString(CS_RACE_CHECKPOINT_TOTAL,
                     va("%u", g_level.race_course.checkpoint_count));

  gi.Print("Race course: map=%s checkpoints=%u starts=%u finishes=%u valid=%d\n",
           g_level.name, g_level.race_course.checkpoint_count,
           g_level.race_course.start_count, g_level.race_course.finish_count,
           g_level.race_course.valid);

  if (!g_level.race_course.valid) {
    gi.Warn(__func__,
            "Map %s has an invalid Race route; add a finish and use contiguous cp values 1 through N\n",
            g_level.name);
  }

  G_ForEachClient(cl, {
    Race_Run_Reset(&cl->race_run);
    cl->race_start_trigger = NULL;
    cl->race_stage_trigger = NULL;
    cl->race_stage_restart_trigger = NULL;
    cl->race_oneway_latches = 0u;
    Race_ClearStoredSpawn(cl);
  });
}

static void Race_TossInventory(g_client_t *cl) {
  Race_Reset(cl);
  Race_TossInventory_Previous(cl);
}

static bool Race_InhibitItem(const g_entity_t *ent) {
  if (ent && ent->item && ent->item->def.tag == WEAPON_BLASTER) {
    return true;
  }

  return Race_InhibitItem_Previous(ent);
}

static void Race_InitItem(g_item_t *it) {
  if (it->def.tag == WEAPON_BLASTER) {
    return;
  }

  Race_InitItem_Previous(it);
}

static void Race_ClientKill(g_client_t *cl) {
  if (!cl || Race_ReplayPlaybackService_ExitClient(cl)) {
    return;
  }

  if (cl->ps.client >= MAX_CLIENTS ||
      g_level.time - cl->respawn_time < 1000u ||
      cl->persistent.spectator || !cl->entity || cl->entity->dead ||
      cl->entity->health <= 0) {
    return;
  }

  const size_t slot = cl->ps.client;
  if (race_client_kill_time_initialized[slot] &&
      g_level.time - race_client_kill_times[slot] < 300u) {
    return;
  }

  race_client_kill_times[slot] = g_level.time;
  race_client_kill_time_initialized[slot] = true;
  Race_Reset(cl);
  G_ClientRespawn(cl, false);
}

void Race_Init(void) {
  static bool initialized;

  if (initialized) {
    return;
  }

  initialized = true;
  Race_ConfigureLevel_Previous = G_ConfigureLevel;
  G_ConfigureLevel = Race_ConfigureLevel;

  Race_InitMedia_Previous = G_InitMedia;
  G_InitMedia = Race_InitMedia;

  Race_InhibitItem_Previous = G_InhibitItem;
  G_InhibitItem = Race_InhibitItem;

  Race_InitItem_Previous = G_InitItem;
  G_InitItem = Race_InitItem;

  Race_TossInventory_Previous = G_TossInventory;
  G_TossInventory = Race_TossInventory;

  memset(race_client_kill_times, 0, sizeof(race_client_kill_times));
  memset(race_client_kill_time_initialized, 0,
         sizeof(race_client_kill_time_initialized));
  G_ClientKill = Race_ClientKill;
}

void Race_ResetClientKillRate(g_client_t *cl) {
  if (cl && cl->ps.client < MAX_CLIENTS) {
    race_client_kill_times[cl->ps.client] = 0u;
    race_client_kill_time_initialized[cl->ps.client] = false;
  }
}

void Race_Reset(g_client_t *cl) {

  if (!cl) {
    return;
  }

  Race_ReplayService_Reset(cl);
  Race_Run_Reset(&cl->race_run);
  cl->race_start_trigger = NULL;
  cl->race_oneway_latches = 0u;
  G_Debug("client=%s reset state=idle\n", cl->persistent.net_name);
}

void Race_MarkInvalid(g_client_t *cl, race_invalid_flags_t flag) {

  if (!cl || !cl->in_use || Race_Mode(cl) != RACE_MODE_RACE) {
    return;
  }

  Race_Run_MarkInvalid(&cl->race_run, flag);
}

bool Race_Start(g_client_t *cl) {

  if (!Race_ClientCanRun(cl)) {
    return false;
  }

  if (!Race_Run_Start(&cl->race_run, g_level.race_course.valid, g_level.time)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "This map has an invalid Race route. A finish and contiguous checkpoints 1 through N are required.\n");
    return false;
  }

  cl->race_start_trigger = NULL;
  cl->race_stage_trigger = NULL;
  cl->race_oneway_latches = 0u;

  const float speed = Vec3_Length(cl->entity->velocity);
  cl->race_run.mode = Race_Mode(cl);
  cl->race_run.start_speed = speed;
  cl->race_run.current_speed = speed;
  cl->race_run.top_speed = speed;

  G_Debug("client=%s start time=%u mode=%s speed=%g\n",
          cl->persistent.net_name, g_level.time,
          Race_ModeName(cl->race_run.mode), speed);
  if (!Race_ReplayService_Start(cl) && cl->race_run.mode == RACE_MODE_RACE) {
    Race_MarkInvalid(cl, RACE_INVALID_REPLAY_CAPACITY);
  }
  gi.ClientPrint(cl, PRINT_HIGH, "^2Race started!^7 Speed: %.1f\n", speed);
  return true;
}

bool Race_RequestStart(g_client_t *cl) {
  if (g_level.race_course.start_count) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "This map uses a start zone. Enter it to begin the run.\n");
    return false;
  }
  return Race_Start(cl);
}

static void Race_AnnouncePublication(
  const g_client_t *cl, const race_leaderboard_evaluation_t *evaluation,
  uint64_t replay_id) {
  const char *event = "Personal best";
  uint16_t sound = 0;
  if (evaluation->world_record) {
    event = evaluation->first_completion
      ? "First completion and world record"
      : "World record";
    sound = race_world_record_sound;
  } else if (evaluation->first_completion) {
    event = "First completion";
    sound = race_first_completion_sound;
  }

  const uint32_t time = cl->race_run.elapsed_time;
  gi.BroadcastPrint(PRINT_HIGH,
                    "%s: %s - %u:%02u.%03u\n",
                    event, cl->persistent.net_name,
                    time / 60000u, time / 1000u % 60u, time % 1000u);
  if (sound) {
    G_MulticastSound(&(const g_play_sound_t) {
      .index = sound,
      .flags = SOUND_RELATIVE
    }, MULTICAST_ALL_R);
  }
  G_Debug("client=%s publication=committed pb=%d wr=%d first=%d top=%d rank=%zu replay=%016llx cue=%s\n",
          cl->persistent.net_name, evaluation->personal_best,
          evaluation->world_record, evaluation->first_completion,
          evaluation->top, evaluation->top_rank,
          (unsigned long long) replay_id,
          evaluation->world_record ? "world-record" :
          evaluation->first_completion ? "first-completion" : "generic-finish");
}

static void Race_SendFinishReport(g_client_t *cl, const uint32_t previous_pb,
                                  const uint32_t world_record) {
  race_finish_report_t report = {
    .mode = cl->race_run.mode,
    .invalid_flags = (uint8_t) cl->race_run.invalid_flags,
    .elapsed_time = cl->race_run.elapsed_time,
    .previous_pb = previous_pb,
    .world_record = world_record,
    .checkpoint_count = cl->race_run.checkpoint_count,
    .start_speed = cl->race_run.start_speed,
    .end_speed = cl->race_run.end_speed,
    .top_speed = cl->race_run.top_speed,
    .average_speed = Race_Run_AverageSpeed(&cl->race_run)
  };
  memcpy(report.checkpoint_times, cl->race_run.checkpoint_times,
         report.checkpoint_count * sizeof(uint32_t));

  uint8_t payload[RACE_FINISH_REPORT_MAX_BYTES];
  const size_t length = Race_FinishReport_Encode(
    &report, payload, sizeof(payload));
  if (!length) {
    G_Warn("Could not encode Race finish report for %s\n",
           cl->persistent.net_name);
    return;
  }
  gi.WriteByte(SV_CMD_RACE_FINISH_REPORT);
  gi.WriteShort((int32_t) length);
  gi.WriteData(payload, length);
  gi.Unicast(cl, true);
}

bool Race_Checkpoint(g_client_t *cl, uint16_t checkpoint) {

  if (!Race_ClientCanRun(cl)) {
    return false;
  }

  const bool accepted = Race_Run_Checkpoint(&cl->race_run, g_level.race_course.checkpoint_count,
                                            checkpoint, g_level.time);
  G_Debug("client=%s checkpoint=%u accepted=%d progress=%u/%u\n",
          cl->persistent.net_name, checkpoint, accepted, cl->race_run.checkpoint_count,
          g_level.race_course.checkpoint_count);

  if (!accepted) {
    if (cl->race_run.state == RACE_RUN_ACTIVE && checkpoint >= 1 &&
        checkpoint <= RACE_MAX_CHECKPOINTS) {
      gi.ClientPrint(cl, PRINT_HIGH, "Checkpoint %u skipped (expected %u).\n",
                     checkpoint, cl->race_run.checkpoint_count + 1u);
    }
    return false;
  }

  const uint32_t elapsed = cl->race_run.checkpoint_times[checkpoint - 1];
  if (Race_SettingsService_CheckpointTimeEnabled()) {
    const char *label = va("Checkpoint %u:", checkpoint);
    Race_PrintTime(cl, label, elapsed);
    G_Debug("client=%s checkpoint=%u feedback=time\n",
            cl->persistent.net_name, checkpoint);
  } else {
    G_Debug("client=%s checkpoint=%u feedback=silent\n",
            cl->persistent.net_name, checkpoint);
  }

  G_MulticastSound(&(const g_play_sound_t) {
    .index = g_media.sounds.teleport,
    .entity = cl->entity,
  }, MULTICAST_PHS);
  return true;
}

bool Race_Split(g_client_t *cl, const uint16_t split, const char *label) {
  if (!Race_ClientCanRun(cl) || !g_level.race_course.splits_valid ||
      !Race_Run_Split(&cl->race_run, g_level.race_course.split_count,
                      split, g_level.time)) {
    return false;
  }

  const uint32_t cumulative = cl->race_run.split_times[split - 1u];
  const uint32_t previous = split > 1u
    ? cl->race_run.split_times[split - 2u] : 0u;
  const char *name = label && *label ? label : va("Split %u", split);
  uint32_t personal_best, world_record;
  Race_MapStateService_ClientSplitTimes(cl, split, &personal_best,
                                        &world_record);
  uint8_t comparison_flags = 0u;
  if (personal_best) {
    comparison_flags |= 1u;
  }
  if (world_record) {
    comparison_flags |= 2u;
  }
  gi.WriteByte(SV_CMD_RACE_SPLIT);
  gi.WriteByte(split);
  gi.WriteString(name);
  gi.WriteLong((int32_t) cumulative);
  gi.WriteLong((int32_t) (cumulative - previous));
  gi.WriteByte(comparison_flags);
  gi.WriteLong(personal_best
                 ? (int32_t) ((int64_t) cumulative - personal_best) : 0);
  gi.WriteLong(world_record
                 ? (int32_t) ((int64_t) cumulative - world_record) : 0);
  gi.Unicast(cl, true);
  Race_PrintTime(cl, name, cumulative);
  gi.ClientPrint(cl, PRINT_HIGH, "  Segment: %u.%03u\n",
                 (cumulative - previous) / 1000u,
                 (cumulative - previous) % 1000u);
  G_Debug("client=%s split=%u accepted=1 cumulative=%u segment=%u\n",
          cl->persistent.net_name, split, cumulative, cumulative - previous);
  return true;
}

bool Race_Stage(g_client_t *cl, g_entity_t *trigger) {
  if (!cl || !trigger || !trigger->race_stage_valid ||
      !g_level.race_course.stages_valid ||
      Race_Mode(cl) == RACE_MODE_SPECTATOR) {
    return false;
  }

  bool accepted = false;
  if (cl->race_run.state == RACE_RUN_ACTIVE) {
    accepted = Race_Run_Stage(&cl->race_run, g_level.race_course.stage_count,
                              (uint16_t) trigger->count, g_level.time);
  } else if (Race_Mode(cl) == RACE_MODE_PRACTICE &&
             cl->race_stage_trigger != trigger) {
    accepted = true;
  }
  if (!accepted) {
    return false;
  }

  cl->race_stage_trigger = trigger;
  const char *label = gi.EntityValue(trigger->def, "label")->nullable_string;
  gi.ClientPrint(cl, PRINT_HIGH, "%s stage %d%s%s.\n",
                 cl->race_run.state == RACE_RUN_ACTIVE ? "Reached" : "Practice",
                 trigger->count, label ? ": " : "", label ? label : "");
  return true;
}

bool Race_Finish(g_client_t *cl) {

  if (!Race_ClientCanRun(cl) || cl->race_run.state != RACE_RUN_ACTIVE) {
    return false;
  }

  if (!Race_Run_Finish(&cl->race_run, g_level.race_course.checkpoint_count, g_level.time)) {
    G_Debug("client=%s finish accepted=0 progress=%u/%u\n",
            cl->persistent.net_name, cl->race_run.checkpoint_count,
            g_level.race_course.checkpoint_count);
    gi.ClientPrint(cl, PRINT_HIGH, "Race incomplete: %u checkpoint(s) remaining\n",
                   g_level.race_course.checkpoint_count - cl->race_run.checkpoint_count);
    return false;
  }

  cl->race_run.end_speed = Vec3_Length(cl->entity->velocity);
  cl->race_run.top_speed = Maxf(cl->race_run.top_speed,
                                cl->race_run.end_speed);
  const float average_speed = Race_Run_AverageSpeed(&cl->race_run);
  const bool valid = Race_Run_IsValid(&cl->race_run);
  uint32_t previous_pb = 0u;
  uint32_t world_record = 0u;
  bool milestone_cue = false;
  Race_MapStateService_ClientTimes(cl, &previous_pb, &world_record);

  G_Debug("client=%s finish accepted=1 elapsed=%u mode=%s valid=%d\n",
          cl->persistent.net_name, cl->race_run.elapsed_time,
          Race_ModeName(cl->race_run.mode), valid);

  if (cl->race_run.mode == RACE_MODE_RACE && Race_Mode(cl) == RACE_MODE_RACE && valid) {
    race_leaderboard_evaluation_t evaluation;
    uint64_t replay_id;
    if (Race_ReplayService_Finish(cl, &evaluation, &replay_id)) {
      Race_AnnouncePublication(cl, &evaluation, replay_id);
      milestone_cue = evaluation.world_record || evaluation.first_completion;
      Race_MapStateService_ClientTimes(cl, NULL, &world_record);
    }
  } else {
    Race_ReplayService_Reset(cl);
  }

  const char *mode_name = cl->race_run.mode == RACE_MODE_PRACTICE
    ? "Practice Mode" : "Race Mode";
  const char *invalid = valid || cl->race_run.mode == RACE_MODE_PRACTICE
    ? "" : " ^1(INVALID)^7";
  gi.ClientPrint(cl, PRINT_HIGH,
                 "^2Race finished!^7 Time: %u:%02u.%03u  Mode: %s%s\n",
                 cl->race_run.elapsed_time / 60000u,
                 cl->race_run.elapsed_time / 1000u % 60u,
                 cl->race_run.elapsed_time % 1000u, mode_name, invalid);
  gi.ClientPrint(cl, PRINT_HIGH,
                 "  Start speed: %.1f  End speed: %.1f  Avg speed: %.1f\n",
                 cl->race_run.start_speed, cl->race_run.end_speed, average_speed);

  if (cl->race_run.mode == RACE_MODE_PRACTICE) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "^3Practice finish:^7 training runs are never submitted as records.\n");
  } else if (!valid) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "^1Invalid finish:^7 this run was not submitted as a record.\n");
  }

  Race_SendFinishReport(cl, previous_pb, world_record);

  if (!milestone_cue && race_finish_sound &&
      Race_SettingsService_FinishCueEnabled()) {
    G_MulticastSound(&(const g_play_sound_t) {
      .index = race_finish_sound,
      .flags = SOUND_RELATIVE,
      .gain = Race_SettingsService_FinishCueGain()
    }, MULTICAST_ALL_R);
    G_Debug("client=%s finish cue=generic-success gain=%g multicast=all-reliable\n",
            cl->persistent.net_name,
            Race_SettingsService_FinishCueGain());
  } else if (!milestone_cue && race_finish_sound) {
    G_Debug("client=%s finish cue=disabled\n", cl->persistent.net_name);
  }
  return true;
}

static bool Race_StartTriggerContains(const g_client_t *cl,
                                      const g_entity_t *trigger) {
  if (!cl || !cl->entity || !trigger || !trigger->in_use) {
    return false;
  }
  const cm_trace_t trace = gi.Clip(cl->entity->s.origin, cl->entity->s.origin,
                                   cl->entity->bounds, trigger, -1);
  return trace.start_solid || trace.all_solid;
}

static void Race_StartTriggerFire(g_entity_t *trigger, g_client_t *cl) {
  if (trigger && cl && (trigger->target || trigger->message)) {
    G_UseTargets(trigger, cl->entity);
  }
}

bool Race_ClientInput(g_client_t *cl, const pm_cmd_t *cmd) {
  g_entity_t *trigger = cl ? cl->race_start_trigger : NULL;
  if (!trigger || trigger->race_start_mode != RACE_START_JUMP || !cmd ||
      !Race_StartJumpEdge(cmd->up, cl->cmd.up)) {
    return false;
  }
  if (!Race_StartTriggerContains(cl, trigger)) {
    cl->race_start_trigger = NULL;
    return false;
  }

  cl->race_start_trigger = NULL;
  if (Race_Start(cl)) {
    Race_StartTriggerFire(trigger, cl);
  }
  return false;
}

void Race_PrintStatus(g_client_t *cl) {
  const race_run_t *run = &cl->race_run;
  const uint16_t next = run->checkpoint_count < g_level.race_course.checkpoint_count
    ? run->checkpoint_count + 1
    : 0;

  gi.ClientPrint(cl, PRINT_HIGH,
                 "Race: state=%s route=%s checkpoints=%u/%u next=%u invalid=0x%02x\n",
                 Race_StateName(run->state),
                 g_level.race_course.valid ? "valid" : "invalid",
                 run->checkpoint_count, g_level.race_course.checkpoint_count, next,
                 (unsigned) run->invalid_flags);
  G_Debug("client=%s status state=%s mode=%s checkpoints=%u/%u invalid=0x%02x\n",
          cl->persistent.net_name, Race_StateName(run->state),
          Race_ModeName(Race_Mode(cl)), run->checkpoint_count,
          g_level.race_course.checkpoint_count, (unsigned) run->invalid_flags);

  if (run->state == RACE_RUN_FINISHED) {
    Race_PrintTime(cl, "Time:", run->elapsed_time);
  } else if (run->state == RACE_RUN_ACTIVE) {
    Race_PrintTime(cl, "Time:", g_level.time - run->start_time);
  }
}

void Race_ClientThink(g_client_t *cl, const pm_cmd_t *cmd) {

  const bool eligible = Race_ClientCanRun(cl);
  const race_mode_t mode = Race_Mode(cl);

  if (eligible && cl->race_run.state == RACE_RUN_ACTIVE) {
    Race_Run_ObserveSpeed(&cl->race_run, Vec3_Length(cl->entity->velocity));
  }

  g_entity_t *trigger = cl->race_start_trigger;
  if (trigger && !Race_StartTriggerContains(cl, trigger)) {
    cl->race_start_trigger = NULL;
    if (Race_StartExitTransition(trigger->race_start_mode, false) &&
        Race_Start(cl)) {
      Race_StartTriggerFire(trigger, cl);
    }
  }

  if (!g_level.race_course.start_count &&
      Race_Run_ShouldAutoStart(mode, &cl->race_run, g_level.race_course.valid,
                               eligible, cmd->forward, cmd->right, cmd->up)) {
    Race_Start(cl);
  }
}

void Race_ClientStats(g_client_t *cl) {
  Race_SynchronizeMode(cl);

  const g_client_t *view = cl;
  if (cl->persistent.spectator && cl->chase_target &&
      G_IsMeat(cl->chase_target->entity)) {
    view = cl->chase_target;
  }

  const uint32_t elapsed = Race_Run_Elapsed(&view->race_run, g_level.time);
  uint32_t personal_best;
  uint32_t world_record;
  Race_MapStateService_ClientTimes(view, &personal_best, &world_record);

  cl->ps.stats[STAT_RACE_MODE] = (int16_t) Race_Mode(view);
  cl->ps.stats[STAT_RACE_RUN_STATE] = (int16_t) view->race_run.state;
  cl->ps.stats[STAT_RACE_ELAPSED_LOW] = Race_WireElapsedLow(elapsed);
  cl->ps.stats[STAT_RACE_ELAPSED_HIGH] = Race_WireElapsedHigh(elapsed);
  cl->ps.stats[STAT_RACE_CHECKPOINT_COUNT] = (int16_t) view->race_run.checkpoint_count;
  cl->ps.stats[STAT_RACE_INVALID_FLAGS] = (int16_t) view->race_run.invalid_flags;
  cl->ps.stats[STAT_RACE_INPUT] = Race_InputFlags(&view->cmd);
  cl->ps.stats[STAT_RACE_PB_LOW] = Race_WireElapsedLow(personal_best);
  cl->ps.stats[STAT_RACE_PB_HIGH] = Race_WireElapsedHigh(personal_best);
  cl->ps.stats[STAT_RACE_WR_LOW] = Race_WireElapsedLow(world_record);
  cl->ps.stats[STAT_RACE_WR_HIGH] = Race_WireElapsedHigh(world_record);
  cl->ps.stats[STAT_RACE_ADMIN_CAPABILITIES] =
    (int16_t) Race_AdminService_ClientCapabilities(cl);
  uint16_t voteFlags = RACE_VOTE_CLIENT_STATE_VALID;
  if (Race_VoteService_ClientCanCast(cl)) {
    voteFlags |= RACE_VOTE_CLIENT_CAN_CAST;
  }
  if (Race_VoteMenuService_ClientCanCast(cl)) {
    voteFlags |= RACE_VOTE_MENU_CLIENT_CAN_CAST;
  }
  if (Race_VoteMenuService_ClientCanNominate(cl)) {
    voteFlags |= RACE_VOTE_MENU_CLIENT_CAN_NOMINATE;
  }
  cl->ps.stats[STAT_RACE_VOTE_FLAGS] = (int16_t) voteFlags;
}

void Race_ClientScore(const g_client_t *cl, g_score_t *score) {
  if (cl->persistent.spectator && cl->chase_target) {
    score->team = (uint8_t) (cl->chase_target->ps.client + 1u);
  }
  score->race_elapsed_time = Race_Run_Elapsed(&cl->race_run, g_level.time);
  score->race_checkpoint_count = cl->race_run.checkpoint_count;
  score->race_mode = (uint8_t) Race_Mode(cl);
  score->race_run_state = (uint8_t) cl->race_run.state;
  score->race_invalid_flags = (uint8_t) cl->race_run.invalid_flags;
}
