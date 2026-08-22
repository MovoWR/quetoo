/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <stdlib.h>

#include "cg_local.h"
#include "cg_input_viewer.h"
#include "cg_jump_viewer.h"
#include "cg_race_hud.h"
#include "cg_race_finish_report.h"
#include "cg_race_physics.h"
#include "cg_race_presentation.h"
#include "cg_race_replay.h"
#include "cg_race_training.h"
#include "cg_race_vote.h"
#include "cg_strafe_helper.h"
#include "race_physics.h"
#include "race_wire.h"

// Top stack, per §3.
#define RACE_HUD_STACK_TOP 56.f
#define RACE_HUD_RECORD_GAP 36.f

// Checkpoint ribbon, per §6.
#define RACE_HUD_RIBBON_OFFSET 96.f
#define RACE_HUD_RIBBON_PADDING 34.f
#define RACE_HUD_RIBBON_LABEL_GAP 9.f

// Pips and clock, per §8.
#define RACE_HUD_PIP_WIDTH 52.f
#define RACE_HUD_PIP_HEIGHT 5.f
#define RACE_HUD_PIP_GAP 5.f
#define RACE_HUD_PIP_CAPTION_GAP 9.f
#define RACE_HUD_CLOCK_CAPTION_GAP 6.f

// Speed, per §5.
#define RACE_HUD_SPEED_OFFSET 80.f
#define RACE_HUD_SPEED_GAP 10.f
#define RACE_HUD_SPEED_THRESHOLD 2.f
#define RACE_HUD_SPEED_LERP .25f
#define RACE_HUD_SPLIT_EVENT_MILLIS 4000u

static cvar_t *cg_hb_climb_helper;
static cvar_t *cg_race_hud_edge;
static cvar_t *cg_race_hud_timer_size;
static cvar_t *cg_race_hud_legibility;

/**
 * @brief Frame-persistent HUD state. The speed smoothing is the anti-strobe
 * for §5; the splits are the §6 ribbon's local capture.
 */
static struct {
  float smoothed_speed;
  uint32_t splits[RACE_MAX_CHECKPOINTS];
  uint16_t split_count;
  uint16_t split_total;
  int32_t top_stack_bottom;
  struct {
    char label[32];
    uint32_t cumulative;
    uint32_t segment;
    int32_t pb_delta;
    int32_t wr_delta;
    uint32_t received_time;
    uint8_t comparison_flags;
    uint8_t number;
    bool valid;
  } analytical;
} cg_race_hud;

void Cg_RaceHud_Clear(void) {
  memset(&cg_race_hud.analytical, 0, sizeof(cg_race_hud.analytical));
}

bool Cg_RaceHud_ParseMessage(const int32_t command) {
  if (command != SV_CMD_RACE_SPLIT) {
    return false;
  }

  const int32_t number = cgi.ReadByte();
  const char *label = cgi.ReadString();
  char copied_label[sizeof(cg_race_hud.analytical.label)];
  if (!label || q_strlen(label) >= sizeof(copied_label)) {
    Cg_Warn("Rejected malformed Race split event\n");
    Cg_RaceHud_Clear();
    return true;
  }
  q_strlcpy(copied_label, label, sizeof(copied_label));
  const int32_t cumulative = cgi.ReadLong();
  const int32_t segment = cgi.ReadLong();
  const int32_t flags = cgi.ReadByte();
  const int32_t pb_delta = cgi.ReadLong();
  const int32_t wr_delta = cgi.ReadLong();
  if (number < 1 || number > RACE_MAX_CHECKPOINTS ||
      cumulative <= 0 || segment <= 0 || segment > cumulative ||
      (flags & ~3)) {
    Cg_Warn("Rejected malformed Race split event\n");
    Cg_RaceHud_Clear();
    return true;
  }

  q_strlcpy(cg_race_hud.analytical.label, copied_label,
            sizeof(cg_race_hud.analytical.label));
  cg_race_hud.analytical.number = (uint8_t) number;
  cg_race_hud.analytical.cumulative = (uint32_t) cumulative;
  cg_race_hud.analytical.segment = (uint32_t) segment;
  cg_race_hud.analytical.comparison_flags = (uint8_t) flags;
  cg_race_hud.analytical.pb_delta = pb_delta;
  cg_race_hud.analytical.wr_delta = wr_delta;
  cg_race_hud.analytical.received_time = cgi.client->unclamped_time;
  cg_race_hud.analytical.valid = true;
  return true;
}

/**
 * @brief One entry in the Race HUD type scale.
 *
 * The design (§1) names seven sizes of Manrope with four weights. The 2D draw
 * path renders from three fixed bitmap atlases -- 16, 32 and 64 pixels tall,
 * monospace, ASCII only -- with no notion of weight or tracking. Each design
 * size therefore resolves to whichever engine font carries it best, and every
 * call site names the design size, so replacing this with real atlases later
 * is a change to this table and nothing else.
 */
