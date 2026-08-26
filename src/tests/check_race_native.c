/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "cg_score_model.h"
#include "cg_team_mode.h"
#include "race_clip.h"
#include "race_kick_broker.h"
#include "race_projectile_compat.h"

typedef struct {
  cm_bsp_plane_t planes[6];
  cm_bsp_brush_side_t sides[6];
  cm_bsp_brush_t brush;
} race_native_brush_t;

typedef struct {
  vec3_t start;
  vec3_t end;
  box3_t abs_bounds;
  vec3_t offsets[8];
  cm_trace_t trace;
  float unnudged_fraction;
} race_native_stock_trace_t;

static uint32_t race_native_assertions;
static uint32_t race_native_failures;
static char race_native_models[MAX_MODELS][MAX_STRING_CHARS];

uint32_t Race_NativeTestCgameModule(uint32_t *assertion_count);
uint32_t Race_NativeTestPersistence(uint32_t *assertion_count);
uint32_t Race_NativeTestUi(uint32_t *assertion_count);

#define RACE_NATIVE_CHECK(condition, label) do { \
  race_native_assertions++; \
  if (!(condition)) { \
    fprintf(stderr, "FAIL:%s:%d:%s\n", __FILE__, __LINE__, label); \
    race_native_failures++; \
  } \
} while (0)

static uint32_t Race_NativeSignBits(const vec3_t normal) {
  return (normal.x < 0.f ? 1u : 0u) |
         (normal.y < 0.f ? 2u : 0u) |
         (normal.z < 0.f ? 4u : 0u);
}

static void Race_NativeMakeAxisBrush(race_native_brush_t *fixture,
                                     const vec3_t mins,
                                     const vec3_t maxs,
                                     const int32_t contents) {
  memset(fixture, 0, sizeof(*fixture));
  const vec3_t normals[6] = {
    Vec3(1.f, 0.f, 0.f), Vec3(-1.f, 0.f, 0.f),
    Vec3(0.f, 1.f, 0.f), Vec3(0.f, -1.f, 0.f),
    Vec3(0.f, 0.f, 1.f), Vec3(0.f, 0.f, -1.f)
  };
  const float distances[6] = {
    maxs.x, -mins.x, maxs.y, -mins.y, maxs.z, -mins.z
  };
  for (int32_t i = 0; i < 6; i++) {
    fixture->planes[i] = (cm_bsp_plane_t) {
      .normal = normals[i],
      .dist = distances[i],
      .type = i / 2,
      .sign_bits = (int32_t) Race_NativeSignBits(normals[i])
    };
    fixture->sides[i] = (cm_bsp_brush_side_t) {
      .plane = fixture->planes + i,
      .material = (cm_material_t *) (uintptr_t) (0x100u + (uint32_t) i),
      .contents = contents,
      .surface = 0x200 + i,
      .value = i
    };
  }
  fixture->brush = (cm_bsp_brush_t) {
    .contents = contents,
    .brush_sides = fixture->sides,
    .num_brush_sides = 6,
    .bounds = Box3(mins, maxs)
  };
}

static bool Race_NativeFloatEqual(const float a, const float b) {
  uint32_t aa;
  uint32_t bb;
  memcpy(&aa, &a, sizeof(aa));
  memcpy(&bb, &b, sizeof(bb));
  return aa == bb;
}

static bool Race_NativeVecEqual(const vec3_t a, const vec3_t b) {
  return Race_NativeFloatEqual(a.x, b.x) &&
         Race_NativeFloatEqual(a.y, b.y) &&
         Race_NativeFloatEqual(a.z, b.z);
}

static bool Race_NativeTraceEqual(const cm_trace_t *a, const cm_trace_t *b) {
  return a->all_solid == b->all_solid &&
         a->start_solid == b->start_solid &&
         Race_NativeFloatEqual(a->fraction, b->fraction) &&
         Race_NativeVecEqual(a->end, b->end) &&
         a->brush == b->brush &&
         a->brush_side == b->brush_side &&
         Race_NativeVecEqual(a->plane.normal, b->plane.normal) &&
         Race_NativeFloatEqual(a->plane.dist, b->plane.dist) &&
         a->plane.type == b->plane.type &&
         a->plane.sign_bits == b->plane.sign_bits &&
         a->contents == b->contents &&
         a->surface == b->surface &&
         a->material == b->material &&
         a->ent == b->ent;
}

