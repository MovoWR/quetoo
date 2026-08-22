/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "cg_race_hud.h"
#include "cg_race_vote.h"
#include "cg_score.h"

// Tab row geometry, in 1440p design pixels.
#define RACE_HUD_VOTE_TAB_W 34.f
#define RACE_HUD_VOTE_TAB_H 5.f
#define RACE_HUD_VOTE_TAB_GAP 4.f

/**
 * @brief The point past which one tab can no longer mean one player.
 * @remarks A full server would otherwise push the row off both edges, and a
 * row of 64 four-pixel slivers reads as a bar, not a tally.
 */
#define RACE_HUD_VOTE_TAB_MAX 32

cg_race_vote_client_state_t Cg_RaceVote_ClientState(
    const player_state_t *ps) {
  const uint16_t flags = ps ? (uint16_t) ps->stats[STAT_RACE_VOTE_FLAGS] : 0u;
  return (cg_race_vote_client_state_t) {
    .authoritative = (flags & RACE_VOTE_CLIENT_STATE_VALID) != 0u,
    .can_cast = (flags & RACE_VOTE_CLIENT_CAN_CAST) != 0u,
    .can_cast_menu = (flags & RACE_VOTE_MENU_CLIENT_CAN_CAST) != 0u,
    .can_nominate = (flags & RACE_VOTE_MENU_CLIENT_CAN_NOMINATE) != 0u
  };
}

static bool Cg_RaceVote_ParseInt(const char *begin, const char *end,
                                int32_t minimum, int32_t maximum,
                                int32_t *value) {
  if (!begin || !end || begin >= end || !value ||
      (size_t) (end - begin) >= 16u) {
    return false;
  }
  char field[16];
  memcpy(field, begin, (size_t) (end - begin));
  field[end - begin] = '\0';
  char *parsed_end;
  errno = 0;
  const long parsed = strtol(field, &parsed_end, 10);
  if (errno || !parsed_end || *parsed_end || parsed < minimum ||
      parsed > maximum) {
    return false;
  }
  *value = (int32_t) parsed;
  return true;
}

static bool Cg_RaceVote_CopyField(char *output, const size_t output_size,
                                  const char **cursor, const char delimiter) {
  const char *begin = *cursor;
  const char *end = strchr(begin, delimiter);
  if (!end || end == begin || (size_t) (end - begin) >= output_size) {
    return false;
  }
  memcpy(output, begin, (size_t) (end - begin));
  output[end - begin] = '\0';
  *cursor = end + 1;
  return true;
}

bool Cg_RaceVote_ParseInfo(const char *wire, cg_race_vote_info_t *info) {
  if (!wire || !*wire || !info || q_strlen(wire) >= MAX_STRING_CHARS) {
    return false;
  }
  cg_race_vote_info_t parsed = { 0 };
  const char *cursor = wire;
  if (!Cg_RaceVote_CopyField(parsed.type, sizeof(parsed.type), &cursor, '|') ||
      !Cg_RaceVote_CopyField(parsed.initiator, sizeof(parsed.initiator),
                            &cursor, '|') ||
      !Cg_RaceVote_CopyField(parsed.target, sizeof(parsed.target), &cursor,
                            '|')) {
    return false;
  }
  int32_t *values[] = {
    &parsed.yes_votes, &parsed.no_votes, &parsed.needed, &parsed.remaining
  };
  for (size_t i = 0; i < lengthof(values); i++) {
    const char *end = i + 1u < lengthof(values) ? strchr(cursor, '|')
                                                : cursor + q_strlen(cursor);
    if (!end || !Cg_RaceVote_ParseInt(cursor, end, 0, INT32_MAX,
                                      values[i])) {
      return false;
    }
    cursor = *end ? end + 1 : end;
  }
  if (*cursor) {
    return false;
  }
  *info = parsed;
  return true;
}

static bool Cg_RaceVote_MapNameValid(const char *name) {
  if (!name || !*name || q_strlen(name) >= RACE_MAP_IDENTITY_SIZE) {
    return false;
  }

  const size_t length = q_strlen(name);
  if (name[0] == '/' || name[length - 1u] == '/') {
    return false;
  }

  size_t segment_start = 0u;
  for (size_t i = 0u; i <= length; i++) {
    if (i < length &&
        !((name[i] >= 'a' && name[i] <= 'z') ||
          (name[i] >= '0' && name[i] <= '9') ||
          name[i] == '_' || name[i] == '-' || name[i] == '.' ||
          name[i] == '/')) {
      return false;
    }

    if (i == length || name[i] == '/') {
      const size_t segment_length = i - segment_start;
      if (!segment_length ||
          (segment_length == 1u && name[segment_start] == '.') ||
          (segment_length == 2u && name[segment_start] == '.' &&
           name[segment_start + 1u] == '.')) {
        return false;
      }
      segment_start = i + 1u;
    }
  }
  return true;
}