typedef struct {
  const char *name;
  float size;
  const char *font;
} cg_race_hud_font_t;

// Seven design sizes over three atlases (16 / 32 / 64px), assigned to keep the
// contrasts the layout reads by rather than by nearest height alone:
//
//   large   the timer alone, so it stays the one hero element
//   medium  everything that is a value someone reads off -- speed, split
//           times, records -- which is what gives each ribbon cell and each
//           WR/PB pair its label-over-value contrast
//   small   everything that labels or qualifies a value
//
// Nearest-height would put race_value and race_record on "small" beside their
// own tags, flattening exactly that contrast, and would put race_speed on
// "large" level with the timer.
static const cg_race_hud_font_t cg_race_hud_font_scale[] = {
  { RACE_FONT_TIMER, 104.f, "large" },
  { RACE_FONT_SPEED, 44.f, "medium" },
  { RACE_FONT_ROUTE, 22.f, "small" },
  { RACE_FONT_VALUE, 24.f, "medium" },
  { RACE_FONT_RECORD, 20.f, "medium" },
  { RACE_FONT_BODY, 16.f, "small" },
  { RACE_FONT_LABEL, 14.f, "small" }
};

void Cg_RaceHud_BindFont(const char *name, int32_t *height) {
  const char *font = NULL;

  for (size_t i = 0; i < lengthof(cg_race_hud_font_scale); i++) {
    if (!q_strcmp(name, cg_race_hud_font_scale[i].name)) {
      font = cg_race_hud_font_scale[i].font;
      break;
    }
  }

  cgi.BindFont(font ? font : "medium", NULL, height);
}

int32_t Cg_RaceHud_Scale(const float value) {
  return (int32_t) roundf(value * (cgi.context->h / RACE_HUD_DESIGN_HEIGHT));
}

int32_t Cg_RaceHud_Edge(void) {
  return Cg_RaceHud_Scale(cg_race_hud_edge
    ? Clampf(cg_race_hud_edge->value, 0.f, 320.f) : RACE_HUD_STACK_TOP);
}

int32_t Cg_RaceHud_TopStackBottom(void) {
  return cg_race_hud.top_stack_bottom
    ? cg_race_hud.top_stack_bottom : Cg_RaceHud_Edge();
}

color_t Cg_RaceHud_Gold(void) {
  return Color4b(0xff, 0xcc, 0x33, 0xff);
}

color_t Cg_RaceHud_Green(void) {
  return Color4b(0x4c, 0xff, 0x6a, 0xff);
}

color_t Cg_RaceHud_Cyan(void) {
  return Color4b(0x8f, 0xd5, 0xf5, 0xff);
}

color_t Cg_RaceHud_Warn(void) {
  return Color4b(0xdc, 0x74, 0x88, 0xff);
}

/**
 * @return The dim tint shared by tags, captions and idle values.
 */
static color_t Cg_RaceHud_Dim(const float alpha) {
  return Color4f(1.f, 1.f, 1.f, alpha);
}

void Cg_RaceHud_DrawShadowedString(const int32_t x, const int32_t y,
                                   const char *text, const color_t color) {
  const color_t shadow = Color4f(0.f, 0.f, 0.f, .92f);
  const char *mode = cg_race_hud_legibility
    ? cg_race_hud_legibility->string : "none";

  if (!q_strcmp(mode, "stroke")) {
    cgi.Draw2DString(x - 1, y, text, shadow);
    cgi.Draw2DString(x + 1, y, text, shadow);
    cgi.Draw2DString(x, y - 1, text, shadow);
    cgi.Draw2DString(x, y + 1, text, shadow);
  } else if (!q_strcmp(mode, "plates")) {
    int32_t height;
    cgi.BindFont(NULL, NULL, &height);
    cgi.Draw2DFill(x - Cg_RaceHud_Scale(8.f), y - Cg_RaceHud_Scale(4.f),
                   cgi.StringWidth(text) + Cg_RaceHud_Scale(16.f),
                   height + Cg_RaceHud_Scale(8.f),
                   Color4f(.027f, .067f, .102f, .6f));
  } else if (!q_strcmp(mode, "shadow")) {
    cgi.Draw2DString(x, y + 2, text, shadow);
    cgi.Draw2DString(x + 1, y + 1, text, shadow);
  }

  cgi.Draw2DString(x, y, text, color);
}

void Cg_RaceHud_DrawRightAligned(const int32_t right, const int32_t y,
                                 const char *text, const color_t color) {
  Cg_RaceHud_DrawShadowedString(right - cgi.StringWidth(text), y, text, color);
}

void Cg_RaceHud_DrawCentered(const int32_t center_x, const int32_t y,
                             const char *text, const color_t color) {
  Cg_RaceHud_DrawShadowedString(center_x - cgi.StringWidth(text) / 2, y,
                                text, color);
}

