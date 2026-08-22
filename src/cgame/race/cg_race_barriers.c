/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_race_barriers.h"
#include "race_clip.h"

typedef struct {
  race_barrier_type_t type;
  race_gate_mode_t gate_mode;
  uint16_t checkpoint;
  uint8_t id;
  uint8_t model_index;
  bool invert;
  bool visual;
  color_t color;
  vec3_t direction;
  Vector *brushes;
} cg_race_barrier_t;

static struct {
  cg_race_barrier_t barriers[RACE_MAX_CHECKPOINTS];
  size_t count;
  race_clip_state_t clip_state;
  uint16_t previous_checkpoint_count;
  bool prediction_initialized;
} cg_race_barriers;

void Cg_RaceBarriers_Clear(void) {
  for (size_t i = 0; i < cg_race_barriers.count; i++) {
    release(cg_race_barriers.barriers[i].brushes);
  }
  memset(&cg_race_barriers, 0, sizeof(cg_race_barriers));
}

bool Cg_RaceBarriers_ResolveModelIndex(
    const char *model,
    const char catalog[][MAX_STRING_CHARS],
    const size_t catalog_count,
    uint8_t *model_index) {
  return Race_ClipResolveModelIndex(model, catalog, catalog_count,
                                    model_index);
}

static Vector *Cg_RaceBarriers_Brushes(const cm_entity_t *entity) {
  Vector *brushes = cgi.EntityBrushes(entity);
  bool valid = brushes && brushes->count;
  for (size_t i = 0; valid && i < brushes->count; i++) {
    const cm_bsp_brush_t *brush = VectorValue(brushes, cm_bsp_brush_t *, i);
    valid = (brush->contents & ~CONTENTS_DETAIL) == CONTENTS_PLAYER_CLIP;
  }
  if (!valid) {
    release(brushes);
    return NULL;
  }
  return brushes;
}

static bool Cg_RaceBarriers_HasModelIndex(const uint8_t model_index) {
  uint8_t modelIndices[RACE_MAX_CHECKPOINTS];
  for (size_t i = 0; i < cg_race_barriers.count; i++) {
    modelIndices[i] = cg_race_barriers.barriers[i].model_index;
  }
  return !Race_ClipModelIndexUnique(model_index, modelIndices,
                                    cg_race_barriers.count);
}

