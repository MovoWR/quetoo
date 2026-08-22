/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "cg_local.h"

#include <inttypes.h>

#include "cg_race_hud.h"
#include "cg_race_presentation.h"
#include "cg_score.h"
#include "cg_score_model.h"
#include "race_physics.h"
#include "race_wire.h"
#include "ui/home/HomeViewController.h"

// Every metric below is a 1440p design pixel and reaches a draw call only
// through Cg_RaceHud_Scale, so 720p, 1080p and 1440p stay one composition
// rather than three. None of it is a cvar: the board is not configurable.
#define SCORES_BAND_H              56.f
#define SCORES_BAND_PAD_X          56.f
#define SCORES_BAND_PAD_Y          16.f
#define SCORES_BAND_GROUP_GAP      40.f
#define SCORES_GROUP_LABEL_GAP     14.f
#define SCORES_GROUP_COUNT_GAP     11.f
#define SCORES_CHIP_ROW_H          24.f
#define SCORES_CHIP_GAP            10.f
#define SCORES_CHIP_PAD_X          13.f
#define SCORES_CHIP_PAD_Y           7.f
#define SCORES_CHIP_PILL_PAD_X     18.f
#define SCORES_CHIP_DETAIL_GAP     10.f
#define SCORES_CHIP_TARGET_GAP      4.f
#define SCORES_SPECTATOR_COLLAPSE  16u

#define SCORES_BOARD_MAX_W       1560.f
#define SCORES_BOARD_MARGIN       112.f
#define SCORES_BOARD_INSET         64.f
#define SCORES_TITLE_GAP            8.f
#define SCORES_PATH_GAP            12.f
#define SCORES_HEAD_PAD            24.f
#define SCORES_STAT_GAP            56.f
#define SCORES_STAT_CAPTION_GAP     3.f
#define SCORES_STANDING_PAD_TOP    20.f
#define SCORES_STANDING_PAD_BOTTOM 24.f
#define SCORES_STANDING_GAP        24.f
#define SCORES_CAPTION_PAD         12.f

// The 56px design title resolves to the 64px atlas; that is the ceiling the
// title is fitted against, not the design size itself.
#define SCORES_TITLE_MAX_H         64.f

#define SCORES_ROW_H               56.f
#define SCORES_EMPTY_ROW_H         48.f
#define SCORES_ROW_INK_PAD          6.f
#define SCORES_ROW_TINT_BLEED      14.f
#define SCORES_ROW_EDGE_W           2.f
#define SCORES_COL_RANK_W          72.f
#define SCORES_COL_TIME_W         210.f
#define SCORES_COL_GAP_W          170.f
#define SCORES_COL_DATE_W         200.f
#define SCORES_COL_GUTTER          24.f
#define SCORES_TAG_GAP             20.f
#define SCORES_TAG_PAD_X           10.f
#define SCORES_TAG_PAD_Y            3.f

// The named colours of the design system, kept as hex so a value here can be
// read against the design file by eye.
#define SCORES_INK        0xf4f8fbu
#define SCORES_VALUE      0xdde8f1u
#define SCORES_CAPTION    0x93aec1u
#define SCORES_MUTED      0x74899bu
#define SCORES_RANK       0x5c8fb5u
#define SCORES_GOLD       0xe7c368u
#define SCORES_SILVER     0xc6d5e1u
#define SCORES_BRONZE     0xc9814fu
#define SCORES_CYAN       0x8fd5f5u
#define SCORES_MINE       0x3e6b9du
#define SCORES_MINE_EDGE  0x73c7f2u
#define SCORES_MINE_INK   0xc3e8fau
#define SCORES_SEPARATOR  0x31516au
#define SCORES_RULE       0x2f4a5eu
#define SCORES_ROW_RULE   0x24333fu
#define SCORES_EMPTY      0x3d4d5bu
#define SCORES_SCRIM      0x060d15u

#define CG_PING_HISTORY_SAMPLES 24

typedef race_leaderboard_wire_entry_t cg_leaderboard_entry_t;

typedef struct {
  char source[MAX_STRING_CHARS];
  cg_leaderboard_entry_t entries[RACE_LEADERBOARD_TOP_MAX];
  size_t count;
  bool valid;
} cg_leaderboard_state_t;

typedef struct {
  char name[32];
  int16_t samples[CG_PING_HISTORY_SAMPLES];
  uint8_t count;
  uint8_t next;
} cg_ping_history_t;

typedef struct {
  cg_score_model_t model;
  cg_ping_history_t ping_history[MAX_CLIENTS];
} cg_score_state_t;

/**
 * @brief The three regions the surface is composed of: two full-width roster
 * bands, and the board centred between them.
 */
typedef struct {
  SDL_Rect spectators;
  SDL_Rect board;
  SDL_Rect players;
  int32_t row_h, rank_x, name_x, name_right, time_right, gap_right, date_right;
  int32_t tier;
} cg_scores_layout_t;

typedef struct {
  size_t counts[3];
  size_t connected;
  int32_t local_index;
} cg_roster_summary_t;

static cg_leaderboard_state_t cg_leaderboard_state;
static cg_score_state_t cg_score_state;

_Static_assert(RACE_LEADERBOARD_CONFIG_MAX_BYTES < MAX_STRING_CHARS,
               "Race leaderboard configstring exceeds MAX_STRING_CHARS");

/**
 * @brief Rebuilds the cached Top-15 only when its configstring changes.
 */
static void Cg_UpdateLeaderboard(void) {
  const char *source = cgi.ConfigString(CS_RACE_LEADERBOARD);
  source = source ? source : "";
  if (!strcmp(cg_leaderboard_state.source, source)) {
    return;
  }

  cg_leaderboard_state_t parsed = { 0 };
  parsed.valid = Race_LeaderboardWire_Decode(
    source, parsed.entries, lengthof(parsed.entries), &parsed.count);
  q_strlcpy(parsed.source, source, sizeof(parsed.source));
  if (!parsed.valid) {
    parsed.count = 0;
    Cg_Debug("Rejected malformed Race leaderboard configstring\n");
  }
  cg_leaderboard_state = parsed;
}

/**
 * @brief Clears a client's recent connection samples.
 */
static void Cg_ClearPingHistory(cg_ping_history_t *history) {
  memset(history, 0, sizeof(*history));
}

static const char *Cg_ScoreName(const g_score_t *score) {
  return score && score->client < MAX_CLIENTS
           ? cg_state.clients[score->client].name
           : "";
}

/**
 * @brief Records one valid scoreboard ping sample for a client slot.
 */
static void Cg_AddPingSample(cg_ping_history_t *history, const char *name, int16_t ping) {

  if (strcmp(history->name, name)) {
    Cg_ClearPingHistory(history);
    q_strlcpy(history->name, name, sizeof(history->name));
  }

  if (ping < 0 || ping >= 999) {
    return;
  }

  history->samples[history->next] = ping;
  history->next = (history->next + 1) % CG_PING_HISTORY_SAMPLES;
  history->count = (uint8_t) Mini(history->count + 1,
                                  CG_PING_HISTORY_SAMPLES);
}

/**
 * @brief Updates rolling ping samples once a complete scoreboard snapshot arrives.
 */
