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

#include "cg_race_presentation.h"
#include "race_finish_report.h"

#define CG_RACE_FINISH_REPORT_MILLIS 10000u
#define CG_RACE_FINISH_REPORT_MAX_WIDTH 760
#define CG_RACE_FINISH_REPORT_MARGIN 16
#define CG_RACE_FINISH_REPORT_PAD 16

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
  if (length <= 0 || length > (int32_t) sizeof(payload)) {
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

static void Cg_RaceFinishReport_DrawRight(const int32_t right,
                                          const int32_t y,
                                          const char *text,
                                          const color_t color) {
  cgi.Draw2DString(right - cgi.StringWidth(text), y, text, color);
}

static const char *Cg_RaceFinishReport_Delta(const uint32_t elapsed,
                                             const uint32_t previous_pb) {
  if (!previous_pb) {
    return "FIRST COMPLETION";
  }
  const int64_t delta = (int64_t) elapsed - previous_pb;
  const uint64_t magnitude = (uint64_t) (delta < 0 ? -delta : delta);
  return va("%s%llu.%03llu vs PB", delta > 0 ? "+" : delta < 0 ? "-" : "",
            (unsigned long long) (magnitude / 1000u),
            (unsigned long long) (magnitude % 1000u));
}

void Cg_RaceFinishReport_Draw(const player_state_t *ps) {
  if (!cg_race_finish_report_state.valid || !cgi.client ||
      cgi.GetKeyDest() != KEY_GAME || !ps->stats[STAT_TIME] ||
      ps->stats[STAT_SCORES] || editor->value ||
      cgi.client->unclamped_time -
        cg_race_finish_report_state.received_time >
          CG_RACE_FINISH_REPORT_MILLIS) {
    return;
  }

  const race_finish_report_t *report = &cg_race_finish_report_state.report;
  const size_t columns = report->checkpoint_count > 16u ? 4u :
                         report->checkpoint_count > 8u ? 2u : 1u;
  const size_t rows = report->checkpoint_count
                        ? (report->checkpoint_count + columns - 1u) / columns
                        : 0u;
  int32_t small_h, medium_h;
  cgi.BindFont("small", NULL, &small_h);
  cgi.BindFont("medium", NULL, &medium_h);

  const int32_t panel_w = Mini(
    cgi.context->w - CG_RACE_FINISH_REPORT_MARGIN * 2,
    CG_RACE_FINISH_REPORT_MAX_WIDTH);
  const int32_t splits_h = rows ? (int32_t) rows * small_h + 12 : 0;
  const int32_t panel_h = CG_RACE_FINISH_REPORT_PAD * 2 + medium_h +
                          small_h * 4 + 36 + splits_h;
  const int32_t x = (cgi.context->w - panel_w) / 2;
  const int32_t y = Maxi(CG_RACE_FINISH_REPORT_MARGIN,
                         (cgi.context->h - panel_h) / 2);
  const color_t accent = report->mode == RACE_MODE_PRACTICE
                           ? Color4f(.25f, .75f, 1.f, 1.f)
                           : report->invalid_flags
                               ? color_red
                               : Color4f(.25f, 1.f, .4f, 1.f);

  cgi.Draw2DFill(x, y, panel_w, panel_h,
                 Color4f(.015f, .03f, .045f, .94f));
  cgi.Draw2DFill(x, y, panel_w, 2, accent);
  cgi.Draw2DFill(x, y + panel_h - 1, panel_w, 1,
                 Color4f(accent.r, accent.g, accent.b, .5f));

  int32_t text_y = y + CG_RACE_FINISH_REPORT_PAD;
  cgi.BindFont("medium", NULL, NULL);
  cgi.Draw2DString(x + CG_RACE_FINISH_REPORT_PAD, text_y, "FINISH", accent);
  const char *mode = report->mode == RACE_MODE_PRACTICE
                       ? "PRACTICE - NOT RANKED"
                       : report->invalid_flags ? "INVALID RUN" : "RACE MODE";
  Cg_RaceFinishReport_DrawRight(
    x + panel_w - CG_RACE_FINISH_REPORT_PAD, text_y, mode, accent);
  text_y += medium_h + 12;

  cgi.BindFont("small", NULL, NULL);
  char elapsed[32];
  Cg_Race_FormatElapsed(report->elapsed_time, elapsed, sizeof(elapsed));
  cgi.Draw2DString(x + CG_RACE_FINISH_REPORT_PAD, text_y,
                   va("TIME  %s", elapsed), color_white);
  const bool improved = report->previous_pb &&
                        report->elapsed_time < report->previous_pb;
  const char *delta = report->mode == RACE_MODE_PRACTICE
                        ? "PRACTICE RUN"
                        : Cg_RaceFinishReport_Delta(
                            report->elapsed_time, report->previous_pb);
  Cg_RaceFinishReport_DrawRight(
    x + panel_w - CG_RACE_FINISH_REPORT_PAD, text_y,
    delta,
    improved ? color_green : report->previous_pb ? color_yellow : accent);
  text_y += small_h + 8;

  cgi.Draw2DString(
    x + CG_RACE_FINISH_REPORT_PAD, text_y,
    va("SPEED  %.0f -> %.0f   TOP %.0f   AVG %.0f",
       report->start_speed, report->end_speed,
       report->top_speed, report->average_speed), color_white);
  text_y += small_h + 8;

  char world_record[32];
  Cg_Race_FormatElapsed(report->world_record, world_record,
                        sizeof(world_record));
  cgi.Draw2DString(x + CG_RACE_FINISH_REPORT_PAD, text_y,
                   report->world_record
                     ? va("WR  %s", world_record)
                     : "WR  --:--.---",
                   Color4f(1.f, .82f, .18f, 1.f));
  text_y += small_h + 12;

  if (report->checkpoint_count) {
    cgi.Draw2DFill(x + CG_RACE_FINISH_REPORT_PAD, text_y - 5,
                   panel_w - CG_RACE_FINISH_REPORT_PAD * 2, 1,
                   Color4f(accent.r, accent.g, accent.b, .25f));
    const int32_t column_w =
      (panel_w - CG_RACE_FINISH_REPORT_PAD * 2) / (int32_t) columns;
    for (size_t i = 0; i < report->checkpoint_count; i++) {
      char split[32];
      Cg_Race_FormatElapsed(report->checkpoint_times[i], split,
                            sizeof(split));
      const size_t column = i / rows;
      const size_t row = i % rows;
      cgi.Draw2DString(x + CG_RACE_FINISH_REPORT_PAD +
                        (int32_t) column * column_w,
                       text_y + (int32_t) row * small_h,
                       va("CP %02zu  %s", i + 1u, split),
                       Color4f(.72f, .8f, .84f, 1.f));
    }
  }
  cgi.BindFont(NULL, NULL, NULL);
}