static box3_t Race_NativeStockTraceBounds(const vec3_t start,
                                          const vec3_t end,
                                          const box3_t bounds) {
  return Box3_Expand(Box3(
    Vec3_Add(Vec3_Minf(start, end), bounds.mins),
    Vec3_Add(Vec3_Maxf(start, end), bounds.maxs)), BOX_EPSILON);
}

static void Race_NativeStockTestBox(race_native_stock_trace_t *data,
                                    const cm_bsp_brush_t *brush) {
  if (!brush->num_brush_sides ||
      !Box3_Intersects(data->abs_bounds, brush->bounds)) {
    return;
  }

  for (int32_t i = 0; i < brush->num_brush_sides; i++) {
    const cm_bsp_brush_side_t *side = brush->brush_sides + i;
    const cm_bsp_plane_t plane = *side->plane;
    const float dist = plane.dist -
      Vec3_Dot(data->offsets[plane.sign_bits], plane.normal);
    if (Vec3_Dot(data->start, plane.normal) - dist > 0.f) {
      return;
    }
  }

  data->trace.start_solid = true;
  data->trace.all_solid = true;
  data->trace.brush = brush;
  data->trace.fraction = 0.f;
  data->trace.contents = brush->contents;
}

static void Race_NativeStockTraceBrush(race_native_stock_trace_t *data,
                                       const cm_bsp_brush_t *brush) {
  if (!brush->num_brush_sides ||
      !Box3_Intersects(data->abs_bounds, brush->bounds)) {
    return;
  }

  float enterFraction = -1.f;
  float leaveFraction = 1.f;
  float nudgedEnterFraction = -1.f;
  cm_bsp_plane_t plane = { };
  const cm_bsp_brush_side_t *impactSide = NULL;
  bool startOutside = false;
  bool endOutside = false;

  for (int32_t i = brush->num_brush_sides - 1; i >= 0; i--) {
    const cm_bsp_brush_side_t *side = brush->brush_sides + i;
    const cm_bsp_plane_t candidate = *side->plane;
    const float dist = candidate.dist -
      Vec3_Dot(data->offsets[candidate.sign_bits], candidate.normal);
    const float d1 = Vec3_Dot(data->start, candidate.normal) - dist;
    const float d2 = Vec3_Dot(data->end, candidate.normal) - dist;

    startOutside |= d1 > 0.f;
    endOutside |= d2 > 0.f;
    if (d1 > 0.f && d2 >= d1) {
      return;
    }
    if (d1 <= 0.f && d2 <= d1) {
      continue;
    }

    const float delta = d1 - d2;
    if (d1 > d2) {
      const float fraction = d1 / delta;
      if (fraction > enterFraction) {
        enterFraction = fraction;
        nudgedEnterFraction = (d1 - TRACE_EPSILON) / delta;
        plane = candidate;
        impactSide = side;
      }
    } else {
      const float fraction = d1 / delta;
      if (fraction < leaveFraction) {
        leaveFraction = fraction;
      }
    }
  }

  if (!startOutside) {
    data->trace.start_solid = true;
    if (!endOutside) {
      data->trace.all_solid = true;
      data->trace.brush = brush;
      data->trace.contents = brush->contents;
      data->trace.fraction = 0.f;
      data->unnudged_fraction = 0.f;
    }
  } else if (enterFraction < leaveFraction && enterFraction > -1.f &&
             enterFraction < data->unnudged_fraction &&
             nudgedEnterFraction < data->trace.fraction) {
    data->unnudged_fraction = enterFraction;
    data->trace.fraction = nudgedEnterFraction;
    data->trace.brush = brush;
    data->trace.brush_side = impactSide;
    data->trace.plane = plane;
    data->trace.contents = impactSide->contents;
    data->trace.surface = impactSide->surface;
    data->trace.material = impactSide->material;
  }
}