static void Cg_UpdatePingHistory(void) {

  bool present[MAX_CLIENTS] = { false };

  for (size_t i = 0; i < cg_score_state.model.num_scores; i++) {
    const g_score_t *score = &cg_score_state.model.scores[i];
    if (score->flags & SCORE_AGGREGATE || score->client >= MAX_CLIENTS) {
      continue;
    }

    present[score->client] = true;
    Cg_AddPingSample(&cg_score_state.ping_history[score->client],
                     Cg_ScoreName(score), score->ping);
  }

  for (size_t i = 0; i < MAX_CLIENTS; i++) {
    if (!present[i]) {
      Cg_ClearPingHistory(&cg_score_state.ping_history[i]);
    }
  }
}

/**
 * @brief Copies recent ping statistics into a presentation-neutral roster entry.
 */
static void Cg_SetConnectionStats(cg_roster_entry_t *entry, const cg_ping_history_t *history) {

  entry->ping_average = entry->ping;
  entry->ping_min = entry->ping;
  entry->ping_max = entry->ping;
  entry->ping_variation = 0;
  entry->ping_samples = history->count;

  if (history->count) {
    int32_t sum = 0;
    int32_t variation = 0;
    int16_t previous = 0;
    const size_t start = history->count == CG_PING_HISTORY_SAMPLES ? history->next : 0;

    entry->ping_min = INT16_MAX;
    entry->ping_max = 0;

    for (size_t i = 0; i < history->count; i++) {
      const int16_t sample = history->samples[(start + i) % CG_PING_HISTORY_SAMPLES];
      sum += sample;
      entry->ping_min = Mini(entry->ping_min, sample);
      entry->ping_max = Maxi(entry->ping_max, sample);
      if (i) {
        variation += abs(sample - previous);
      }
      previous = sample;
    }

    entry->ping_average = (int16_t) ((sum + history->count / 2) / history->count);
    if (history->count > 1) {
      entry->ping_variation = (int16_t) ((variation + (history->count - 1) / 2) /
                                         (history->count - 1));
    }
  }

  if (entry->ping < 0 || entry->ping >= 999) {
    entry->quality = CG_CONNECTION_UNAVAILABLE;
  } else if (entry->ping <= 70 && (entry->ping_samples < 4 || entry->ping_variation <= 20)) {
    entry->quality = CG_CONNECTION_GOOD;
  } else if (entry->ping <= 140 && (entry->ping_samples < 4 || entry->ping_variation <= 45)) {
    entry->quality = CG_CONNECTION_FAIR;
  } else {
    entry->quality = CG_CONNECTION_POOR;
  }
}

/**
 * @brief A comparator for sorting scores by score value (ascending = best first).
 */
void Cg_ParseScores(void) {
  const int32_t index = cgi.ReadShort();
  const int32_t count = cgi.ReadShort();

  if (!Cg_ScoreModelRangeValid(index, count)) {
    Cg_Error("Invalid score index and count: %d + %d\n", index, count);
  }

  g_score_t scores[CG_SCORE_MODEL_CAPACITY];
  cgi.ReadData(scores, (size_t) count * sizeof(*scores));
  const cg_score_model_result_t result = Cg_ScoreModelApply(
    &cg_score_state.model, index, scores, count, cgi.ReadByte() != 0);
  if (result == CG_SCORE_MODEL_INVALID) {
    Cg_Error("Invalid partial score sequence: %d + %d\n", index, count);
  }
  if (result == CG_SCORE_MODEL_COMPLETE) {
    Cg_UpdatePingHistory();
    HomeViewController_Refresh();
  }
}

/**
 * @brief Returns the latest round-trip latency for the local client.
 */
int16_t Cg_LocalPing(void) {

  if (cgi.client) {
    const uint16_t client = cgi.client->frame.ps.client;
    for (size_t i = 0; i < cg_score_state.model.num_scores; i++) {
      const g_score_t *score = &cg_score_state.model.scores[i];
      if (!(score->flags & SCORE_AGGREGATE) && score->client == client) {
        return score->ping;
      }
    }
  }

  return -1;
}

/**
 * @brief Copies the latest server roster into presentation-neutral entries.
 */
size_t Cg_RosterSnapshot(cg_roster_entry_t *entries, size_t capacity) {

  size_t count = 0;

  for (size_t i = 0; i < cg_score_state.model.num_scores && count < capacity; i++) {
    const g_score_t *score = &cg_score_state.model.scores[i];
    if (score->flags & SCORE_AGGREGATE || score->client >= MAX_CLIENTS) {
      continue;
    }

    cg_roster_entry_t *entry = &entries[count++];
    memset(entry, 0, sizeof(*entry));

    entry->client = score->client;
    entry->ping = score->ping;
    entry->flags = score->flags;
    entry->spectator_target = -1;
    q_strlcpy(entry->name, Cg_ScoreName(score), sizeof(entry->name));
    Cg_SetConnectionStats(entry, &cg_score_state.ping_history[score->client]);

    if (score->flags & SCORE_SPECTATOR) {
      entry->group = CG_ROSTER_SPECTATOR;
      if (score->team > 0) {
        entry->spectator_target = score->team - 1;
      }
    } else if (score->race_mode == RACE_MODE_PRACTICE) {
      entry->group = CG_ROSTER_PRACTICE_MODE;
    } else {
      entry->group = CG_ROSTER_RACE_MODE;
    }
  }

  return count;
}

/**
 * @brief Format a score value as a time string M:SS.mmm.
 */
static const char *Cg_FormatTime(int32_t score_ms) {
  if (score_ms <= 0) {
    return "--:--.---";
  }
  const int32_t minutes = score_ms / 60000;
  const int32_t seconds = (score_ms / 1000) % 60;
  const int32_t millis = score_ms % 1000;
  return va("%d:%02d.%03d", minutes, seconds, millis);
}

/**
 * @brief Formats authoritative Unix seconds as a UTC YYYY-MM-DD date.
 */
static const char *Cg_FormatDate(uint64_t unix_seconds) {
  if (!unix_seconds) {
    return "--";
  }
  int64_t z = (int64_t) (unix_seconds / 86400u) + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = yoe + era * 400;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const int64_t m = mp + (mp < 10 ? 3 : -9);
  y += (m <= 2);
  return va("%04" PRId64 "-%02" PRId64 "-%02" PRId64, y, m, d);
}

/**
 * @brief Formats a leaderboard gap from the authoritative first-place time.
 */
static const char *Cg_FormatGap(uint32_t time_ms, uint32_t best_ms) {
  if (time_ms <= best_ms) {
    return "-";
  }
  const uint32_t gap = time_ms - best_ms;
  return va("+%u.%03u", gap / 1000u, gap % 1000u);
}

/**
 * @brief Copies text into a fixed-width presentation buffer with an ellipsis.
 */
static void Cg_CopyFittedText(char *output, size_t output_size,
                              const char *text, int32_t maximum_width) {
  if (!output || !output_size) {
    return;
  }
  q_strlcpy(output, text ? text : "", output_size);
  if (maximum_width <= 0) {
    output[0] = '\0';
    return;
  }
  if (cgi.StringWidth(output) <= maximum_width) {
    return;
  }

  const char *ellipsis = "...";
  const int32_t ellipsis_width = cgi.StringWidth(ellipsis);
  if (ellipsis_width > maximum_width) {
    output[0] = '\0';
    return;
  }

  size_t length = strlen(output);
  while (length) {
    length--;
    while (length && ((uint8_t) output[length] & 0xc0u) == 0x80u) {
      length--;
    }
    output[length] = '\0';
    if (cgi.StringWidth(output) + ellipsis_width <= maximum_width) {
      break;
    }
  }
  q_strlcat(output, ellipsis, output_size);
}

