/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "g_local.h"

#include <stdlib.h>
#include <string.h>

#include "race_map_browser_service.h"
#include "race_map_browser_wire.h"
#include "race_map_catalog.h"
#include "race_map_state_service.h"

static cvar_t *race_map_browser_list;

/**
 * @brief One catalog entry that survived the filter, with its records already
 * resolved when the scope needed them to decide.
 */
typedef struct {
  size_t entry;
  bool resolved;
  int32_t best_ms;
  int32_t pb_ms;
} race_map_browser_match_t;

bool Race_MapBrowserService_LoadCatalog(race_map_catalog_t *catalog) {
  if (!catalog || !race_map_browser_list || !*race_map_browser_list->string) {
    return false;
  }

  void *contents;
  if (gi.LoadFile(race_map_browser_list->string, &contents) <= 0) {
    return false;
  }
  const bool parsed = Race_MapCatalog_Parse(contents, catalog);
  gi.FreeFile(contents);
  return parsed;
}

static void Race_MapBrowserService_CopyField(char *output,
                                             const size_t output_size,
                                             const char *input) {
  if (!output || !output_size) {
    return;
  }
  size_t length = 0;
  while (input && *input && length + 1u < output_size) {
    char c = *input++;
    if (c == '\\' || c == '|' || c == '\r' || c == '\n') {
      c = ' ';
    }
    output[length++] = c;
  }
  output[length] = '\0';
}

static bool Race_MapBrowserService_Contains(const char *text,
                                            const char *query) {
  if (!query || !*query) {
    return true;
  }
  if (!text) {
    return false;
  }
  const size_t query_length = q_strlen(query);
  for (const char *cursor = text; *cursor; cursor++) {
    if (!q_strncasecmp(cursor, query, query_length)) {
      return true;
    }
  }
  return false;
}

static bool Race_MapBrowserService_MatchesPrefix(
  const race_map_catalog_entry_t *entry, const char *prefix) {
  return Race_MapBrowserService_Contains(entry->name, prefix) ||
         Race_MapBrowserService_Contains(entry->title, prefix) ||
         Race_MapBrowserService_Contains(entry->author, prefix);
}

/**
 * @brief The route offers two scopes, not six categories: this mod carries no
 * official/custom sources and no difficulty tiers, so the only meaningful cut
 * is between every map and the ones the caller has already run.
 */
static void Race_MapBrowserService_Scope(char output[16], const char *input) {
  static const char *scopes[] = { "all", "pb" };
  if (input) {
    for (size_t i = 0; i < lengthof(scopes); i++) {
      if (!q_strcmp(input, scopes[i])) {
        q_strlcpy(output, input, 16);
        return;
      }
    }
  }
  q_strlcpy(output, "all", 16);
}

static int32_t Race_MapBrowserService_PageArgument(void) {
  if (gi.Argc() < 2) {
    return 1;
  }
  char *end;
  const long page = strtol(gi.Argv(1), &end, 10);
  return end && !*end && page > 0 && page <= INT32_MAX
    ? (int32_t) page
    : 1;
}

static const char *Race_MapBrowserService_Uid(const g_client_t *cl) {
  return cl && cl->persistent.race_profile.ready
    ? cl->persistent.race_profile.uid
    : NULL;
}

static int32_t Race_MapBrowserService_Clamp(const uint32_t time) {
  return (int32_t) min(time, (uint32_t) INT32_MAX);
}

/**
 * @brief Resolves one map's world record and the caller's own best from a
 * single map-state load.
 */
static void Race_MapBrowserService_Record(const char *map, const char *uid,
                                          int32_t *best_ms, int32_t *pb_ms) {
  *best_ms = 0;
  *pb_ms = 0;
  race_map_state_summary_t summary;
  if (Race_MapStateService_LoadSummary(map, uid, &summary) && summary.count) {
    *best_ms = Race_MapBrowserService_Clamp(summary.records[0].elapsed_time);
    *pb_ms = Race_MapBrowserService_Clamp(summary.personal_best);
  }
}

