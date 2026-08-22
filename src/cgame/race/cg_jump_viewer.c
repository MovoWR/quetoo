#include "cg_local.h"
#include "cg_jump_viewer.h"
#include "cg_jump_viewer_math.h"
#include "cg_race_replay.h"

static cvar_t *cg_jump_viewer;
static cvar_t *cg_jump_viewer_path;
static cvar_t *cg_jump_viewer_hold_time;
static cg_jump_tracker_t cg_jump_viewer_tracker;
static cg_jump_replay_sample_state_t cg_jump_viewer_replay_sample;

static uint64_t Cg_JumpViewerLifecycle(const player_state_t *ps, const pm_state_t *pm) {
  // Shared score wire field: bits 0-15; spectator: bit 16; pm type: bits 17-23; deaths: bits 24-39.
  return ((uint64_t) (uint16_t) ps->stats[STAT_TEAM]) |
         ((uint64_t) !!ps->stats[STAT_SPECTATOR] << 16) |
         ((uint64_t) pm->type << 17) |
         ((uint64_t) (uint16_t) ps->stats[STAT_DEATHS] << 24);
}

static cg_jump_sample_t Cg_JumpViewerSample(const pm_state_t *pm, cg_jump_source_t source,
                                            int16_t identity, uint64_t lifecycle, uint64_t time) {
  return (cg_jump_sample_t) {
    .origin = pm->origin,
    .vertical_velocity = pm->velocity.z,
    .time = time,
    .source = source,
    .identity = identity,
    .lifecycle = lifecycle,
    .grounded = (pm->flags & PMF_ON_GROUND) != 0,
    .valid = pm->type == PM_NORMAL,
    .teleport = (pm->flags & PMF_TIME_TELEPORT) != 0,
    .predicted = source == CG_JUMP_SOURCE_PREDICTION,
    .launch = Cg_JumpViewer_ClassifyLaunch(pm->flags, pm->velocity.z)
  };
}

void Cg_InitJumpViewer(void) {
  cg_jump_viewer = cgi.AddCvar("cg_jump_viewer", "0", CVAR_ARCHIVE,
                               "Draw live jump length and height telemetry.");
  cg_jump_viewer_path = cgi.AddCvar("cg_jump_viewer_path", "0", CVAR_ARCHIVE,
                                    "Show accumulated 3D path distance in the jump viewer.");
  cg_jump_viewer_hold_time = cgi.AddCvar("cg_jump_viewer_hold_time", "3", CVAR_ARCHIVE,
                                         "Seconds to hold a completed jump result.");
}

void Cg_ClearJumpViewer(void) {
  Cg_JumpTracker_Clear(&cg_jump_viewer_tracker);
  memset(&cg_jump_viewer_replay_sample, 0,
         sizeof(cg_jump_viewer_replay_sample));
}

void Cg_JumpViewer_UpdatePredicted(const pm_state_t *pm) {
  if (Cg_JumpViewer_ClearOnUpdate(cg_jump_viewer != NULL,
                                  cg_jump_viewer && cg_jump_viewer->integer)) {
    Cg_ClearJumpViewer();
    return;
  }

  const player_state_t *ps = &cgi.client->frame.ps;
  const cg_jump_sample_t sample = Cg_JumpViewerSample(pm, CG_JUMP_SOURCE_PREDICTION,
                                                       ps->client, Cg_JumpViewerLifecycle(ps, pm),
                                                       cgi.client->unclamped_time);
  Cg_JumpTracker_Update(&cg_jump_viewer_tracker, &sample);
}