/**
 * @brief Binds the largest font in `name`'s fallback chain whose glyphs fit
 * `limit` pixels tall.
 * @details The type scale of the Race HUD spec §1.4 resolves each design size
 * to one of three fixed bitmap atlases -- 16, 32 and 64 pixels -- and those do
 * not shrink with the viewport. Every other Race cluster is padding-derived so
 * it simply grows, but the board's rows are a fixed height by design (§7.2),
 * so the glyphs have to step down instead of overflowing the row. This is the
 * same move the checkpoint ribbon makes for a long route.
 */
static void Cg_ScoresBindFitted(const char *name, const int32_t limit,
                                int32_t *height) {
  const char *const chain[] = { name, RACE_FONT_VALUE, RACE_FONT_BODY };

  for (size_t i = 0; i < lengthof(chain); i++) {
    int32_t bound;
    Cg_RaceHud_BindFont(chain[i], &bound);
    if (height) {
      *height = bound;
    }
    if (limit <= 0 || bound <= limit) {
      return;
    }
  }
}

/**
 * @return One of the design's named colours at `alpha`.
 */
static color_t Cg_ScoresColor(const uint32_t rgb, const float alpha) {
  return Color4f(((rgb >> 16) & 0xffu) / 255.f,
                 ((rgb >> 8) & 0xffu) / 255.f,
                 (rgb & 0xffu) / 255.f, alpha);
}

/**
 * @brief Binds the font a roster chip carries its name in, fitted to the chip
 * row so the bands never grow taller than the design.
 */
static void Cg_ScoresBindChipFont(int32_t *height) {
  Cg_ScoresBindFitted(RACE_FONT_RECORD, Cg_RaceHud_Scale(SCORES_CHIP_ROW_H),
                      height);
}

/**
 * @brief Measures the chip row the roster bands are built from, which is what
 * gives both bands their height.
 */
static int32_t Cg_ScoresChipHeight(void) {
  int32_t name_height;
  Cg_ScoresBindChipFont(&name_height);
  return Cg_RaceHud_Scale(SCORES_CHIP_PAD_Y) * 2 + name_height;
}

/**
 * @brief Computes the three regions of §7.1: a full-width spectator band at
 * the top, a full-width player band at the bottom, and the board centred
 * between them. Either band collapses to zero height when it has no chips,
 * and the board grows into the space.
 */
static cg_scores_layout_t Cg_ScoresLayout(const cg_roster_summary_t *summary) {
  cg_scores_layout_t layout = { 0 };
  const int32_t viewport_w = cgi.context->w;
  const int32_t viewport_h = cgi.context->h;

  const int32_t band_h = Maxi(Cg_RaceHud_Scale(SCORES_BAND_H),
                              Cg_RaceHud_Scale(SCORES_BAND_PAD_Y) * 2 +
                                Cg_ScoresChipHeight());

  // A band with nobody in it has nothing to say, and 56px of dead space under
  // a hairline says it loudly. Either band collapses to nothing and gives the
  // board the room. Both are reachable: a server with no spectators empties
  // the top, and a lobby where everyone is spectating empties the bottom.
  const int32_t spectator_h = summary->counts[CG_ROSTER_SPECTATOR] ? band_h : 0;
  const int32_t player_h = (summary->counts[CG_ROSTER_RACE_MODE] ||
                            summary->counts[CG_ROSTER_PRACTICE_MODE]) ? band_h : 0;

  layout.spectators = (SDL_Rect) { 0, 0, viewport_w, spectator_h };
  layout.players = (SDL_Rect) {
    0, viewport_h - player_h, viewport_w, player_h
  };

  const int32_t inset = Cg_RaceHud_Scale(SCORES_BOARD_INSET);
  const int32_t board_w = Mini(Cg_RaceHud_Scale(SCORES_BOARD_MAX_W),
                               viewport_w - Cg_RaceHud_Scale(SCORES_BOARD_MARGIN));
  const int32_t board_y = spectator_h + inset;
  layout.board = (SDL_Rect) {
    (viewport_w - board_w) / 2, board_y, board_w,
    Maxi(0, viewport_h - player_h - inset - board_y)
  };

  const int32_t gutter = Cg_RaceHud_Scale(SCORES_COL_GUTTER);
  layout.rank_x = layout.board.x;
  layout.name_x = layout.rank_x + Cg_RaceHud_Scale(SCORES_COL_RANK_W) + gutter;
  layout.date_right = layout.board.x + layout.board.w;
  layout.gap_right = layout.date_right - Cg_RaceHud_Scale(SCORES_COL_DATE_W) - gutter;
  layout.time_right = layout.gap_right - Cg_RaceHud_Scale(SCORES_COL_GAP_W) - gutter;
  layout.name_right = layout.time_right - Cg_RaceHud_Scale(SCORES_COL_TIME_W) - gutter;

  layout.row_h = Cg_RaceHud_Scale(SCORES_ROW_H);

  // The count thresholds of §7.4 are only the opening bid; Cg_ScoresDrawBand
  // escalates from here by measuring, which is what makes the bands survive a
  // 720p viewport where far fewer chips fit than the counts assume.
  const size_t connected = summary->connected;
  layout.tier = connected <= 12 ? 0 : connected <= 24 ? 1 : connected <= 40 ? 2 : 3;

  return layout;
}

/**
 * @brief Returns the local player's current score-snapshot row.
 */
static const g_score_t *Cg_LocalScore(void) {
  if (!cgi.client) {
    return NULL;
  }
  const uint16_t local = cgi.client->frame.ps.client;
  for (size_t i = 0; i < cg_score_state.model.num_scores; i++) {
    const g_score_t *score = &cg_score_state.model.scores[i];
    if (!(score->flags & SCORE_AGGREGATE) && score->client == local) {
      return score;
    }
  }
  return NULL;
}

/**
 * @brief Returns the requesting player's current-map Race Mode PB.
 */
static uint32_t Cg_LocalBestTimeMilliseconds(void) {
  if (!cgi.client) {
    return 0;
  }
  const player_state_t *ps = &cgi.client->frame.ps;
  return Race_WireElapsed(ps->stats[STAT_RACE_PB_LOW],
                          ps->stats[STAT_RACE_PB_HIGH]);
}

/**
 * @brief Requires the complete score-snapshot name, time, and date triple.
 */
static bool Cg_LeaderboardEntryIsLocalPb(const cg_leaderboard_entry_t *entry,
                                         const g_score_t *local) {
  if (!entry || !local || local->client >= MAX_CLIENTS) {
    return false;
  }
  const uint32_t local_time = Cg_LocalBestTimeMilliseconds();
  return local_time && local_time == entry->time_ms &&
         !strcmp(entry->name, Cg_ScoreName(local));
}

/**
 * @brief Copies the validated current-map Top-15 into presentation-neutral entries.
 */
