/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/shared.h"

#define RACE_MAP_BROWSER_WIRE_VERSION "3"

/**
 * @brief One page fills the list pane exactly, so the pane never scrolls
 * internally and the pager below it does the paging.
 */
#define RACE_MAP_BROWSER_MAX_ROWS 10

/**
 * @brief The Times tab shows a top-four table beneath the two record chips.
 */
#define RACE_MAP_BROWSER_MAX_TIMES 4

/**
 * @brief Mirrors RACE_LEADERBOARD_MAX_DATE_UNIX_S. Restated rather than
 * included because this header is compiled into the client too, where the
 * leaderboard and profile types are not in scope.
 */
#define RACE_MAP_BROWSER_MAX_DATE_UNIX_S UINT64_C(253402300799)

typedef struct {
  char name[32];
  char title[64];
  char author[48];
  /**
   * @brief The map's world record in milliseconds, or zero when nobody has
   * run it. A row without a world record cannot have a personal best either,
   * so the client renders both as an em dash.
   */
  int32_t best_ms;
  /**
   * @brief The requesting client's own best in milliseconds, or zero.
   */
  int32_t pb_ms;
} race_map_browser_row_t;

typedef struct {
  int32_t page;
  int32_t pages;
  /**
   * @brief Maps matching the filter and scope across every page. The route
   * states the count three times - header stat, pane meta, list footer - and
   * all three read this one field so they cannot disagree.
   */
  int32_t total;
  char prefix[32];
  /**
   * @brief "all" or "pb". Replaces the source and difficulty categories,
   * which filtered on data this mod does not carry.
   */
  char scope[16];
  int32_t num_rows;
  race_map_browser_row_t rows[RACE_MAP_BROWSER_MAX_ROWS];
} race_map_browser_page_t;

typedef struct {
  char player[64];
  int32_t time_ms;
} race_map_browser_time_t;

typedef struct {
  bool valid;
  char name[32];
  char title[64];
  char author[96];
  char record_holder[64];
  int32_t best_ms;
  int32_t pb_ms;
  /**
   * @brief Ranked completions across the whole map state, not just the rows
   * carried in `times`.
   */
  int32_t ranked_runs;
  /**
   * @brief UTC completion time of the world record in Unix seconds, or zero
   * for records imported from map-state formats that predate dated records.
   */
  uint64_t record_date_unix_s;
  /**
   * @brief One-based index into `times` of the requesting client's own row,
   * or zero when they do not hold one of the rows carried.
   */
  int32_t local_rank;
  int32_t num_times;
  race_map_browser_time_t times[RACE_MAP_BROWSER_MAX_TIMES];
} race_map_browser_detail_t;

bool Race_MapBrowserWire_EncodePage(const race_map_browser_page_t *page,
                                    char *output, size_t output_size);
bool Race_MapBrowserWire_DecodePage(const char *payload,
                                    race_map_browser_page_t *page);
bool Race_MapBrowserWire_EncodeDetail(const race_map_browser_detail_t *detail,
                                      char *output, size_t output_size);
bool Race_MapBrowserWire_DecodeDetail(const char *payload,
                                      race_map_browser_detail_t *detail);
