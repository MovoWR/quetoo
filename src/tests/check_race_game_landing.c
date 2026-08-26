/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <check.h>
#include <stdarg.h>
#include <string.h>

#include "g_local.h"
#include "race_physics.h"

box3_t Pm_PlayerBounds(bool ducked);

/*
 * Keep the production consumers in this test translation unit so their static
 * entry points are exercised without adding test seams to production code.
 */
#include "game/race/g_client.c"
#include "game/race/g_ai_main.c"
#include "game/race/race_trigger.c"
#include "game/race/race_logic.c"

g_import_t gi;
g_export_t ge;
g_level_t g_level;
cvar_t *g_fall_damage;
cvar_t *sv_max_entities;
cvar_t *sv_max_clients;
g_item_t *g_items;

bool G_Ai_CanPickup(const g_client_t *cl, const g_entity_t *other) {
  (void) cl;
  (void) other;
  return false;
}

void G_Ai_SetEntityGoal(const g_client_t *cl, ai_goal_t *goal,
                        const float priority, const g_entity_t *entity) {
  (void) cl;
  (void) priority;
  memset(goal, 0, sizeof(*goal));
  goal->type = AI_GOAL_ENTITY;
  goal->entity.ent = entity;
}

bool G_Ai_GoalHasEntity(const ai_goal_t *goal, const g_entity_t *ent) {
  return goal && goal->type == AI_GOAL_ENTITY && goal->entity.ent == ent;
}

void G_Ai_CopyGoal(const ai_goal_t *from, ai_goal_t *to) {
  *to = *from;
}

bool G_OnSameTeam(const g_client_t *a, const g_client_t *b) {
  (void) a;
  (void) b;
  return false;
}

static bool race_game_ai_mutate_latches;
static size_t race_game_ai_movement_traces;

bool G_Module_ShouldClipMovementEntity(g_entity_t *mover,
                                       const g_entity_t *candidate,
                                       const vec3_t start, const vec3_t end,
                                       const box3_t bounds) {
  (void) mover;
  (void) candidate;
  (void) start;
  (void) end;
  (void) bounds;
  return true;
}

cm_trace_t G_Module_TraceMovement(g_entity_t *mover, const vec3_t start,
                                 const vec3_t end, const box3_t bounds,
                                 const int32_t contents) {
  if (race_game_ai_mutate_latches && mover && mover->client) {
    mover->client->race_oneway_latches ^= UINT64_C(0x8000000000000001);
    race_game_ai_movement_traces++;
  }
  return gi.Trace(start, end, bounds, mover, contents);
}

char *vtos(const vec3_t value) {
  (void) value;
  return "";
}

void G_Ai_ClearGoal(ai_goal_t *goal) {
  memset(goal, 0, sizeof(*goal));
}

void G_Ai_SetPathGoal(const g_client_t *cl, ai_goal_t *goal,
                      const float priority, Vector *path,
                      const g_entity_t *path_target) {
  (void) cl;
  (void) goal;
  (void) priority;
  (void) path;
  (void) path_target;
}