size_t Cg_LeaderboardSnapshot(cg_leaderboard_snapshot_entry_t *entries, size_t capacity) {

  Cg_UpdateLeaderboard();

  if (entries == NULL || capacity == 0) {
    return 0;
  }

  const g_score_t *local = Cg_LocalScore();
  const size_t count = Minz(cg_leaderboard_state.count, capacity);
  for (size_t i = 0; i < count; i++) {
    const cg_leaderboard_entry_t *source = &cg_leaderboard_state.entries[i];
    cg_leaderboard_snapshot_entry_t *entry = &entries[i];

    memset(entry, 0, sizeof(*entry));
    q_strlcpy(entry->name, source->name, sizeof(entry->name));
    entry->time_ms = source->time_ms;
    entry->date_unix_s = source->date_unix_s;
    entry->local_pb = Cg_LeaderboardEntryIsLocalPb(source, local);
  }

  return count;
}

/**
 * @brief Builds complete live roster counts and local-player identity.
 */
static cg_roster_summary_t Cg_RosterSummary(const cg_roster_entry_t *entries,
                                             size_t count) {
  cg_roster_summary_t summary = { .connected = count, .local_index = -1 };
  const int32_t local = cgi.client ? cgi.client->frame.ps.client : -1;
  for (size_t i = 0; i < count; i++) {
    const int32_t group = entries[i].group;
    if (group >= CG_ROSTER_RACE_MODE && group <= CG_ROSTER_SPECTATOR) {
      summary.counts[group]++;
    }
    if (entries[i].client == local) {
      summary.local_index = (int32_t) i;
    }
  }
  return summary;
}

/**
 * @brief Sentence case, like every other label on the surface: the group
 * heading is a heading, not a status light.
 */
static const char *Cg_RosterGroupName(cg_roster_group_t group) {
  switch (group) {
    case CG_ROSTER_RACE_MODE:
      return "Race mode";
    case CG_ROSTER_PRACTICE_MODE:
      return "Practice";
    case CG_ROSTER_SPECTATOR:
    default:
      return "Spectators";
  }
}

static color_t Cg_RosterGroupColor(cg_roster_group_t group) {
  switch (group) {
    case CG_ROSTER_RACE_MODE:
      return Color4b(0xe7, 0xc3, 0x68, 0xff);
    case CG_ROSTER_PRACTICE_MODE:
      return Color4b(0x8f, 0xd5, 0xf5, 0xff);
    case CG_ROSTER_SPECTATOR:
    default:
      return Color4b(0x93, 0xae, 0xc1, 0xff);
  }
}

/**
 * @brief Resolves the same spectator-target semantics as the ESC roster.
 */
static const char *Cg_RosterTargetName(const cg_roster_entry_t *entries,
                                       size_t count, int16_t target) {
  if (target < 0) {
    return "Free camera";
  }
  for (size_t i = 0; i < count; i++) {
    if (entries[i].client == target) {
      return entries[i].name;
    }
  }
  return "No target";
}

/**
 * @brief Draws one of the design's hairline rules.
 */
static void Cg_ScoresDrawRule(const int32_t x, const int32_t y, const int32_t w,
                              const uint32_t rgb, const float alpha) {
  cgi.Draw2DFill(x, y, w, 1, Cg_ScoresColor(rgb, alpha));
}

/**
 * @return The local player's row on the board, or -1 when they hold no ranked
 * time on this map.
 */
static int32_t Cg_ScoresLocalRank(void) {
  const g_score_t *local = Cg_LocalScore();

  for (size_t i = 0; i < cg_leaderboard_state.count; i++) {
    if (Cg_LeaderboardEntryIsLocalPb(&cg_leaderboard_state.entries[i], local)) {
      return (int32_t) i;
    }
  }

  return -1;
}

/**
 * @brief Draws the map identity and the three live header stats.
 * @return The y coordinate below the header's rule.
 */
static int32_t Cg_ScoresDrawBoardHeader(const cg_scores_layout_t *layout) {
  const int32_t x = layout->board.x;
  const int32_t right = layout->board.x + layout->board.w;
  int32_t eyebrow_h, title_h, path_h, value_h, caption_h;

  // The 56px line is the map's title, not its file name -- the same
  // CS_MESSAGE the HUD route line and the Home route already read. The
  // basename is only the fallback, because the server seeds CS_MESSAGE with
  // it before the game module publishes g_level.message.
  const char *bsp = cgi.ConfigString(CS_BSP);
  const char *message = cgi.ConfigString(CS_MESSAGE);
  char path[MAX_QPATH];
  char map_name[MAX_QPATH];
  if (bsp && *bsp) {
    q_strlcpy(path, bsp, sizeof(path));
    StripExtension(Basename(bsp), map_name);
  } else {
    q_strlcpy(path, "maps/?.bsp", sizeof(path));
    q_strlcpy(map_name, "Unknown map", sizeof(map_name));
  }
  if (message && *message) {
    q_strlcpy(map_name, message, sizeof(map_name));
  }

  // The design bottom-aligns the stats with the map path, so the left block is
  // measured first and the stats hang off its baseline.
  const race_physics_config_t *config = Race_Physics_Current();
  const race_physics_preset_descriptor_t *preset = Race_Physics_Preset(config->preset);
  const char *clock = cgi.ConfigString(CS_TIME);
  const char *stat_values[] = {
    va("%zu / %u", cg_leaderboard_state.count, (unsigned) RACE_LEADERBOARD_TOP_MAX),
    preset && preset->short_name ? preset->short_name : "--",
    clock && *clock ? clock : "--"
  };
  const char *stat_captions[] = { "Ranked", "Physics", "Map time" };

  int32_t y = layout->board.y;
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &eyebrow_h);
  Cg_RaceHud_DrawShadowedString(x, y, "Race leaderboard",
                                Cg_ScoresColor(SCORES_CAPTION, 1.f));

  y += eyebrow_h + Cg_RaceHud_Scale(SCORES_TITLE_GAP);
  Cg_ScoresBindFitted(RACE_FONT_TIMER, Cg_RaceHud_Scale(SCORES_TITLE_MAX_H),
                      &title_h);
  char title[MAX_STRING_CHARS];
  Cg_CopyFittedText(title, sizeof(title), map_name, Maxi(0, layout->board.w / 2));
  Cg_RaceHud_DrawShadowedString(x, y, title, Cg_ScoresColor(SCORES_INK, 1.f));

  y += title_h + Cg_RaceHud_Scale(SCORES_PATH_GAP);
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &path_h);
  Cg_RaceHud_DrawShadowedString(x, y, path, Cg_ScoresColor(SCORES_MUTED, 1.f));
  const int32_t block_bottom = y + path_h;

  Cg_RaceHud_BindFont(RACE_FONT_VALUE, &value_h);
  Cg_RaceHud_BindFont(RACE_FONT_LABEL, &caption_h);
  const int32_t caption_y = block_bottom - caption_h;
  const int32_t value_y = caption_y - Cg_RaceHud_Scale(SCORES_STAT_CAPTION_GAP) -
                          value_h;
  int32_t stat_right = right;
  for (size_t i = lengthof(stat_values); i > 0; i--) {
    const size_t stat = i - 1u;

    Cg_RaceHud_BindFont(RACE_FONT_VALUE, NULL);
    const int32_t value_w = cgi.StringWidth(stat_values[stat]);
    Cg_RaceHud_DrawRightAligned(stat_right, value_y, stat_values[stat],
                                Cg_ScoresColor(SCORES_VALUE, 1.f));

    Cg_RaceHud_BindFont(RACE_FONT_LABEL, NULL);
    const int32_t caption_w = cgi.StringWidth(stat_captions[stat]);
    Cg_RaceHud_DrawRightAligned(stat_right, caption_y, stat_captions[stat],
                                Cg_ScoresColor(SCORES_MUTED, 1.f));

    stat_right -= Maxi(value_w, caption_w) + Cg_RaceHud_Scale(SCORES_STAT_GAP);
  }

  const int32_t rule_y = block_bottom + Cg_RaceHud_Scale(SCORES_HEAD_PAD);
  Cg_ScoresDrawRule(x, rule_y, layout->board.w, SCORES_RULE, .45f);
  return rule_y + 1;
}

