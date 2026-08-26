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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g_local.h"
#include "race_physics_service.h"

box3_t Pm_PlayerBounds(bool ducked);

g_import_t gi;
g_level_t g_level;

static size_t race_physics_service_previous_prepare_calls;
static char race_physics_service_config_string[MAX_STRING_CHARS];
static char race_physics_service_output[16384];
static char race_physics_service_selector_value[64];
static char race_physics_service_selector_latched[64];
static cvar_t race_physics_service_selector;
static char race_physics_service_snap_mode_value[16];
static char race_physics_service_snap_mode_latched[16];
static cvar_t race_physics_service_snap_mode;
static size_t race_physics_service_ai_first_command_checks;

static void Race_PhysicsService_TestPreviousPrepare(g_client_t *cl,
                                                    pm_move_t *pm) {
  (void) cl;
  race_physics_service_previous_prepare_calls++;
  pm->hook_pull_speed = 123.f;
}

PrepareMove G_PrepareMove = Race_PhysicsService_TestPreviousPrepare;

static void Race_PhysicsService_TestPrint(const char *format, ...) {
  const size_t length = strlen(race_physics_service_output);
  if (length >= sizeof(race_physics_service_output) - 1u) {
    return;
  }

  va_list args;
  va_start(args, format);
  vsnprintf(race_physics_service_output + length,
            sizeof(race_physics_service_output) - length, format, args);
  va_end(args);
}

static void __attribute__((noreturn)) Race_PhysicsService_TestError(
    const char *function, const char *format, ...) {
  (void) function;
  (void) format;
  abort();
}

static cmd_t *Race_PhysicsService_TestAddCmd(const char *name,
                                             CmdExecuteFunc function,
                                             uint32_t flags,
                                             const char *description) {
  static cmd_t command;
  command = (cmd_t) {
    .name = name,
    .description = description,
    .Execute = function,
    .flags = flags
  };
  return &command;
}

static cvar_t *Race_PhysicsService_TestAddCvar(const char *name,
                                               const char *value,
                                               uint32_t flags,
                                               const char *description) {
  ck_assert_ptr_nonnull(description);
  if (!strcmp(name, "g_race_physics")) {
    ck_assert_uint_eq(flags, CVAR_LATCH | CVAR_SERVER_INFO);
    ck_assert_str_eq(value, RACE_PHYSICS_SELECTOR_Q2_KEY);
    race_physics_service_selector.name = name;
    race_physics_service_selector.default_string = value;
    race_physics_service_selector.flags = flags;
    race_physics_service_selector.description = description;
    return &race_physics_service_selector;
  }

  ck_assert_str_eq(name, "g_q2_snap_mode");
  ck_assert_uint_eq(flags, CVAR_LATCH);
  ck_assert_str_eq(value, "2");
  race_physics_service_snap_mode.name = name;
  race_physics_service_snap_mode.default_string = value;
  race_physics_service_snap_mode.flags = flags;
  race_physics_service_snap_mode.description = description;
  return &race_physics_service_snap_mode;
}

static void Race_PhysicsService_TestSetSelector(const char *key) {
  q_strlcpy(race_physics_service_selector_value, key,
            sizeof(race_physics_service_selector_value));
  race_physics_service_selector.string = race_physics_service_selector_value;
  race_physics_service_selector.latched_string = NULL;
}

static void Race_PhysicsService_TestLatchSelector(const char *key) {
  q_strlcpy(race_physics_service_selector_latched, key,
            sizeof(race_physics_service_selector_latched));
  race_physics_service_selector.latched_string =
    race_physics_service_selector_latched;
}

static void Race_PhysicsService_TestSetSnapMode(const int32_t mode) {
  snprintf(race_physics_service_snap_mode_value,
           sizeof(race_physics_service_snap_mode_value), "%d", mode);
  race_physics_service_snap_mode.string =
    race_physics_service_snap_mode_value;
  race_physics_service_snap_mode.integer = mode;
  race_physics_service_snap_mode.latched_string = NULL;
}

static void Race_PhysicsService_TestLatchSnapMode(const int32_t mode) {
  snprintf(race_physics_service_snap_mode_latched,
           sizeof(race_physics_service_snap_mode_latched), "%d", mode);
  race_physics_service_snap_mode.latched_string =
    race_physics_service_snap_mode_latched;
}