static bool Race_MapBrowserService_BuildPage(
  const race_map_catalog_t *catalog, const char *uid,
  const int32_t requested_page, const char *prefix, const char *scope,
  race_map_browser_page_t *page) {
  if (!catalog || !page) {
    return false;
  }

  memset(page, 0, sizeof(*page));
  Race_MapBrowserService_CopyField(page->prefix, sizeof(page->prefix), prefix);
  Race_MapBrowserService_Scope(page->scope, scope);

  /* The "pb" scope cannot know whether an entry matches without reading its
     records, so the single filtering pass keeps what it read rather than
     loading the same map-state twice. */
  const bool scope_needs_records = !q_strcmp(page->scope, "pb");
  race_map_browser_match_t matches[RACE_MAP_CATALOG_MAX_ENTRIES];
  size_t num_matches = 0;

  for (size_t i = 0; i < catalog->count &&
       num_matches < lengthof(matches); i++) {
    const race_map_catalog_entry_t *entry = catalog->entries + i;
    if (!Race_MapBrowserService_MatchesPrefix(entry, page->prefix)) {
      continue;
    }

    race_map_browser_match_t match = { .entry = i };
    if (scope_needs_records) {
      Race_MapBrowserService_Record(entry->name, uid, &match.best_ms,
                                    &match.pb_ms);
      match.resolved = true;
      if (!match.pb_ms) {
        continue;
      }
    }
    matches[num_matches++] = match;
  }

  page->total = (int32_t) num_matches;
  page->pages = num_matches
    ? (int32_t) ((num_matches + RACE_MAP_BROWSER_MAX_ROWS - 1u) /
                 RACE_MAP_BROWSER_MAX_ROWS)
    : 1;
  page->page = min(max(requested_page, 1), page->pages);

  const size_t first = (size_t) (page->page - 1) * RACE_MAP_BROWSER_MAX_ROWS;
  const size_t last = min(first + RACE_MAP_BROWSER_MAX_ROWS, num_matches);
  for (size_t i = first; i < last; i++) {
    const race_map_browser_match_t *match = matches + i;
    const race_map_catalog_entry_t *entry = catalog->entries + match->entry;
    race_map_browser_row_t *row = page->rows + page->num_rows++;
    Race_MapBrowserService_CopyField(row->name, sizeof(row->name), entry->name);
    Race_MapBrowserService_CopyField(row->title, sizeof(row->title),
                                     *entry->title ? entry->title : "-");
    Race_MapBrowserService_CopyField(row->author, sizeof(row->author),
                                     *entry->author ? entry->author
                                                    : "Unknown");
    if (match->resolved) {
      row->best_ms = match->best_ms;
      row->pb_ms = match->pb_ms;
    } else {
      Race_MapBrowserService_Record(entry->name, uid, &row->best_ms,
                                    &row->pb_ms);
    }
  }
  return true;
}

static void Race_MapBrowserService_SendPage(g_client_t *cl) {
  race_map_catalog_t catalog;
  if (!Race_MapBrowserService_LoadCatalog(&catalog)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race map browser unavailable: could not parse %s\n",
                   race_map_browser_list ? race_map_browser_list->string
                                         : "sv_map_list");
    return;
  }

  race_map_browser_page_t page;
  if (!Race_MapBrowserService_BuildPage(
        &catalog, Race_MapBrowserService_Uid(cl),
        Race_MapBrowserService_PageArgument(),
        gi.Argc() >= 3 ? gi.Argv(2) : "",
        gi.Argc() >= 4 ? gi.Argv(3) : "all", &page)) {
    return;
  }

  char payload[MAX_STRING_CHARS];
  if (!Race_MapBrowserWire_EncodePage(&page, payload, sizeof(payload))) {
    G_Warn("Could not encode Race map browser page\n");
    return;
  }
  gi.WriteByte(SV_CMD_RACE_MAP_BROWSER);
  gi.WriteString(payload);
  gi.Unicast(cl, true);
}