/**
 * @brief One coloured run of the standing line.
 */
typedef struct {
  const char *text;
  uint32_t rgb;
  bool lead;
} cg_scores_run_t;

/**
 * @brief Draws the standing line: where the local player sits, and the two
 * gaps they can act on.
 * @details Everything here derives from the cached board plus the local PB, so
 * the line costs no new wire data.
 * @return The y coordinate below the line's rule.
 */
static int32_t Cg_ScoresDrawStanding(const cg_scores_layout_t *layout,
                                     const int32_t top) {
  cg_scores_run_t runs[16];
  size_t count = 0;

  // Every run owns a buffer for the whole function: va() rotates through only
  // eight slots, and Cg_FormatGap spends one of its own per call.
  char rank_text[8], ranked[16], pb[32], record_gap[64], above_gap[64], lead[64];

  const int32_t rank = Cg_ScoresLocalRank();
  const uint32_t local_ms = Cg_LocalBestTimeMilliseconds();

  if (rank < 0 || !local_ms) {
    runs[count++] = (cg_scores_run_t) { "No ranked time yet", SCORES_MUTED, false };
  } else {
    q_snprintf(rank_text, sizeof(rank_text), "%02d", rank + 1);
    q_snprintf(ranked, sizeof(ranked), "%zu", cg_leaderboard_state.count);
    Cg_Race_FormatElapsed(local_ms, pb, sizeof(pb));

    runs[count++] = (cg_scores_run_t) { "Your standing", SCORES_MUTED, false };
    runs[count++] = (cg_scores_run_t) { rank_text, SCORES_CYAN, true };
    runs[count++] = (cg_scores_run_t) { " of ", SCORES_MUTED, false };
    runs[count++] = (cg_scores_run_t) { ranked, SCORES_VALUE, false };
    runs[count++] = (cg_scores_run_t) { "-", SCORES_SEPARATOR, true };
    runs[count++] = (cg_scores_run_t) { "PB ", SCORES_MUTED, true };
    runs[count++] = (cg_scores_run_t) { pb, SCORES_VALUE, false };

    // The two gap fields are the numbers a racer acts on: how far off the
    // record they are, and how far off the slot they can actually take next.
    if (rank > 0) {
      const uint32_t best_ms = cg_leaderboard_state.entries[0].time_ms;
      const cg_leaderboard_entry_t *above = &cg_leaderboard_state.entries[rank - 1];

      q_snprintf(record_gap, sizeof(record_gap), "%s to the record",
                 Cg_FormatGap(local_ms, best_ms));
      q_snprintf(above_gap, sizeof(above_gap), "%s to ",
                 Cg_FormatGap(local_ms, above->time_ms));

      runs[count++] = (cg_scores_run_t) { "-", SCORES_SEPARATOR, true };
      runs[count++] = (cg_scores_run_t) { record_gap, SCORES_MUTED, true };
      runs[count++] = (cg_scores_run_t) { "-", SCORES_SEPARATOR, true };
      runs[count++] = (cg_scores_run_t) { above_gap, SCORES_MUTED, true };
      runs[count++] = (cg_scores_run_t) { above->name, SCORES_VALUE, false };
    } else if (cg_leaderboard_state.count > 1u) {

      // At rank 01 both of those are gaps to yourself. The number the record
      // holder acts on runs the other way: the lead over 02 they have to
      // lose. Unsigned, unlike the two above -- "ahead of" already carries
      // the direction, and Cg_FormatGap collapses a tie to a dash, which
      // would read as "- ahead of".
      const cg_leaderboard_entry_t *below = &cg_leaderboard_state.entries[1];
      const uint32_t margin = below->time_ms > local_ms
                                ? below->time_ms - local_ms : 0u;

      q_snprintf(lead, sizeof(lead), "%u.%03u ahead of ",
                 margin / 1000u, margin % 1000u);

      runs[count++] = (cg_scores_run_t) { "-", SCORES_SEPARATOR, true };
      runs[count++] = (cg_scores_run_t) { lead, SCORES_MUTED, true };
      runs[count++] = (cg_scores_run_t) { below->name, SCORES_VALUE, false };
    }
  }

  int32_t height;
  Cg_RaceHud_BindFont(RACE_FONT_ROUTE, &height);

  const int32_t gap = Cg_RaceHud_Scale(SCORES_STANDING_GAP);
  const int32_t y = top + Cg_RaceHud_Scale(SCORES_STANDING_PAD_TOP);
  int32_t x = layout->board.x;
  for (size_t i = 0; i < count; i++) {
    if (runs[i].lead) {
      x += gap;
    }
    Cg_RaceHud_DrawShadowedString(x, y, runs[i].text,
                                  Cg_ScoresColor(runs[i].rgb, 1.f));
    x += cgi.StringWidth(runs[i].text);
  }

  const int32_t rule_y = y + height + Cg_RaceHud_Scale(SCORES_STANDING_PAD_BOTTOM);
  Cg_ScoresDrawRule(layout->board.x, rule_y, layout->board.w, SCORES_RULE, .45f);
  return rule_y + 1;
}

/**
 * @brief Draws the sentence-case column captions.
 * @return The y coordinate below the caption rule.
 */
static int32_t Cg_ScoresDrawCaptions(const cg_scores_layout_t *layout,
                                     const int32_t top) {
  int32_t height;
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &height);

  const color_t color = Cg_ScoresColor(SCORES_CAPTION, 1.f);
  Cg_RaceHud_DrawShadowedString(layout->rank_x, top, "#", color);
  Cg_RaceHud_DrawShadowedString(layout->name_x, top, "Player", color);
  Cg_RaceHud_DrawRightAligned(layout->time_right, top, "Time", color);
  Cg_RaceHud_DrawRightAligned(layout->gap_right, top, "Gap", color);
  Cg_RaceHud_DrawRightAligned(layout->date_right, top, "Date", color);

  const int32_t rule_y = top + height + Cg_RaceHud_Scale(SCORES_CAPTION_PAD);
  Cg_ScoresDrawRule(layout->board.x, rule_y, layout->board.w, SCORES_RULE, .45f);
  return rule_y + 1;
}

/**
 * @brief Draws the inline Record / Your PB tag that follows a name.
 */
static void Cg_ScoresDrawTag(const int32_t x, const int32_t row_y,
                             const int32_t row_h, const char *text,
                             const uint32_t fill, const float fill_alpha,
                             const uint32_t ink) {
  int32_t height;
  Cg_RaceHud_BindFont(RACE_FONT_LABEL, &height);

  const int32_t pad_x = Cg_RaceHud_Scale(SCORES_TAG_PAD_X);
  const int32_t pad_y = Cg_RaceHud_Scale(SCORES_TAG_PAD_Y);
  const int32_t w = cgi.StringWidth(text) + pad_x * 2;
  const int32_t h = height + pad_y * 2;
  const int32_t y = row_y + (row_h - h) / 2;

  cgi.Draw2DFill(x, y, w, h, Cg_ScoresColor(fill, fill_alpha));
  Cg_RaceHud_DrawShadowedString(x + pad_x, y + pad_y, text,
                                Cg_ScoresColor(ink, 1.f));
}