static int32_t Race_PhysicsService_TestArgc(void) {
  return 1;
}

static const char *Race_PhysicsService_TestArgv(int32_t argument) {
  return argument == 0 ? "race_physics" : "";
}

static void Race_PhysicsService_TestSetConfigString(int32_t index,
                                                    const char *value) {
  ck_assert_int_eq(index, CS_RACE_PHYSICS_CONFIG);
  q_strlcpy(race_physics_service_config_string, value,
            sizeof(race_physics_service_config_string));
}

static int32_t Race_PhysicsService_TestPointContents(const vec3_t point) {
  (void) point;
  return 0;
}

static int32_t Race_PhysicsService_TestBoxContents(const box3_t box) {
  (void) box;
  return 0;
}

static cm_trace_t Race_PhysicsService_TestTrace(const vec3_t start,
                                                const vec3_t end,
                                                const box3_t bounds) {
  (void) start;
  (void) bounds;
  return (cm_trace_t) {
    .fraction = 1.f,
    .end = end
  };
}

static const uint8_t race_physics_service_floor;

static cm_trace_t Race_PhysicsService_TestLandingTrace(const vec3_t start,
                                                       const vec3_t end,
                                                       const box3_t bounds) {
  cm_trace_t trace = {
    .fraction = 1.f,
    .end = end
  };
  const float start_bottom = start.z + bounds.mins.z;
  const float end_bottom = end.z + bounds.mins.z;

  if (start_bottom < 0.f) {
    trace.start_solid = true;
    trace.all_solid = end_bottom < 0.f;
    trace.fraction = 0.f;
    trace.end = start;
  } else if (end_bottom < 0.f) {
    trace.fraction = start_bottom / (start_bottom - end_bottom);
    trace.end = Vec3_Fmaf(start, trace.fraction,
                          Vec3_Subtract(end, start));
  } else {
    return trace;
  }

  trace.plane.normal = Vec3_Up();
  trace.contents = CONTENTS_SOLID;
  trace.ent = (void *) &race_physics_service_floor;
  return trace;
}

static debug_t Race_PhysicsService_TestDebugMask(void) {
  return 0;
}

static void Race_PhysicsService_TestDebug(debug_t debug, const char *function,
                                          const char *format, ...) {
  (void) debug;
  (void) function;
  if (strstr(format, "AI first-move verification")) {
    race_physics_service_ai_first_command_checks++;
  }
}

static pm_move_t Race_PhysicsService_TestMove(const g_client_t *cl) {
  return (pm_move_t) {
    .cmd = {
      .msec = 16,
      .forward = 300
    },
    .s = cl->ps.pm_state,
    .PointContents = Race_PhysicsService_TestPointContents,
    .BoxContents = Race_PhysicsService_TestBoxContents,
    .Trace = Race_PhysicsService_TestTrace,
    .DebugMask = Race_PhysicsService_TestDebugMask,
    .Debug = Race_PhysicsService_TestDebug
  };
}

static pm_move_t Race_PhysicsService_TestLandingMove(
    const g_client_t *cl, const uint16_t msec) {
  pm_move_t move = Race_PhysicsService_TestMove(cl);
  move.s.origin = Vec3(-64.f, -32.f, 24.5f);
  move.s.velocity = Vec3(0.f, 0.f, -800.f);
  move.s.flags = PMF_JUMP_HELD;
  move.s.time = 0;
  move.cmd = (pm_cmd_t) {
    .msec = msec,
    .up = 10
  };
  move.Trace = Race_PhysicsService_TestLandingTrace;
  return move;
}