char *va(const char *format, ...) {
  static char string[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(string, sizeof(string), format, args);
  va_end(args);
  return string;
}

void G_SetMoveDir(g_entity_t *ent) {
  (void) ent;
}

static size_t race_game_free_calls;

void G_FreeEntity(g_entity_t *ent) {
  race_game_free_calls++;
  ent->in_use = false;
}

void G_UseTargets(g_entity_t *ent, g_entity_t *activator) {
  (void) ent;
  (void) activator;
}

bool Race_Start(g_client_t *cl) {
  (void) cl;
  return false;
}

void Race_Reset(g_client_t *cl) {
  (void) cl;
}

bool Race_Finish(g_client_t *cl) {
  (void) cl;
  return false;
}

static g_entity_t race_game_landing_floor;
static pm_params_t race_game_landing_params;
static size_t race_game_landing_damage_calls;
static g_damage_t race_game_landing_damage;
static size_t race_game_landing_animation_calls;
static entity_animation_t race_game_landing_animation;
static size_t race_game_landing_link_calls;
static size_t race_game_landing_occupy_calls;
static size_t race_game_bounds_box_entities_calls;
static box3_t race_game_bounds_box_entities_bounds;
static uint32_t race_game_bounds_box_entities_type;
static g_entity_t race_game_ai_mover;
static size_t race_game_ai_trace_calls;
static box3_t race_game_ai_trace_bounds[2];
static bool race_game_ai_force_narrow;
static cm_entity_t race_game_start_mode;
static uint32_t race_game_trigger_touch_times[MAX_CLIENTS];

static const cm_entity_t *Race_GameEntityValue(const cm_entity_t *entity,
                                               const char *key) {
  (void) entity;
  ck_assert_str_eq(key, "start_mode");
  return &race_game_start_mode;
}

static void *Race_GameMalloc(const size_t size, const mem_tag_t tag) {
  ck_assert_uint_eq(tag, MEM_TAG_GAME_LEVEL);
  ck_assert_uint_le(size, sizeof(race_game_trigger_touch_times));
  memset(race_game_trigger_touch_times, 0, sizeof(race_game_trigger_touch_times));
  return race_game_trigger_touch_times;
}

static void Race_GameSetModel(g_entity_t *ent, const char *model) {
  ck_assert_ptr_nonnull(ent);
  ck_assert_ptr_eq(model, ent->model);
}

typedef struct {
  uint8_t bytes[64];
  size_t length;
  size_t offset;
  size_t close_calls;
  bool exists;
} race_game_nav_file_t;

static race_game_nav_file_t race_game_nav;
static file_t race_game_nav_handle;

static void Race_GameNavPrint(const char *format, ...) {
  (void) format;
}

static void Race_GameNavWarn(const char *function, const char *format, ...) {
  (void) function;
  (void) format;
}

static bool Race_GameNavFileExists(const char *path) {
  (void) path;
  return race_game_nav.exists;
}

static file_t *Race_GameNavOpenFile(const char *path) {
  (void) path;
  if (!race_game_nav.exists) {
    return NULL;
  }

  race_game_nav.offset = 0u;
  race_game_nav_handle.opaque = &race_game_nav;
  return &race_game_nav_handle;
}

static int64_t Race_GameNavReadFile(file_t *file, void *buffer,
                                    const size_t size, const size_t count) {
  race_game_nav_file_t *nav = file ? file->opaque : NULL;
  if (!nav || !size) {
    return nav ? (int64_t) count : -1;
  }

  const size_t available = (nav->length - nav->offset) / size;
  const size_t objects = min(count, available);
  memcpy(buffer, nav->bytes + nav->offset, objects * size);
  nav->offset += objects * size;
  return (int64_t) objects;
}

static bool Race_GameNavCloseFile(file_t *file) {
  race_game_nav_file_t *nav = file ? file->opaque : NULL;
  if (!nav) {
    return false;
  }

  nav->close_calls++;
  file->opaque = NULL;
  return true;
}

static void Race_GameNavAppend(const void *data, const size_t size) {
  ck_assert_msg(race_game_nav.length <= sizeof(race_game_nav.bytes) - size,
                "Navigation fixture overflow");
  memcpy(race_game_nav.bytes + race_game_nav.length, data, size);
  race_game_nav.length += size;
}

static void Race_GameNavAppendLong(const int32_t value) {
  const int32_t encoded = LittleLong(value);
  Race_GameNavAppend(&encoded, sizeof(encoded));
}

static void Race_GameNavAppendShort(const int16_t value) {
  const int16_t encoded = LittleShort(value);
  Race_GameNavAppend(&encoded, sizeof(encoded));
}

static void Race_GameNavAppendFloat(const float value) {
  const float encoded = LittleFloat(value);
  Race_GameNavAppend(&encoded, sizeof(encoded));
}

static void Race_GameNavAppendNode(const vec3_t position,
                                   const uint32_t links) {
  const vec3_t encoded_position = LittleVec3(position);
  Race_GameNavAppend(&encoded_position, sizeof(encoded_position));
  Race_GameNavAppendLong((int32_t) links);
}

static void Race_GameNavBegin(const uint32_t nodes) {
  memset(&race_game_nav, 0, sizeof(race_game_nav));
  race_game_nav.exists = true;
  Race_GameNavAppendLong('Q' | '2' << 8 | 'N' << 16 | 'S' << 24);
  Race_GameNavAppendLong(2);
  Race_GameNavAppendLong((int32_t) nodes);
}

bool G_IsMeat(const g_entity_t *ent) {
  return ent && ent->client;
}

static size_t Race_GameBoundsBoxEntities(const box3_t bounds,
                                         g_entity_t **list,
                                         const size_t len,
                                         const uint32_t type) {
  (void) list;
  (void) len;
  race_game_bounds_box_entities_calls++;
  race_game_bounds_box_entities_bounds = bounds;
  race_game_bounds_box_entities_type = type;
  return 0u;
}

static cm_trace_t Race_GameAiBoundsTrace(const vec3_t start,
                                         const vec3_t end,
                                         const box3_t bounds,
                                         const g_entity_t *skip,
                                         const int32_t contents) {
  (void) skip;
  (void) contents;

  ck_assert_msg(race_game_ai_trace_calls < lengthof(race_game_ai_trace_bounds),
                "AI pathability issued too many traces");
  race_game_ai_trace_bounds[race_game_ai_trace_calls] = bounds;

  cm_trace_t trace = {
    .fraction = 1.f,
    .end = end
  };
  if (race_game_ai_force_narrow && race_game_ai_trace_calls == 0u) {
    trace.start_solid = true;
    trace.fraction = 0.f;
    trace.end = start;
    trace.contents = CONTENTS_SOLID;
    trace.ent = &race_game_ai_mover;
  }

  race_game_ai_trace_calls++;
  return trace;
}

static pm_params_t Race_GameLandingDefaultParams(void) {
  return (pm_params_t) {
    .gravity = 800,
    .gravity_water = PM_GRAVITY_WATER,
    .accel_ground = PM_ACCEL_GROUND,
    .accel_ground_slick = PM_ACCEL_GROUND_SLICK,
    .accel_air = PM_ACCEL_AIR,
    .accel_water = PM_ACCEL_WATER,
    .accel_spectator = PM_ACCEL_SPECTATOR,
    .accel_ladder = PM_ACCEL_LADDER,
    .friction_ground = PM_FRICT_GROUND,
    .friction_ground_slick = PM_FRICT_GROUND_SLICK,
    .friction_air = PM_FRICT_AIR,
    .friction_water = PM_FRICT_WATER,
    .friction_spectator = PM_FRICT_SPECTATOR,
    .friction_ladder = PM_FRICT_LADDER,
    .speed_ground = PM_SPEED_RUN,
    .speed_air = PM_SPEED_AIR,
    .speed_water = PM_SPEED_WATER,
    .speed_ladder = PM_SPEED_LADDER,
    .speed_spectator = PM_SPEED_SPECTATOR,
    .speed_stop = PM_SPEED_STOP,
    .speed_jump = PM_SPEED_JUMP,
    .speed_ducked = PM_SPEED_DUCKED,
    .speed_duck_stand = PM_SPEED_DUCK_STAND,
    .speed_water_jump = PM_SPEED_WATER_JUMP
  };
}

pm_params_t G_MovementParams(void) {
  return race_game_landing_params;
}

void G_SetAnimation(g_client_t *cl, entity_animation_t animation,
                    bool restart) {
  (void) cl;
  (void) restart;
  race_game_landing_animation_calls++;
  race_game_landing_animation = animation;
}

bool G_IsAnimation(g_client_t *cl, entity_animation_t animation) {
  (void) cl;
  (void) animation;
  return false;
}

void G_Damage(const g_damage_t *damage) {
  race_game_landing_damage_calls++;
  race_game_landing_damage = *damage;
  damage->target->health -= damage->damage;
}

void G_TouchOccupy(g_entity_t *ent) {
  (void) ent;
  race_game_landing_occupy_calls++;
}

static int32_t Race_GameLandingPointContents(const vec3_t point) {
  (void) point;
  return 0;
}

static int32_t Race_GameLandingBoxContents(const box3_t bounds) {
  (void) bounds;
  return 0;
}

static cm_trace_t Race_GameLandingTrace(const vec3_t start, const vec3_t end,
                                        const box3_t bounds,
                                        const g_entity_t *skip,
                                        int32_t contents) {
  (void) skip;
  (void) contents;

  cm_trace_t trace = {
    .fraction = 1.f,
    .end = end
  };
  const float startBottom = start.z + bounds.mins.z;
  const float endBottom = end.z + bounds.mins.z;

  if (startBottom < 0.f) {
    trace.start_solid = true;
    trace.all_solid = endBottom < 0.f;
    trace.fraction = 0.f;
    trace.end = start;
  } else if (endBottom < 0.f) {
    trace.fraction = startBottom / (startBottom - endBottom);
    trace.end = Vec3_Fmaf(start, trace.fraction,
                          Vec3_Subtract(end, start));
  } else {
    return trace;
  }

  trace.plane.normal = Vec3_Up();
  trace.contents = CONTENTS_SOLID;
  trace.ent = &race_game_landing_floor;
  return trace;
}

static debug_t Race_GameLandingDebugMask(void) {
  return 0;
}

static void Race_GameLandingDebug(debug_t debug, const char *function,
                                  const char *format, ...) {
  (void) debug;
  (void) function;
  (void) format;
}

static void Race_GameLandingLinkEntity(g_entity_t *ent) {
  (void) ent;
  race_game_landing_link_calls++;
}

static void Race_GameLandingUseCommon(void) {
  const race_physics_config_t config = {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = RACE_PHYSICS_FAMILY_QUETOO,
    .preset = RACE_PHYSICS_PRESET_QUETOO_COMMON_V1,
    .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_OFF
  };
  Race_Physics_SetProvider(NULL);
  ck_assert(Race_Physics_SetActive(&config));
  race_game_landing_params = Race_GameLandingDefaultParams();
}

static void Race_GameLandingUseNamed(
    const race_physics_preset_id_t preset) {
  const race_physics_config_t config = {
    .version = RACE_PHYSICS_CONFIG_VERSION,
    .family = RACE_PHYSICS_FAMILY_Q2,
    .preset = preset,
    .q2_snap_mode = RACE_PHYSICS_Q2_SNAP_NEAREST
  };

  ck_assert(Race_Physics_SetActive(&config));
  ck_assert(Race_Physics_FixedParamsForPreset(
    preset, &race_game_landing_params));
}

static void Race_GameLandingReset(void) {
  G_Ai_ShutdownNodes();
  memset(&gi, 0, sizeof(gi));
  gi.Print = Race_GameNavPrint;
  gi.PointContents = Race_GameLandingPointContents;
  gi.BoxContents = Race_GameLandingBoxContents;
  gi.Trace = Race_GameLandingTrace;
  gi.LinkEntity = Race_GameLandingLinkEntity;
  gi.DebugMask = Race_GameLandingDebugMask;
  gi.Debug = Race_GameLandingDebug;
  gi.Warn = Race_GameNavWarn;
  gi.OpenFile = Race_GameNavOpenFile;
  gi.ReadFile = Race_GameNavReadFile;
  gi.CloseFile = Race_GameNavCloseFile;
  gi.FileExists = Race_GameNavFileExists;

  memset(&g_level, 0, sizeof(g_level));
  memset(&race_game_landing_floor, 0, sizeof(race_game_landing_floor));
  memset(&race_game_landing_damage, 0, sizeof(race_game_landing_damage));
  race_game_landing_damage_calls = 0u;
  race_game_landing_animation_calls = 0u;
  race_game_landing_animation = 0;
  race_game_landing_link_calls = 0u;
  race_game_landing_occupy_calls = 0u;

  static cvar_t fallDamage = {
    .name = "g_fall_damage",
    .value = 1.f,
    .integer = 1
  };
  g_fall_damage = &fallDamage;

  static cvar_t maxEntities = {
    .name = "sv_max_entities",
    .integer = 0
  };
  sv_max_entities = &maxEntities;
}

static void Race_GameLandingMove(const float velocity,
                                 g_client_t *cl,
                                 g_entity_t *ent) {
  memset(cl, 0, sizeof(*cl));
  memset(ent, 0, sizeof(*ent));

  cl->entity = ent;
  cl->ps.client = 1u;
  cl->ps.pm_state.flags = PMF_JUMP_HELD;
  cl->land_time = 17u;

  ent->client = cl;
  ent->move_type = MOVE_TYPE_WALK;
  ent->clip_mask = CONTENTS_MASK_CLIP_PLAYER;
  ent->health = 100;
  ent->max_health = 100;
  ent->s.origin = Vec3(-64.f, -32.f, 24.5f);
  ent->s.event = EV_CLIENT_TELEPORT;
  ent->velocity = Vec3(0.f, 0.f, velocity);

  g_level.time = 1000u;
  g_level.current_entity = ent;

  pm_cmd_t cmd = {
    .msec = 16u,
    .up = 10
  };
  G_ClientMove(cl, &cmd);
}

START_TEST(_Race_GameLandingNamedSuppression) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1,
    RACE_PHYSICS_PRESET_DP2_V1
  };
  static const float velocities[] = { -300.f, -800.f, -1000.f };

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    for (size_t velocity = 0; velocity < lengthof(velocities); velocity++) {
      Race_GameLandingReset();
      Race_GameLandingUseNamed(presets[preset]);

      g_client_t cl;
      g_entity_t ent;
      Race_GameLandingMove(velocities[velocity], &cl, &ent);

      ck_assert(cl.ps.pm_state.flags & PMF_ON_GROUND);
      ck_assert(cl.ps.pm_state.flags & PMF_JUMP_HELD);
      ck_assert(!(cl.ps.pm_state.flags & (PMF_TIME_LAND | PMF_JUMPED)));
      ck_assert_uint_eq(cl.ps.pm_state.time, 0u);
      ck_assert_float_eq(ent.s.origin.x, -64.f);
      ck_assert_float_eq(ent.s.origin.y, -32.f);
      ck_assert_float_eq(ent.s.origin.z, 24.f);
      ck_assert_float_eq(ent.velocity.x, 0.f);
      ck_assert_float_eq(ent.velocity.y, 0.f);
      ck_assert_float_eq(ent.velocity.z, 0.f);
      ck_assert_ptr_eq(ent.ground.ent, &race_game_landing_floor);
      ck_assert_int_eq(ent.s.event, EV_CLIENT_TELEPORT);
      ck_assert_uint_eq(cl.land_time, 17u);
      ck_assert_uint_eq(cl.pain_time, 0u);
      ck_assert_int_eq(ent.health, 100);
      ck_assert_uint_eq(race_game_landing_damage_calls, 0u);
      ck_assert_uint_eq(race_game_landing_animation_calls, 0u);
      ck_assert_uint_eq(race_game_landing_link_calls, 1u);
      ck_assert_uint_eq(race_game_landing_occupy_calls, 1u);
    }
  }
} END_TEST

