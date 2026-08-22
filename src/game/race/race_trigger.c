/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <limits.h>
#include <stdlib.h>

#include "race.h"
#include "race_clip.h"
#include "race_trigger.h"

#define RACE_TRIGGER_LABEL_MAX 31u
#define RACE_GATE_CLEARANCE 1024.f

static const char *Race_TriggerLabel(const g_entity_t *ent) {
  const cm_entity_t *label = gi.EntityValue(ent->def, "label");
  return label->parsed & ENTITY_STRING ? label->string : NULL;
}

static bool Race_TriggerLabelValid(g_entity_t *ent, const char *kind) {
  const char *label = Race_TriggerLabel(ent);
  if (!label || q_strlen(label) <= RACE_TRIGGER_LABEL_MAX) {
    return true;
  }
  G_Warn("Invalid Race %s at %s; label is limited to %u characters\n",
         kind, etos(ent), RACE_TRIGGER_LABEL_MAX);
  return false;
}

static void Race_TriggerFire(g_entity_t *ent, g_entity_t *activator) {
  if (ent->target || ent->message) {
    G_UseTargets(ent, activator);
  }
}

static bool Race_TriggerTouchAllowed(g_entity_t *ent, g_entity_t *other) {

  if (!other->client) {
    return false;
  }

  const int32_t client = other->client->ps.client;
  if (client < 0 || client >= sv_max_clients->integer) {
    return false;
  }

  if (ent->wait <= 0.f) {
    return true;
  }

  const uint32_t wait = (uint32_t) (ent->wait * 1000.f);
  return !Race_Trigger_Debounced(ent->race_touch_times + client, g_level.time, wait);
}

static void Race_TriggerStart_Touch(g_entity_t *ent, g_entity_t *other, const cm_trace_t *trace) {
  (void) trace;

  if (!Race_TriggerTouchAllowed(ent, other)) {
    return;
  }

  if (ent->race_start_mode == RACE_START_TOUCH) {
    if (Race_Start(other->client)) {
      Race_TriggerFire(ent, other);
    }
    return;
  }

  if (other->client->race_start_trigger != ent) {
    if (other->client->race_run.state != RACE_RUN_IDLE) {
      Race_Reset(other->client);
    }
    other->client->race_start_trigger = ent;
    G_Debug("client=%s start armed mode=%s entity=%d\n",
            other->client->persistent.net_name,
            ent->race_start_mode == RACE_START_EXIT ? "exit" : "jump",
            ent->s.number);
  }
}

static void Race_TriggerCheckpoint_Touch(g_entity_t *ent, g_entity_t *other, const cm_trace_t *trace) {
  (void) trace;

  if (Race_TriggerTouchAllowed(ent, other)) {
    G_Debug("client=%d entity=%d checkpoint=%d\n",
            other->client->ps.client, ent->s.number, ent->count);
    if (Race_Checkpoint(other->client, (uint16_t) ent->count)) {
      Race_TriggerFire(ent, other);
    }
  }
}

static void Race_TriggerSplit_Touch(g_entity_t *ent, g_entity_t *other,
                                    const cm_trace_t *trace) {
  (void) trace;
  if (Race_TriggerTouchAllowed(ent, other) &&
      Race_Split(other->client, (uint16_t) ent->count,
                 Race_TriggerLabel(ent))) {
    Race_TriggerFire(ent, other);
  }
}

static void Race_TriggerStage_Touch(g_entity_t *ent, g_entity_t *other,
                                    const cm_trace_t *trace) {
  (void) trace;
  if (Race_TriggerTouchAllowed(ent, other) && Race_Stage(other->client, ent)) {
    Race_TriggerFire(ent, other);
  }
}

static void Race_TriggerFinish_Touch(g_entity_t *ent, g_entity_t *other, const cm_trace_t *trace) {
  (void) trace;

  if (Race_TriggerTouchAllowed(ent, other)) {
    if (Race_Finish(other->client)) {
      Race_TriggerFire(ent, other);
    }
  }
}