static void Race_PhysicsService_TestImports(void) {
  gi = (g_import_t) {
    .Print = Race_PhysicsService_TestPrint,
    .DebugMask = Race_PhysicsService_TestDebugMask,
    .Debug = Race_PhysicsService_TestDebug,
    .Error = Race_PhysicsService_TestError,
    .AddCvar = Race_PhysicsService_TestAddCvar,
    .AddCmd = Race_PhysicsService_TestAddCmd,
    .Argc = Race_PhysicsService_TestArgc,
    .Argv = Race_PhysicsService_TestArgv,
    .SetConfigString = Race_PhysicsService_TestSetConfigString
  };
  race_physics_service_previous_prepare_calls = 0u;
  race_physics_service_config_string[0] = '\0';
  race_physics_service_output[0] = '\0';
  race_physics_service_ai_first_command_checks = 0u;
  memset(&g_level, 0, sizeof(g_level));
  g_level.gravity = 800;
  memset(&race_physics_service_selector, 0,
         sizeof(race_physics_service_selector));
  memset(&race_physics_service_snap_mode, 0,
         sizeof(race_physics_service_snap_mode));
  Race_PhysicsService_TestSetSelector(RACE_PHYSICS_SELECTOR_Q2_KEY);
  Race_PhysicsService_TestSetSnapMode(RACE_PHYSICS_Q2_SNAP_TRUNCATE);
}