START_TEST(_Race_GameLandingDefaultConsumers) {
  typedef struct {
    float velocity;
    uint16_t time;
    g_entity_event_t event;
    int32_t damage;
  } race_game_landing_case_t;

  static const race_game_landing_case_t cases[] = {
    { -300.f, 1u, EV_CLIENT_LAND, 0 },
    { -800.f, 16u, EV_CLIENT_FALL, 5 },
    { -1000.f, 256u, EV_CLIENT_FALL_FAR, 15 }
  };

  for (size_t i = 0; i < lengthof(cases); i++) {
    Race_GameLandingReset();
    Race_GameLandingUseCommon();

    g_client_t cl;
    g_entity_t ent;
    Race_GameLandingMove(cases[i].velocity, &cl, &ent);

    ck_assert(cl.ps.pm_state.flags & PMF_ON_GROUND);
    ck_assert(cl.ps.pm_state.flags & PMF_JUMP_HELD);
    ck_assert(cl.ps.pm_state.flags & PMF_TIME_LAND);
    ck_assert(!(cl.ps.pm_state.flags & PMF_JUMPED));
    ck_assert_uint_eq(cl.ps.pm_state.time, cases[i].time);
    ck_assert_int_eq(ent.s.event, cases[i].event);
    ck_assert_uint_eq(cl.land_time, g_level.time);
    ck_assert_uint_eq(race_game_landing_animation_calls, 1u);
    ck_assert_int_eq(race_game_landing_animation, ANIM_LEGS_LAND1);
    ck_assert_uint_eq(race_game_landing_damage_calls,
                      cases[i].damage > 0 ? 1u : 0u);

    if (cases[i].damage > 0) {
      ck_assert_ptr_eq(race_game_landing_damage.target, &ent);
      ck_assert_ptr_null(race_game_landing_damage.inflictor);
      ck_assert_ptr_null(race_game_landing_damage.attacker);
      ck_assert_int_eq(race_game_landing_damage.damage, cases[i].damage);
      ck_assert_int_eq(race_game_landing_damage.knockback, 0);
      ck_assert_int_eq(race_game_landing_damage.flags, DMG_NO_ARMOR);
      ck_assert_int_eq(race_game_landing_damage.mod, MOD_FALLING);
      ck_assert_uint_eq(cl.pain_time, g_level.time);
      ck_assert_int_eq(ent.health, 100 - cases[i].damage);
    } else {
      ck_assert_uint_eq(cl.pain_time, 0u);
      ck_assert_int_eq(ent.health, 100);
    }
  }
} END_TEST