static void Race_TriggerInit(g_entity_t *ent) {

  if (!Vec3_Equal(ent->s.angles, Vec3_Zero())) {
    G_SetMoveDir(ent);
  }

  if (ent->wait == 0.f) {
    ent->wait = .5f;
  }

  ent->race_touch_times = gi.Malloc(sizeof(*ent->race_touch_times) * sv_max_clients->integer,
                                    MEM_TAG_GAME_LEVEL);
  for (int32_t i = 0; i < sv_max_clients->integer; i++) {
    ent->race_touch_times[i] = UINT32_MAX;
  }

  ent->solid = SOLID_TRIGGER;
  ent->move_type = MOVE_TYPE_NONE;
  ent->sv_flags = SVF_NO_CLIENT;

  gi.SetModel(ent, ent->model);
}

static void Race_TriggerStart(g_entity_t *ent) {
  Race_Course_AddStart(&g_level.race_course);

  const cm_entity_t *mode = gi.EntityValue(ent->def, "start_mode");
  if (!Race_StartMode_Parse(mode->nullable_string, &ent->race_start_mode)) {
    G_Warn("Invalid Race start at %s; start_mode must be touch, exit, or jump\n",
           etos(ent));
    G_FreeEntity(ent);
    return;
  }

  Race_TriggerInit(ent);
  ent->Touch = Race_TriggerStart_Touch;
  gi.LinkEntity(ent);
}

static bool Race_TriggerNumber(g_entity_t *ent, const char *key,
                               const int32_t minimum, int32_t *value) {
  const cm_entity_t *number = gi.EntityValue(ent->def, key);
  if (!(number->parsed & ENTITY_INTEGER) || number->integer < minimum ||
      number->integer > RACE_MAX_CHECKPOINTS) {
    G_Warn("Invalid Race %s at %s; %s must be an integer from %d through %d\n",
           key, etos(ent), key, minimum, RACE_MAX_CHECKPOINTS);
    return false;
  }
  *value = number->integer;
  return true;
}

static void Race_TriggerCheckpoint(g_entity_t *ent) {
  const cm_entity_t *checkpoint = gi.EntityValue(ent->def, "cp");

  if (!(checkpoint->parsed & ENTITY_INTEGER)) {
    Race_Course_AddCheckpoint(&g_level.race_course, 0);
    G_Warn("Invalid Race checkpoint at %s; cp must be an integer from 1 through %d\n",
           etos(ent), RACE_MAX_CHECKPOINTS);
    G_FreeEntity(ent);
    return;
  }

  if (!Race_Course_AddCheckpoint(&g_level.race_course, checkpoint->integer)) {
    G_Warn("Invalid Race checkpoint at %s; cp must be an integer from 1 through %d\n",
           etos(ent), RACE_MAX_CHECKPOINTS);
    G_FreeEntity(ent);
    return;
  }

  ent->count = checkpoint->integer;
  Race_TriggerInit(ent);
  ent->Touch = Race_TriggerCheckpoint_Touch;
  gi.LinkEntity(ent);
}

static void Race_TriggerFinish(g_entity_t *ent) {
  Race_TriggerInit(ent);
  ent->Touch = Race_TriggerFinish_Touch;
  gi.LinkEntity(ent);
}

static void Race_TriggerSplit(g_entity_t *ent) {
  int32_t split;
  if (!Race_TriggerNumber(ent, "split", 1, &split) ||
      !Race_TriggerLabelValid(ent, "split") ||
      !Race_Course_AddSplit(&g_level.race_course, split)) {
    Race_Course_InvalidateSplits(&g_level.race_course);
    G_FreeEntity(ent);
    return;
  }
  ent->count = split;
  Race_TriggerInit(ent);
  ent->Touch = Race_TriggerSplit_Touch;
  gi.LinkEntity(ent);
}