START_TEST(_Race_PhysicsServiceHydrationLifecycle) {
  Race_PhysicsService_TestImports();
  Race_PhysicsService_Init();
  ck_assert(G_PrepareMove != Race_PhysicsService_TestPreviousPrepare);

  pm_params_t q2, q2fix, dp2;
  ck_assert(Race_Physics_FixedParamsForPreset(
    RACE_PHYSICS_PRESET_Q2, &q2));
  ck_assert(Race_Physics_FixedParamsForPreset(
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1, &q2fix));
  ck_assert(Race_Physics_FixedParamsForPreset(
    RACE_PHYSICS_PRESET_DP2_V1, &dp2));
  ck_assert(Race_Physics_ParamsEqual(&dp2, &q2));

  Race_PhysicsService_ConfigureLevel("q2-map");
  ck_assert_str_eq(race_physics_service_config_string,
                   "v2\\q2\\q2-v1\\truncate");
  ck_assert_int_eq(Race_Physics_Current()->preset, RACE_PHYSICS_PRESET_Q2);

  Race_PhysicsService_TestSetSelector(RACE_PHYSICS_SELECTOR_QUAKE2_KEY);
  Race_PhysicsService_ConfigureLevel("quake2-alias-map");
  ck_assert_str_eq(race_physics_service_config_string,
                   "v2\\q2\\q2-v1\\truncate");
  ck_assert_int_eq(Race_Physics_Current()->preset, RACE_PHYSICS_PRESET_Q2);

  Race_PhysicsService_TestSetSelector(RACE_PHYSICS_PRESET_Q2_V1_KEY);
  Race_PhysicsService_ConfigureLevel("q2-canonical-map");
  ck_assert_str_eq(race_physics_service_config_string,
                   "v2\\q2\\q2-v1\\truncate");
  Race_PhysicsService_TestSetSelector(RACE_PHYSICS_SELECTOR_Q2_KEY);

  g_client_t human;
  memset(&human, 0, sizeof(human));
  human.ps.client = 1u;
  Race_PhysicsService_SeedClient(&human);
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &q2));

  // Common movement cvars may drift, but the named owner restores the complete
  // vector at the last existing boundary before every human Pm_Move.
  human.ps.pm_state.params = q2fix;
  pm_move_t human_move = Race_PhysicsService_TestMove(&human);
  G_PrepareMove(&human, &human_move);
  ck_assert_uint_eq(race_physics_service_previous_prepare_calls, 1u);
  ck_assert_float_eq(human_move.hook_pull_speed, 123.f);
  ck_assert(Race_Physics_ParamsEqual(&human_move.s.params, &q2));
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &q2));
  Pm_Move(&human_move);
  ck_assert(Race_Physics_ParamsEqual(&human_move.s.params, &q2));

  // A direct AI move bypasses G_PrepareMove. Its freshly spawned state must be
  // valid before the first copy into pm_move_t and remain unchanged by Pm_Move.
  g_client_t bot;
  memset(&bot, 0, sizeof(bot));
  bot.ps.client = 2u;
  bot.ai = (struct ai_s *) (uintptr_t) 1u;
  Race_PhysicsService_SeedClient(&bot);
  ck_assert_float_eq(bot.ps.pm_state.params.accel_air, q2.accel_air);
  ck_assert_float_eq(bot.ps.pm_state.params.speed_water, q2.speed_water);
  ck_assert_float_eq(bot.ps.pm_state.params.speed_ladder, q2.speed_ladder);
  ck_assert_float_eq(bot.ps.pm_state.params.speed_spectator,
                     q2.speed_spectator);
  pm_move_t ai_move = Race_PhysicsService_TestMove(&bot);
  Pm_Move(&ai_move);
  bot.ps.pm_state = ai_move.s;
  ck_assert(Race_Physics_ParamsEqual(&bot.ps.pm_state.params, &q2));
  Race_PhysicsService_ClientThink(&bot, &ai_move.cmd);
  ck_assert_uint_eq(race_physics_service_ai_first_command_checks, 1u);

  // DP2 has its own semantic identity while intentionally hydrating the exact
  // immutable q2-v1 vector for both authoritative humans and direct AI moves.
  Race_PhysicsService_TestSetSelector(RACE_PHYSICS_SELECTOR_DP2_KEY);
  Race_PhysicsService_ConfigureLevel("dp2-alias-map");
  ck_assert_str_eq(race_physics_service_config_string,
                   "v2\\q2\\dp2-v1\\truncate");
  ck_assert_int_eq(Race_Physics_Current()->preset,
                   RACE_PHYSICS_PRESET_DP2_V1);

  human.ps.pm_state.params = q2fix;
  Race_PhysicsService_SeedClient(&human);
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &dp2));
  human_move = Race_PhysicsService_TestMove(&human);
  G_PrepareMove(&human, &human_move);
  ck_assert(Race_Physics_ParamsEqual(&human_move.s.params, &dp2));
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &dp2));

  memset(&bot.ps.pm_state.params, 0, sizeof(bot.ps.pm_state.params));
  Race_PhysicsService_SeedClient(&bot);
  ck_assert(Race_Physics_ParamsEqual(&bot.ps.pm_state.params, &dp2));
  ai_move = Race_PhysicsService_TestMove(&bot);
  Pm_Move(&ai_move);
  bot.ps.pm_state = ai_move.s;
  Race_PhysicsService_ClientThink(&bot, &ai_move.cmd);
  ck_assert_uint_eq(race_physics_service_ai_first_command_checks, 2u);

  Race_PhysicsService_TestSetSelector(RACE_PHYSICS_PRESET_DP2_V1_KEY);
  Race_PhysicsService_ConfigureLevel("dp2-canonical-map");
  ck_assert_str_eq(race_physics_service_config_string,
                   "v2\\q2\\dp2-v1\\truncate");
  ck_assert_int_eq(Race_Physics_Current()->preset,
                   RACE_PHYSICS_PRESET_DP2_V1);

  Race_PhysicsService_TestSetSelector(RACE_PHYSICS_SELECTOR_Q2_KEY);
  Race_PhysicsService_ConfigureLevel("q2-restored-map");
  ck_assert(Race_Physics_ConfigEquals(Race_Physics_Current(),
                                      Race_Physics_Default()));

  // A latched request is merely pending. The active identity and first-move
  // vector cannot change until the engine applies it at a map/session load.
  Race_PhysicsService_TestLatchSelector(
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1_KEY);
  Race_PhysicsService_TestLatchSnapMode(RACE_PHYSICS_Q2_SNAP_NEAREST);
  ck_assert(Race_Physics_ConfigEquals(Race_Physics_Current(),
                                     Race_Physics_Default()));
  memset(&human.ps.pm_state.params, 0, sizeof(human.ps.pm_state.params));
  Race_PhysicsService_SeedClient(&human);
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &q2));

  // Reconnect and the pre-ConfigureLevel respawn ordering used by map restart
  // both seed from the still-active map-fixed owner.
  memset(&human, 0, sizeof(human));
  human.ps.client = 1u;
  Race_PhysicsService_SeedClient(&human);
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &q2));
  memset(&bot.ps.pm_state.params, 0, sizeof(bot.ps.pm_state.params));
  Race_PhysicsService_SeedClient(&bot);
  ck_assert(Race_Physics_ParamsEqual(&bot.ps.pm_state.params, &q2));
  race_physics_service_selector.latched_string = NULL;
  race_physics_service_snap_mode.latched_string = NULL;
  Race_PhysicsService_ConfigureLevel("q2-map");
  ck_assert(Race_Physics_ParamsEqual(&bot.ps.pm_state.params, &q2));
  ai_move = Race_PhysicsService_TestMove(&bot);
  Pm_Move(&ai_move);
  bot.ps.pm_state = ai_move.s;
  Race_PhysicsService_ClientThink(&bot, &ai_move.cmd);
  ck_assert_uint_eq(race_physics_service_ai_first_command_checks, 3u);

  // A new map selection replaces, rather than merges with, the previous vector.
  Race_PhysicsService_TestSetSelector(
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1_KEY);
  Race_PhysicsService_ConfigureLevel("fix-map");
  ck_assert_str_eq(race_physics_service_config_string,
                   "v2\\q2\\quetoo-fix-v1\\truncate");
  memset(&bot.ps.pm_state.params, 0, sizeof(bot.ps.pm_state.params));
  Race_PhysicsService_SeedClient(&bot);
  ck_assert(Race_Physics_ParamsEqual(&bot.ps.pm_state.params, &q2fix));
  ck_assert(!Race_Physics_ParamsEqual(&bot.ps.pm_state.params, &q2));

  // Snap policy is a separate latched dimension of the same authoritative
  // identity and therefore changes the wire and ruleset at a safe transition.
  Race_PhysicsService_TestSetSnapMode(RACE_PHYSICS_Q2_SNAP_NEAREST);
  Race_PhysicsService_ConfigureLevel("fix-nearest-map");
  ck_assert_str_eq(race_physics_service_config_string,
                   "v2\\q2\\quetoo-fix-v1\\nearest");
  ck_assert_int_eq(Race_Physics_Current()->q2_snap_mode,
                   RACE_PHYSICS_Q2_SNAP_NEAREST);
  ck_assert_str_eq(Race_Physics_ConfigRuleset(Race_Physics_Current()),
                   RACE_PHYSICS_PRESET_QUETOO_FIX_V1_KEY);

  Race_PhysicsService_TestSetSnapMode(RACE_PHYSICS_Q2_SNAP_OFF);
  Race_PhysicsService_ConfigureLevel("fix-unsnapped-map");
  ck_assert_str_eq(race_physics_service_config_string,
                   "v2\\q2\\quetoo-fix-v1\\off");
  ck_assert_int_eq(Race_Physics_Current()->q2_snap_mode,
                   RACE_PHYSICS_Q2_SNAP_OFF);
  ck_assert_str_eq(Race_Physics_ConfigRuleset(Race_Physics_Current()),
                   "quetoo-fix-v1-snap-off");

  // Server shutdown clears the owner. Reinitialization and configuration seed
  // the first post-restart movement without installing the hook twice.
  PrepareMove installed_prepare_move = G_PrepareMove;
  Race_PhysicsService_Shutdown();
  Race_PhysicsService_Init();
  ck_assert(G_PrepareMove == installed_prepare_move);
  ck_assert(Race_PhysicsService_ConfigureTestLevel(
    "fix-map", RACE_PHYSICS_PRESET_QUETOO_FIX_V1));
  ck_assert_int_eq(Race_Physics_Current()->q2_snap_mode,
                   RACE_PHYSICS_Q2_SNAP_TRUNCATE);
  memset(&human.ps.pm_state.params, 0, sizeof(human.ps.pm_state.params));
  Race_PhysicsService_SeedClient(&human);
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &q2fix));

  human.ps.pm_state.params = q2;
  human_move = Race_PhysicsService_TestMove(&human);
  G_PrepareMove(&human, &human_move);
  ck_assert(Race_Physics_ParamsEqual(&human_move.s.params, &q2fix));

  ck_assert(!Race_PhysicsService_ConfigureTestLevel(
    "invalid", RACE_PHYSICS_PRESET_INVALID));

  // The third production selector deliberately retains common's dynamic
  // authoritative vector instead of constructing a custom named Q2 vector.
  Race_PhysicsService_TestSetSelector(
    RACE_PHYSICS_PRESET_QUETOO_COMMON_V1_KEY);
  Race_PhysicsService_ConfigureLevel("common-map");
  ck_assert_str_eq(race_physics_service_config_string,
                   "v2\\quetoo\\quetoo-common-v1\\off");
  ck_assert_int_eq(Race_Physics_Current()->q2_snap_mode,
                   RACE_PHYSICS_Q2_SNAP_OFF);
  pm_params_t sentinel;
  memset(&sentinel, 0x5a, sizeof(sentinel));
  human.ps.pm_state.params = sentinel;
  Race_PhysicsService_SeedClient(&human);
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &sentinel));
  human_move = Race_PhysicsService_TestMove(&human);
  G_PrepareMove(&human, &human_move);
  ck_assert(Race_Physics_ParamsEqual(&human_move.s.params, &sentinel));

  Race_PhysicsService_Shutdown();
} END_TEST