static cm_trace_t Race_NativeStockClip(
    const vec3_t start, const vec3_t end, const box3_t bounds,
    const cm_bsp_brush_t *const *brushes, const size_t numBrushes,
    const int32_t contents) {
  race_native_stock_trace_t data = {
    .start = start,
    .end = end,
    .abs_bounds = Race_NativeStockTraceBounds(start, end, bounds),
    .trace = { .fraction = 1.f, .end = end },
    .unnudged_fraction = 1.f + TRACE_EPSILON
  };
  Box3_ToPoints(bounds, data.offsets);

  if (!brushes) {
    return data.trace;
  }

  const bool position = Vec3_Equal(start, end);
  for (size_t i = 0; i < numBrushes; i++) {
    const cm_bsp_brush_t *brush = brushes[i];
    if (!brush || !(brush->contents & contents)) {
      continue;
    }
    if (position) {
      Race_NativeStockTestBox(&data, brush);
    } else {
      Race_NativeStockTraceBrush(&data, brush);
    }
    if (data.trace.all_solid) {
      break;
    }
  }

  if (position) {
    data.trace.end = start;
  } else {
    data.trace.fraction = Maxf(0.f, data.trace.fraction);
    data.trace.end = data.trace.fraction == 0.f ? start
      : data.trace.fraction == 1.f ? end
      : Vec3_Mix(start, end, data.trace.fraction);
  }
  return data.trace;
}

static uint32_t Race_NativeRandom(uint32_t *state) {
  uint32_t value = *state;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  *state = value;
  return value;
}

static float Race_NativeRandomCoord(uint32_t *state,
                                    const int32_t magnitude) {
  const int32_t span = magnitude * 8;
  const int32_t value = (int32_t) (
    Race_NativeRandom(state) % (uint32_t) (span * 2 + 1)) - span;
  return (float) value * .125f;
}

