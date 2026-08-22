/*
 * Copyright(c) 2006 Quetoo.
 *
 * Deterministic integration fixture for the Race CGAME lifecycle coordinator.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cg_local.h"
#include "cg_module_compat.h"
#include "cg_race_barriers.h"
#include "cg_race_finish_report.h"
#include "cg_race_hud.h"
#include "cg_race_map_browser.h"
#include "cg_race_markers.h"
#include "cg_race_physics.h"
#include "cg_race_practice_markers.h"
#include "cg_race_replay.h"
#include "cg_race_training.h"
#include "ui/home/HomeViewController.h"
#include "ui/main/MainViewController.h"
#include "ui/voting/VotingViewController.h"

cg_import_t cgi;
cg_state_t cg_state;

typedef enum {
  EVENT_BARRIER_CLEAR,
  EVENT_MAP_CLEAR,
  EVENT_FINISH_CLEAR,
  EVENT_HUD_CLEAR,
  EVENT_REPLAY_CLEAR,
  EVENT_PRACTICE_CLEAR,
  EVENT_PHYSICS_CLEAR,
  EVENT_TRAINING_CLEAR,
  EVENT_MAIN_CLEAR,
  EVENT_BARRIER_PREPARE,
  EVENT_TRAINING_PREPARE,
  EVENT_TRAINING_COMPLETE_COMMAND,
  EVENT_TRAINING_COMPLETE,
  EVENT_BARRIER_TRACE,
  EVENT_BARRIER_DRAW,
  EVENT_MARKERS_DRAW,
  EVENT_PRACTICE_DRAW,
  EVENT_REPLAY_DRAW,
  EVENT_HUD_MESSAGE,
  EVENT_FINISH_MESSAGE,
  EVENT_MAP_MESSAGE,
  EVENT_REPLAY_MESSAGE,
  EVENT_BARRIER_LOAD,
  EVENT_PRACTICE_LOAD,
  EVENT_REPLAY_LOAD
} race_cgame_event_t;

static race_cgame_event_t events[64];
static size_t num_events;
static uint32_t assertions;
static uint32_t failures;
static bool hud_owns;
static bool finish_owns;
static bool map_owns;
static bool replay_owns;
static bool replay_active;
static bool physics_synchronized = true;

#define MODULE_CHECK(condition, label) do { \
  assertions++; \
  if (!(condition)) { \
    fprintf(stderr, "FAIL:%s:%d:%s\n", __FILE__, __LINE__, label); \
    failures++; \
  } \
} while (0)

static void Record(const race_cgame_event_t event) {
  if (num_events < lengthof(events)) {
    events[num_events++] = event;
  }
}

static void ResetEvents(void) {
  memset(events, 0, sizeof(events));
  num_events = 0;
}

static bool EventsEqual(const race_cgame_event_t *expected,
                        const size_t count) {
  return num_events == count &&
         !memcmp(events, expected, count * sizeof(*expected));
}

void Cg_RacePhysics_Init(void) { }
void Cg_RacePhysics_Shutdown(void) { }
void Cg_RacePhysics_Clear(void) { Record(EVENT_PHYSICS_CLEAR); }
bool Cg_RacePhysics_Synchronized(void) { return physics_synchronized; }
void Cg_RaceFinishReport_Init(void) { }
void Cg_RaceFinishReport_Clear(void) { Record(EVENT_FINISH_CLEAR); }
bool Cg_RaceFinishReport_ParseMessage(const int32_t command) {
  (void) command;
  Record(EVENT_FINISH_MESSAGE);
  return finish_owns;
}
void Cg_RaceHud_Init(void) { }
void Cg_RaceHud_Clear(void) { Record(EVENT_HUD_CLEAR); }
bool Cg_RaceHud_ParseMessage(const int32_t command) {
  (void) command;
  Record(EVENT_HUD_MESSAGE);
  return hud_owns;
}
void Cg_RaceMarkers_Init(void) { }
void Cg_RaceMarkers_Draw(void) { Record(EVENT_MARKERS_DRAW); }
void Cg_RacePracticeMarkers_Init(void) { }
void Cg_RacePracticeMarkers_Shutdown(void) { }
void Cg_RacePracticeMarkers_Clear(void) { Record(EVENT_PRACTICE_CLEAR); }
void Cg_RacePracticeMarkers_Load(void) { Record(EVENT_PRACTICE_LOAD); }
void Cg_RacePracticeMarkers_Draw(void) { Record(EVENT_PRACTICE_DRAW); }
void Cg_RaceReplay_Init(void) { }
void Cg_RaceReplay_Clear(void) { Record(EVENT_REPLAY_CLEAR); }
void Cg_RaceReplay_LoadMedia(void) { Record(EVENT_REPLAY_LOAD); }
bool Cg_RaceReplay_ParseMessage(const int32_t command) {
  (void) command;
  Record(EVENT_REPLAY_MESSAGE);
  return replay_owns;
}
void Cg_RaceReplay_PopulateScene(void) { Record(EVENT_REPLAY_DRAW); }
bool Cg_ReplayActive(void) { return replay_active; }
void Cg_RaceTraining_Init(void) { }
void Cg_RaceTraining_Clear(void) { Record(EVENT_TRAINING_CLEAR); }
void Cg_RaceTraining_PreparePredictionCommand(
    pm_move_t *pm, const size_t index, const size_t count) {
  (void) pm;
  (void) index;
  (void) count;
  Record(EVENT_TRAINING_PREPARE);
}
void Cg_RaceTraining_CompletePredictionCommand(
    const pm_move_t *pm, const size_t index, const size_t count) {
  (void) pm;
  (void) index;
  (void) count;
  Record(EVENT_TRAINING_COMPLETE_COMMAND);
}
void Cg_RaceTraining_CompletePrediction(const pm_move_t *pm) {
  (void) pm;
  Record(EVENT_TRAINING_COMPLETE);
}
bool Cg_RaceMapBrowser_ParseMessage(const int32_t command) {
  (void) command;
  Record(EVENT_MAP_MESSAGE);
  return map_owns;
}
void Cg_RaceMapBrowser_Clear(void) { Record(EVENT_MAP_CLEAR); }
void Cg_RaceBarriers_Clear(void) { Record(EVENT_BARRIER_CLEAR); }
void Cg_RaceBarriers_Load(void) { Record(EVENT_BARRIER_LOAD); }
cm_trace_t Cg_RaceBarriers_TracePrediction(
    const vec3_t start, const vec3_t end, const box3_t bounds) {
  (void) start;
  (void) bounds;
  Record(EVENT_BARRIER_TRACE);
  return (cm_trace_t) { .fraction = .25f, .end = end };
}
void Cg_RaceBarriers_PreparePredictionCommand(const size_t index) {
  (void) index;
  Record(EVENT_BARRIER_PREPARE);
}
void Cg_RaceBarriers_Draw(void) { Record(EVENT_BARRIER_DRAW); }
void HomeViewController_ClearState(void) { }
void HomeViewController_RefreshPlayerActions(const player_state_t *ps) {
  (void) ps;
}
void MainViewController_ClearState(void) { Record(EVENT_MAIN_CLEAR); }
void MainViewController_RefreshEscState(void) { }
void MainViewController_RefreshAdmin(const player_state_t *ps) { (void) ps; }
void MainViewController_RefreshVote(void) { }
void VotingViewController_Refresh(const player_state_t *ps) { (void) ps; }
cl_entity_t *Cg_Self(void) {
  static cl_entity_t self;
  return &self;
}

static cvar_t module_cvar = { .name = "cg_show_jumpers", .integer = 1 };
static cmd_t module_cmd;

static cvar_t *AddCvar(const char *name, const char *value,
                       const uint32_t flags, const char *description) {
  (void) name;
  (void) value;
  (void) flags;
  (void) description;
  return &module_cvar;
}

static cmd_t *AddCmd(const char *name, CmdExecuteFunc execute,
                     const uint32_t flags, const char *description) {
  module_cmd = (cmd_t) {
    .name = name,
    .description = description,
    .Execute = execute,
    .flags = flags
  };
  return &module_cmd;
}

uint32_t Race_NativeTestCgameModule(uint32_t *assertion_count) {
  memset(&cgi, 0, sizeof(cgi));
  cgi.AddCvar = AddCvar;
  cgi.AddCmd = AddCmd;
  Cg_Module_Init();
  MODULE_CHECK(!strcmp(module_cmd.name, "jumpers") && module_cmd.Execute,
               "module initialization registers jumpers command");

  ResetEvents();
  Cg_Module_ClearState();
  const race_cgame_event_t clear[] = {
    EVENT_MAIN_CLEAR, EVENT_MAP_CLEAR, EVENT_FINISH_CLEAR, EVENT_HUD_CLEAR,
    EVENT_REPLAY_CLEAR, EVENT_PRACTICE_CLEAR, EVENT_PHYSICS_CLEAR,
    EVENT_TRAINING_CLEAR, EVENT_BARRIER_CLEAR
  };
  MODULE_CHECK(EventsEqual(clear, lengthof(clear)),
               "module clear-state order and barrier reset");

  ResetEvents();
  pm_move_t pm = { };
  Cg_Module_PreparePredictionCommand(&pm, 3u, 9u);
  Cg_Module_CompletePredictionCommand(&pm, 3u, 9u);
  Cg_Module_CompletePrediction(&pm);
  const cm_trace_t trace = Cg_Module_TracePrediction(
    Vec3_Zero(), Vec3(8.f, 0.f, 0.f), Box3_Zero());
  const race_cgame_event_t prediction[] = {
    EVENT_BARRIER_PREPARE, EVENT_TRAINING_PREPARE,
    EVENT_TRAINING_COMPLETE_COMMAND, EVENT_TRAINING_COMPLETE,
    EVENT_BARRIER_TRACE
  };
  MODULE_CHECK(EventsEqual(prediction, lengthof(prediction)) &&
               trace.fraction == .25f && trace.end.x == 8.f,
               "prediction lifecycle and barrier trace delegation");

  ResetEvents();
  Cg_Module_LoadMedia();
  const race_cgame_event_t media[] = {
    EVENT_BARRIER_LOAD, EVENT_PRACTICE_LOAD, EVENT_REPLAY_LOAD
  };
  MODULE_CHECK(EventsEqual(media, lengthof(media)),
               "media lifecycle starts with barrier catalog");

  ResetEvents();
  Cg_Module_PopulateScene();
  const race_cgame_event_t scene[] = {
    EVENT_BARRIER_DRAW, EVENT_MARKERS_DRAW, EVENT_PRACTICE_DRAW,
    EVENT_REPLAY_DRAW
  };
  MODULE_CHECK(EventsEqual(scene, lengthof(scene)),
               "scene delegation order");

  hud_owns = true;
  finish_owns = map_owns = replay_owns = false;
  ResetEvents();
  MODULE_CHECK(Cg_Module_ParseMessage(77) && num_events == 1u &&
               events[0] == EVENT_HUD_MESSAGE,
               "message first owner short-circuits");

  hud_owns = false;
  finish_owns = true;
  ResetEvents();
  const race_cgame_event_t finish_message[] = {
    EVENT_HUD_MESSAGE, EVENT_FINISH_MESSAGE
  };
  MODULE_CHECK(Cg_Module_ParseMessage(78) &&
               EventsEqual(finish_message, lengthof(finish_message)),
               "message later owner short-circuits");

  finish_owns = false;
  ResetEvents();
  const race_cgame_event_t fallthrough[] = {
    EVENT_HUD_MESSAGE, EVENT_FINISH_MESSAGE, EVENT_MAP_MESSAGE,
    EVENT_REPLAY_MESSAGE
  };
  MODULE_CHECK(!Cg_Module_ParseMessage(79) &&
               EventsEqual(fallthrough, lengthof(fallthrough)),
               "stock-command fallthrough remains unowned");

  replay_active = false;
  physics_synchronized = true;
  MODULE_CHECK(!Cg_Module_DisablePrediction(),
               "prediction enabled while synchronized");
  replay_active = true;
  MODULE_CHECK(Cg_Module_DisablePrediction(),
               "replay disables prediction");
  replay_active = false;
  physics_synchronized = false;
  MODULE_CHECK(Cg_Module_DisablePrediction(),
               "unsynchronized physics disables prediction");

  if (assertion_count) {
    *assertion_count = assertions;
  }
  return failures;
}
