/*
 * Race-owned conditional brush collision.
 *
 * The sweep below is a module-owned transcription of the bounded portion of
 * Quetoo Cm_BoxTrace / Cm_TraceToBrush_ at the pinned source commit
 * 3fd1e9253284ad7f5431de17fe503475a9af704b. It intentionally preserves the
 * upstream operation order because trace fractions are prediction data.
 */

#include "race_clip.h"

#include <string.h>

bool Race_ClipResolveModelIndex(const char *model,
                                const char catalog[][MAX_STRING_CHARS],
                                const size_t catalog_count,
                                uint8_t *model_index) {
  if (!model || model[0] != '*' || model[1] < '1' || model[1] > '9' ||
      !catalog || !model_index) {
    return false;
  }

  uint32_t inlineModel = 0u;
  for (const char *digit = model + 1; *digit; digit++) {
    if (*digit < '0' || *digit > '9') {
      return false;
    }
    const uint32_t value = (uint32_t) (*digit - '0');
    if (inlineModel > (UINT8_MAX - value) / 10u) {
      return false;
    }
    inlineModel = inlineModel * 10u + value;
  }

  size_t resolved = 0u;
  const size_t count = Minz(catalog_count, MAX_MODELS);
  for (size_t i = 1u; i < count; i++) {
    if (!strcmp(catalog[i], model)) {
      if (resolved) {
        return false;
      }
      resolved = i;
    }
  }
  if (!resolved || resolved > UINT8_MAX) {
    return false;
  }

  *model_index = (uint8_t) resolved;
  return true;
}

bool Race_ClipModelIndexUnique(const uint8_t model_index,
                               const uint8_t *model_indices,
                               const size_t count) {
  if (!model_index || (!model_indices && count)) {
    return false;
  }
  for (size_t i = 0u; i < count; i++) {
    if (model_indices[i] == model_index) {
      return false;
    }
  }
  return true;
}

typedef struct {
  vec3_t start;
  vec3_t end;
  box3_t abs_bounds;
  vec3_t offsets[8];
  cm_trace_t trace;
  float unnudged_fraction;
} race_clip_data_t;

static box3_t Race_ClipTraceBounds(const vec3_t start, const vec3_t end,
                                   const box3_t bounds) {
  const box3_t swept = Box3(
    Vec3_Add(Vec3_Minf(start, end), bounds.mins),
    Vec3_Add(Vec3_Maxf(start, end), bounds.maxs)
  );
  return Box3_Expand(swept, BOX_EPSILON);
}

static void Race_ClipTestBoxInBrush(race_clip_data_t *data,
                                    const cm_bsp_brush_t *brush) {
  if (!brush->num_brush_sides ||
      !Box3_Intersects(data->abs_bounds, brush->bounds)) {
    return;
  }

  const cm_bsp_brush_side_t *side = brush->brush_sides;
  for (int32_t i = 0; i < brush->num_brush_sides; i++, side++) {
    const cm_bsp_plane_t plane = *side->plane;
    const float dist = plane.dist -
      Vec3_Dot(data->offsets[plane.sign_bits], plane.normal);
    const float d1 = Vec3_Dot(data->start, plane.normal) - dist;
    if (d1 > 0.f) {
      return;
    }
  }

  data->trace.start_solid = true;
  data->trace.all_solid = true;
  data->trace.brush = brush;
  data->trace.fraction = 0.f;
  data->trace.contents = brush->contents;
}

static void Race_ClipTraceToBrush(race_clip_data_t *data,
                                  const cm_bsp_brush_t *brush) {
  if (!brush->num_brush_sides ||
      !Box3_Intersects(data->abs_bounds, brush->bounds)) {
    return;
  }

  float enter_fraction = -1.f;
  float leave_fraction = 1.f;
  float nudged_enter_fraction = -1.f;
  cm_bsp_plane_t plane = { };
  const cm_bsp_brush_side_t *impact_side = NULL;
  bool start_outside = false;
  bool end_outside = false;

  const cm_bsp_brush_side_t *side =
    brush->brush_sides + brush->num_brush_sides - 1;
  for (int32_t i = brush->num_brush_sides - 1; i >= 0; i--, side--) {
    const cm_bsp_plane_t p = *side->plane;
    const float dist = p.dist -
      Vec3_Dot(data->offsets[p.sign_bits], p.normal);
    const float d1 = Vec3_Dot(data->start, p.normal) - dist;
    const float d2 = Vec3_Dot(data->end, p.normal) - dist;

    if (d1 > 0.f) {
      start_outside = true;
    }
    if (d2 > 0.f) {
      end_outside = true;
    }

    if (d1 > 0.f && d2 >= d1) {
      return;
    }
    if (d1 <= 0.f && d2 <= d1) {
      continue;
    }

    const float d2d1_dist = d1 - d2;
    if (d1 > d2) {
      const float fraction = d1 / d2d1_dist;
      if (fraction > enter_fraction) {
        enter_fraction = fraction;
        plane = p;
        impact_side = side;
        nudged_enter_fraction = (d1 - TRACE_EPSILON) / d2d1_dist;
      }
    } else {
      const float fraction = d1 / d2d1_dist;
      if (fraction < leave_fraction) {
        leave_fraction = fraction;
      }
    }
  }

  if (!start_outside) {
    data->trace.start_solid = true;
    if (!end_outside) {
      data->trace.all_solid = true;
      data->trace.brush = brush;
      data->trace.contents = brush->contents;
      data->trace.fraction = 0.f;
      data->unnudged_fraction = 0.f;
    }
  } else if (enter_fraction < leave_fraction) {
    if (enter_fraction > -1.f &&
        enter_fraction < data->unnudged_fraction &&
        nudged_enter_fraction < data->trace.fraction) {
      data->unnudged_fraction = enter_fraction;
      data->trace.fraction = nudged_enter_fraction;
      data->trace.brush = brush;
      data->trace.brush_side = impact_side;
      data->trace.plane = plane;
      data->trace.contents = impact_side->contents;
      data->trace.surface = impact_side->surface;
      data->trace.material = impact_side->material;
    }
  }
}