START_TEST(_Race_PhysicsServiceGravityOverrideLifecycle) {
  Race_PhysicsService_TestImports();
  Race_PhysicsService_Init();

  pm_params_t q2;
  ck_assert(Race_Physics_FixedParamsForPreset(
    RACE_PHYSICS_PRESET_Q2, &q2));
  Race_PhysicsService_ConfigureLevel("gravity-default");
  ck_assert(Race_PhysicsService_Rankable());

  g_client_t human;
  memset(&human, 0, sizeof(human));
  human.ps.client = 1u;
  Race_PhysicsService_SeedClient(&human);
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &q2));

  pm_params_t custom = q2;
  custom.gravity = 700;
  g_level.gravity = custom.gravity;
  Race_PhysicsService_RefreshLevelParams();
  ck_assert(!Race_PhysicsService_Rankable());
  Race_PhysicsService_SeedClient(&human);
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &custom));

  pm_move_t move = Race_PhysicsService_TestMove(&human);
  G_PrepareMove(&human, &move);
  ck_assert(Race_Physics_ParamsEqual(&move.s.params, &custom));
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &custom));

  custom.gravity = 777;
  g_level.gravity = custom.gravity;
  Race_PhysicsService_ConfigureLevel("gravity-restart");
  ck_assert(!Race_PhysicsService_Rankable());
  memset(&human.ps.pm_state.params, 0, sizeof(human.ps.pm_state.params));
  Race_PhysicsService_SeedClient(&human);
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &custom));

  g_level.gravity = q2.gravity;
  Race_PhysicsService_RefreshLevelParams();
  ck_assert(Race_PhysicsService_Rankable());
  Race_PhysicsService_SeedClient(&human);
  ck_assert(Race_Physics_ParamsEqual(&human.ps.pm_state.params, &q2));

  Race_PhysicsService_Shutdown();
} END_TEST