bool Cg_RaceVote_ParseMenu(const char *wire, cg_race_vote_menu_t *menu) {
  if (!wire || !*wire || !menu || q_strlen(wire) >= MAX_STRING_CHARS) {
    return false;
  }
  cg_race_vote_menu_t parsed = { 0 };
  const char *pipe = strchr(wire, '|');
  if (!pipe || !Cg_RaceVote_ParseInt(wire, pipe, 0, INT32_MAX,
                                     &parsed.remaining)) {
    return false;
  }
  const char *cursor = pipe + 1;
  while (*cursor) {
    if (parsed.num_choices == RACE_VOTE_MENU_MAX_CHOICES) {
      return false;
    }
    const char *name_end = strchr(cursor, '\\');
    if (!name_end || name_end == cursor ||
        (size_t) (name_end - cursor) >= RACE_MAP_IDENTITY_SIZE) {
      return false;
    }
    race_vote_menu_choice_t *choice =
      parsed.choices + parsed.num_choices;
    char name[RACE_MAP_IDENTITY_SIZE];
    memcpy(name, cursor, (size_t) (name_end - cursor));
    name[name_end - cursor] = '\0';
    if (!Cg_RaceVote_MapNameValid(name)) {
      return false;
    }
    q_strlcpy(choice->name, name, sizeof(choice->name));
    cursor = name_end + 1;
    const char *votes_end = strchr(cursor, '\\');
    if (!votes_end) {
      votes_end = cursor + q_strlen(cursor);
    }
    int32_t votes;
    if (!Cg_RaceVote_ParseInt(cursor, votes_end, 0, UINT16_MAX, &votes)) {
      return false;
    }
    choice->votes = (uint16_t) votes;
    parsed.num_choices++;
    cursor = *votes_end ? votes_end + 1 : votes_end;
  }
  if (!parsed.num_choices) {
    return false;
  }
  *menu = parsed;
  return true;
}

/**
 * @brief The resolved state of an open vote.
 */
typedef enum {
  CG_RACE_VOTE_OPEN,
  CG_RACE_VOTE_CLOSING,
  CG_RACE_VOTE_PASSED,
  CG_RACE_VOTE_FAILED
} cg_race_vote_state_t;

static cg_race_vote_state_t Cg_RaceVote_State(
    const cg_race_vote_info_t *info) {
  if (info->remaining > 0) {
    return info->remaining < 5 ? CG_RACE_VOTE_CLOSING : CG_RACE_VOTE_OPEN;
  }

  return info->yes_votes >= info->needed
    ? CG_RACE_VOTE_PASSED : CG_RACE_VOTE_FAILED;
}

/**
 * @return The one accent that recolours the line and the tabs together.
 */
static color_t Cg_RaceVote_Accent(const cg_race_vote_state_t state) {
  switch (state) {
    case CG_RACE_VOTE_CLOSING:
      return Cg_RaceHud_Gold();
    case CG_RACE_VOTE_PASSED:
      return Cg_RaceHud_Green();
    case CG_RACE_VOTE_FAILED:
      return Cg_RaceHud_Warn();
    default:
      return Cg_RaceHud_Cyan();
  }
}

/**
 * @return The number of tabs the row draws.
 * @details The eligible-player count is not on the vote wire -- the info
 * carries type, initiator, target, yes, no, needed and remaining, and nothing
 * else -- so it is counted client-side from the roster: connected
 * non-spectators, floored by the votes already cast so the row can never be
 * shorter than the tally it has to show.
 * @remarks Past RACE_HUD_VOTE_TAB_MAX the row stops meaning one-tab-per-player
 * and falls back to `needed` tabs, which reads as votes toward passing. That
 * is a different sentence, but it is the only one that still fits.
 */
static uint16_t Cg_RaceVote_TabCount(const cg_race_vote_info_t *info) {
  cg_roster_entry_t roster[MAX_CLIENTS];
  const size_t count = Cg_RosterSnapshot(roster, lengthof(roster));

  size_t eligible = 0;
  for (size_t i = 0; i < count; i++) {
    if (roster[i].group != CG_ROSTER_SPECTATOR) {
      eligible++;
    }
  }

  int32_t total = Maxi((int32_t) eligible, info->yes_votes + info->no_votes);
  if (total > RACE_HUD_VOTE_TAB_MAX) {
    total = info->needed;
  }

  return (uint16_t) Maxi(Mini(total, RACE_HUD_VOTE_TAB_MAX), 1);
}

