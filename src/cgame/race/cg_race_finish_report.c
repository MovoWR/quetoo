/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"
#include "cg_race_finish_report.h"

#include "cg_race_finish_report_math.h"
#include "cg_race_hud.h"
#include "cg_race_message.h"
#include "cg_race_presentation.h"
#include "race_finish_report.h"

#define CG_RACE_FINISH_REPORT_MILLIS 10000u

// Bar geometry, in the 1440p design pixels every Race HUD constant uses. The
// design draws the bar at 720p, so each value here is twice the one in it.
#define CG_RACE_FINISH_RULE_HEIGHT 4.f
#define CG_RACE_FINISH_PAD_TOP 36.f
#define CG_RACE_FINISH_PAD_BOTTOM 40.f
#define CG_RACE_FINISH_HEADLINE_GAP 12.f
#define CG_RACE_FINISH_CHIP_GAP 12.f
#define CG_RACE_FINISH_CHIP_SPACING 40.f
#define CG_RACE_FINISH_TAG_GAP 12.f
#define CG_RACE_FINISH_DOT 12.f
#define CG_RACE_FINISH_DOT_GAP 20.f
#define CG_RACE_FINISH_HINT_GAP 12.f

// The rule fades out over the outer 22% of the screen at each end, and the
// scrim reaches half opacity two fifths of the way down, per the design's two
// gradients. Both are drawn as bands: the 2D path fills flat rects only.
#define CG_RACE_FINISH_RULE_INSET .22f
#define CG_RACE_FINISH_SCRIM_KNEE .4f
#define CG_RACE_FINISH_SCRIM_KNEE_ALPHA .5f
#define CG_RACE_FINISH_SCRIM_ALPHA .8f
#define CG_RACE_FINISH_BAND_WIDTH 12.f
#define CG_RACE_FINISH_MAX_BANDS 32

typedef struct {
  race_finish_report_t report;
  uint32_t received_time;
  bool valid;
} cg_race_finish_report_state_t;

static cg_race_finish_report_state_t cg_race_finish_report_state;

void Cg_RaceFinishReport_Init(void) {
  Cg_RaceFinishReport_Clear();
}

void Cg_RaceFinishReport_Clear(void) {
  memset(&cg_race_finish_report_state, 0,
         sizeof(cg_race_finish_report_state));
}

bool Cg_RaceFinishReport_ParseMessage(const int32_t command) {
  if (command != SV_CMD_RACE_FINISH_REPORT) {
    return false;
  }

  const int32_t length = cgi.ReadShort();
  uint8_t payload[RACE_FINISH_REPORT_MAX_BYTES];
  if (length < 0) {
    Cg_Error("Invalid Race finish report length %d\n", length);
  }
  if (length == 0) {
    Cg_Warn("Invalid Race finish report length %d\n", length);
    Cg_RaceFinishReport_Clear();
    return true;
  }
  if (length > (int32_t) sizeof(payload)) {
    if (!Cg_RaceMessage_Drain(cgi.ReadData, (size_t) length)) {
      Cg_Error("Race finish report reader is unavailable\n");
    }
    Cg_Warn("Invalid Race finish report length %d\n", length);
    Cg_RaceFinishReport_Clear();
    return true;
  }
  cgi.ReadData(payload, (size_t) length);

  race_finish_report_t report;
  if (!Race_FinishReport_Decode(payload, (size_t) length, &report)) {
    Cg_Warn("Rejected malformed Race finish report\n");
    Cg_RaceFinishReport_Clear();
    return true;
  }
  cg_race_finish_report_state.report = report;
  cg_race_finish_report_state.received_time = cgi.client->unclamped_time;
  cg_race_finish_report_state.valid = true;
  return true;
}

bool Cg_RaceFinishReport_Active(const player_state_t *ps) {
  return cg_race_finish_report_state.valid && cgi.client &&
         cgi.GetKeyDest() == KEY_GAME && ps->stats[STAT_TIME] &&
         !ps->stats[STAT_SCORES] && !editor->value &&
         cgi.client->unclamped_time -
           cg_race_finish_report_state.received_time <
             CG_RACE_FINISH_REPORT_MILLIS;
}

/**
 * @return The dim tint shared by captions, tags and hint prose.
 */