void Cg_RaceBarriers_Load(void) {
  Cg_RaceBarriers_Clear();
  const cm_bsp_t *bsp = cgi.Bsp();
  if (!bsp || !cgi.client) {
    return;
  }

  for (int32_t i = 0; i < bsp->num_entities; i++) {
    const cm_entity_t *entity = bsp->entities[i];
    const char *classname = cgi.EntityValue(entity, "classname")->nullable_string;
    race_barrier_type_t type = RACE_BARRIER_NONE;
    if (classname && !q_strcmp(classname, "func_race_checkpoint_gate")) {
      type = RACE_BARRIER_CHECKPOINT_GATE;
    } else if (classname && !q_strcmp(classname, "func_race_oneway_wall")) {
      type = RACE_BARRIER_ONEWAY_WALL;
    } else {
      continue;
    }

    const cm_entity_t *model = cgi.EntityValue(entity, "model");
    uint8_t modelIndex;
    if (cg_race_barriers.count == RACE_MAX_CHECKPOINTS ||
        !(model->parsed & ENTITY_STRING) ||
        !Cg_RaceBarriers_ResolveModelIndex(
          model->string, cgi.client->config_strings + CS_MODELS,
          MAX_MODELS, &modelIndex) ||
        Cg_RaceBarriers_HasModelIndex(modelIndex)) {
      continue;
    }

    Vector *brushes = Cg_RaceBarriers_Brushes(entity);
    if (!brushes) {
      continue;
    }

    cg_race_barrier_t barrier = {
      .type = type,
      .model_index = modelIndex,
      .gate_mode = RACE_GATE_AT_LEAST,
      .color = Color4f(.25f, .75f, 1.f, .7f),
      .brushes = brushes
    };
    const cm_entity_t *visual = cgi.EntityValue(entity, "visual");
    const cm_entity_t *color = cgi.EntityValue(entity, "color");
    barrier.visual = (visual->parsed & ENTITY_INTEGER) && visual->integer == 1;
    if (color->parsed & ENTITY_COLOR) {
      barrier.color = Color_Normalize(Color4f(color->vec3.x, color->vec3.y,
                                               color->vec3.z, .7f));
    }

    if (type == RACE_BARRIER_CHECKPOINT_GATE) {
      const cm_entity_t *checkpoint = cgi.EntityValue(entity, "cp");
      const cm_entity_t *mode = cgi.EntityValue(entity, "mode");
      const cm_entity_t *invert = cgi.EntityValue(entity, "invert");
      if (!(checkpoint->parsed & ENTITY_INTEGER) || checkpoint->integer < 1 ||
          checkpoint->integer > RACE_MAX_CHECKPOINTS) {
        release(barrier.brushes);
        continue;
      }
      barrier.checkpoint = (uint16_t) checkpoint->integer;
      barrier.gate_mode = mode->nullable_string &&
                          !q_strcmp(mode->string, "exact")
        ? RACE_GATE_EXACT : RACE_GATE_AT_LEAST;
      barrier.invert = invert->integer != 0;
    } else {
      vec3_t angles = Vec3_Zero();
      const cm_entity_t *angle = cgi.EntityValue(entity, "angle");
      const cm_entity_t *authoredAngles = cgi.EntityValue(entity, "angles");
      if (authoredAngles->parsed & ENTITY_VEC3) {
        angles = authoredAngles->vec3;
      } else if (angle->parsed & ENTITY_FLOAT) {
        angles.y = angle->value;
      }
      Vec3_Vectors(angles, &barrier.direction, NULL, NULL);
    }
    cg_race_barriers.barriers[cg_race_barriers.count++] = barrier;
  }

  for (size_t i = 0; i < cg_race_barriers.count; i++) {
    for (size_t j = i + 1u; j < cg_race_barriers.count; j++) {
      if (cg_race_barriers.barriers[j].model_index <
          cg_race_barriers.barriers[i].model_index) {
        const cg_race_barrier_t swap = cg_race_barriers.barriers[i];
        cg_race_barriers.barriers[i] = cg_race_barriers.barriers[j];
        cg_race_barriers.barriers[j] = swap;
      }
    }
    cg_race_barriers.barriers[i].id = (uint8_t) i;
  }
  Cg_Debug("Loaded %zu Race conditional brushes\n", cg_race_barriers.count);
}

static const cg_race_barrier_t *Cg_RaceBarriers_ForEntity(
    const cl_entity_t *candidate) {
  if (!candidate || !(candidate->current.effects & EF_RACE_GATE)) {
    return NULL;
  }
  for (size_t i = 0; i < cg_race_barriers.count; i++) {
    if (cg_race_barriers.barriers[i].model_index == candidate->current.model1) {
      return cg_race_barriers.barriers + i;
    }
  }
  return NULL;
}

static bool Cg_RaceBarriers_ShouldClipEntity(const cl_entity_t *candidate,
                                              const vec3_t start,
                                              const vec3_t end,
                                              const box3_t bounds) {
  const cg_race_barrier_t *barrier = Cg_RaceBarriers_ForEntity(candidate);
  if (!barrier) {
    return true;
  }

  const race_clip_barrier_t clipBarrier = {
    .type = barrier->type,
    .gate_mode = barrier->gate_mode,
    .checkpoint = barrier->checkpoint,
    .id = barrier->id,
    .invert = barrier->invert,
    .direction = barrier->direction,
    .abs_bounds = candidate->abs_bounds
  };
  const uint16_t reached = (uint16_t) Maxi(
    cgi.client->frame.ps.stats[STAT_RACE_CHECKPOINT_COUNT], 0);
  return Race_ClipBarrierBlocks(&clipBarrier,
                                &cg_race_barriers.clip_state,
                                reached, start, end, bounds);
}