static void Race_NativeTestCollision(void) {
  race_native_brush_t fixture;
  Race_NativeMakeAxisBrush(&fixture, Vec3(-8.f, -8.f, -8.f),
                           Vec3(8.f, 8.f, 8.f), CONTENTS_PLAYER_CLIP);
  const cm_bsp_brush_t *brushes[] = { &fixture.brush };
  const box3_t player = Box3(Vec3(-16.f, -16.f, -24.f),
                             Vec3(16.f, 16.f, 32.f));

  cm_trace_t trace = Race_ClipBoxToBrushes(
    Vec3(-64.f, 0.f, 0.f), Vec3(64.f, 0.f, 0.f), player,
    brushes, 1u, CONTENTS_PLAYER_CLIP);
  RACE_NATIVE_CHECK(
    Race_NativeFloatEqual(trace.fraction, (40.f - TRACE_EPSILON) / 128.f),
    "analytic fraction");
  RACE_NATIVE_CHECK(Race_NativeVecEqual(trace.plane.normal,
                                         Vec3(-1.f, 0.f, 0.f)),
                    "analytic plane");
  RACE_NATIVE_CHECK(Race_NativeFloatEqual(trace.plane.dist, 8.f),
                    "analytic plane distance");
  RACE_NATIVE_CHECK(!trace.start_solid && !trace.all_solid,
                    "analytic solid flags");
  RACE_NATIVE_CHECK(Race_NativeVecEqual(
                      trace.end, Vec3_Mix(Vec3(-64.f, 0.f, 0.f),
                                          Vec3(64.f, 0.f, 0.f),
                                          trace.fraction)),
                    "analytic end point");

  trace = Race_ClipBoxToBrushes(Vec3_Zero(), Vec3(64.f, 0.f, 0.f),
                                Box3_Zero(), brushes, 1u,
                                CONTENTS_PLAYER_CLIP);
  RACE_NATIVE_CHECK(trace.start_solid && !trace.all_solid &&
                    trace.fraction == 1.f, "start solid exit");
  trace = Race_ClipBoxToBrushes(Vec3_Zero(), Vec3(4.f, 0.f, 0.f),
                                Box3_Zero(), brushes, 1u,
                                CONTENTS_PLAYER_CLIP);
  RACE_NATIVE_CHECK(trace.start_solid && trace.all_solid &&
                    trace.fraction == 0.f &&
                    Race_NativeVecEqual(trace.end, Vec3_Zero()),
                    "all solid move");
  trace = Race_ClipBoxToBrushes(Vec3_Zero(), Vec3_Zero(), Box3_Zero(),
                                brushes, 1u, CONTENTS_PLAYER_CLIP);
  RACE_NATIVE_CHECK(trace.start_solid && trace.all_solid &&
                    trace.fraction == 0.f, "stationary all solid");

  cm_trace_t merged = { .fraction = 1.f };
  void *token = (void *) (uintptr_t) 0x1234u;
  RACE_NATIVE_CHECK(Race_ClipMerge(&merged, &trace, token) &&
                    merged.ent == token, "trace merge");

  race_clip_barrier_t gate = {
    .type = RACE_BARRIER_CHECKPOINT_GATE,
    .gate_mode = RACE_GATE_AT_LEAST,
    .checkpoint = 3u
  };
  RACE_NATIVE_CHECK(Race_ClipBarrierBlocks(&gate, NULL, 2u, Vec3_Zero(),
                                            Vec3_Zero(), Box3_Zero()),
                    "at least gate before");
  RACE_NATIVE_CHECK(!Race_ClipBarrierBlocks(&gate, NULL, 3u, Vec3_Zero(),
                                             Vec3_Zero(), Box3_Zero()),
                    "at least gate equal");
  gate.gate_mode = RACE_GATE_EXACT;
  RACE_NATIVE_CHECK(Race_ClipBarrierBlocks(&gate, NULL, 4u, Vec3_Zero(),
                                            Vec3_Zero(), Box3_Zero()),
                    "exact gate after");
  gate.invert = true;
  RACE_NATIVE_CHECK(Race_ClipBarrierBlocks(&gate, NULL, 3u, Vec3_Zero(),
                                            Vec3_Zero(), Box3_Zero()),
                    "gate inversion");

  race_clip_barrier_t wall = {
    .type = RACE_BARRIER_ONEWAY_WALL,
    .id = 5u,
    .direction = Vec3(1.f, 0.f, 0.f),
    .abs_bounds = Box3(Vec3(-8.f, -8.f, -8.f), Vec3(8.f, 8.f, 8.f))
  };
  race_clip_state_t state = { };
  const uint64_t bit = UINT64_C(1) << wall.id;
  RACE_NATIVE_CHECK(!Race_ClipBarrierBlocks(
                      &wall, &state, 0u, Vec3(-20.f, 0.f, 0.f),
                      Vec3_Zero(), Box3_Zero()) &&
                    (state.oneway_latches & bit), "oneway allowed and latched");
  RACE_NATIVE_CHECK(!Race_ClipBarrierBlocks(
                      &wall, &state, 0u, Vec3_Zero(),
                      Vec3(-20.f, 0.f, 0.f), Box3_Zero()) &&
                    (state.oneway_latches & bit), "oneway latch hold");
  RACE_NATIVE_CHECK(Race_ClipBarrierBlocks(
                      &wall, &state, 0u, Vec3(20.f, 0.f, 0.f),
                      Vec3_Zero(), Box3_Zero()) &&
                    !(state.oneway_latches & bit), "oneway latch clear");
  RACE_NATIVE_CHECK(Race_ClipBarrierBlocks(
                      &wall, &state, 0u, Vec3_Zero(),
                      Vec3(RACE_CLIP_ONEWAY_EPSILON, 0.f, 0.f), Box3_Zero()),
                    "oneway epsilon strict");
  state.oneway_latches = bit;
  Race_ClipResetState(&state);
  RACE_NATIVE_CHECK(state.oneway_latches == 0u, "oneway reset");

  uint32_t random = UINT32_C(0x9e3779b9);
  for (uint32_t iteration = 0; iteration < 100000u; iteration++) {
    race_native_brush_t fixtures[3];
    const cm_bsp_brush_t *randomBrushes[3];
    for (size_t i = 0; i < 3u; i++) {
      const vec3_t center = Vec3(Race_NativeRandomCoord(&random, 48),
                                 Race_NativeRandomCoord(&random, 48),
                                 Race_NativeRandomCoord(&random, 48));
      const vec3_t half = Vec3(
        1.f + (Race_NativeRandom(&random) % 128u) * .125f,
        1.f + (Race_NativeRandom(&random) % 128u) * .125f,
        1.f + (Race_NativeRandom(&random) % 128u) * .125f);
      Race_NativeMakeAxisBrush(fixtures + i, Vec3_Subtract(center, half),
                               Vec3_Add(center, half), CONTENTS_PLAYER_CLIP);
      randomBrushes[i] = &fixtures[i].brush;
    }

    const vec3_t start = Vec3(Race_NativeRandomCoord(&random, 96),
                              Race_NativeRandomCoord(&random, 96),
                              Race_NativeRandomCoord(&random, 96));
    const vec3_t end = iteration % 23u == 0u ? start :
      Vec3(Race_NativeRandomCoord(&random, 96),
           Race_NativeRandomCoord(&random, 96),
           Race_NativeRandomCoord(&random, 96));
    const box3_t unordered = Box3(
      Vec3(-Race_NativeRandomCoord(&random, 16) * .25f,
           -Race_NativeRandomCoord(&random, 16) * .25f,
           -Race_NativeRandomCoord(&random, 24) * .25f),
      Vec3(Race_NativeRandomCoord(&random, 16) * .25f,
           Race_NativeRandomCoord(&random, 16) * .25f,
           Race_NativeRandomCoord(&random, 24) * .25f));
    const box3_t bounds = Box3(Vec3_Minf(unordered.mins, unordered.maxs),
                               Vec3_Maxf(unordered.mins, unordered.maxs));
    const cm_trace_t actual = Race_ClipBoxToBrushes(
      start, end, bounds, randomBrushes, 3u, CONTENTS_PLAYER_CLIP);
    const cm_trace_t expected = Race_NativeStockClip(
      start, end, bounds, randomBrushes, 3u, CONTENTS_PLAYER_CLIP);
    if (!Race_NativeTraceEqual(&actual, &expected)) {
      fprintf(stderr, "FAIL:differential trace iteration=%" PRIu32 "\n",
              iteration);
      race_native_failures++;
      break;
    }
  }
  race_native_assertions += 100000u;
}