START_TEST(_Race_GameStandingBoundsConsumers) {
  static const struct {
    race_physics_preset_id_t preset;
    float standing_max_z;
  } cases[] = {
    { RACE_PHYSICS_PRESET_INVALID, 36.f },
    { RACE_PHYSICS_PRESET_Q2, 32.f },
    { RACE_PHYSICS_PRESET_QUETOO_FIX_V1, 32.f },
    { RACE_PHYSICS_PRESET_DP2_V1, 32.f }
  };
  const vec3_t spot = Vec3(100.f, 200.f, 300.f);

  for (size_t i = 0; i < lengthof(cases); i++) {
    Race_GameLandingReset();
    if (cases[i].preset == RACE_PHYSICS_PRESET_INVALID) {
      Race_GameLandingUseCommon();
    } else {
      Race_GameLandingUseNamed(cases[i].preset);
    }

    gi.BoxEntities = Race_GameBoundsBoxEntities;
    race_game_bounds_box_entities_calls = 0u;
    race_game_bounds_box_entities_bounds = Box3_Zero();
    race_game_bounds_box_entities_type = 0u;

    ck_assert(!G_WouldTelefrag(spot));
    ck_assert_uint_eq(race_game_bounds_box_entities_calls, 1u);
    ck_assert_uint_eq(race_game_bounds_box_entities_type, BOX_COLLIDE);
    ck_assert_float_eq(race_game_bounds_box_entities_bounds.mins.x, 84.f);
    ck_assert_float_eq(race_game_bounds_box_entities_bounds.mins.y, 184.f);
    ck_assert_float_eq(race_game_bounds_box_entities_bounds.mins.z, 260.f);
    ck_assert_float_eq(race_game_bounds_box_entities_bounds.maxs.x, 116.f);
    ck_assert_float_eq(race_game_bounds_box_entities_bounds.maxs.y, 216.f);
    ck_assert_float_eq(race_game_bounds_box_entities_bounds.maxs.z,
                       300.f + cases[i].standing_max_z + PM_STEP_HEIGHT);

    const box3_t first_link_bounds = Pm_PlayerBounds(false);
    ck_assert_float_eq(first_link_bounds.mins.z, -24.f);
    ck_assert_float_eq(first_link_bounds.maxs.z,
                       cases[i].standing_max_z);
  }
} END_TEST