/**
 * @return The accent driving the mode tag and the checkpoint pips, per §3.
 */
static color_t Cg_RaceHud_ModeAccent(const player_state_t *ps) {
  if ((race_run_state_t) ps->stats[STAT_RACE_RUN_STATE] == RACE_RUN_FINISHED) {
    return Cg_RaceHud_Green();
  }

  switch ((race_mode_t) ps->stats[STAT_RACE_MODE]) {
    case RACE_MODE_RACE:
      return Cg_RaceHud_Cyan();
    case RACE_MODE_PRACTICE:
      return Color4b(0xa5, 0x44, 0x4e, 0xff);
    default:
      return Cg_RaceHud_Gold();
  }
}

/**
 * @return The tracked, upper-case mode tag. This is the one place the design
 * breaks the design system's sentence-case rule, because it reads as a status
 * light rather than a label.
 */
static const char *Cg_RaceHud_ModeLabel(const race_mode_t mode) {
  switch (mode) {
    case RACE_MODE_RACE:
      return "RACE";
    case RACE_MODE_PRACTICE:
      return "PRACTICE";
    default:
      return "SPECTATE";
  }
}

/**
 * @return The timer tint. Nothing turns green before the run is finished --
 * green means done, never on pace.
 */
static color_t Cg_RaceHud_TimerColor(const player_state_t *ps) {
  if (ps->stats[STAT_SPECTATOR] && !ps->stats[STAT_CHASE]) {
    return Cg_RaceHud_Gold();
  }

  switch ((race_run_state_t) ps->stats[STAT_RACE_RUN_STATE]) {
    case RACE_RUN_FINISHED:
      return Cg_RaceHud_Green();
    case RACE_RUN_ACTIVE:
      return Color4f(.957f, .973f, .984f, .94f);
    default:
      return Color4f(.957f, .973f, .984f, .42f);
  }
}

static uint32_t Cg_RaceHud_WireTime(const player_state_t *ps,
                                    const int32_t low,
                                    const int32_t high) {
  return Race_WireElapsed(ps->stats[low], ps->stats[high]);
}

static void Cg_RaceHud_FormatRecord(const uint32_t elapsed,
                                    char *output,
                                    const size_t size) {
  if (!elapsed) {
    q_strlcpy(output, "--:--.---", size);
    return;
  }

  Cg_Race_FormatElapsed(elapsed, output, size);
}

static void Cg_RaceHud_FormatDelta(const int32_t delta, char *output,
                                   const size_t size) {
  const int64_t magnitude = delta < 0 ? -(int64_t) delta : delta;
  q_snprintf(output, size, "%c%lld.%03lld",
             delta < 0 ? '-' : '+',
             (long long) (magnitude / 1000),
             (long long) (magnitude % 1000));
}

static uint16_t Cg_RaceHud_CheckpointTotal(void) {
  const char *value = cgi.ConfigString(CS_RACE_CHECKPOINT_TOTAL);
  const unsigned long parsed = value ? strtoul(value, NULL, 10) : 0;
  return parsed <= RACE_MAX_CHECKPOINTS
    ? (uint16_t) parsed
    : RACE_MAX_CHECKPOINTS;
}

static uint16_t Cg_RaceHud_CheckpointsReached(const player_state_t *ps,
                                              const uint16_t total) {
  return Cg_Race_CheckpointProgress(
    (uint16_t) Maxi(ps->stats[STAT_RACE_CHECKPOINT_COUNT], 0), total);
}

/**
 * @brief Builds the route line: the map title, the physics identity it is
 * being run under, and the qualifier for anything that is not a live race.
 */
static void Cg_RaceHud_RouteLine(const player_state_t *ps, char *output,
                                 const size_t size) {
  const char *map_name = cgi.ConfigString(CS_MESSAGE);
  if (!map_name || !*map_name) {
    map_name = "unknown";
  }

  const race_physics_config_t *physics = Race_Physics_Current();
  const race_physics_preset_descriptor_t *preset =
    Race_Physics_Preset(physics->preset);
  const char *feel = Cg_RacePhysics_Synchronized()
    ? preset ? preset->short_name : "Unavailable"
    : "Syncing";

  q_snprintf(output, size, "%s%s%s physics", map_name,
             RACE_HUD_SEPARATOR, feel);

  if (Cg_ReplayActive()) {
    q_strlcat(output, RACE_HUD_SEPARATOR "ghost replay", size);
  } else if ((race_mode_t) ps->stats[STAT_RACE_MODE] == RACE_MODE_PRACTICE) {
    q_strlcat(output, RACE_HUD_SEPARATOR "practice", size);
  }

  if (ps->stats[STAT_RACE_INVALID_FLAGS]) {
    q_strlcat(output, RACE_HUD_SEPARATOR "invalid", size);
  }
}