static void Race_MapBrowserService_SendDetail(g_client_t *cl) {
  if (gi.Argc() != 2) {
    return;
  }
  char canonical[RACE_MAP_IDENTITY_SIZE];
  if (!Race_MapState_CanonicalizeMap(gi.Argv(1), canonical)) {
    return;
  }

  race_map_catalog_t catalog;
  if (!Race_MapBrowserService_LoadCatalog(&catalog)) {
    return;
  }
  const race_map_catalog_entry_t *entry = Race_MapCatalog_Find(&catalog,
                                                                canonical);
  if (!entry) {
    return;
  }

  race_map_browser_detail_t detail;
  memset(&detail, 0, sizeof(detail));
  detail.valid = true;
  Race_MapBrowserService_CopyField(detail.name, sizeof(detail.name),
                                   entry->name);
  Race_MapBrowserService_CopyField(detail.title, sizeof(detail.title),
                                   entry->title);
  Race_MapBrowserService_CopyField(detail.author, sizeof(detail.author),
                                   *entry->author ? entry->author : "Unknown");

  race_map_state_summary_t summary;
  if (Race_MapStateService_LoadSummary(entry->name,
                                       Race_MapBrowserService_Uid(cl),
                                       &summary) && summary.count) {
    const race_leaderboard_record_t *best = summary.records;
    detail.best_ms = Race_MapBrowserService_Clamp(best->elapsed_time);
    detail.pb_ms = Race_MapBrowserService_Clamp(summary.personal_best);
    detail.record_date_unix_s =
      min(best->date_unix_s, RACE_MAP_BROWSER_MAX_DATE_UNIX_S);
    Race_MapBrowserService_CopyField(detail.record_holder,
                                     sizeof(detail.record_holder),
                                     best->display_name);

    const size_t carried = min(summary.count,
                               (size_t) RACE_MAP_BROWSER_MAX_TIMES);
    for (size_t i = 0; i < carried; i++) {
      race_map_browser_time_t *time = detail.times + detail.num_times++;
      Race_MapBrowserService_CopyField(time->player, sizeof(time->player),
                                       summary.records[i].display_name);
      time->time_ms =
        Race_MapBrowserService_Clamp(summary.records[i].elapsed_time);
    }
    /* Only a rank the client can actually see is worth tinting a row for. */
    if (summary.personal_rank &&
        summary.personal_rank <= (size_t) detail.num_times) {
      detail.local_rank = (int32_t) summary.personal_rank;
    }
    detail.ranked_runs = (int32_t) min(summary.total, (size_t) INT32_MAX);
  }

  char payload[MAX_STRING_CHARS];
  if (!Race_MapBrowserWire_EncodeDetail(&detail, payload, sizeof(payload))) {
    G_Warn("Could not encode Race map browser detail\n");
    return;
  }
  gi.WriteByte(SV_CMD_RACE_MAP_BROWSER_DETAIL);
  gi.WriteString(payload);
  gi.Unicast(cl, true);
}

static void Race_MapBrowserService_PrintTimes(g_client_t *cl) {
  const char *name = gi.Argc() >= 2 ? gi.Argv(1) : g_level.name;
  char canonical[RACE_MAP_IDENTITY_SIZE];
  if (!Race_MapState_CanonicalizeMap(name, canonical)) {
    gi.ClientPrint(cl, PRINT_HIGH, "Usage: maptimes [map]\n");
    return;
  }

  race_map_state_summary_t summary;
  if (!Race_MapStateService_LoadSummary(canonical, NULL, &summary)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race times for %s are unavailable.\n", canonical);
    return;
  }
  gi.ClientPrint(cl, PRINT_HIGH, "Race times for ^2%s^7:\n", canonical);
  if (!summary.count) {
    gi.ClientPrint(cl, PRINT_HIGH, "  No ranked completions.\n");
    return;
  }
  for (size_t i = 0; i < summary.count; i++) {
    const race_leaderboard_record_t *record = summary.records + i;
    gi.ClientPrint(cl, PRINT_HIGH, "%2zu. %-24s %u:%02u.%03u\n",
                   i + 1u, record->display_name,
                   record->elapsed_time / 60000u,
                   record->elapsed_time / 1000u % 60u,
                   record->elapsed_time % 1000u);
  }
}

void Race_MapBrowserService_Init(void) {
  race_map_browser_list = gi.AddCvar(
    "sv_map_list", "maps.lst", 0,
    "The server-owned map rotation file mirrored by the Race browser.");
}

bool Race_MapBrowserService_ClientCommand(g_client_t *cl, const char *cmd) {
  if (!cl || !cl->in_use || !cmd) {
    return false;
  }
  if (!q_strcmp(cmd, "maps_ui")) {
    Race_MapBrowserService_SendPage(cl);
    return true;
  }
  if (!q_strcmp(cmd, "mapinfo_ui")) {
    Race_MapBrowserService_SendDetail(cl);
    return true;
  }
  if (!q_strcmp(cmd, "maptimes")) {
    Race_MapBrowserService_PrintTimes(cl);
    return true;
  }
  return false;
}