START_TEST(_Race_GameAiPathBoundsConsumers) {
  static const struct {
    race_physics_preset_id_t preset;
    float standing_max_z;
  } cases[] = {
    { RACE_PHYSICS_PRESET_INVALID, 36.f },
    { RACE_PHYSICS_PRESET_Q2, 32.f },
    { RACE_PHYSICS_PRESET_QUETOO_FIX_V1, 32.f },
    { RACE_PHYSICS_PRESET_DP2_V1, 32.f }
  };

  for (size_t i = 0; i < lengthof(cases); i++) {
    Race_GameLandingReset();
    if (cases[i].preset == RACE_PHYSICS_PRESET_INVALID) {
      Race_GameLandingUseCommon();
    } else {
      Race_GameLandingUseNamed(cases[i].preset);
    }

    memset(&race_game_ai_mover, 0, sizeof(race_game_ai_mover));
    race_game_ai_mover.s.number = 7u;
    gi.Trace = Race_GameAiBoundsTrace;

    race_game_ai_trace_calls = 0u;
    race_game_ai_force_narrow = false;
    ck_assert(G_Ai_Node_CanPathTo(Vec3(32.f, 64.f, 96.f)));
    ck_assert_uint_eq(race_game_ai_trace_calls, 1u);
    ck_assert_float_eq(race_game_ai_trace_bounds[0].mins.x, -17.f);
    ck_assert_float_eq(race_game_ai_trace_bounds[0].mins.y, -17.f);
    ck_assert_float_eq(race_game_ai_trace_bounds[0].mins.z, -24.f);
    ck_assert_float_eq(race_game_ai_trace_bounds[0].maxs.x, 17.f);
    ck_assert_float_eq(race_game_ai_trace_bounds[0].maxs.y, 17.f);
    ck_assert_float_eq(race_game_ai_trace_bounds[0].maxs.z,
                       cases[i].standing_max_z);

    race_game_ai_trace_calls = 0u;
    race_game_ai_force_narrow = true;
    ck_assert(G_Ai_Node_CanPathTo(Vec3(32.f, 64.f, 96.f)));
    ck_assert_uint_eq(race_game_ai_trace_calls, 2u);
    ck_assert_float_eq(race_game_ai_trace_bounds[1].mins.x, -4.f);
    ck_assert_float_eq(race_game_ai_trace_bounds[1].mins.y, -4.f);
    ck_assert_float_eq(race_game_ai_trace_bounds[1].mins.z, -24.f);
    ck_assert_float_eq(race_game_ai_trace_bounds[1].maxs.x, 4.f);
    ck_assert_float_eq(race_game_ai_trace_bounds[1].maxs.y, 4.f);
    ck_assert_float_eq(race_game_ai_trace_bounds[1].maxs.z,
                       cases[i].standing_max_z);
  }
} END_TEST