/**
 * @brief Snapshots the run timer on every checkpoint the player crosses.
 *
 * The server does not publish per-checkpoint splits, so the ribbon captures
 * them here from the edges of STAT_RACE_CHECKPOINT_COUNT. This is the only
 * writer of the split cache; Cg_RaceHud_Split is the only reader, so a
 * server-fed split feed replaces both without touching the draw code.
 */
static void Cg_RaceHud_UpdateSplits(const player_state_t *ps) {
  const uint16_t total = Cg_RaceHud_CheckpointTotal();
  const uint16_t reached = Cg_RaceHud_CheckpointsReached(ps, total);
  const race_run_state_t state =
    (race_run_state_t) ps->stats[STAT_RACE_RUN_STATE];

  if (state == RACE_RUN_IDLE || total != cg_race_hud.split_total ||
      reached < cg_race_hud.split_count) {
    memset(cg_race_hud.splits, 0, sizeof(cg_race_hud.splits));
    cg_race_hud.split_count = 0;
    cg_race_hud.split_total = total;
  }

  if (state != RACE_RUN_ACTIVE) {
    return;
  }

  const uint32_t elapsed = Cg_RaceHud_WireTime(
    ps, STAT_RACE_ELAPSED_LOW, STAT_RACE_ELAPSED_HIGH);

  while (cg_race_hud.split_count < reached &&
         cg_race_hud.split_count < RACE_MAX_CHECKPOINTS) {
    cg_race_hud.splits[cg_race_hud.split_count++] = elapsed;
  }
}

/**
 * @brief Reads one checkpoint cell's value for the §6 ribbon.
 * @param checkpoint The zero-based checkpoint index.
 * @param elapsed Set to the captured split when the checkpoint was reached.
 * @param delta_ms Set to the signed delta against the reference split, when a
 * reference is known.
 * @return True if `checkpoint` has been reached on this run.
 * @remarks `delta_ms` is never populated today: deltas need the reference
 * run's own splits, which the server does not send. The ribbon paints
 * absolute splits until it does, and only this function has to change.
 */
static bool Cg_RaceHud_Split(const uint16_t checkpoint, uint32_t *elapsed,
                             int32_t *delta_ms) {
  if (checkpoint >= cg_race_hud.split_count) {
    return false;
  }

  if (elapsed) {
    *elapsed = cg_race_hud.splits[checkpoint];
  }

  (void) delta_ms;
  return true;
}

/**
 * @brief Binds the run timer font.
 * @remarks The three engine fonts are the three stops §10 can offer.
 */
static void Cg_RaceHud_BindTimerFont(int32_t *height) {
  const float size = cg_race_hud_timer_size
    ? cg_race_hud_timer_size->value : 104.f;

  if (size <= 64.f) {
    cgi.BindFont("medium", NULL, height);
  } else if (size >= 172.f) {
    cgi.BindFont("large", NULL, height);
  } else {
    Cg_RaceHud_BindFont(RACE_FONT_TIMER, height);
  }
}

/**
 * @brief Draws the centered top stack: mode tag and route, the run timer,
 * then the world record and personal best.
 */