cm_trace_t Cg_RaceBarriers_TracePrediction(const vec3_t start,
                                           const vec3_t end,
                                           const box3_t bounds) {
  typedef struct {
    entity_state_t *state;
    solid_t solid;
    cl_entity_t *entity;
    const cg_race_barrier_t *barrier;
  } hidden_barrier_t;

  hidden_barrier_t hidden[RACE_MAX_CHECKPOINTS];
  size_t hiddenCount = 0u;
  for (int32_t i = 0; i < cgi.client->frame.num_entities; i++) {
    const uint32_t stateNumber =
      (cgi.client->frame.entity_state + i) & ENTITY_STATE_MASK;
    entity_state_t *state = cgi.client->entity_states + stateNumber;
    cl_entity_t *entity = cgi.client->entities + state->number;
    const cg_race_barrier_t *barrier = Cg_RaceBarriers_ForEntity(entity);
    if (!barrier || state->solid < SOLID_BOX ||
        hiddenCount == lengthof(hidden)) {
      continue;
    }
    hidden[hiddenCount++] = (hidden_barrier_t) {
      .state = state,
      .solid = state->solid,
      .entity = entity,
      .barrier = barrier
    };
    state->solid = SOLID_NOT;
  }

  const int32_t contents = Race_MovementClipMask(CONTENTS_MASK_CLIP_PLAYER);
  cm_trace_t trace = cgi.Trace(start, end, bounds, NULL, contents);
  for (size_t i = 0; i < hiddenCount; i++) {
    hidden[i].state->solid = hidden[i].solid;
  }

  const box3_t absBounds = Box3_Expand(
    Box3(Vec3_Add(Vec3_Minf(start, end), bounds.mins),
         Vec3_Add(Vec3_Maxf(start, end), bounds.maxs)),
    BOX_EPSILON);
  for (size_t i = 0; i < hiddenCount; i++) {
    const hidden_barrier_t *item = hidden + i;
    if (!Box3_Intersects(absBounds, item->entity->abs_bounds) ||
        !Cg_RaceBarriers_ShouldClipEntity(item->entity,
                                          start, end, bounds)) {
      continue;
    }
    const Vector *brushes = item->barrier->brushes;
    const cm_trace_t clipped = Race_ClipBoxToBrushes(
      start, end, bounds,
      (const cm_bsp_brush_t *const *) brushes->elements,
      brushes->count, contents);
    Race_ClipMerge(&trace, &clipped, item->entity);
  }
  return trace;
}

void Cg_RaceBarriers_PreparePredictionCommand(const size_t index) {
  if (index != 0u) {
    return;
  }

  const uint16_t checkpoints = (uint16_t) Maxi(
    cgi.client->frame.ps.stats[STAT_RACE_CHECKPOINT_COUNT], 0);
  if (!cg_race_barriers.prediction_initialized ||
      checkpoints < cg_race_barriers.previous_checkpoint_count ||
      cgi.client->frame.ps.stats[STAT_RACE_RUN_STATE] != RACE_RUN_ACTIVE ||
      (Cg_Self() && Cg_Self()->current.event == EV_CLIENT_TELEPORT)) {
    Race_ClipResetState(&cg_race_barriers.clip_state);
  }
  cg_race_barriers.previous_checkpoint_count = checkpoints;
  cg_race_barriers.prediction_initialized = true;
}

void Cg_RaceBarriers_Draw(void) {
  for (int32_t i = 0; i < cgi.client->frame.num_entities; i++) {
    const uint32_t stateNumber =
      (cgi.client->frame.entity_state + i) & ENTITY_STATE_MASK;
    const entity_state_t *state = cgi.client->entity_states + stateNumber;
    const cl_entity_t *entity = cgi.client->entities + state->number;
    const cg_race_barrier_t *barrier = Cg_RaceBarriers_ForEntity(entity);
    if (!barrier || !barrier->visual) {
      continue;
    }

    bool blocks;
    if (barrier->type == RACE_BARRIER_CHECKPOINT_GATE) {
      const uint16_t reached = (uint16_t) Maxi(
        cgi.client->frame.ps.stats[STAT_RACE_CHECKPOINT_COUNT], 0);
      blocks = !Race_CheckpointGateSatisfied(reached, barrier->checkpoint,
                                              barrier->gate_mode,
                                              barrier->invert);
    } else {
      const uint64_t bit = UINT64_C(1) << barrier->id;
      blocks = !(cg_race_barriers.clip_state.oneway_latches & bit) &&
               !Race_OneWayDirectionAllowed(
                 cgi.client->frame.ps.pm_state.velocity,
                 barrier->direction, RACE_CLIP_ONEWAY_EPSILON);
    }

    if (blocks) {
      cgi.Draw3DBox(entity->abs_bounds, barrier->color, true);
    }
  }
}