/**
 * @brief Draws all fifteen persistent ranked slots.
 * @details The slots are persistent, so an unfilled one reads as Open slot
 * rather than vanishing: the board's length is the map's ranking capacity, not
 * its current population.
 */
static void Cg_ScoresDrawRows(const cg_scores_layout_t *layout, const int32_t top) {
  const int32_t bleed = Cg_RaceHud_Scale(SCORES_ROW_TINT_BLEED);
  const int32_t edge_w = Cg_RaceHud_Scale(SCORES_ROW_EDGE_W);
  const int32_t ink_pad = Cg_RaceHud_Scale(SCORES_ROW_INK_PAD);
  const int32_t available = layout->board.y + layout->board.h - top;

  int32_t name_h;
  Cg_ScoresBindFitted(RACE_FONT_VALUE, layout->row_h - ink_pad * 2, &name_h);

  // Row height is fixed by design, but a short viewport cannot spend 15 of
  // them; give the rows back their slack before letting them run off the
  // bottom of the board.
  int32_t row_h = layout->row_h;
  if (row_h * (int32_t) RACE_LEADERBOARD_TOP_MAX > available) {
    row_h = Maxi(name_h + Cg_RaceHud_Scale(4.f),
                 available / (int32_t) RACE_LEADERBOARD_TOP_MAX);
  }
  const int32_t empty_h = (int32_t) (row_h * SCORES_EMPTY_ROW_H / SCORES_ROW_H);

  const int32_t local_rank = Cg_ScoresLocalRank();
  const uint32_t best_ms = cg_leaderboard_state.count
                             ? cg_leaderboard_state.entries[0].time_ms : 0u;
  int32_t y = top;

  for (size_t i = 0; i < RACE_LEADERBOARD_TOP_MAX; i++) {
    const cg_leaderboard_entry_t *entry = i < cg_leaderboard_state.count
                                            ? &cg_leaderboard_state.entries[i]
                                            : NULL;
    const bool mine = entry && (int32_t) i == local_rank;
    const bool record = entry && i == 0u;
    const int32_t h = entry ? row_h : empty_h;

    if (entry && (i % 2u)) {
      cgi.Draw2DFill(layout->board.x, y, layout->board.w, h,
                     Color4f(1.f, 1.f, 1.f, .04f));
    }

    // The two reserved row tints bleed past the columns so the text sits
    // inside the fill rather than against its edge, each with a 2px edge on
    // the left. A local record takes the local tint: you already know it is
    // the record from the rank colour.
    if (mine || record) {
      const uint32_t tint = mine ? SCORES_MINE : SCORES_GOLD;
      cgi.Draw2DFill(layout->board.x - bleed, y, layout->board.w + bleed * 2, h,
                     Cg_ScoresColor(tint, mine ? .27f : .10f));
      cgi.Draw2DFill(layout->board.x - bleed, y, edge_w, h,
                     Cg_ScoresColor(mine ? SCORES_MINE_EDGE : SCORES_GOLD,
                                    mine ? 1.f : .7f));
    }

    if (entry) {
      int32_t rank_h, value_h, quiet_h;

      Cg_RaceHud_BindFont(RACE_FONT_ROUTE, &rank_h);
      const uint32_t rank_rgb = i == 0u ? SCORES_GOLD
                              : i == 1u ? SCORES_SILVER
                              : i == 2u ? SCORES_BRONZE : SCORES_RANK;
      Cg_RaceHud_DrawShadowedString(layout->rank_x, y + (h - rank_h) / 2,
                                    va("%02zu", i + 1u),
                                    Cg_ScoresColor(rank_rgb, 1.f));

      const char *tag = mine ? "Your PB" : record ? "Record" : NULL;
      int32_t tag_w = 0;
      if (tag) {
        Cg_RaceHud_BindFont(RACE_FONT_LABEL, NULL);
        tag_w = Cg_RaceHud_Scale(SCORES_TAG_GAP) + cgi.StringWidth(tag) +
                Cg_RaceHud_Scale(SCORES_TAG_PAD_X) * 2;
      }

      Cg_ScoresBindFitted(RACE_FONT_VALUE, layout->row_h - ink_pad * 2, &value_h);
      const int32_t value_y = y + (h - value_h) / 2;
      char name[sizeof(entry->name)];
      Cg_CopyFittedText(name, sizeof(name), entry->name,
                        Maxi(0, layout->name_right - layout->name_x - tag_w));
      Cg_RaceHud_DrawShadowedString(layout->name_x, value_y, name,
                                    Cg_ScoresColor(mine ? SCORES_CYAN : SCORES_INK,
                                                   1.f));
      Cg_RaceHud_DrawRightAligned(layout->time_right, value_y,
                                  Cg_FormatTime((int32_t) entry->time_ms),
                                  Cg_ScoresColor(mine ? SCORES_CYAN
                                                 : record ? SCORES_GOLD
                                                 : SCORES_VALUE, 1.f));

      if (tag) {
        Cg_ScoresDrawTag(layout->name_x + cgi.StringWidth(name) +
                           Cg_RaceHud_Scale(SCORES_TAG_GAP), y, h, tag,
                         mine ? SCORES_MINE : SCORES_GOLD, mine ? .45f : .18f,
                         mine ? SCORES_MINE_INK : SCORES_GOLD);
      }

      Cg_RaceHud_BindFont(RACE_FONT_ROUTE, &quiet_h);
      const int32_t quiet_y = y + (h - quiet_h) / 2;
      const color_t quiet = Cg_ScoresColor(SCORES_MUTED, 1.f);
      Cg_RaceHud_DrawRightAligned(layout->gap_right, quiet_y,
                                  Cg_FormatGap(entry->time_ms, best_ms), quiet);
      Cg_RaceHud_DrawRightAligned(layout->date_right, quiet_y,
                                  Cg_FormatDate(entry->date_unix_s), quiet);
    } else {
      int32_t height;
      Cg_RaceHud_BindFont(RACE_FONT_ROUTE, &height);
      const color_t empty = Cg_ScoresColor(SCORES_EMPTY, 1.f);
      Cg_RaceHud_DrawShadowedString(layout->rank_x, y + (h - height) / 2,
                                    va("%02zu", i + 1u), empty);
      Cg_RaceHud_DrawShadowedString(layout->name_x, y + (h - height) / 2,
                                    "Open slot", empty);
    }

    Cg_ScoresDrawRule(layout->board.x, y + h - 1, layout->board.w,
                      SCORES_ROW_RULE, .34f);
    y += h;
  }
}

/**
 * @return True when `group` shows a count instead of chips at `tier`.
 * @details Detail is shed in order of usefulness: pings first, then the
 * Practice chips, then Race, with spectators the last to collapse because a
 * spectator chip carries who they are watching.
 */
static bool Cg_ScoresGroupCollapsed(const cg_roster_group_t group,
                                    const size_t members, const int32_t tier) {
  switch (group) {
    case CG_ROSTER_PRACTICE_MODE:
      return tier >= 2;
    case CG_ROSTER_SPECTATOR:
      return tier >= 3 && members > SCORES_SPECTATOR_COLLAPSE;
    default:
      return tier >= 3;
  }
}