static void Race_TriggerStage(g_entity_t *ent) {
  int32_t stage;
  const cm_entity_t *restart = gi.EntityValue(ent->def, "restart_target");
  if (!Race_TriggerNumber(ent, "stage", 2, &stage) ||
      !Race_TriggerLabelValid(ent, "stage") ||
      !(restart->parsed & ENTITY_STRING) ||
      !Race_Course_AddStage(&g_level.race_course, stage)) {
    Race_Course_InvalidateStages(&g_level.race_course);
    G_Warn("Invalid Race stage at %s; restart_target is required\n", etos(ent));
    G_FreeEntity(ent);
    return;
  }
  ent->count = stage;
  Race_TriggerInit(ent);
  ent->Touch = Race_TriggerStage_Touch;
  gi.LinkEntity(ent);
}

static bool Race_BarrierBrushValid(g_entity_t *ent) {
  if (!ent->model || ent->model[0] != '*' || !ent->model[1]) {
    return false;
  }

  Vector *brushes = gi.EntityBrushes(ent->def);
  bool valid = brushes && brushes->count;
  for (size_t i = 0; valid && i < brushes->count; i++) {
    const cm_bsp_brush_t *brush = VectorValue(brushes, cm_bsp_brush_t *, i);
    valid = (brush->contents & ~CONTENTS_DETAIL) == CONTENTS_PLAYER_CLIP;
  }
  release(brushes);
  return valid;
}

static bool Race_BarrierInit(g_entity_t *ent, race_barrier_type_t type) {
  if (!Race_BarrierBrushValid(ent)) {
    G_Warn("Invalid Race barrier at %s; use an inline brush textured only with player_clip\n",
           etos(ent));
    Race_Course_InvalidateBarrier(&g_level.race_course);
    G_FreeEntity(ent);
    return false;
  }

  ent->race_barrier_type = type;
  ent->race_barrier_valid = true;
  ent->solid = SOLID_BSP;
  ent->move_type = MOVE_TYPE_NONE;
  ent->s.effects |= EF_RACE_GATE;
  gi.SetModel(ent, ent->model);
  gi.LinkEntity(ent);
  return true;
}

static void Race_CheckpointGate(g_entity_t *ent) {
  int32_t checkpoint;
  const cm_entity_t *mode = gi.EntityValue(ent->def, "mode");
  const cm_entity_t *invert = gi.EntityValue(ent->def, "invert");
  if (!Race_TriggerNumber(ent, "cp", 1, &checkpoint)) {
    Race_Course_InvalidateBarrier(&g_level.race_course);
    G_FreeEntity(ent);
    return;
  }

  ent->race_gate_mode = RACE_GATE_AT_LEAST;
  if (mode->parsed & ENTITY_STRING) {
    if (!q_strcmp(mode->string, "exact")) {
      ent->race_gate_mode = RACE_GATE_EXACT;
    } else if (q_strcmp(mode->string, "atleast")) {
      G_Warn("Invalid Race checkpoint gate at %s; mode must be atleast or exact\n",
             etos(ent));
      Race_Course_InvalidateBarrier(&g_level.race_course);
      G_FreeEntity(ent);
      return;
    }
  }
  if ((invert->parsed & ENTITY_INTEGER) &&
      invert->integer != 0 && invert->integer != 1) {
    G_Warn("Invalid Race checkpoint gate at %s; invert must be 0 or 1\n", etos(ent));
    Race_Course_InvalidateBarrier(&g_level.race_course);
    G_FreeEntity(ent);
    return;
  }

  ent->race_gate_checkpoint = (uint16_t) checkpoint;
  ent->race_gate_invert = invert->integer != 0;
  Race_BarrierInit(ent, RACE_BARRIER_CHECKPOINT_GATE);
}

static void Race_OneWayWall(g_entity_t *ent) {
  if (ent->s.angles.x != 0.f || ent->s.angles.z != 0.f) {
    G_Warn("Invalid Race one-way wall at %s; only horizontal yaw is supported\n",
           etos(ent));
    Race_Course_InvalidateBarrier(&g_level.race_course);
    G_FreeEntity(ent);
    return;
  }

  G_SetMoveDir(ent);
  Race_BarrierInit(ent, RACE_BARRIER_ONEWAY_WALL);
}