START_TEST(_Race_PhysicsServiceAiLandingContract) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1,
    RACE_PHYSICS_PRESET_DP2_V1
  };
  static const uint16_t command_msec[] = { 8u, 16u, 25u, 50u };

  Race_PhysicsService_TestImports();
  Race_PhysicsService_Init();

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    pm_params_t expected;
    ck_assert(Race_Physics_FixedParamsForPreset(presets[preset], &expected));
    ck_assert(Race_PhysicsService_ConfigureTestLevel(
      "ai-landing", presets[preset]));

    for (size_t command = 0; command < lengthof(command_msec); command++) {
      g_client_t bot;
      memset(&bot, 0, sizeof(bot));
      bot.ps.client = 2u;
      bot.ai = (struct ai_s *) (uintptr_t) 1u;
      Race_PhysicsService_SeedClient(&bot);

      // This is the direct AI contract: copy the already-seeded player state
      // and call the shared mover without G_PrepareMove or a last-moment patch.
      pm_move_t move = Race_PhysicsService_TestLandingMove(
        &bot, command_msec[command]);
      Pm_Move(&move);
      ck_assert_ptr_eq(move.ground.ent, &race_physics_service_floor);
      ck_assert(move.s.flags & PMF_ON_GROUND);
      ck_assert(move.s.flags & PMF_JUMP_HELD);
      ck_assert(!(move.s.flags & (PMF_JUMPED | PMF_TIME_LAND)));
      ck_assert_uint_eq(move.s.time, 0u);
      ck_assert(Race_Physics_ParamsEqual(&move.s.params, &expected));

      for (size_t hold = 0; hold < 3u; hold++) {
        move.cmd = (pm_cmd_t) {
          .msec = command_msec[command],
          .up = 10
        };
        Pm_Move(&move);
        ck_assert(move.s.flags & PMF_ON_GROUND);
        ck_assert(move.s.flags & PMF_JUMP_HELD);
        ck_assert(!(move.s.flags & (PMF_JUMPED | PMF_TIME_LAND)));
      }

      move.cmd.up = 0;
      Pm_Move(&move);
      ck_assert(!(move.s.flags & PMF_JUMP_HELD));
      ck_assert(move.s.flags & PMF_ON_GROUND);

      move.cmd.up = 10;
      Pm_Move(&move);
      ck_assert(move.s.flags & PMF_JUMPED);
      ck_assert(move.s.flags & PMF_JUMP_HELD);
      ck_assert(!(move.s.flags & (PMF_ON_GROUND | PMF_TIME_LAND)));
      ck_assert_uint_eq(move.s.time, 0u);
      ck_assert(Race_Physics_ParamsEqual(&move.s.params, &expected));

      Pm_Move(&move);
      ck_assert(!(move.s.flags & PMF_JUMPED));
      ck_assert(move.s.flags & PMF_JUMP_HELD);
    }
  }

  Race_PhysicsService_Shutdown();
} END_TEST