static void Race_NativeTestModelIdentity(void) {
  memset(race_native_models, 0, sizeof(race_native_models));
  strcpy(race_native_models[7], "*1");
  strcpy(race_native_models[31], "*31");
  strcpy(race_native_models[255], "*255");

  uint8_t modelIndex = 0u;
  RACE_NATIVE_CHECK(Race_ClipResolveModelIndex(
                      "*1", race_native_models, MAX_MODELS, &modelIndex) &&
                    modelIndex == 7u, "reordered model catalog");
  RACE_NATIVE_CHECK(Race_ClipResolveModelIndex(
                      "*31", race_native_models, MAX_MODELS, &modelIndex) &&
                    modelIndex == 31u, "game cgame catalog agreement");
  RACE_NATIVE_CHECK(Race_ClipResolveModelIndex(
                      "*255", race_native_models, MAX_MODELS, &modelIndex) &&
                    modelIndex == 255u, "model identity 255");

  const char *invalid[] = {
    "*", "*0", "*1junk", "*+1", "*-1", "*256", "*257",
    "*9999999999999999999999999999999999999999", "1", ""
  };
  for (size_t i = 0; i < lengthof(invalid); i++) {
    RACE_NATIVE_CHECK(!Race_ClipResolveModelIndex(
                        invalid[i], race_native_models, MAX_MODELS,
                        &modelIndex), "malformed model identity");
  }
  RACE_NATIVE_CHECK(!Race_ClipResolveModelIndex(
                      "*2", race_native_models, MAX_MODELS, &modelIndex),
                    "missing model configstring");
  RACE_NATIVE_CHECK(!Race_ClipResolveModelIndex(
                      "*1", race_native_models, 7u, &modelIndex),
                    "catalog bound");
  strcpy(race_native_models[8], "*1");
  RACE_NATIVE_CHECK(!Race_ClipResolveModelIndex(
                      "*1", race_native_models, MAX_MODELS, &modelIndex),
                    "duplicate catalog identity");

  const uint8_t accepted[] = { 7u, 31u };
  RACE_NATIVE_CHECK(Race_ClipModelIndexUnique(255u, accepted,
                                              lengthof(accepted)),
                    "unique barrier model");
  RACE_NATIVE_CHECK(!Race_ClipModelIndexUnique(31u, accepted,
                                               lengthof(accepted)),
                    "duplicate barrier model");
  RACE_NATIVE_CHECK(!Race_ClipModelIndexUnique(0u, accepted,
                                               lengthof(accepted)),
                    "zero barrier model");
}