static void Cg_RaceHud_DrawTopStack(const player_state_t *ps) {
  const int32_t center = cgi.context->w / 2;
  const int32_t gap = Cg_RaceHud_Scale(RACE_HUD_STACK_GAP);
  const color_t accent = Cg_RaceHud_ModeAccent(ps);
  int32_t y = Cg_RaceHud_Edge();

  const char *tag = Cg_RaceHud_ModeLabel(
    (race_mode_t) ps->stats[STAT_RACE_MODE]);
  char route[MAX_STRING_CHARS];
  Cg_RaceHud_RouteLine(ps, route, sizeof(route));

  int32_t tag_height, route_height;
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &tag_height);
  const int32_t tag_width = cgi.StringWidth(tag);
  Cg_RaceHud_BindFont(RACE_FONT_ROUTE, &route_height);
  const int32_t route_width = cgi.StringWidth(route);

  const int32_t mode_gap = Cg_RaceHud_Scale(RACE_HUD_MODE_GAP);
  const int32_t row_height = Maxi(tag_height, route_height);
  const int32_t row_x = center - (tag_width + mode_gap + route_width) / 2;

  Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
  Cg_RaceHud_DrawShadowedString(row_x, y + (row_height - tag_height) / 2,
                                tag, accent);
  Cg_RaceHud_BindFont(RACE_FONT_ROUTE, NULL);
  Cg_RaceHud_DrawShadowedString(row_x + tag_width + mode_gap,
                                y + (row_height - route_height) / 2, route,
                                ps->stats[STAT_RACE_INVALID_FLAGS]
                                  ? Cg_RaceHud_Warn()
                                  : Cg_RaceHud_Dim(.66f));
  y += row_height + gap;

  int32_t timer_height;
  Cg_RaceHud_BindTimerFont(&timer_height);
  char elapsed[32];
  Cg_Race_FormatElapsed(
    Cg_RaceHud_WireTime(ps, STAT_RACE_ELAPSED_LOW, STAT_RACE_ELAPSED_HIGH),
    elapsed, sizeof(elapsed));
  Cg_RaceHud_DrawCentered(center, y, elapsed, Cg_RaceHud_TimerColor(ps));
  y += timer_height + gap;

  char world_record[32], personal_best[32];
  Cg_RaceHud_FormatRecord(
    Cg_RaceHud_WireTime(ps, STAT_RACE_WR_LOW, STAT_RACE_WR_HIGH),
    world_record, sizeof(world_record));
  Cg_RaceHud_FormatRecord(
    Cg_RaceHud_WireTime(ps, STAT_RACE_PB_LOW, STAT_RACE_PB_HIGH),
    personal_best, sizeof(personal_best));

  int32_t label_height, value_height;
  Cg_RaceHud_BindFont(RACE_FONT_LABEL, &label_height);
  const int32_t wr_tag = cgi.StringWidth("WR");
  const int32_t pb_tag = cgi.StringWidth("PB");
  Cg_RaceHud_BindFont(RACE_FONT_RECORD, &value_height);
  const int32_t wr_value = cgi.StringWidth(world_record);
  const int32_t pb_value = cgi.StringWidth(personal_best);

  const int32_t tag_gap = Cg_RaceHud_Scale(10.f);
  const int32_t record_gap = Cg_RaceHud_Scale(RACE_HUD_RECORD_GAP);
  const int32_t records_width = wr_tag + tag_gap + wr_value + record_gap +
                                pb_tag + tag_gap + pb_value;
  const int32_t records_height = Maxi(label_height, value_height);
  const int32_t label_y = y + (records_height - label_height) / 2;
  const int32_t value_y = y + (records_height - value_height) / 2;
  int32_t x = center - records_width / 2;

  Cg_RaceHud_BindFont(RACE_FONT_LABEL, NULL);
  Cg_RaceHud_DrawShadowedString(x, label_y, "WR", Cg_RaceHud_Dim(.44f));
  Cg_RaceHud_BindFont(RACE_FONT_RECORD, NULL);
  Cg_RaceHud_DrawShadowedString(x + wr_tag + tag_gap, value_y, world_record,
                                Cg_RaceHud_Gold());
  x += wr_tag + tag_gap + wr_value + record_gap;

  Cg_RaceHud_BindFont(RACE_FONT_LABEL, NULL);
  Cg_RaceHud_DrawShadowedString(x, label_y, "PB", Cg_RaceHud_Dim(.44f));
  Cg_RaceHud_BindFont(RACE_FONT_RECORD, NULL);
  Cg_RaceHud_DrawShadowedString(x + pb_tag + tag_gap, value_y, personal_best,
                                Cg_RaceHud_Green());

  cg_race_hud.top_stack_bottom = y + records_height + gap;
}

/**
 * @brief Draws the checkpoint ribbon: one cell per checkpoint on the route,
 * not just per checkpoint reached, centered as a group.
 */