void Cg_JumpViewer_UpdateFrame(const player_state_t *ps) {
  if (Cg_JumpViewer_ClearOnUpdate(cg_jump_viewer != NULL,
                                  cg_jump_viewer && cg_jump_viewer->integer)) {
    Cg_ClearJumpViewer();
    return;
  }

  if (Cg_UsePrediction()) {
    return;
  }

  const int16_t chase = ps->stats[STAT_CHASE];
  const bool replay = Cg_ReplayActive();
  const cg_jump_source_t source = Cg_JumpViewer_SelectSource(false, replay, chase);
  const int16_t identity = source == CG_JUMP_SOURCE_CHASE ? chase : ps->client;
  uint64_t sample_time = cgi.client->unclamped_time;
  pm_state_t pm = ps->pm_state;
  if (replay) {
    uint32_t generation, playhead_ms, frame_cursor;
    bool paused;
    if (!Cg_ReplayTimeline(&generation, &playhead_ms, &frame_cursor, &paused) ||
        !Cg_JumpViewer_AcceptReplaySample(
          &cg_jump_viewer_replay_sample, generation, playhead_ms,
          frame_cursor, paused) ||
        !Cg_RaceReplay_Telemetry(&pm, NULL, NULL)) {
      return;
    }
    sample_time =
      Cg_JumpViewer_ReplayTelemetryTime(&cg_jump_viewer_replay_sample);
  }
  const cg_jump_sample_t sample = Cg_JumpViewerSample(&pm, source, identity,
                                                       Cg_JumpViewerLifecycle(ps, &pm),
                                                       sample_time);
  Cg_JumpTracker_Update(&cg_jump_viewer_tracker, &sample);
}

void Cg_DrawJumpViewer(const player_state_t *ps, int32_t bottom_offset) {
  const cg_jump_viewer_visibility_t visibility = Cg_JumpViewer_Visibility(
    cg_draw_hud->integer, cg_jump_viewer->integer, !ps->stats[STAT_TIME], editor->value,
    ps->pm_state.type == PM_DEAD, ps->stats[STAT_SPECTATOR] && !ps->stats[STAT_CHASE],
    ps->stats[STAT_SCORES] != 0);

  if (visibility == CG_JUMP_VIEWER_HIDE_AND_CLEAR) {
    Cg_ClearJumpViewer();
    return;
  }
  if (visibility == CG_JUMP_VIEWER_HIDE) {
    return;
  }

  const uint64_t hold_time = Clampf(cg_jump_viewer_hold_time->value, 0.f, 30.f) * 1000.f;
  const uint64_t now = Cg_ReplayActive()
                         ? Cg_JumpViewer_ReplayTelemetryTime(
                             &cg_jump_viewer_replay_sample)
                         : cgi.client->unclamped_time;
  bool live;
  const cg_jump_result_t *result = Cg_JumpViewer_SelectResult(&cg_jump_viewer_tracker,
                                                              now,
                                                              hold_time, &live);
  if (!result) {
    return;
  }

  cg_jump_viewer_text_t text;
  Cg_JumpViewer_FormatResult(result, live, cg_jump_viewer_path->integer, &text);
  char previous[96];

  const bool show_previous = live && cg_jump_viewer_tracker.completed.valid;
  if (show_previous) {
    q_snprintf(previous, sizeof(previous), "Previous: %.1f x %.1f",
               cg_jump_viewer_tracker.completed.length, cg_jump_viewer_tracker.completed.peak);
  }

  int32_t cw, ch;
  cgi.BindFont("medium", &cw, &ch);
  const int32_t panel_w = 240;
  const int32_t panel_h = (show_previous ? 5 : 4) * ch + 16;
  const int32_t x = Cg_JumpViewer_PanelX(cgi.context->w);
  const int32_t margin_bottom = cgi.context->w <= 1024 ? 12 : 18;
  const int32_t y = cgi.context->h - 64 - panel_h - margin_bottom - Maxi(bottom_offset, 0);
  cgi.Draw2DFill(x, y, panel_w, panel_h, Color4f(0.f, 0.f, 0.f, 0.7f));
  cgi.Draw2DFill(x, y, panel_w, 2, Color4f(1.f, 0.85f, 0.15f, 0.8f));

  int32_t text_y = y + 8;
  cgi.Draw2DString(x + 10, text_y, text.header, color_yellow);
  cgi.Draw2DString(x + 10, text_y += ch, text.length, color_white);
  cgi.Draw2DString(x + 10, text_y += ch, text.peak, color_white);
  cgi.Draw2DString(x + 10, text_y += ch, text.air, Color4f(0.9f, 0.9f, 0.9f, 0.9f));
  if (show_previous) {
    cgi.Draw2DString(x + 10, text_y += ch, previous, Color4f(0.7f, 0.7f, 0.7f, 0.8f));
  }
  cgi.BindFont(NULL, NULL, NULL);
}