START_TEST(_Race_GameAiNavLoaderRejectsMalformedFiles) {
  Race_GameLandingReset();
  q_strlcpy(g_level.name, "nav_fixture", sizeof(g_level.name));

  Race_GameNavBegin(1u);
  Race_GameNavAppendNode(Vec3(16.f, -32.f, 48.f), 0u);
  const size_t valid_length = race_game_nav.length;

  for (size_t length = 0u; length < valid_length; length++) {
    race_game_nav.length = length;
    G_Ai_InitNodes();
    ck_assert_uint_eq(G_Ai_Node_Count(), 0u);
  }

  race_game_nav.length = valid_length;
  G_Ai_InitNodes();
  ck_assert_uint_eq(G_Ai_Node_Count(), 1u);
  const vec3_t position = G_Ai_Node_GetPosition(0u);
  ck_assert_float_eq(position.x, 16.f);
  ck_assert_float_eq(position.y, -32.f);
  ck_assert_float_eq(position.z, 48.f);

  Race_GameNavBegin(65536u);
  G_Ai_InitNodes();
  ck_assert_uint_eq(G_Ai_Node_Count(), 0u);

  Race_GameNavBegin(1u);
  Race_GameNavAppendNode(Vec3(NAN, 0.f, 0.f), 0u);
  G_Ai_InitNodes();
  ck_assert_uint_eq(G_Ai_Node_Count(), 0u);

  Race_GameNavBegin(2u);
  Race_GameNavAppendNode(Vec3_Zero(), 1u);
  Race_GameNavAppendShort(2);
  Race_GameNavAppendShort(0);
  Race_GameNavAppendFloat(1.f);
  Race_GameNavAppendNode(Vec3(64.f, 0.f, 0.f), 0u);
  G_Ai_InitNodes();
  ck_assert_uint_eq(G_Ai_Node_Count(), 0u);

  Race_GameNavBegin(2u);
  Race_GameNavAppendNode(Vec3_Zero(), 1u);
  Race_GameNavAppendShort(1);
  Race_GameNavAppendShort(1);
  Race_GameNavAppendFloat(1.f);
  Race_GameNavAppendNode(Vec3(64.f, 0.f, 0.f), 0u);
  G_Ai_InitNodes();
  ck_assert_uint_eq(G_Ai_Node_Count(), 0u);

  Race_GameNavBegin(2u);
  Race_GameNavAppendNode(Vec3_Zero(), 1u);
  Race_GameNavAppendShort(1);
  Race_GameNavAppendShort(0);
  Race_GameNavAppendFloat(NAN);
  Race_GameNavAppendNode(Vec3(64.f, 0.f, 0.f), 0u);
  G_Ai_InitNodes();
  ck_assert_uint_eq(G_Ai_Node_Count(), 0u);

  ck_assert_uint_gt(race_game_nav.close_calls, 0u);
} END_TEST