static void Cg_RaceHud_DrawCheckpointRibbon(const uint16_t total) {
  char labels[RACE_MAX_CHECKPOINTS][8];
  char values[RACE_MAX_CHECKPOINTS][32];
  int32_t widths[RACE_MAX_CHECKPOINTS];

  for (uint16_t i = 0; i < total; i++) {
    uint32_t elapsed = 0;

    q_snprintf(labels[i], sizeof(labels[i]), "cp%u", (unsigned) (i + 1u));
    if (Cg_RaceHud_Split(i, &elapsed, NULL)) {
      Cg_Race_FormatElapsed(elapsed, values[i], sizeof(values[i]));
    } else {
      q_strlcpy(values[i], "--", sizeof(values[i]));
    }
  }

  // Atlas glyphs are a fixed pixel size, so unlike every other constant here
  // the cell text does not shrink with the viewport. A long route on a small
  // viewport would run the group off both edges, so step the value down a
  // size rather than overflow.
  const int32_t padding = Cg_RaceHud_Scale(RACE_HUD_RIBBON_PADDING);
  const int32_t available = cgi.context->w - Cg_RaceHud_Edge() * 2;
  const char *const candidates[] = { RACE_FONT_VALUE, RACE_FONT_BODY };
  const char *value_font = candidates[0];
  int32_t ribbon_width = 0;

  for (size_t attempt = 0; attempt < lengthof(candidates); attempt++) {
    value_font = candidates[attempt];
    ribbon_width = 0;

    for (uint16_t i = 0; i < total; i++) {
      Cg_RaceHud_BindFont(RACE_FONT_LABEL, NULL);
      const int32_t label_width = cgi.StringWidth(labels[i]);
      Cg_RaceHud_BindFont(value_font, NULL);
      widths[i] = padding * 2 + Maxi(label_width, cgi.StringWidth(values[i]));
      ribbon_width += widths[i];
    }

    if (ribbon_width <= available) {
      break;
    }
  }

  int32_t label_height, value_height;
  Cg_RaceHud_BindFont(RACE_FONT_LABEL, &label_height);
  Cg_RaceHud_BindFont(value_font, &value_height);

  const int32_t label_gap = Cg_RaceHud_Scale(RACE_HUD_RIBBON_LABEL_GAP);
  const int32_t cell_height = label_height + label_gap + value_height;
  const int32_t bottom = cgi.context->h - Cg_RaceHud_Edge() -
                         Cg_RaceHud_Scale(RACE_HUD_RIBBON_OFFSET);
  const int32_t top = bottom - cell_height;
  int32_t x = cgi.context->w / 2 - ribbon_width / 2;

  for (uint16_t i = 0; i < total; i++) {
    const bool reached = Cg_RaceHud_Split(i, NULL, NULL);
    const int32_t center = x + widths[i] / 2;

    if (i) {
      cgi.Draw2DFill(x, top, 1, cell_height, Cg_RaceHud_Dim(.13f));
    }

    Cg_RaceHud_BindFont(RACE_FONT_LABEL, NULL);
    Cg_RaceHud_DrawCentered(center, top, labels[i],
                            Cg_RaceHud_Dim(reached ? .44f : .22f));
    Cg_RaceHud_BindFont(value_font, NULL);
    Cg_RaceHud_DrawCentered(center, top + label_height + label_gap, values[i],
                            Cg_RaceHud_Dim(reached ? .88f : .22f));
    x += widths[i];
  }
}

/**
 * @brief Draws the checkpoint pips and their caption.
 */
static void Cg_RaceHud_DrawPips(const player_state_t *ps,
                                const uint16_t total) {
  const int32_t pip_height = Cg_RaceHud_Scale(RACE_HUD_PIP_HEIGHT);
  const int32_t pip_gap = Cg_RaceHud_Scale(RACE_HUD_PIP_GAP);
  const uint16_t reached = Cg_RaceHud_CheckpointsReached(ps, total);

  // A long route would push 52px cells past both edges, so the pips give up
  // width before the row gives up fitting on screen.
  const int32_t pip_budget = cgi.context->w - Cg_RaceHud_Edge() * 2 -
                             (total - 1) * pip_gap;
  const int32_t pip_width = Maxi(
    Mini(Cg_RaceHud_Scale(RACE_HUD_PIP_WIDTH), pip_budget / total), 1);

  char caption[64];
  q_snprintf(caption, sizeof(caption), "%u / %u checkpoints",
             (unsigned) reached, (unsigned) total);

  int32_t caption_height;
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &caption_height);

  const int32_t caption_y = cgi.context->h - Cg_RaceHud_Edge() -
                            caption_height;
  const int32_t pips_y = caption_y -
                         Cg_RaceHud_Scale(RACE_HUD_PIP_CAPTION_GAP) -
                         pip_height;
  const int32_t pips_width = total * pip_width + (total - 1) * pip_gap;
  const color_t accent = Cg_RaceHud_ModeAccent(ps);
  int32_t x = cgi.context->w / 2 - pips_width / 2;

  for (uint16_t i = 0; i < total; i++) {
    cgi.Draw2DFill(x, pips_y, pip_width, pip_height,
                   i < reached ? accent : Cg_RaceHud_Dim(.15f));
    x += pip_width + pip_gap;
  }

  Cg_RaceHud_DrawCentered(cgi.context->w / 2, caption_y, caption,
                          Cg_RaceHud_Dim(.5f));
}

/**
 * @brief Draws the map clock in its own right-aligned slot.
 */
static void Cg_RaceHud_DrawClock(void) {
  const char *clock = cgi.ConfigString(CS_TIME);
  if (!clock || !*clock) {
    return;
  }

  int32_t caption_height, value_height;
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &caption_height);
  Cg_RaceHud_BindFont(RACE_FONT_VALUE, &value_height);

  const int32_t right = cgi.context->w - Cg_RaceHud_Edge();
  const int32_t caption_y = cgi.context->h - Cg_RaceHud_Edge() -
                            caption_height;
  const int32_t value_y = caption_y -
                          Cg_RaceHud_Scale(RACE_HUD_CLOCK_CAPTION_GAP) -
                          value_height;

  Cg_RaceHud_BindFont(RACE_FONT_VALUE, NULL);
  Cg_RaceHud_DrawRightAligned(right, value_y, clock, Cg_RaceHud_Dim(.82f));
  Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
  Cg_RaceHud_DrawRightAligned(right, caption_y, "Map time",
                              Cg_RaceHud_Dim(.5f));
}