static color_t Cg_RaceFinishReport_Dim(const float alpha) {
  return Color4f(1.f, 1.f, 1.f, alpha);
}

/**
 * @return The accent the whole bar is keyed to, per finish state.
 */
static color_t Cg_RaceFinishReport_Accent(const cg_race_finish_state_t state) {
  switch (state) {
    case CG_RACE_FINISH_STATE_WORLD_RECORD:
      return Cg_RaceHud_Gold();
    case CG_RACE_FINISH_STATE_PERSONAL_BEST:
      return Cg_RaceHud_Cyan();
    case CG_RACE_FINISH_STATE_PRACTICE:
      // The same red the top lockup already tags a practice run with, so one
      // run reads as one color top and bottom.
      return Color4b(0xa5, 0x44, 0x4e, 0xff);
    case CG_RACE_FINISH_STATE_INVALID:
      return Cg_RaceHud_Warn();
    default:
      return Color4b(0x93, 0xae, 0xc1, 0xff);
  }
}

/**
 * @return How many bands to spend covering `extent` pixels of gradient.
 */
static int32_t Cg_RaceFinishReport_Bands(const int32_t extent) {
  const int32_t width = Maxi(Cg_RaceHud_Scale(CG_RACE_FINISH_BAND_WIDTH), 1);
  return Mini(Maxi(extent / width, 2), CG_RACE_FINISH_MAX_BANDS);
}

/**
 * @brief Draws the scrim the bar sits on: transparent at its top edge, half
 * opaque two fifths of the way down, near solid at the screen bottom.
 */
static void Cg_RaceFinishReport_DrawScrim(const int32_t y,
                                          const int32_t height) {
  const int32_t bands = Cg_RaceFinishReport_Bands(height);

  for (int32_t i = 0; i < bands; i++) {
    const int32_t top = y + height * i / bands;
    const int32_t bottom = y + height * (i + 1) / bands;
    const float t = (i + .5f) / bands;
    const float alpha = t < CG_RACE_FINISH_SCRIM_KNEE
      ? CG_RACE_FINISH_SCRIM_KNEE_ALPHA * (t / CG_RACE_FINISH_SCRIM_KNEE)
      : CG_RACE_FINISH_SCRIM_KNEE_ALPHA +
        (CG_RACE_FINISH_SCRIM_ALPHA - CG_RACE_FINISH_SCRIM_KNEE_ALPHA) *
        ((t - CG_RACE_FINISH_SCRIM_KNEE) / (1.f - CG_RACE_FINISH_SCRIM_KNEE));

    cgi.Draw2DFill(0, top, cgi.context->w, bottom - top,
                   Color4f(6 / 255.f, 9 / 255.f, 10 / 255.f, alpha));
  }
}

/**
 * @brief Draws the accent rule capping the bar, solid across the middle and
 * fading to nothing at both screen edges.
 */
static void Cg_RaceFinishReport_DrawRule(const int32_t y,
                                         const color_t accent) {
  const int32_t height = Maxi(
    Cg_RaceHud_Scale(CG_RACE_FINISH_RULE_HEIGHT), 1);
  const int32_t inset = (int32_t) (cgi.context->w * CG_RACE_FINISH_RULE_INSET);
  const int32_t bands = Cg_RaceFinishReport_Bands(inset);

  cgi.Draw2DFill(inset, y, cgi.context->w - inset * 2, height, accent);

  for (int32_t i = 0; i < bands; i++) {
    const int32_t near_edge = inset * i / bands;
    const int32_t far_edge = inset * (i + 1) / bands;
    const color_t faded = Color4f(accent.r, accent.g, accent.b,
                                  accent.a * (i + .5f) / bands);

    cgi.Draw2DFill(near_edge, y, far_edge - near_edge, height, faded);
    cgi.Draw2DFill(cgi.context->w - far_edge, y, far_edge - near_edge, height,
                   faded);
  }
}

/**
 * @brief Draws the accent dot and its halo, standing in for the design's
 * box-shadow glow.
 */