START_TEST(_Race_GameTriggerFinalizationUsesLiveEntities) {
  Race_GameLandingReset();

  static cvar_t maxClients = {
    .name = "sv_max_clients",
    .integer = 2
  };
  sv_max_clients = &maxClients;
  gi.EntityValue = Race_GameEntityValue;
  gi.Malloc = Race_GameMalloc;
  gi.SetModel = Race_GameSetModel;

  Race_Course_Reset(&g_level.race_course);
  race_game_free_calls = 0u;
  memset(&race_game_start_mode, 0, sizeof(race_game_start_mode));
  race_game_start_mode.parsed = ENTITY_STRING;
  race_game_start_mode.nullable_string = "typo";

  g_entity_t invalid_start = {
    .in_use = true,
    .classname = "trigger_race_start",
    .model = "*1"
  };
  Race_TriggerStart(&invalid_start);
  ck_assert(!invalid_start.in_use);
  ck_assert_uint_eq(race_game_free_calls, 1u);
  ck_assert_uint_eq(g_level.race_course.start_count, 0u);
  ck_assert_uint_eq(race_game_landing_link_calls, 0u);
  ck_assert(!Race_Course_Validate(&g_level.race_course));

  Race_Course_Reset(&g_level.race_course);
  race_game_start_mode.nullable_string = "exit";
  g_entity_t start = {
    .in_use = true,
    .classname = "trigger_race_start",
    .model = "*2"
  };
  Race_TriggerStart(&start);
  ck_assert(start.in_use);
  ck_assert_int_eq(start.race_start_mode, RACE_START_EXIT);
  ck_assert_ptr_nonnull(start.Touch);
  ck_assert_uint_eq(g_level.race_course.start_count, 1u);
  ck_assert_uint_eq(race_game_landing_link_calls, 1u);
  ck_assert(!Race_Course_Validate(&g_level.race_course));

  g_entity_t finish = {
    .in_use = true,
    .classname = "trigger_race_finish",
    .model = "*3"
  };
  Race_TriggerFinish(&finish);
  ck_assert(finish.in_use);
  ck_assert_ptr_nonnull(finish.Touch);
  ck_assert_uint_eq(g_level.race_course.finish_count, 1u);
  ck_assert_uint_eq(race_game_landing_link_calls, 2u);
  ck_assert(Race_Course_Validate(&g_level.race_course));
} END_TEST