/**
 * @brief Draws the tab row: one tab per player, yes filling the accent from
 * the left and no filling from the right.
 *
 * This is the same visual language as the checkpoint pips, so it needs no
 * explanation.
 */
static void Cg_RaceVote_DrawTabs(const cg_race_vote_info_t *info,
                                 const int32_t y, const color_t accent) {
  const uint16_t players = Cg_RaceVote_TabCount(info);
  const int32_t tab_height = Cg_RaceHud_Scale(RACE_HUD_VOTE_TAB_H);
  const int32_t gap = Cg_RaceHud_Scale(RACE_HUD_VOTE_TAB_GAP);

  // The row gives up tab width before it gives up fitting, exactly as the
  // checkpoint pips do. With the count capped at 32 this has slack at every
  // supported resolution; it is what keeps that true if the cap, the design
  // width or the edge inset ever moves.
  const int32_t budget = cgi.context->w - Cg_RaceHud_Edge() * 2 -
                         (players - 1) * gap;
  const int32_t tab_width = Maxi(Mini(Cg_RaceHud_Scale(RACE_HUD_VOTE_TAB_W),
                                      budget / players), 1);
  const int32_t total = players * tab_width + (players - 1) * gap;
  const int32_t yes = Mini(info->yes_votes, players);
  const int32_t no = Mini(info->no_votes, players - yes);
  int32_t x = cgi.context->w / 2 - total / 2;

  for (uint16_t i = 0; i < players; i++) {
    color_t color;
    if (i < yes) {
      color = accent;
    } else if (i >= players - no) {
      color = Cg_RaceHud_Warn();
    } else {
      color = Color4f(1.f, 1.f, 1.f, .15f);
    }

    cgi.Draw2DFill(x, y, tab_width, tab_height, color);

    // The tab at the pass threshold carries an inset outline, so the bar
    // shows how far the yes side still has to travel.
    if (info->needed > 0 && i == (uint16_t) (info->needed - 1)) {
      const color_t outline = Color4f(1.f, 1.f, 1.f, .55f);
      cgi.Draw2DFill(x, y, tab_width, 1, outline);
      cgi.Draw2DFill(x, y + tab_height - 1, tab_width, 1, outline);
      cgi.Draw2DFill(x, y, 1, tab_height, outline);
      cgi.Draw2DFill(x + tab_width - 1, y, 1, tab_height, outline);
    }

    x += tab_width + gap;
  }
}

/**
 * @brief Draws the active vote on the line under WR/PB. The vote rides the
 * top lockup: no new HUD region, no plate.
 */