static void Cg_RaceFinishReport_DrawDot(const int32_t x, const int32_t y,
                                        const int32_t size,
                                        const color_t accent) {
  const int32_t halo = Maxi(size / 2, 1);

  cgi.Draw2DFill(x - halo, y - halo, size + halo * 2, size + halo * 2,
                 Color4f(accent.r, accent.g, accent.b, accent.a * .18f));
  cgi.Draw2DFill(x, y, size, size, accent);
}

/**
 * @brief Writes the key bound to `bind` into `output`, or empties it when the
 * command has no binding.
 */
static void Cg_RaceFinishReport_KeyName(const SDL_Scancode key, char *output,
                                        const size_t size) {
  const char *name = key == SDL_SCANCODE_UNKNOWN ? NULL : cgi.KeyName(key);

  q_strlcpy(output, name ? name : "", size);
}

/**
 * @brief Draws one hint line, with its key glyph brighter than its prose.
 * @remarks The two are drawn as separate runs because the design separates
 * them by weight, and the bitmap atlas has no weights to separate them with.
 */
static void Cg_RaceFinishReport_DrawHint(const int32_t x, const int32_t y,
                                         const char *key, const char *prose) {
  Cg_RaceHud_DrawShadowedString(x, y, key, Cg_RaceFinishReport_Dim(.85f));
  Cg_RaceHud_DrawShadowedString(x + cgi.StringWidth(key), y, prose,
                                Cg_RaceFinishReport_Dim(.58f));
}

/**
 * @brief Draws the left column: how to restart, how to watch the replay and
 * how to reach the menu, resolved from the player's own bindings.
 * @remarks A command nobody has bound contributes no clause rather than
 * naming a key that would do nothing.
 */
static void Cg_RaceFinishReport_DrawHints(const int32_t x, const int32_t y,
                                          const int32_t line_height,
                                          const int32_t gap) {
  char restart[32], replay[32], menu[32];
  Cg_RaceFinishReport_KeyName(
    cgi.KeyForBind(SDL_SCANCODE_UNKNOWN, "kill"), restart, sizeof(restart));
  Cg_RaceFinishReport_KeyName(
    cgi.KeyForBind(SDL_SCANCODE_UNKNOWN, "replay pb"), replay,
    sizeof(replay));
  Cg_RaceFinishReport_KeyName(SDL_SCANCODE_ESCAPE, menu, sizeof(menu));

  if (*restart) {
    Cg_RaceFinishReport_DrawHint(x, y, restart, " restarts the run");
  }

  const int32_t second = y + line_height + gap;
  int32_t cursor = x;

  if (*replay) {
    const char *prose = " watches the replay";
    Cg_RaceFinishReport_DrawHint(cursor, second, replay, prose);
    cursor += cgi.StringWidth(replay) + cgi.StringWidth(prose);
    Cg_RaceHud_DrawShadowedString(cursor, second, RACE_HUD_SEPARATOR,
                                  Cg_RaceFinishReport_Dim(.3f));
    cursor += cgi.StringWidth(RACE_HUD_SEPARATOR);
  }

  if (*menu) {
    Cg_RaceFinishReport_DrawHint(cursor, second, menu, " opens the menu");
  }
}

/**
 * @return The width of one record chip, tag and value together.
 */
static int32_t Cg_RaceFinishReport_ChipWidth(const char *tag,
                                             const char *value) {
  Cg_RaceHud_BindFont(RACE_FONT_LABEL, NULL);
  const int32_t tag_width = cgi.StringWidth(tag);
  Cg_RaceHud_BindFont(RACE_FONT_RECORD, NULL);

  return tag_width + Cg_RaceHud_Scale(CG_RACE_FINISH_TAG_GAP) +
         cgi.StringWidth(value);
}

/**
 * @brief Draws one record chip: a small colored tag, then the value it
 * qualifies, sitting on the same baseline.
 */
static void Cg_RaceFinishReport_DrawChip(const int32_t x, const int32_t y,
                                         const int32_t tag_height,
                                         const int32_t value_height,
                                         const char *tag,
                                         const color_t tag_color,
                                         const char *value,
                                         const color_t value_color) {
  Cg_RaceHud_BindFont(RACE_FONT_LABEL, NULL);
  Cg_RaceHud_DrawShadowedString(x, y + (value_height - tag_height), tag,
                                tag_color);
  const int32_t tag_width = cgi.StringWidth(tag);
  Cg_RaceHud_BindFont(RACE_FONT_RECORD, NULL);
  Cg_RaceHud_DrawShadowedString(
    x + tag_width + Cg_RaceHud_Scale(CG_RACE_FINISH_TAG_GAP), y, value,
    value_color);
}