/**
 * @brief Draws the speed readout under the crosshair, colored by the smoothed
 * frame-over-frame delta of horizontal velocity.
 */
static void Cg_RaceHud_DrawSpeed(const player_state_t *ps) {
  const float speed = (float) Cg_Race_HorizontalSpeed(ps->pm_state.velocity);
  const float delta = speed - cg_race_hud.smoothed_speed;

  color_t color;
  if (delta > RACE_HUD_SPEED_THRESHOLD) {
    color = Cg_RaceHud_Cyan();
  } else if (delta < -RACE_HUD_SPEED_THRESHOLD) {
    color = Cg_RaceHud_Warn();
  } else {
    color = Cg_RaceHud_Dim(.5f);
  }

  // The lerp is the anti-strobe: a raw per-frame delta flickers at 400+ fps.
  cg_race_hud.smoothed_speed += (speed - cg_race_hud.smoothed_speed) *
                                RACE_HUD_SPEED_LERP;

  char value[16];
  q_snprintf(value, sizeof(value), "%d", (int32_t) speed);

  int32_t value_height, unit_height;
  Cg_RaceHud_BindFont(RACE_FONT_SPEED, &value_height);
  const int32_t value_width = cgi.StringWidth(value);
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &unit_height);
  const int32_t unit_width = cgi.StringWidth("ups");

  const int32_t gap = Cg_RaceHud_Scale(RACE_HUD_SPEED_GAP);
  const int32_t y = cgi.context->h / 2 +
                    Cg_RaceHud_Scale(RACE_HUD_SPEED_OFFSET);
  const int32_t x = cgi.context->w / 2 -
                    (value_width + gap + unit_width) / 2;

  Cg_RaceHud_BindFont(RACE_FONT_SPEED, NULL);
  Cg_RaceHud_DrawShadowedString(x, y, value, color);
  Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
  Cg_RaceHud_DrawShadowedString(x + value_width + gap,
                                y + (value_height - unit_height), "ups",
                                Cg_RaceHud_Dim(.5f));
}

static void Cg_RaceHud_DrawHyperblasterClimbHelper(const player_state_t *ps) {
  if (!cg_hb_climb_helper || !cg_hb_climb_helper->integer ||
      ps->pm_state.type == PM_DEAD ||
      (ps->stats[STAT_SPECTATOR] && !ps->stats[STAT_CHASE]) ||
      ps->stats[STAT_SCORES] ||
      (ps->stats[STAT_WEAPON] & 0xff) != WEAPON_HYPERBLASTER) {
    return;
  }

  const vec3_t start = cgi.view->origin;
  const vec3_t end = Vec3_Fmaf(start, MAX_WORLD_DIST, cgi.view->forward);
  const cm_trace_t trace = cgi.Trace(
    start, end, Box3_Zero(), NULL, CONTENTS_MASK_SOLID);
  if (!trace.ent) {
    return;
  }

  const float distance = Vec3_Distance(ps->pm_state.origin, trace.end);
  const cg_race_climb_state_t state = Cg_Race_ClimbState(distance);
  const char *text = Cg_Race_ClimbLabel(state);
  color_t color;
  if (state == CG_RACE_CLIMB_READY) {
    color = Cg_RaceHud_Green();
  } else if (state == CG_RACE_CLIMB_CLOSER) {
    color = Cg_RaceHud_Gold();
  } else {
    color = Cg_RaceHud_Warn();
  }

  Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
  Cg_RaceHud_DrawCentered(cgi.context->w / 2,
                          cgi.context->h / 2 + Cg_RaceHud_Scale(25.f),
                          text, color);
}

