/*
 * Race-owned conditional brush collision shared by GAME and CGAME.
 *
 * This file is deliberately independent of g_entity_t and cl_entity_t so the
 * exact same source can be compiled into GAME authority and CGAME prediction.
 */

#pragma once

#include "collision/cm_types.h"
#include "race_types.h"

#define RACE_CLIP_ONEWAY_EPSILON .001f

typedef struct {
  race_barrier_type_t type;
  race_gate_mode_t gate_mode;
  uint16_t checkpoint;
  uint8_t id;
  bool invert;
  vec3_t direction;
  box3_t abs_bounds;
} race_clip_barrier_t;

typedef struct {
  uint64_t oneway_latches;
} race_clip_state_t;

/**
 * @brief Sweeps `bounds` from `start` to `end` against the supplied convex BSP
 * brushes using Quetoo v1.0.79's exact epsilon and plane-selection rules.
 */
cm_trace_t Race_ClipBoxToBrushes(const vec3_t start, const vec3_t end,
                                 const box3_t bounds,
                                 const cm_bsp_brush_t *const *brushes,
                                 size_t num_brushes, int32_t contents);

/**
 * @brief Merges one conditional-brush result into a stock world/entity trace.
 * GAME and CGAME use this identical rule so start/all-solid behavior cannot
 * diverge at the module boundary.
 */
bool Race_ClipMerge(cm_trace_t *trace, const cm_trace_t *candidate, void *ent);

/**
 * @brief Applies the current Race checkpoint and one-way latch policy.
 * @return `true` when the barrier should participate in this movement trace.
 */
bool Race_ClipBarrierBlocks(const race_clip_barrier_t *barrier,
                            race_clip_state_t *state, uint16_t checkpoints,
                            const vec3_t start, const vec3_t end,
                            const box3_t bounds);

void Race_ClipResetState(race_clip_state_t *state);

bool Race_ClipResolveModelIndex(const char *model,
                                const char catalog[][MAX_STRING_CHARS],
                                size_t catalog_count,
                                uint8_t *model_index);

bool Race_ClipModelIndexUnique(uint8_t model_index,
                               const uint8_t *model_indices,
                               size_t count);