static float Race_BoundsClearance(const box3_t a, const box3_t b) {
  const float dx = a.maxs.x < b.mins.x ? b.mins.x - a.maxs.x
    : b.maxs.x < a.mins.x ? a.mins.x - b.maxs.x : 0.f;
  const float dy = a.maxs.y < b.mins.y ? b.mins.y - a.maxs.y
    : b.maxs.y < a.mins.y ? a.mins.y - b.maxs.y : 0.f;
  const float dz = a.maxs.z < b.mins.z ? b.mins.z - a.maxs.z
    : b.maxs.z < a.mins.z ? a.mins.z - b.maxs.z : 0.f;
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void Race_ConfigureBarriers(void) {
  g_entity_t *barriers[RACE_MAX_CHECKPOINTS];
  size_t count = 0u;
  const char *classnames[] = {
    "func_race_checkpoint_gate",
    "func_race_oneway_wall"
  };
  for (size_t classname = 0; classname < lengthof(classnames); classname++) {
    g_entity_t *ent = NULL;
    while ((ent = G_Find(ent, EOFS(classname), classnames[classname]))) {
      if (count == lengthof(barriers)) {
        Race_Course_InvalidateBarrier(&g_level.race_course);
        ent->race_barrier_valid = false;
        ent->solid = SOLID_NOT;
        gi.LinkEntity(ent);
        G_Warn("Race supports at most %d conditional brushes; disabling %s\n",
               RACE_MAX_CHECKPOINTS, etos(ent));
        continue;
      }
      barriers[count++] = ent;
    }
  }

  for (size_t i = 0; i < count; i++) {
    for (size_t j = i + 1u; j < count; j++) {
      if (barriers[j]->s.model1 < barriers[i]->s.model1) {
        g_entity_t *swap = barriers[i];
        barriers[i] = barriers[j];
        barriers[j] = swap;
      }
    }
    barriers[i]->race_barrier_id = (uint8_t) i;
  }
  g_level.race_course.barrier_count = (uint16_t) count;

  for (size_t i = 0; i < count; i++) {
    g_entity_t *gate = barriers[i];
    if (gate->race_barrier_type != RACE_BARRIER_CHECKPOINT_GATE) {
      continue;
    }

    g_entity_t *checkpoint = NULL;
    g_entity_t *candidate = NULL;
    while ((candidate = G_Find(candidate, EOFS(classname), "trigger_race_cp"))) {
      if (candidate->count == gate->race_gate_checkpoint) {
        checkpoint = candidate;
        break;
      }
    }

    if (!checkpoint ||
        Race_BoundsClearance(gate->abs_bounds, checkpoint->abs_bounds) <
          RACE_GATE_CLEARANCE) {
      gate->race_barrier_valid = false;
      gate->solid = SOLID_NOT;
      gi.LinkEntity(gate);
      Race_Course_InvalidateBarrier(&g_level.race_course);
      G_Warn("Race checkpoint gate at %s must be at least %.0f units from cp %u; gate disabled and course is non-rankable\n",
             etos(gate), RACE_GATE_CLEARANCE, gate->race_gate_checkpoint);
    }
  }
}

void Race_Trigger_ConfigureLevel(void) {
  g_entity_t *stage = NULL;
  while ((stage = G_Find(stage, EOFS(classname), "trigger_race_stage"))) {
    const char *name = gi.EntityValue(stage->def, "restart_target")->nullable_string;
    g_entity_t *anchor = NULL;
    size_t matches = 0u;
    g_entity_t *candidate = NULL;
    while ((candidate = G_Find(candidate, EOFS(target_name), name))) {
      anchor = candidate;
      matches++;
    }

    const cm_trace_t safety = anchor
      ? gi.Trace(anchor->s.origin, anchor->s.origin, PM_BOUNDS, anchor,
                 CONTENTS_MASK_CLIP_PLAYER)
      : (cm_trace_t) { 0 };
    if (matches != 1u || !anchor || q_strcmp(anchor->classname, "info_notnull") ||
        anchor->solid != SOLID_NOT || safety.start_solid || safety.all_solid) {
      stage->race_stage_valid = false;
      Race_Course_InvalidateStages(&g_level.race_course);
      G_Warn("Race stage %d at %s requires one non-solid info_notnull named %s\n",
             stage->count, etos(stage), name);
      continue;
    }
    stage->target_ent = anchor;
    stage->race_stage_valid = true;
  }

  Race_ConfigureBarriers();
}

static uint64_t Race_SplitLayoutHash(uint64_t hash, const void *data,
                                     const size_t length) {
  const uint8_t *bytes = data;
  for (size_t i = 0; i < length; i++) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

void Race_Trigger_FinalizeCourse(void) {
  g_level.race_course.split_layout = 0u;
  if (!g_level.race_course.splits_valid ||
      !g_level.race_course.split_count) {
    return;
  }

  uint64_t hash = UINT64_C(14695981039346656037);
  for (uint16_t number = 1u;
       number <= g_level.race_course.split_count; number++) {
    uint16_t matching = 0u;
    g_entity_t *candidate = NULL;
    while ((candidate = G_Find(candidate, EOFS(classname),
                               "trigger_race_split"))) {
      if (candidate->count != number) {
        continue;
      }
      matching++;
      const char *label = Race_TriggerLabel(candidate);
      hash = Race_SplitLayoutHash(hash, &number, sizeof(number));
      hash = Race_SplitLayoutHash(hash, &candidate->abs_bounds,
                                  sizeof(candidate->abs_bounds));
      if (label) {
        hash = Race_SplitLayoutHash(hash, label, q_strlen(label) + 1u);
      } else {
        const uint8_t empty = 0u;
        hash = Race_SplitLayoutHash(hash, &empty, sizeof(empty));
      }
    }
    if (!matching) {
      g_level.race_course.split_layout = 0u;
      return;
    }
    hash = Race_SplitLayoutHash(hash, &matching, sizeof(matching));
  }
  g_level.race_course.split_layout = hash ? hash : 1u;
}

bool Race_Trigger_ShouldClipMovementEntity(g_entity_t *mover,
                                           const g_entity_t *candidate,
                                           const vec3_t start,
                                           const vec3_t end,
                                           const box3_t bounds) {
  if (!candidate || !candidate->race_barrier_valid ||
      candidate->race_barrier_type == RACE_BARRIER_NONE) {
    return true;
  }

  const race_clip_barrier_t barrier = {
    .type = candidate->race_barrier_type,
    .gate_mode = candidate->race_gate_mode,
    .checkpoint = candidate->race_gate_checkpoint,
    .id = candidate->race_barrier_id,
    .invert = candidate->race_gate_invert,
    .direction = candidate->move_dir,
    .abs_bounds = candidate->abs_bounds
  };
  const uint16_t reached = mover && mover->client
    ? mover->client->race_run.checkpoint_count : 0u;
  race_clip_state_t state = {
    .oneway_latches = mover && mover->client
      ? mover->client->race_oneway_latches : 0u
  };
  const bool blocks = Race_ClipBarrierBlocks(
    &barrier, mover && mover->client ? &state : NULL,
    reached, start, end, bounds);
  if (mover && mover->client) {
    mover->client->race_oneway_latches = state.oneway_latches;
  }
  return blocks;
}

bool Race_SpawnEntity(g_entity_t *ent) {

  if (q_strcmp(ent->classname, "trigger_race_start") == 0) {
    Race_TriggerStart(ent);
    return true;
  }

  if (q_strcmp(ent->classname, "trigger_race_cp") == 0) {
    Race_TriggerCheckpoint(ent);
    return true;
  }

  if (q_strcmp(ent->classname, "trigger_race_finish") == 0) {
    Race_TriggerFinish(ent);
    return true;
  }

  if (q_strcmp(ent->classname, "trigger_race_split") == 0) {
    Race_TriggerSplit(ent);
    return true;
  }

  if (q_strcmp(ent->classname, "trigger_race_stage") == 0) {
    Race_TriggerStage(ent);
    return true;
  }

  if (q_strcmp(ent->classname, "func_race_checkpoint_gate") == 0) {
    Race_CheckpointGate(ent);
    return true;
  }

  if (q_strcmp(ent->classname, "func_race_oneway_wall") == 0) {
    Race_OneWayWall(ent);
    return true;
  }

  return false;
}