START_TEST(_Race_GameAiLookaheadPreservesAuthoritativeLatches) {
  Race_GameLandingReset();
  Race_GameLandingUseCommon();

  ai_t ai;
  g_client_t cl;
  g_entity_t ent;
  memset(&ai, 0, sizeof(ai));
  memset(&cl, 0, sizeof(cl));
  memset(&ent, 0, sizeof(ent));

  cl.ai = &ai;
  cl.entity = &ent;
  cl.race_oneway_latches = UINT64_C(0x0000000100000002);
  ai.move_target.type = AI_GOAL_POSITION;
  ai.move_target.position.pos = Vec3(128.f, 0.f, 24.f);
  ai.move_target.last_distance = FLT_MAX;

  ent.client = &cl;
  ent.solid = SOLID_BOX;
  ent.clip_mask = CONTENTS_MASK_CLIP_PLAYER;
  ent.s.origin = Vec3(0.f, 0.f, 24.f);
  ent.ground.ent = &race_game_landing_floor;

  g_level.frame_num = 42u;
  race_game_ai_mutate_latches = true;
  race_game_ai_movement_traces = 0u;

  pm_cmd_t cmd = { .msec = 16u };
  ck_assert_uint_eq(G_Ai_Move(&cl, &cmd), 0u);
  ck_assert_uint_gt(race_game_ai_movement_traces, 0u);
  ck_assert_uint_eq(cl.race_oneway_latches, UINT64_C(0x0000000100000002));
  ck_assert_uint_eq(ai.lookahead_frame, g_level.frame_num);

  const size_t first_pass_traces = race_game_ai_movement_traces;
  ck_assert_uint_eq(G_Ai_Move(&cl, &cmd), 0u);
  ck_assert_uint_gt(race_game_ai_movement_traces, first_pass_traces);
  ck_assert_uint_eq(cl.race_oneway_latches, UINT64_C(0x0000000100000002));

  race_game_ai_mutate_latches = false;
} END_TEST

int main(void) {
  Suite *suite = suite_create("check_race_game_landing");
  TCase *consumer = tcase_create("consumer");
  tcase_add_test(consumer, _Race_GameLandingNamedSuppression);
  tcase_add_test(consumer, _Race_GameLandingDefaultConsumers);
  tcase_add_test(consumer, _Race_GameStandingBoundsConsumers);
  tcase_add_test(consumer, _Race_GameAiPathBoundsConsumers);
  tcase_add_test(consumer, _Race_GameAiNavLoaderRejectsMalformedFiles);
  tcase_add_test(consumer, _Race_GameTriggerFinalizationUsesLiveEntities);
  tcase_add_test(consumer, _Race_GameAiLookaheadPreservesAuthoritativeLatches);
  suite_add_tcase(suite, consumer);

  SRunner *runner = srunner_create(suite);
  srunner_set_fork_status(runner, CK_NOFORK);
  srunner_run_all(runner, CK_VERBOSE);
  const int32_t failed = srunner_ntests_failed(runner);
  srunner_free(runner);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
