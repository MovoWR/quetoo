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
#include "cg_race_admin_auth.h"
#include "cg_race_admin_command.h"
#include "cg_race_barriers.h"
#include "cg_race_double_jump.h"
#include "cg_race_finish_report.h"
#include "cg_race_hud.h"
#include "cg_race_map_browser.h"
#include "cg_race_markers.h"
#include "cg_race_client_file.h"
#include "cg_race_physics.h"
#include "cg_race_practice_markers.h"
#include "cg_race_profiles.h"
#include "cg_race_replay.h"
#include "cg_race_training.h"
#include "cg_race_weapon_tuning.h"
#include "race_hook.h"
#include "ui/home/HomeViewController.h"
#include "ui/main/MainViewController.h"
#include "ui/voting/VotingViewController.h"

cg_import_t cgi;
cg_state_t cg_state;

typedef enum {
  EVENT_PROFILE_MESSAGE,
  EVENT_ADMIN_AUTH_CLEAR,
  EVENT_ADMIN_AUTH_MESSAGE,
  EVENT_WEAPON_TUNING_CLEAR,
  EVENT_WEAPON_TUNING_UPDATE,
  EVENT_WEAPON_TUNING_MESSAGE,
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
static bool profile_owns;
static bool admin_auth_owns;
static bool hud_owns;
static bool finish_owns;
static bool map_owns;
static bool replay_owns;
static bool weapon_tuning_owns;
static bool replay_active;
static bool physics_synchronized = true;
static bool hook_pull_speed_valid = true;

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

void Cg_RaceAdminAuth_Init(void) { }
void Cg_RaceAdminAuth_Shutdown(void) { }
void Cg_RaceAdminAuth_Clear(void) { Record(EVENT_ADMIN_AUTH_CLEAR); }
bool Cg_RaceAdminAuth_ParseMessage(const int32_t command) {
  (void) command;
  Record(EVENT_ADMIN_AUTH_MESSAGE);
  return admin_auth_owns;
}

void Cg_RaceProfiles_Init(void) { }
void Cg_RaceProfiles_Shutdown(void) { }
bool Cg_RaceProfiles_ParseMessage(const int32_t command) {
  (void) command;
  Record(EVENT_PROFILE_MESSAGE);
  return profile_owns;
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
bool Cg_HookPullSpeedValid(void) { return hook_pull_speed_valid; }
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
// The registry mirror is a passive ConfigString reader with no lifecycle of
// its own to assert, so it stubs out like the tuning cache above.
void Cg_RaceSettings_Init(void) { }
void Cg_RaceSettings_Clear(void) { }
void Cg_RaceWeaponTuning_Init(void) { }
void Cg_RaceWeaponTuning_Clear(void) {
  Record(EVENT_WEAPON_TUNING_CLEAR);
}
void Cg_RaceWeaponTuning_Update(void) {
  Record(EVENT_WEAPON_TUNING_UPDATE);
}
bool Cg_RaceWeaponTuning_ParseMessage(const int32_t command) {
  (void) command;
  Record(EVENT_WEAPON_TUNING_MESSAGE);
  return weapon_tuning_owns;
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
static cvar_t up_speed_cvar = {
  .name = "cl_up_speed",
  .value = 300.f
};
static cmd_t module_cmds[8];
static size_t num_module_cmds;
static uint32_t common_move_calls;

typedef enum {
  CLIENT_FILE_SUCCESS,
  CLIENT_FILE_OPEN_FAILURE,
  CLIENT_FILE_SHORT_WRITE,
  CLIENT_FILE_CLOSE_FAILURE,
  CLIENT_FILE_READBACK_MISMATCH
} client_file_outcome_t;

static client_file_outcome_t client_file_outcome;
static uint8_t client_file_written[64];
static uint8_t client_file_readback[64];
static size_t client_file_written_length;

static file_t *ClientFileOpen(const char *path) {
  (void) path;
  client_file_written_length = 0u;
  return client_file_outcome == CLIENT_FILE_OPEN_FAILURE
    ? NULL : (file_t *) client_file_written;
}

static int64_t ClientFileWrite(file_t *file, const void *buffer,
                               size_t size, size_t count) {
  (void) file;
  const size_t write_count = client_file_outcome == CLIENT_FILE_SHORT_WRITE && count
    ? count - 1u : count;
  const size_t bytes = size * write_count;
  if (bytes > sizeof(client_file_written)) {
    return -1;
  }
  memcpy(client_file_written, buffer, bytes);
  client_file_written_length = bytes;
  return (int64_t) write_count;
}

static bool ClientFileClose(file_t *file) {
  (void) file;
  return client_file_outcome != CLIENT_FILE_CLOSE_FAILURE;
}

static int64_t ClientFileLoad(const char *path, void **buffer) {
  (void) path;
  memcpy(client_file_readback, client_file_written,
         client_file_written_length);
  if (client_file_outcome == CLIENT_FILE_READBACK_MISMATCH &&
      client_file_written_length) {
    client_file_readback[0] ^= 0xffu;
  }
  *buffer = client_file_readback;
  return (int64_t) client_file_written_length;
}

static void ClientFileFree(void *buffer) {
  (void) buffer;
}

static cvar_t *AddCvar(const char *name, const char *value,
                       const uint32_t flags, const char *description) {
  (void) name;
  (void) value;
  (void) flags;
  (void) description;
  return &module_cvar;
}

static cvar_t *GetCvar(const char *name) {
  return !strcmp(name, up_speed_cvar.name) ? &up_speed_cvar : NULL;
}

static cmd_t *AddCmd(const char *name, CmdExecuteFunc execute,
                     const uint32_t flags, const char *description) {
  if (num_module_cmds == lengthof(module_cmds)) {
    return NULL;
  }

  cmd_t *cmd = module_cmds + num_module_cmds++;
  *cmd = (cmd_t) {
    .name = name,
    .description = description,
    .Execute = execute,
    .flags = flags
  };
  return cmd;
}

static cmd_t *FindCmd(const char *name) {
  for (size_t i = 0; i < num_module_cmds; i++) {
    if (!strcmp(module_cmds[i].name, name)) {
      return module_cmds + i;
    }
  }
  return NULL;
}

static void KeyDown(button_t *button) {
  if (!(button->state & BUTTON_STATE_HELD)) {
    button->keys[0] = 1u;
    button->state |= BUTTON_STATE_HELD | BUTTON_STATE_DOWN;
  }
}

static void KeyUp(button_t *button) {
  memset(button, 0, sizeof(*button));
}

void Cg_Move(pm_cmd_t *cmd) {
  (void) cmd;
  common_move_calls++;
}

uint32_t Race_NativeTestCgameModule(uint32_t *assertion_count) {
  memset(&cgi, 0, sizeof(cgi));
  memset(module_cmds, 0, sizeof(module_cmds));
  num_module_cmds = 0;
  common_move_calls = 0;
  cgi.AddCvar = AddCvar;
  cgi.GetCvar = GetCvar;
  cgi.AddCmd = AddCmd;
  cgi.KeyDown = KeyDown;
  cgi.KeyUp = KeyUp;
  cgi.OpenFileWrite = ClientFileOpen;
  cgi.WriteFile = ClientFileWrite;
  cgi.CloseFile = ClientFileClose;
  cgi.LoadFile = ClientFileLoad;
  cgi.FreeFile = ClientFileFree;

  static const char persisted[] = "verified Race client data";
  client_file_outcome = CLIENT_FILE_SUCCESS;
  MODULE_CHECK(Cg_RaceClientFile_WriteVerified(
                 "race/test.0", persisted, sizeof(persisted)) &&
               client_file_written_length == sizeof(persisted),
               "client file helper accepts exact verified persistence");
  client_file_outcome = CLIENT_FILE_OPEN_FAILURE;
  MODULE_CHECK(!Cg_RaceClientFile_WriteVerified(
                 "race/test.0", persisted, sizeof(persisted)),
               "client file helper rejects open failure");
  client_file_outcome = CLIENT_FILE_SHORT_WRITE;
  MODULE_CHECK(!Cg_RaceClientFile_WriteVerified(
                 "race/test.0", persisted, sizeof(persisted)),
               "client file helper rejects short write");
  client_file_outcome = CLIENT_FILE_CLOSE_FAILURE;
  MODULE_CHECK(!Cg_RaceClientFile_WriteVerified(
                 "race/test.0", persisted, sizeof(persisted)),
               "client file helper rejects close failure");
  client_file_outcome = CLIENT_FILE_READBACK_MISMATCH;
  MODULE_CHECK(!Cg_RaceClientFile_WriteVerified(
                 "race/test.0", persisted, sizeof(persisted)),
               "client file helper rejects readback mismatch");

  float hook_speed = -1.f;
  MODULE_CHECK(Race_HookPullSpeed_Parse("0", &hook_speed) &&
               hook_speed == 0.f &&
               Race_HookPullSpeed_Parse("800", &hook_speed) &&
               hook_speed == RACE_HOOK_PULL_SPEED_DEFAULT &&
               Race_HookPullSpeed_Parse("11586", &hook_speed) &&
               hook_speed == RACE_HOOK_PULL_SPEED_MAX,
               "paired hook speed parser accepts its complete bounded range");
  static const char *invalid_hook_speeds[] = {
    "", " 800", "800 ", "800junk", "-1", "11587", "nan", "inf",
    "-inf", "1e999"
  };
  for (size_t i = 0; i < lengthof(invalid_hook_speeds); i++) {
    hook_speed = 123.f;
    MODULE_CHECK(!Race_HookPullSpeed_Parse(invalid_hook_speeds[i],
                                           &hook_speed) &&
                 hook_speed == 123.f,
                 "paired hook speed parser rejects hostile text transactionally");
  }

  char map_token[MAX_QPATH];
  MODULE_CHECK(Cg_RaceAdminCommand_MapToken("maps/potato.bsp", map_token) &&
               !strcmp(map_token, "potato") &&
               Cg_RaceAdminCommand_MapToken("mzc_dj.bsp", map_token) &&
               !strcmp(map_token, "mzc_dj"),
               "administrator map command accepts canonical BSP identities");
  static const char *invalid_map_configstrings[] = {
    "", "maps/.bsp", "maps/potato", "maps/potato.pk3",
    "maps/potato;quit.bsp", "maps/potato.bsp;quit", "maps/../potato.bsp",
    "maps/subdir/potato.bsp", "maps/potato\\quit.bsp",
    "maps/potato\nquit.bsp"
  };
  for (size_t i = 0; i < lengthof(invalid_map_configstrings); i++) {
    memcpy(map_token, "unchanged", sizeof("unchanged"));
    MODULE_CHECK(!Cg_RaceAdminCommand_MapToken(
                   invalid_map_configstrings[i], map_token) && !*map_token,
                 "administrator map command rejects hostile configstrings");
  }

  Cg_Module_Init();
  cmd_t *double_jump_down = FindCmd("+double_jump");
  cmd_t *double_jump_up = FindCmd("-double_jump");
  cmd_t *jumpers = FindCmd("jumpers");
  MODULE_CHECK(double_jump_down && double_jump_down->Execute &&
               double_jump_up && double_jump_up->Execute &&
               jumpers && jumpers->Execute,
               "module initialization registers Race input commands");

  double_jump_down->Execute();
  pm_cmd_t preview = { .msec = 8 };
  Cg_RaceDoubleJump_Preview(&preview);
  Cg_RaceDoubleJump_Preview(&preview);
  MODULE_CHECK(preview.up == 2400,
               "prediction preview is repeatable without double-adding");

  pm_cmd_t first = { .msec = 8 };
  Cg_RaceDoubleJump_Move(&first);
  MODULE_CHECK(first.up == 2400 && common_move_calls == 1u,
               "first finalized command jumps and delegates common movement");

  pm_cmd_t neutral_preview = { .msec = 8 };
  Cg_RaceDoubleJump_Preview(&neutral_preview);
  pm_cmd_t neutral = { .msec = 8 };
  Cg_RaceDoubleJump_Move(&neutral);
  MODULE_CHECK(neutral_preview.up == 0 && neutral.up == 0,
               "second command releases jump in prediction and final input");

  pm_cmd_t second_preview = { .msec = 8 };
  Cg_RaceDoubleJump_Preview(&second_preview);
  pm_cmd_t second = { .msec = 8 };
  Cg_RaceDoubleJump_Move(&second);
  MODULE_CHECK(second_preview.up == 2400 && second.up == 2400,
               "third command restores jump symmetrically");

  double_jump_down->Execute();
  pm_cmd_t repeated = { .msec = 8 };
  Cg_RaceDoubleJump_Move(&repeated);
  MODULE_CHECK(repeated.up == 2400,
               "repeated key down does not restart the sequence");

  double_jump_up->Execute();
  pm_cmd_t released = { .msec = 8 };
  Cg_RaceDoubleJump_Preview(&released);
  MODULE_CHECK(released.up == 0,
               "physical release cancels the sequence");

  double_jump_down->Execute();

  ResetEvents();
  Cg_Module_ClearState();
  const race_cgame_event_t clear[] = {
    EVENT_ADMIN_AUTH_CLEAR, EVENT_WEAPON_TUNING_CLEAR,
    EVENT_MAIN_CLEAR, EVENT_MAP_CLEAR,
    EVENT_FINISH_CLEAR, EVENT_HUD_CLEAR,
    EVENT_REPLAY_CLEAR, EVENT_PRACTICE_CLEAR, EVENT_PHYSICS_CLEAR,
    EVENT_TRAINING_CLEAR, EVENT_BARRIER_CLEAR
  };
  MODULE_CHECK(EventsEqual(clear, lengthof(clear)),
               "module clear-state order and barrier reset");
  pm_cmd_t cleared = { .msec = 8 };
  Cg_RaceDoubleJump_Preview(&cleared);
  MODULE_CHECK(cleared.up == 0,
               "module clear state cancels held double jump input");

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

  ResetEvents();
  Cg_Module_Update();
  const race_cgame_event_t update[] = {
    EVENT_WEAPON_TUNING_UPDATE
  };
  MODULE_CHECK(EventsEqual(update, lengthof(update)),
               "frame sync updates authoritative weapon tuning");

  profile_owns = true;
  admin_auth_owns = false;
  weapon_tuning_owns = false;
  hud_owns = finish_owns = map_owns = replay_owns = false;
  ResetEvents();
  MODULE_CHECK(Cg_Module_ParseMessage(75) && num_events == 1u &&
               events[0] == EVENT_PROFILE_MESSAGE,
               "ranked profile authentication owns its server message first");

  profile_owns = false;
  admin_auth_owns = true;
  weapon_tuning_owns = false;
  hud_owns = finish_owns = map_owns = replay_owns = false;
  ResetEvents();
  const race_cgame_event_t admin_auth_message[] = {
    EVENT_PROFILE_MESSAGE, EVENT_ADMIN_AUTH_MESSAGE
  };
  MODULE_CHECK(Cg_Module_ParseMessage(76) &&
               EventsEqual(admin_auth_message, lengthof(admin_auth_message)),
               "administrator authentication follows ranked profiles");

  admin_auth_owns = false;
  weapon_tuning_owns = true;
  ResetEvents();
  const race_cgame_event_t weapon_tuning_message[] = {
    EVENT_PROFILE_MESSAGE, EVENT_ADMIN_AUTH_MESSAGE,
    EVENT_WEAPON_TUNING_MESSAGE
  };
  MODULE_CHECK(Cg_Module_ParseMessage(77) &&
               EventsEqual(weapon_tuning_message,
                           lengthof(weapon_tuning_message)),
               "weapon tuning owns its message after administrator auth");

  weapon_tuning_owns = false;
  hud_owns = true;
  finish_owns = map_owns = replay_owns = false;
  ResetEvents();
  MODULE_CHECK(Cg_Module_ParseMessage(78) && num_events == 4u &&
               events[0] == EVENT_PROFILE_MESSAGE &&
               events[1] == EVENT_ADMIN_AUTH_MESSAGE &&
               events[2] == EVENT_WEAPON_TUNING_MESSAGE &&
               events[3] == EVENT_HUD_MESSAGE,
               "message first owner short-circuits");

  hud_owns = false;
  finish_owns = true;
  ResetEvents();
  const race_cgame_event_t finish_message[] = {
    EVENT_PROFILE_MESSAGE, EVENT_ADMIN_AUTH_MESSAGE,
    EVENT_WEAPON_TUNING_MESSAGE,
    EVENT_HUD_MESSAGE, EVENT_FINISH_MESSAGE
  };
  MODULE_CHECK(Cg_Module_ParseMessage(79) &&
               EventsEqual(finish_message, lengthof(finish_message)),
               "message later owner short-circuits");

  finish_owns = false;
  ResetEvents();
  const race_cgame_event_t fallthrough[] = {
    EVENT_PROFILE_MESSAGE, EVENT_ADMIN_AUTH_MESSAGE,
    EVENT_WEAPON_TUNING_MESSAGE,
    EVENT_HUD_MESSAGE, EVENT_FINISH_MESSAGE, EVENT_MAP_MESSAGE,
    EVENT_REPLAY_MESSAGE
  };
  MODULE_CHECK(!Cg_Module_ParseMessage(80) &&
               EventsEqual(fallthrough, lengthof(fallthrough)),
               "stock-command fallthrough remains unowned");

  replay_active = false;
  hook_pull_speed_valid = true;
  physics_synchronized = true;
  MODULE_CHECK(!Cg_Module_DisablePrediction(),
               "prediction enabled while synchronized");
  replay_active = true;
  MODULE_CHECK(Cg_Module_DisablePrediction(),
               "replay disables prediction");
  replay_active = false;
  hook_pull_speed_valid = false;
  MODULE_CHECK(Cg_Module_DisablePrediction(),
               "invalid hook tuning disables prediction");
  hook_pull_speed_valid = true;
  physics_synchronized = false;
  MODULE_CHECK(Cg_Module_DisablePrediction(),
               "unsynchronized physics disables prediction");

  if (assertion_count) {
    *assertion_count = assertions;
  }
  return failures;
}