static void Race_NativeTestKickBroker(void) {
  uint64_t counter = 0u;
  uint64_t firstIdentity = 0u;
  race_kick_ticket_t first = { };
  RACE_NATIVE_CHECK(Race_KickBrokerCapture(
                      7u, 64u, true, &firstIdentity, &counter, &first),
                    "kick capture");
  RACE_NATIVE_CHECK(first.slot == 7u && first.connection_id == 1u,
                    "kick ticket identity");
  RACE_NATIVE_CHECK(Race_KickBrokerValidate(
                      first, 64u, true, firstIdentity) ==
                    RACE_KICK_BROKER_EXECUTE, "kick valid ticket");

  char command[128];
  RACE_NATIVE_CHECK(Race_KickBrokerFormatCommit(
                      command, sizeof(command), "race_kick_commit", first) &&
                    !strcmp(command, "race_kick_commit 7 1\n"),
                    "kick commit format");
  RACE_NATIVE_CHECK(Race_KickBrokerFormatStockKick(
                      command, sizeof(command), first) &&
                    !strcmp(command, "kick 7\n"), "stock kick format");

  uint64_t replacementIdentity = 0u;
  race_kick_ticket_t replacement = { };
  RACE_NATIVE_CHECK(Race_KickBrokerCapture(
                      7u, 64u, true, &replacementIdentity, &counter,
                      &replacement) && replacement.connection_id == 2u,
                    "same slot replacement identity");
  RACE_NATIVE_CHECK(Race_KickBrokerValidate(
                      first, 64u, true, replacementIdentity) ==
                    RACE_KICK_BROKER_STALE, "stale same slot ticket");
  RACE_NATIVE_CHECK(Race_KickBrokerValidate(
                      first, 64u, false, firstIdentity) ==
                    RACE_KICK_BROKER_STALE, "disconnected ticket");
  RACE_NATIVE_CHECK(Race_KickBrokerValidate(
                      (race_kick_ticket_t) { .slot = 64u,
                                             .connection_id = 1u },
                      64u, true, 1u) == RACE_KICK_BROKER_INVALID,
                    "kick numeric bound");
  RACE_NATIVE_CHECK(Race_KickBrokerValidate(
                      (race_kick_ticket_t) { .slot = 1u },
                      64u, true, 1u) == RACE_KICK_BROKER_INVALID,
                    "kick malformed ticket");
  char tiny[4];
  RACE_NATIVE_CHECK(!Race_KickBrokerFormatCommit(
                      tiny, sizeof(tiny), "race_kick_commit", first) &&
                    !Race_KickBrokerFormatStockKick(
                      tiny, sizeof(tiny), first), "kick truncated commands");

  counter = UINT64_MAX;
  RACE_NATIVE_CHECK(Race_KickBrokerNextConnectionId(&counter) == 0u &&
                    counter == UINT64_MAX, "kick counter exhaustion");
  uint64_t exhaustedIdentity = 0u;
  race_kick_ticket_t exhausted = { };
  RACE_NATIVE_CHECK(!Race_KickBrokerCapture(
                      8u, 64u, true, &exhaustedIdentity, &counter,
                      &exhausted), "kick identity reuse rejected");
}

static uint32_t race_native_current_observations;
static uint32_t race_native_previous_observations;
static g_projectile_observation_t race_native_last_observation;

static void Race_NativeCurrentObserver(
    const g_projectile_observation_t *observation) {
  race_native_current_observations++;
  race_native_last_observation = *observation;
}

static void Race_NativePreviousObserver(
    const g_projectile_observation_t *observation) {
  (void) observation;
  race_native_previous_observations++;
}