cm_trace_t Race_ClipBoxToBrushes(const vec3_t start, const vec3_t end,
                                 const box3_t bounds,
                                 const cm_bsp_brush_t *const *brushes,
                                 const size_t num_brushes,
                                 const int32_t contents) {
  race_clip_data_t data = {
    .start = start,
    .end = end,
    .abs_bounds = Race_ClipTraceBounds(start, end, bounds),
    .trace = {
      .fraction = 1.f,
      .end = end
    },
    .unnudged_fraction = 1.f + TRACE_EPSILON
  };
  Box3_ToPoints(bounds, data.offsets);

  if (!brushes) {
    return data.trace;
  }

  if (Vec3_Equal(start, end)) {
    for (size_t i = 0; i < num_brushes; i++) {
      const cm_bsp_brush_t *brush = brushes[i];
      if (brush && (brush->contents & contents)) {
        Race_ClipTestBoxInBrush(&data, brush);
        if (data.trace.all_solid) {
          break;
        }
      }
    }
    data.trace.end = start;
    return data.trace;
  }

  for (size_t i = 0; i < num_brushes; i++) {
    const cm_bsp_brush_t *brush = brushes[i];
    if (brush && (brush->contents & contents)) {
      Race_ClipTraceToBrush(&data, brush);
      if (data.trace.all_solid) {
        break;
      }
    }
  }

  data.trace.fraction = Maxf(0.f, data.trace.fraction);
  if (data.trace.fraction == 0.f) {
    data.trace.end = start;
  } else if (data.trace.fraction == 1.f) {
    data.trace.end = end;
  } else {
    data.trace.end = Vec3_Mix(start, end, data.trace.fraction);
  }
  return data.trace;
}

bool Race_ClipMerge(cm_trace_t *trace, const cm_trace_t *candidate, void *ent) {
  if (!trace || !candidate || trace->all_solid) {
    return false;
  }

  if (candidate->all_solid || candidate->start_solid ||
      candidate->fraction < trace->fraction) {
    *trace = *candidate;
    trace->ent = ent;
    return true;
  }
  return false;
}

bool Race_ClipBarrierBlocks(const race_clip_barrier_t *barrier,
                            race_clip_state_t *state,
                            const uint16_t checkpoints,
                            const vec3_t start, const vec3_t end,
                            const box3_t bounds) {
  if (!barrier || barrier->type == RACE_BARRIER_NONE) {
    return false;
  }

  if (barrier->type == RACE_BARRIER_CHECKPOINT_GATE) {
    return !Race_CheckpointGateSatisfied(checkpoints, barrier->checkpoint,
                                         barrier->gate_mode, barrier->invert);
  }

  if (!state || barrier->id >= RACE_MAX_CHECKPOINTS) {
    return true;
  }

  const uint64_t bit = UINT64_C(1) << barrier->id;
  if (state->oneway_latches & bit) {
    if (Box3_Intersects(Box3_Translate(bounds, start),
                        barrier->abs_bounds)) {
      return false;
    }
    state->oneway_latches &= ~bit;
  }

  if (Race_OneWayDirectionAllowed(Vec3_Subtract(end, start),
                                  barrier->direction,
                                  RACE_CLIP_ONEWAY_EPSILON)) {
    state->oneway_latches |= bit;
    return false;
  }
  return true;
}

void Race_ClipResetState(race_clip_state_t *state) {
  if (state) {
    state->oneway_latches = 0u;
  }
}