/**
 * @brief Draws the right column: the map clock, in the slot and at the sizes
 * the live HUD's own clock uses, so it does not move when the bar appears.
 */
static void Cg_RaceFinishReport_DrawClock(const int32_t right,
                                          const int32_t bottom) {
  const char *clock = cgi.ConfigString(CS_TIME);
  if (!clock || !*clock) {
    return;
  }

  int32_t caption_height, value_height;
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &caption_height);
  Cg_RaceHud_BindFont(RACE_FONT_VALUE, &value_height);

  const int32_t caption_y = bottom - caption_height;
  const int32_t value_y = caption_y -
                          Cg_RaceHud_Scale(RACE_HUD_CLOCK_CAPTION_GAP) -
                          value_height;

  Cg_RaceHud_BindFont(RACE_FONT_VALUE, NULL);
  Cg_RaceHud_DrawRightAligned(right, value_y, clock,
                              Cg_RaceFinishReport_Dim(.82f));
  Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
  Cg_RaceHud_DrawRightAligned(right, caption_y, "Map time",
                              Cg_RaceFinishReport_Dim(.5f));
}

/**
 * @brief Fills `value` and `color` with the run's standing against `record`.
 */
static void Cg_RaceFinishReport_Compare(const uint32_t elapsed,
                                        const uint32_t record, char *value,
                                        const size_t size, color_t *color) {
  const int32_t delta = (int32_t) elapsed - (int32_t) record;

  Cg_RaceHud_FormatDelta(delta, value, size);
  *color = Cg_RaceFinishReport_Ahead(delta)
    ? Cg_RaceHud_Green() : Cg_RaceHud_Warn();
}