static void Race_NativeTestProjectileObserver(void) {
  g_entity_t owner = { };
  g_entity_t projectile = {
    .owner = &owner,
    .velocity = Vec3(4.f, 5.f, 6.f)
  };
  projectile.s.origin = Vec3(1.f, 2.f, 3.f);

  G_SetProjectileObserver(Race_NativePreviousObserver);
  g_projectile_observer_lifecycle_t lifecycle = { };
  RACE_NATIVE_CHECK(G_InstallProjectileObserver(
                      &lifecycle, Race_NativeCurrentObserver) &&
                    lifecycle.installed &&
                    lifecycle.previous == Race_NativePreviousObserver,
                    "projectile observer install");
  RACE_NATIVE_CHECK(G_InstallProjectileObserver(
                      &lifecycle, Race_NativeCurrentObserver) &&
                    lifecycle.previous == Race_NativePreviousObserver,
                    "projectile observer idempotent install");

  G_ObserveProjectile(&projectile, G_PROJECTILE_ROCKET,
                      G_PROJECTILE_SPAWN, Vec3_Zero());
  RACE_NATIVE_CHECK(race_native_current_observations == 1u &&
                    race_native_previous_observations == 0u &&
                    race_native_last_observation.owner == &owner &&
                    race_native_last_observation.projectile == &projectile &&
                    Race_NativeVecEqual(race_native_last_observation.origin,
                                        projectile.s.origin),
                    "projectile spawn observation");
  G_ObserveProjectile(&projectile, G_PROJECTILE_ROCKET,
                      G_PROJECTILE_IMPACT, Vec3(0.f, 0.f, 1.f));
  RACE_NATIVE_CHECK(race_native_current_observations == 2u &&
                    race_native_last_observation.operation ==
                      G_PROJECTILE_IMPACT,
                    "projectile impact observation");
  G_ObserveProjectile(&projectile, G_PROJECTILE_HYPERBLASTER,
                      G_PROJECTILE_SILENT_DESPAWN, Vec3_Zero());
  RACE_NATIVE_CHECK(race_native_current_observations == 3u &&
                    race_native_last_observation.kind ==
                      G_PROJECTILE_HYPERBLASTER &&
                    race_native_last_observation.operation ==
                      G_PROJECTILE_SILENT_DESPAWN,
                    "projectile silent despawn observation");
  G_ObserveProjectile(NULL, G_PROJECTILE_ROCKET,
                      G_PROJECTILE_SPAWN, Vec3_Zero());
  RACE_NATIVE_CHECK(race_native_current_observations == 3u,
                    "projectile null does not duplicate");

  G_RestoreProjectileObserver(&lifecycle);
  G_RestoreProjectileObserver(&lifecycle);
  G_ObserveProjectile(&projectile, G_PROJECTILE_ROCKET,
                      G_PROJECTILE_SPAWN, Vec3_Zero());
  RACE_NATIVE_CHECK(!lifecycle.installed && !lifecycle.previous &&
                    race_native_current_observations == 3u &&
                    race_native_previous_observations == 1u,
                    "projectile observer restore");

  RACE_NATIVE_CHECK(G_InstallProjectileObserver(
                      &lifecycle, Race_NativeCurrentObserver),
                    "projectile observer reinitialize");
  G_ObserveProjectile(&projectile, G_PROJECTILE_ROCKET,
                      G_PROJECTILE_SPAWN, Vec3_Zero());
  RACE_NATIVE_CHECK(race_native_current_observations == 4u,
                    "projectile reinitialize observation");
  G_RestoreProjectileObserver(&lifecycle);
  G_SetProjectileObserver(NULL);
}

static void Race_NativeTestTeamModes(void) {
  size_t count = 0u;
  const cg_team_mode_t *modes = Cg_TeamModes(&count);
  RACE_NATIVE_CHECK(modes && count == 2u, "team mode catalog count");
  RACE_NATIVE_CHECK(Cg_TeamModes(NULL) == modes,
                    "team mode optional count");
  RACE_NATIVE_CHECK(Cg_TeamMode(0u) == modes &&
                    !strcmp(modes[0].name, "Free for all") &&
                    !strcmp(modes[0].cvars[0].var, "g_teams") &&
                    !strcmp(modes[0].cvars[0].value, "0") &&
                    !modes[0].cvars[1].var,
                    "team mode free for all");
  RACE_NATIVE_CHECK(Cg_TeamMode(1u) == modes + 1u &&
                    !strcmp(modes[1].name, "Team deathmatch") &&
                    !strcmp(modes[1].cvars[0].var, "g_teams") &&
                    !strcmp(modes[1].cvars[0].value, "1") &&
                    !modes[1].cvars[1].var,
                    "team mode team deathmatch");
  RACE_NATIVE_CHECK(!Cg_TeamMode(2u) && !Cg_TeamMode(SIZE_MAX),
                    "team mode bounds");
}