static void Cg_RaceHud_DrawAnalyticalSplit(void) {
  if (!cg_race_hud.analytical.valid) {
    return;
  }

  if (cgi.client->unclamped_time - cg_race_hud.analytical.received_time >
      RACE_HUD_SPLIT_EVENT_MILLIS) {
    Cg_RaceHud_Clear();
    return;
  }

  char heading[64];
  if (*cg_race_hud.analytical.label) {
    q_snprintf(heading, sizeof(heading), "Split %u" RACE_HUD_SEPARATOR "%s",
               cg_race_hud.analytical.number,
               cg_race_hud.analytical.label);
  } else {
    q_snprintf(heading, sizeof(heading), "Split %u",
               cg_race_hud.analytical.number);
  }

  char cumulative[32];
  char segment[32];
  Cg_Race_FormatElapsed(cg_race_hud.analytical.cumulative,
                        cumulative, sizeof(cumulative));
  Cg_Race_FormatElapsed(cg_race_hud.analytical.segment,
                        segment, sizeof(segment));

  const int32_t center = cgi.context->w / 2;
  const int32_t top = Cg_RaceHud_TopStackBottom() + Cg_RaceHud_Scale(16.f);
  int32_t height;
  Cg_RaceHud_BindFont(RACE_FONT_LABEL, &height);
  Cg_RaceHud_DrawCentered(center, top, heading, Cg_RaceHud_Dim(.72f));

  Cg_RaceHud_BindFont(RACE_FONT_VALUE, &height);
  char timing[96];
  q_snprintf(timing, sizeof(timing), "%s  segment %s", cumulative, segment);
  Cg_RaceHud_DrawCentered(center, top + height, timing,
                          Color4f(.957f, .973f, .984f, .94f));

  char comparisons[96] = "Comparison unavailable";
  if (cg_race_hud.analytical.comparison_flags) {
    char pb[24] = "";
    char wr[24] = "";
    if (cg_race_hud.analytical.comparison_flags & 1u) {
      Cg_RaceHud_FormatDelta(cg_race_hud.analytical.pb_delta, pb, sizeof(pb));
    }
    if (cg_race_hud.analytical.comparison_flags & 2u) {
      Cg_RaceHud_FormatDelta(cg_race_hud.analytical.wr_delta, wr, sizeof(wr));
    }
    q_snprintf(comparisons, sizeof(comparisons), "%s%s%s%s",
               *pb ? "PB " : "", pb,
               *pb && *wr ? "   " : "",
               *wr ? va("WR %s", wr) : "");
  }
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &height);
  Cg_RaceHud_DrawCentered(center, top + height * 3,
                          comparisons, Cg_RaceHud_Dim(.72f));
}

static void Cg_RaceHud_DrawElements(const player_state_t *ps,
                                    cg_hud_layout_t *layout) {
  (void) layout;
  Cg_RaceTraining_UpdateFrame(ps);
  Cg_RaceTraining_DrawStrafeHelper(ps);
  cg_race_hud.top_stack_bottom = 0;

  if (!Cg_Race_RunHudVisible(
        cg_draw_hud->integer, !ps->stats[STAT_TIME], editor->value,
        ps->stats[STAT_SCORES] != 0, ps->stats[STAT_SPECTATOR] != 0,
        ps->stats[STAT_CHASE] != 0)) {
    Cg_RaceVote_Draw();
    Cg_RaceFinishReport_Draw(ps);
    Cg_RaceReplay_DrawHud(ps);
    return;
  }

  Cg_RaceHud_UpdateSplits(ps);

  // The replay HUD draws its own lockup, speed and bottom clusters into these
  // same slots, so the live HUD stands down for the duration rather than
  // double-drawing over it.
  if (!Cg_ReplayActive()) {
    Cg_RaceHud_DrawTopStack(ps);
    Cg_RaceHud_DrawAnalyticalSplit();
    // Exactly one speed readout, per the spec's §6: the helper's own is the
    // configurable one the player opted into, so the HUD yields to it rather
    // than stacking a second copy of the same number 85 pixels away.
    if (!Cg_StrafeHelper_OwnsSpeedReadout()) {
      Cg_RaceHud_DrawSpeed(ps);
    }

    const uint16_t total = Cg_RaceHud_CheckpointTotal();
    if (total) {
      Cg_RaceHud_DrawCheckpointRibbon(total);
      Cg_RaceHud_DrawPips(ps, total);
    }
    Cg_RaceHud_DrawClock();
    Cg_InputViewer_Draw(ps);
  }

  Cg_DrawJumpViewer(ps, 0);
  Cg_RaceHud_DrawHyperblasterClimbHelper(ps);
  Cg_RaceVote_Draw();
  Cg_RaceFinishReport_Draw(ps);
  Cg_RaceReplay_DrawHud(ps);
  cgi.BindFont(NULL, NULL, NULL);
}

void Cg_RaceHud_Init(void) {
  static bool initialized;
  if (initialized) {
    return;
  }

  cg_hb_climb_helper = cgi.AddCvar(
    "cg_hb_climb_helper", "1", CVAR_ARCHIVE,
    "Draw the hyperblaster climb-distance helper.");
  cg_race_hud_edge = cgi.AddCvar(
    "cg_race_hud_edge", "56", CVAR_ARCHIVE,
    "Edge inset for the Race HUD corner clusters, in 1440p pixels.");
  cg_race_hud_timer_size = cgi.AddCvar(
    "cg_race_hud_timer_size", "104", CVAR_ARCHIVE,
    "Run timer size, in 1440p pixels.");
  cg_race_hud_legibility = cgi.AddCvar(
    "cg_race_hud_legibility", "none", CVAR_ARCHIVE,
    "Race HUD text legibility treatment: none, shadow, stroke or plates.");

  Cg_DrawHudElements = Cg_RaceHud_DrawElements;
  initialized = true;
}