/**
 * @return True when a chip in `group` still carries its detail line.
 */
static bool Cg_ScoresChipDetailed(const cg_roster_group_t group, const int32_t tier) {
  return group == CG_ROSTER_SPECTATOR ? tier < 3 : tier < 1;
}

/**
 * @brief Formats a chip's detail: a ping for a racer, a target for a
 * spectator.
 * @return The name the target run should draw in the live hue, or NULL when
 * the whole detail is one muted run.
 */
static const char *Cg_ScoresChipDetail(const cg_roster_entry_t *entry,
                                       const cg_roster_entry_t *entries,
                                       const size_t count, char *detail,
                                       const size_t size) {
  if (entry->group != CG_ROSTER_SPECTATOR) {
    if (entry->ping >= 0 && entry->ping < 999) {
      q_snprintf(detail, size, "%d ms", entry->ping);
    } else {
      q_strlcpy(detail, "--", size);
    }
    return NULL;
  }

  const char *target = Cg_RosterTargetName(entries, count, entry->spectator_target);
  if (entry->spectator_target < 0 || !strcmp(target, "No target")) {
    q_strlcpy(detail, entry->spectator_target < 0 ? "free camera" : "no target", size);
    return NULL;
  }

  q_strlcpy(detail, "watching", size);
  return target;
}

/**
 * @return The width of one chip, and the runs it will draw.
 */
static int32_t Cg_ScoresChipWidth(const cg_roster_entry_t *entry,
                                  const char *detail, const char *target) {
  const bool spectator = entry->group == CG_ROSTER_SPECTATOR;
  const int32_t pad_x = Cg_RaceHud_Scale(spectator ? SCORES_CHIP_PILL_PAD_X
                                                   : SCORES_CHIP_PAD_X);
  Cg_ScoresBindChipFont(NULL);
  int32_t w = pad_x * 2 + cgi.StringWidth(entry->name);

  if (detail && *detail) {
    Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
    w += Cg_RaceHud_Scale(SCORES_CHIP_DETAIL_GAP) + cgi.StringWidth(detail);
    if (target) {
      w += Cg_RaceHud_Scale(SCORES_CHIP_TARGET_GAP) + cgi.StringWidth(target);
    }
  }

  return w;
}

/**
 * @brief Draws one roster chip.
 */
static void Cg_ScoresDrawChip(const int32_t x, const int32_t y,
                              const int32_t chip_h,
                              const cg_roster_entry_t *entry, const bool local,
                              const char *detail, const char *target) {
  const bool spectator = entry->group == CG_ROSTER_SPECTATOR;
  const int32_t pad_x = Cg_RaceHud_Scale(spectator ? SCORES_CHIP_PILL_PAD_X
                                                   : SCORES_CHIP_PAD_X);
  const int32_t pad_y = Cg_RaceHud_Scale(SCORES_CHIP_PAD_Y);
  const int32_t w = Cg_ScoresChipWidth(entry, detail, target);

  // Flat fills only, so the design's 3px chip and 999px spectator pill both
  // land as rectangles; the pill keeps its wider horizontal padding, which is
  // what actually distinguished the two shapes at chip size.
  if (local) {
    cgi.Draw2DFill(x, y, w, chip_h, Cg_ScoresColor(SCORES_MINE, .3f));
    const color_t edge = Cg_ScoresColor(SCORES_MINE_EDGE, 1.f);
    cgi.Draw2DFill(x, y, w, 1, edge);
    cgi.Draw2DFill(x, y + chip_h - 1, w, 1, edge);
    cgi.Draw2DFill(x, y, 1, chip_h, edge);
    cgi.Draw2DFill(x + w - 1, y, 1, chip_h, edge);
  } else {
    cgi.Draw2DFill(x, y, w, chip_h,
                   Color4f(1.f, 1.f, 1.f, spectator ? .06f : .05f));
  }

  int32_t name_h, detail_h;
  Cg_ScoresBindChipFont(&name_h);
  const int32_t name_y = y + pad_y;
  Cg_RaceHud_DrawShadowedString(x + pad_x, name_y, entry->name,
                                Cg_ScoresColor(local ? SCORES_CYAN : SCORES_INK,
                                               1.f));
  int32_t text_x = x + pad_x + cgi.StringWidth(entry->name);

  if (detail && *detail) {
    Cg_RaceHud_BindFont(RACE_FONT_BODY, &detail_h);
    const int32_t detail_y = name_y + (name_h - detail_h);
    text_x += Cg_RaceHud_Scale(SCORES_CHIP_DETAIL_GAP);
    Cg_RaceHud_DrawShadowedString(text_x, detail_y, detail,
                                  Cg_ScoresColor(SCORES_MUTED, 1.f));
    if (target) {
      text_x += cgi.StringWidth(detail) + Cg_RaceHud_Scale(SCORES_CHIP_TARGET_GAP);
      Cg_RaceHud_DrawShadowedString(text_x, detail_y, target,
                                    Cg_ScoresColor(SCORES_CYAN, 1.f));
    }
  }
}

/**
 * @brief Lays out one group heading -- `Race mode - 3` -- and returns its width.
 * @details Measuring and drawing walk the same runs, so the band's centring can
 * never drift from what it paints. The separator is what stops the count from
 * reading as part of the label: `Race mode 1` parses as a noun phrase, `Race
 * mode - 3` as a heading and its tally. It is the ASCII stand-in the standing
 * line already uses -- the 2D atlas has no U+00B7.
 */
static int32_t Cg_ScoresGroupHeading(const int32_t x, const int32_t y,
                                     const cg_roster_group_t group,
                                     const size_t members, const bool draw) {
  const cg_scores_run_t runs[] = {
    { Cg_RosterGroupName(group), 0u, false },
    { "-", SCORES_SEPARATOR, true },
    { va("%zu", members), SCORES_MUTED, true }
  };

  Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);

  const int32_t gap = Cg_RaceHud_Scale(SCORES_GROUP_COUNT_GAP);
  int32_t cursor = x;
  for (size_t i = 0; i < lengthof(runs); i++) {
    if (runs[i].lead) {
      cursor += gap;
    }
    if (draw) {
      const color_t color = i ? Cg_ScoresColor(runs[i].rgb, 1.f)
                              : Cg_RosterGroupColor(group);
      Cg_RaceHud_DrawShadowedString(cursor, y, runs[i].text, color);
    }
    cursor += cgi.StringWidth(runs[i].text);
  }
  return cursor - x;
}

/**
 * @return The width one group occupies at `tier`, label included.
 */
static int32_t Cg_ScoresGroupWidth(const cg_roster_entry_t *entries,
                                   const size_t count,
                                   const cg_roster_group_t group,
                                   const size_t members, const int32_t tier,
                                   const int32_t local_index) {
  int32_t w = Cg_ScoresGroupHeading(0, 0, group, members, false);

  const bool collapsed = Cg_ScoresGroupCollapsed(group, members, tier);
  const bool detailed = Cg_ScoresChipDetailed(group, tier);
  size_t chips = 0;

  for (size_t i = 0; i < count; i++) {
    if (entries[i].group != group) {
      continue;
    }
    const bool local = (int32_t) i == local_index;
    if (collapsed && !local) {
      continue;
    }

    char detail[64];
    const char *target = detailed
      ? Cg_ScoresChipDetail(&entries[i], entries, count, detail, sizeof(detail))
      : NULL;
    w += (chips++ ? Cg_RaceHud_Scale(SCORES_CHIP_GAP)
                  : Cg_RaceHud_Scale(SCORES_GROUP_LABEL_GAP)) +
         Cg_ScoresChipWidth(&entries[i], detailed ? detail : NULL, target);
  }

  return w;
}