static void Race_NativeTestScoreModel(void) {
  cg_score_model_t model = { };
  const g_score_t first[] = {
    { .client = 4u, .race_mode = RACE_MODE_PRACTICE },
    { .client = 2u, .race_mode = RACE_MODE_RACE }
  };
  const g_score_t second[] = {
    { .client = 1u, .race_mode = RACE_MODE_SPECTATOR,
      .flags = SCORE_SPECTATOR }
  };

  RACE_NATIVE_CHECK(Cg_ScoreModelApply(
                      &model, 0, first, (int32_t) lengthof(first), false) ==
                    CG_SCORE_MODEL_PARTIAL && model.assembling &&
                    model.num_scores == 0u && model.pending_scores == 2u,
                    "score partial snapshot hidden");
  RACE_NATIVE_CHECK(Cg_ScoreModelApply(
                      &model, 2, second, (int32_t) lengthof(second), true) ==
                    CG_SCORE_MODEL_COMPLETE && !model.assembling &&
                    model.num_scores == 3u,
                    "score snapshot completion");
  RACE_NATIVE_CHECK(model.scores[0].client == 2u &&
                    model.scores[1].client == 4u &&
                    model.scores[2].client == 1u,
                    "score group ordering");

  const cg_score_model_t complete = model;
  RACE_NATIVE_CHECK(!Cg_ScoreModelRangeValid(-1, 1) &&
                    !Cg_ScoreModelRangeValid(0, -1) &&
                    !Cg_ScoreModelRangeValid(CG_SCORE_MODEL_CAPACITY, 1) &&
                    Cg_ScoreModelRangeValid(CG_SCORE_MODEL_CAPACITY, 0),
                    "score numeric bounds");
  RACE_NATIVE_CHECK(Cg_ScoreModelApply(
                      &model, 4, second, 1, true) == CG_SCORE_MODEL_INVALID &&
                    model.num_scores == complete.num_scores,
                    "score malformed continuation rejected");
  RACE_NATIVE_CHECK(Cg_ScoreModelApply(
                      &model, 0, NULL, 1, true) == CG_SCORE_MODEL_INVALID &&
                    model.num_scores == complete.num_scores,
                    "score missing payload rejected");

  const g_score_t bounded[] = {
    { .client = 3u, .race_mode = RACE_MODE_RACE },
    { .client = 1u, .race_mode = RACE_MODE_RACE }
  };
  RACE_NATIVE_CHECK(Cg_ScoreModelApply(
                      &model, 0, bounded, 2, true) ==
                    CG_SCORE_MODEL_COMPLETE && model.num_scores == 2u &&
                    model.scores[0].client == 1u &&
                    model.scores[1].client == 3u,
                    "score complete replacement and client ordering");
  RACE_NATIVE_CHECK(!Cg_ScoreOverlayVisible(true, true) &&
                    !Cg_ScoreOverlayVisible(false, false) &&
                    Cg_ScoreOverlayVisible(false, true),
                    "score presentation state");
}

#undef main
int main(void) {
  Race_NativeTestCollision();
  Race_NativeTestModelIdentity();
  Race_NativeTestKickBroker();
  Race_NativeTestProjectileObserver();
  Race_NativeTestTeamModes();
  Race_NativeTestScoreModel();
  uint32_t moduleAssertions = 0u;
  race_native_failures += Race_NativeTestCgameModule(&moduleAssertions);
  race_native_assertions += moduleAssertions;
  uint32_t persistenceAssertions = 0u;
  race_native_failures += Race_NativeTestPersistence(&persistenceAssertions);
  race_native_assertions += persistenceAssertions;
  uint32_t uiAssertions = 0u;
  race_native_failures += Race_NativeTestUi(&uiAssertions);
  race_native_assertions += uiAssertions;

  if (race_native_failures) {
    fprintf(stderr, "RACE_NATIVE_TEST FAILED assertions=%" PRIu32
                    " failures=%" PRIu32 "\n",
            race_native_assertions, race_native_failures);
    return 1;
  }
  printf("RACE_NATIVE_TEST PASS assertions=%" PRIu32
         " differential=100000\n", race_native_assertions);
  return 0;
}