static void Race_PhysicsService_AssertQ2StandingHull(void) {
  const box3_t bounds = Pm_PlayerBounds(false);
  ck_assert_float_eq(bounds.mins.x, -16.f);
  ck_assert_float_eq(bounds.mins.y, -16.f);
  ck_assert_float_eq(bounds.mins.z, -24.f);
  ck_assert_float_eq(bounds.maxs.x, 16.f);
  ck_assert_float_eq(bounds.maxs.y, 16.f);
  ck_assert_float_eq(bounds.maxs.z, 32.f);
}

START_TEST(_Race_PhysicsServiceFirstLinkHullLifecycle) {
  static const race_physics_preset_id_t presets[] = {
    RACE_PHYSICS_PRESET_Q2,
    RACE_PHYSICS_PRESET_QUETOO_FIX_V1,
    RACE_PHYSICS_PRESET_DP2_V1
  };

  Race_PhysicsService_TestImports();
  Race_PhysicsService_Init();

  for (size_t preset = 0; preset < lengthof(presets); preset++) {
    ck_assert(Race_PhysicsService_ConfigureTestLevel(
      "first-link", presets[preset]));

    g_client_t human, bot;
    memset(&human, 0, sizeof(human));
    memset(&bot, 0, sizeof(bot));
    human.ps.client = 1u;
    bot.ps.client = 2u;
    bot.ai = (struct ai_s *) (uintptr_t) 1u;
    Race_PhysicsService_SeedClient(&human);
    Race_PhysicsService_SeedClient(&bot);
    Race_PhysicsService_AssertQ2StandingHull();

    // Reconnect uses the still-active map-fixed identity.
    memset(&human, 0, sizeof(human));
    human.ps.client = 1u;
    Race_PhysicsService_SeedClient(&human);
    Race_PhysicsService_AssertQ2StandingHull();

    // Map restart seeds clients before reasserting the same level selection.
    memset(&bot.ps.pm_state.params, 0, sizeof(bot.ps.pm_state.params));
    Race_PhysicsService_SeedClient(&bot);
    Race_PhysicsService_AssertQ2StandingHull();
    ck_assert(Race_PhysicsService_ConfigureTestLevel(
      "first-link", presets[preset]));
    Race_PhysicsService_AssertQ2StandingHull();

    // Server restart selects the map-fixed identity before either first link.
    Race_PhysicsService_Shutdown();
    Race_PhysicsService_Init();
    ck_assert(Race_PhysicsService_ConfigureTestLevel(
      "first-link", presets[preset]));
    memset(&human, 0, sizeof(human));
    memset(&bot, 0, sizeof(bot));
    human.ps.client = 1u;
    bot.ps.client = 2u;
    bot.ai = (struct ai_s *) (uintptr_t) 1u;
    Race_PhysicsService_SeedClient(&human);
    Race_PhysicsService_SeedClient(&bot);
    Race_PhysicsService_AssertQ2StandingHull();
  }

  Race_PhysicsService_Shutdown();
} END_TEST

void Race_PhysicsService_AddTests(TCase *tcase) {
  tcase_add_test(tcase, _Race_PhysicsServiceHydrationLifecycle);
  tcase_add_test(tcase, _Race_PhysicsServiceGravityOverrideLifecycle);
  tcase_add_test(tcase, _Race_PhysicsServiceAiLandingContract);
  tcase_add_test(tcase, _Race_PhysicsServiceFirstLinkHullLifecycle);
}