void Cg_RaceFinishReport_Draw(const player_state_t *ps) {
  if (!Cg_RaceFinishReport_Active(ps)) {
    return;
  }

  const race_finish_report_t *report = &cg_race_finish_report_state.report;
  const cg_race_finish_state_t state = Cg_RaceFinishReport_Classify(
    report->mode, report->invalid_flags, report->publication_committed,
    report->new_world_record);
  const color_t accent = Cg_RaceFinishReport_Accent(state);
  const char *headline = Cg_RaceFinishReport_Headline(state);

  char elapsed[32];
  Cg_Race_FormatElapsed(report->elapsed_time, elapsed, sizeof(elapsed));

  char pb_value[32], wr_value[32];
  color_t pb_color, wr_color;

  if (report->previous_pb) {
    Cg_RaceFinishReport_Compare(report->elapsed_time, report->previous_pb,
                                pb_value, sizeof(pb_value), &pb_color);
  } else {
    q_strlcpy(pb_value, "first", sizeof(pb_value));
    pb_color = Cg_RaceFinishReport_Dim(.85f);
  }

  if (Cg_RaceFinishReport_ComparesToRecord(state, report->world_record)) {
    Cg_RaceFinishReport_Compare(report->elapsed_time, report->world_record,
                                wr_value, sizeof(wr_value), &wr_color);
  } else if (state == CG_RACE_FINISH_STATE_WORLD_RECORD) {
    // The standing record is this run, so a delta would read +0.000 -- which
    // looks like having lost to the record it just set.
    q_strlcpy(wr_value, "new", sizeof(wr_value));
    wr_color = Cg_RaceHud_Gold();
  } else {
    q_strlcpy(wr_value, "none", sizeof(wr_value));
    wr_color = Cg_RaceFinishReport_Dim(.5f);
  }

  int32_t headline_height, time_height, tag_height, value_height, hint_height;
  Cg_RaceHud_BindFont(RACE_FONT_VALUE, &headline_height);
  Cg_RaceHud_BindFont(RACE_FONT_TIMER, &time_height);
  Cg_RaceHud_BindFont(RACE_FONT_LABEL, &tag_height);
  Cg_RaceHud_BindFont(RACE_FONT_RECORD, &value_height);
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &hint_height);

  const int32_t edge = Cg_RaceHud_Edge();
  const int32_t rule_height = Maxi(
    Cg_RaceHud_Scale(CG_RACE_FINISH_RULE_HEIGHT), 1);
  const int32_t headline_gap = Cg_RaceHud_Scale(CG_RACE_FINISH_HEADLINE_GAP);
  const int32_t chip_gap = Cg_RaceHud_Scale(CG_RACE_FINISH_CHIP_GAP);
  const int32_t hint_gap = Cg_RaceHud_Scale(CG_RACE_FINISH_HINT_GAP);
  const int32_t chip_height = Maxi(tag_height, value_height);

  // The bar has to clear both stacks it contains: the hero in the middle, and
  // whichever corner cluster is taller. The hero hangs off its own padding
  // and the corners off the edge inset the rest of the HUD shares, so nothing
  // shifts as the bar comes and goes.
  const int32_t hero_height = Cg_RaceHud_Scale(CG_RACE_FINISH_PAD_TOP) +
                              headline_height + headline_gap + time_height +
                              chip_gap + chip_height +
                              Cg_RaceHud_Scale(CG_RACE_FINISH_PAD_BOTTOM);
  const int32_t hints_height = hint_height * 2 + hint_gap;
  const int32_t clock_height = value_height +
                               Cg_RaceHud_Scale(RACE_HUD_CLOCK_CAPTION_GAP) +
                               hint_height;
  const int32_t corner_height = edge + Maxi(hints_height, clock_height) +
                                Cg_RaceHud_Scale(CG_RACE_FINISH_PAD_TOP);
  const int32_t bar_height = Mini(
    rule_height + Maxi(hero_height, corner_height), cgi.context->h);
  const int32_t bar_y = cgi.context->h - bar_height;

  Cg_RaceFinishReport_DrawScrim(bar_y, bar_height);
  Cg_RaceFinishReport_DrawRule(bar_y, accent);

  const int32_t center_x = cgi.context->w / 2;
  const int32_t chip_y = cgi.context->h -
                         Cg_RaceHud_Scale(CG_RACE_FINISH_PAD_BOTTOM) -
                         chip_height;
  const int32_t time_y = chip_y - chip_gap - time_height;
  const int32_t headline_y = time_y - headline_gap - headline_height;

  const int32_t dot = Maxi(Cg_RaceHud_Scale(CG_RACE_FINISH_DOT), 2);
  const int32_t dot_gap = Cg_RaceHud_Scale(CG_RACE_FINISH_DOT_GAP);
  Cg_RaceHud_BindFont(RACE_FONT_VALUE, NULL);
  const int32_t headline_x = center_x -
                             (dot + dot_gap + cgi.StringWidth(headline)) / 2;

  Cg_RaceFinishReport_DrawDot(headline_x,
                              headline_y + (headline_height - dot) / 2, dot,
                              accent);
  Cg_RaceHud_DrawShadowedString(headline_x + dot + dot_gap, headline_y,
                                headline, accent);

  Cg_RaceHud_BindFont(RACE_FONT_TIMER, NULL);
  Cg_RaceHud_DrawCentered(center_x, time_y, elapsed, color_white);

  const int32_t chip_spacing = Cg_RaceHud_Scale(CG_RACE_FINISH_CHIP_SPACING);
  const int32_t pb_width = Cg_RaceFinishReport_ChipWidth("PB", pb_value);
  const int32_t wr_width = Cg_RaceFinishReport_ChipWidth("WR", wr_value);
  int32_t chip_x = center_x - (pb_width + chip_spacing + wr_width) / 2;

  Cg_RaceFinishReport_DrawChip(chip_x, chip_y, tag_height, value_height, "PB",
                               Cg_RaceHud_Cyan(), pb_value, pb_color);
  chip_x += pb_width + chip_spacing;
  Cg_RaceFinishReport_DrawChip(chip_x, chip_y, tag_height, value_height, "WR",
                               Cg_RaceHud_Gold(), wr_value, wr_color);

  Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
  Cg_RaceFinishReport_DrawHints(edge, cgi.context->h - edge - hints_height,
                                hint_height, hint_gap);
  Cg_RaceFinishReport_DrawClock(cgi.context->w - edge, cgi.context->h - edge);

  cgi.BindFont(NULL, NULL, NULL);
}