static void Cg_RaceVote_DrawActive(void) {
  if (cgi.GetKeyDest() == KEY_UI) {
    return;
  }

  cg_race_vote_info_t info;
  if (!Cg_RaceVote_ParseInfo(cgi.ConfigString(CS_RACE_VOTE_INFO), &info)) {
    return;
  }

  const cg_race_vote_state_t state = Cg_RaceVote_State(&info);
  const color_t accent = Cg_RaceVote_Accent(state);
  const bool resolved = state == CG_RACE_VOTE_PASSED ||
                        state == CG_RACE_VOTE_FAILED;

  char eyebrow[24];
  q_strlcpy(eyebrow, info.type, sizeof(eyebrow));
  for (char *c = eyebrow; *c; c++) {
    *c = (char) toupper((unsigned char) *c);
  }

  char subject[128], tally[64], remaining[16];
  const cg_race_vote_client_state_t client = Cg_RaceVote_ClientState(
    cgi.client ? &cgi.client->frame.ps : NULL);
  const bool can_cast = !client.authoritative || client.can_cast;

  if (state == CG_RACE_VOTE_PASSED) {
    q_snprintf(subject, sizeof(subject), "Vote passed%snext map",
               RACE_HUD_SEPARATOR);
  } else if (state == CG_RACE_VOTE_FAILED) {
    q_snprintf(subject, sizeof(subject), "Vote failed%sstays",
               RACE_HUD_SEPARATOR);
  } else {
    q_strlcpy(subject, info.target, sizeof(subject));
  }

  q_snprintf(tally, sizeof(tally), "%d / %d", info.yes_votes, info.needed);
  q_snprintf(remaining, sizeof(remaining), "%ds", info.remaining);

  // Measure the row before placing it, so it stays centered on the lockup.
  int32_t eyebrow_height, body_height, subject_height;
  Cg_RaceHud_BindFont(RACE_FONT_LABEL, &eyebrow_height);
  const int32_t eyebrow_width = cgi.StringWidth(eyebrow);
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &body_height);
  const int32_t proposer_width = resolved ? 0 : cgi.StringWidth(info.initiator);
  const int32_t tally_width = resolved ? 0 : cgi.StringWidth(tally);
  const int32_t remaining_width = resolved ? 0 : cgi.StringWidth(remaining);
  const int32_t hint_width = resolved || !can_cast
    ? 0 : cgi.StringWidth(RACE_HUD_SEPARATOR "Y / N");
  Cg_RaceHud_BindFont(RACE_FONT_RECORD, &subject_height);
  const int32_t subject_width = cgi.StringWidth(subject);

  const int32_t gap = Cg_RaceHud_Scale(14.f);
  int32_t width = eyebrow_width + gap + subject_width;
  if (proposer_width) {
    width += gap + proposer_width;
  }
  if (tally_width) {
    width += gap + tally_width + gap + remaining_width;
  }
  if (hint_width) {
    width += hint_width;
  }

  const int32_t row_height = Maxi(Maxi(eyebrow_height, body_height),
                                  subject_height);
  const int32_t y = Cg_RaceHud_TopStackBottom() + Cg_RaceHud_Scale(12.f);
  int32_t x = cgi.context->w / 2 - width / 2;

  Cg_RaceHud_BindFont(RACE_FONT_LABEL, NULL);
  Cg_RaceHud_DrawShadowedString(x, y + (row_height - eyebrow_height) / 2,
                                eyebrow, accent);
  x += eyebrow_width + gap;

  if (proposer_width) {
    Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
    Cg_RaceHud_DrawShadowedString(x, y + (row_height - body_height) / 2,
                                  info.initiator,
                                  Color4f(1.f, 1.f, 1.f, .66f));
    x += proposer_width + gap;
  }

  Cg_RaceHud_BindFont(RACE_FONT_RECORD, NULL);
  Cg_RaceHud_DrawShadowedString(x, y + (row_height - subject_height) / 2,
                                subject, Color4f(1.f, 1.f, 1.f, .88f));
  x += subject_width;

  if (tally_width) {
    Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
    x += gap;
    Cg_RaceHud_DrawShadowedString(x, y + (row_height - body_height) / 2,
                                  tally, accent);
    x += tally_width + gap;
    Cg_RaceHud_DrawShadowedString(x, y + (row_height - body_height) / 2,
                                  remaining, Color4f(1.f, 1.f, 1.f, .5f));
    x += remaining_width;
  }

  if (hint_width) {
    Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
    Cg_RaceHud_DrawShadowedString(x, y + (row_height - body_height) / 2,
                                  RACE_HUD_SEPARATOR "Y / N",
                                  Color4f(1.f, 1.f, 1.f, .44f));
  }

  Cg_RaceVote_DrawTabs(&info, y + row_height + Cg_RaceHud_Scale(10.f),
                       accent);
  cgi.BindFont(NULL, NULL, NULL);
}

static void Cg_RaceVote_DrawMenu(void) {
  if (cgi.GetKeyDest() == KEY_UI) {
    return;
  }

  cg_race_vote_menu_t menu;
  if (!Cg_RaceVote_ParseMenu(cgi.ConfigString(CS_RACE_VOTE_MENU), &menu)) {
    return;
  }
  cgi.BindFont("medium", NULL, NULL);
  char header[96];
  q_snprintf(header, sizeof(header),
             "^2=== Vote for next map! (%ds) ===^7", menu.remaining);
  int32_t y = cgi.context->h / 2 - 96;
  cgi.Draw2DString((cgi.context->w - cgi.StringWidth(header)) / 2, y,
                   header, color_white);
  cgi.BindFont("small", NULL, NULL);
  y += 48;
  for (uint8_t i = 0; i < menu.num_choices; i++) {
    char line[128];
    q_snprintf(line, sizeof(line), "^2%u^7: %s (%u votes)", i + 1u,
               menu.choices[i].name, menu.choices[i].votes);
    cgi.Draw2DString((cgi.context->w - cgi.StringWidth(line)) / 2, y,
                     line, color_white);
    y += 18;
  }
  cgi.BindFont(NULL, NULL, NULL);
}

void Cg_RaceVote_Draw(void) {
  Cg_RaceVote_DrawActive();
  Cg_RaceVote_DrawMenu();
}