/**
 * @brief Draws one roster band: the groups named in `groups`, centred, with
 * the band's hairline on its inner edge.
 * @details The count thresholds of the spec are only where the tier starts.
 * The honest rule is measurement, so the band escalates its own tier until the
 * groups fit -- which is also what keeps 720p legible, where far fewer chips
 * fit than the counts assume.
 */
static void Cg_ScoresDrawBand(const SDL_Rect *band, const cg_roster_group_t *groups,
                              const size_t num_groups,
                              const cg_roster_entry_t *entries, const size_t count,
                              const cg_roster_summary_t *summary,
                              const int32_t start_tier, const bool rule_on_top) {
  // A collapsed band draws nothing at all -- its rule included. A hairline
  // with no chips under it is the dead space it was supposed to remove.
  if (band->h <= 0) {
    return;
  }

  Cg_ScoresDrawRule(band->x, rule_on_top ? band->y : band->y + band->h - 1,
                    band->w, SCORES_RULE, .45f);

  const int32_t chip_h = Cg_ScoresChipHeight();
  const int32_t group_gap = Cg_RaceHud_Scale(SCORES_BAND_GROUP_GAP);
  const int32_t available = band->w - Cg_RaceHud_Scale(SCORES_BAND_PAD_X) * 2;

  int32_t tier = start_tier;
  int32_t total = 0;
  for (;;) {
    total = 0;
    size_t drawn = 0;
    for (size_t i = 0; i < num_groups; i++) {
      const size_t members = summary->counts[groups[i]];
      if (!members) {
        continue;
      }
      total += (drawn++ ? group_gap : 0) +
               Cg_ScoresGroupWidth(entries, count, groups[i], members, tier,
                                   summary->local_index);
    }
    if (total <= available || tier >= 3) {
      break;
    }
    tier++;
  }

  const int32_t chip_y = band->y + (band->h - chip_h) / 2;
  int32_t x = Maxi(band->x + Cg_RaceHud_Scale(SCORES_BAND_PAD_X),
                   band->x + (band->w - total) / 2);
  const int32_t clip = band->x + band->w - Cg_RaceHud_Scale(SCORES_BAND_PAD_X);

  int32_t label_h, name_h;
  Cg_ScoresBindChipFont(&name_h);
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &label_h);
  const int32_t label_y = chip_y + Cg_RaceHud_Scale(SCORES_CHIP_PAD_Y) +
                          (name_h - label_h);

  size_t drawn_groups = 0;
  for (size_t i = 0; i < num_groups; i++) {
    const cg_roster_group_t group = groups[i];
    const size_t members = summary->counts[group];
    if (!members) {
      continue;
    }
    if (drawn_groups++) {
      x += group_gap;
    }

    x += Cg_ScoresGroupHeading(x, label_y, group, members, true);

    const bool collapsed = Cg_ScoresGroupCollapsed(group, members, tier);
    const bool detailed = Cg_ScoresChipDetailed(group, tier);

    // Choose the chips before drawing any of them. A group that runs out of
    // room still has to keep the local player -- theirs is the one chip a
    // player looks for -- so the last chip that fits gives up its slot rather
    // than the band silently dropping them.
    size_t selected[MAX_CLIENTS];
    size_t selected_count = 0;
    size_t local_entry = (size_t) -1;
    bool local_selected = false;
    int32_t cursor = x;

    for (size_t j = 0; j < count; j++) {
      if (entries[j].group != group) {
        continue;
      }
      const bool local = (int32_t) j == summary->local_index;
      if (local) {
        local_entry = j;
      }
      if (collapsed && !local) {
        continue;
      }

      char detail[64];
      const char *target = detailed
        ? Cg_ScoresChipDetail(&entries[j], entries, count, detail, sizeof(detail))
        : NULL;
      const int32_t w = Cg_ScoresChipWidth(&entries[j], detailed ? detail : NULL,
                                           target);
      const int32_t lead = selected_count ? Cg_RaceHud_Scale(SCORES_CHIP_GAP)
                                          : Cg_RaceHud_Scale(SCORES_GROUP_LABEL_GAP);
      if (cursor + lead + w > clip) {
        break;
      }

      cursor += lead + w;
      selected[selected_count++] = j;
      local_selected |= local;
    }

    if (!local_selected && local_entry != (size_t) -1 && selected_count) {
      selected[selected_count - 1u] = local_entry;
    }

    for (size_t j = 0; j < selected_count; j++) {
      const size_t index = selected[j];
      const bool local = (int32_t) index == summary->local_index;

      char detail[64];
      const char *target = detailed
        ? Cg_ScoresChipDetail(&entries[index], entries, count, detail, sizeof(detail))
        : NULL;

      x += j ? Cg_RaceHud_Scale(SCORES_CHIP_GAP)
             : Cg_RaceHud_Scale(SCORES_GROUP_LABEL_GAP);
      Cg_ScoresDrawChip(x, chip_y, chip_h, &entries[index], local,
                        detailed ? detail : NULL, target);
      x += Cg_ScoresChipWidth(&entries[index], detailed ? detail : NULL, target);
    }
  }
}

/**
 * @brief Returns true when the held-score overlay should render.
 */
static bool Cg_RaceScoreOverlayEnabled(void) {
  return true;
}

/**
 * @brief Draws the authoritative Race Top-15 between two live roster bands.
 */
void Cg_DrawScores(const player_state_t *ps) {
  if (!Cg_RaceScoreOverlayEnabled()) {
    return;
  }

  if (!Cg_ScoreOverlayVisible(cgi.GetKeyDest() == KEY_UI,
                              ps && ps->stats[STAT_SCORES])) {
    return;
  }

  Cg_UpdateLeaderboard();

  cg_roster_entry_t roster[MAX_CLIENTS];
  const size_t roster_count = Cg_RosterSnapshot(roster, lengthof(roster));
  const cg_roster_summary_t summary = Cg_RosterSummary(roster, roster_count);
  const cg_scores_layout_t layout = Cg_ScoresLayout(&summary);

  // No panels, no borders: the board sits on the menu's scrim and nothing
  // else, and every string carries its own legibility.
  cgi.Draw2DFill(0, 0, cgi.context->w, cgi.context->h,
                 Cg_ScoresColor(SCORES_SCRIM, .9f));

  int32_t y = Cg_ScoresDrawBoardHeader(&layout);
  y = Cg_ScoresDrawStanding(&layout, y);
  y = Cg_ScoresDrawCaptions(&layout, y);
  Cg_ScoresDrawRows(&layout, y);

  static const cg_roster_group_t spectators[] = { CG_ROSTER_SPECTATOR };
  static const cg_roster_group_t players[] = {
    CG_ROSTER_RACE_MODE, CG_ROSTER_PRACTICE_MODE
  };
  Cg_ScoresDrawBand(&layout.spectators, spectators, lengthof(spectators),
                    roster, roster_count, &summary, layout.tier, false);
  Cg_ScoresDrawBand(&layout.players, players, lengthof(players),
                    roster, roster_count, &summary, layout.tier, true);

  cgi.BindFont(NULL, NULL, NULL);
}
