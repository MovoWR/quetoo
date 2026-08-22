/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <check.h>
#include <string.h>

#include "race_map_browser_wire.h"
#include "race_map_catalog.h"

START_TEST(_Race_MapBrowserPageWireV3) {
  race_map_browser_page_t page = {
    .page = 2,
    .pages = 4,
    .total = 14,
    .num_rows = 2
  };
  q_strlcpy(page.prefix, "jump", sizeof(page.prefix));
  q_strlcpy(page.scope, "pb", sizeof(page.scope));
  q_strlcpy(page.rows[0].name, "jump-one", sizeof(page.rows[0].name));
  q_strlcpy(page.rows[0].title, "Jump One", sizeof(page.rows[0].title));
  q_strlcpy(page.rows[0].author, "Alice", sizeof(page.rows[0].author));
  page.rows[0].best_ms = 61234;
  page.rows[0].pb_ms = 63001;
  q_strlcpy(page.rows[1].name, "jump-two", sizeof(page.rows[1].name));
  q_strlcpy(page.rows[1].title, "Jump Two", sizeof(page.rows[1].title));
  q_strlcpy(page.rows[1].author, "Bob", sizeof(page.rows[1].author));
  page.rows[1].best_ms = 0;
  page.rows[1].pb_ms = 0;

  char encoded[MAX_STRING_CHARS];
  ck_assert(Race_MapBrowserWire_EncodePage(&page, encoded, sizeof(encoded)));
  ck_assert_str_eq(encoded,
    "3\\2\\4\\14\\jump\\pb\\2"
    "\\jump-one\\Jump One\\Alice\\61234\\63001"
    "\\jump-two\\Jump Two\\Bob\\0\\0");

  race_map_browser_page_t decoded;
  memset(&decoded, 0xa5, sizeof(decoded));
  ck_assert(Race_MapBrowserWire_DecodePage(encoded, &decoded));
  ck_assert_int_eq(decoded.page, 2);
  ck_assert_int_eq(decoded.pages, 4);
  ck_assert_int_eq(decoded.total, 14);
  ck_assert_int_eq(decoded.num_rows, 2);
  ck_assert_str_eq(decoded.prefix, "jump");
  ck_assert_str_eq(decoded.scope, "pb");
  ck_assert_str_eq(decoded.rows[0].name, "jump-one");
  ck_assert_int_eq(decoded.rows[0].best_ms, 61234);
  ck_assert_int_eq(decoded.rows[0].pb_ms, 63001);
  ck_assert_int_eq(decoded.rows[1].pb_ms, 0);
} END_TEST

/**
 * @brief A full page has to fit the one string the server may send, or the
 * route silently shows nothing at all.
 */
START_TEST(_Race_MapBrowserFullPageFitsOneMessage) {
  race_map_browser_page_t page = {
    .page = 1,
    .pages = 1,
    .total = RACE_MAP_BROWSER_MAX_ROWS,
    .num_rows = RACE_MAP_BROWSER_MAX_ROWS
  };
  q_strlcpy(page.scope, "all", sizeof(page.scope));
  for (int32_t i = 0; i < page.num_rows; i++) {
    race_map_browser_row_t *row = page.rows + i;
    q_snprintf(row->name, sizeof(row->name), "race-map-name-%d", i);
    q_strlcpy(row->title, "A reasonably long map title", sizeof(row->title));
    q_strlcpy(row->author, "Some Mapper Name", sizeof(row->author));
    row->best_ms = 61234 + i;
    row->pb_ms = 71234 + i;
  }

  char encoded[MAX_STRING_CHARS];
  ck_assert(Race_MapBrowserWire_EncodePage(&page, encoded, sizeof(encoded)));

  race_map_browser_page_t decoded;
  memset(&decoded, 0xa5, sizeof(decoded));
  ck_assert(Race_MapBrowserWire_DecodePage(encoded, &decoded));
  ck_assert_int_eq(decoded.num_rows, RACE_MAP_BROWSER_MAX_ROWS);
  ck_assert_str_eq(decoded.rows[RACE_MAP_BROWSER_MAX_ROWS - 1].author,
                   "Some Mapper Name");
} END_TEST

START_TEST(_Race_MapBrowserDetailWireV3) {
  race_map_browser_detail_t detail = {
    .valid = true,
    .best_ms = 98765,
    .pb_ms = 102300,
    .ranked_runs = 15,
    .record_date_unix_s = UINT64_C(1773532800),
    .local_rank = 2,
    .num_times = 2
  };
  q_strlcpy(detail.name, "race-one", sizeof(detail.name));
  q_strlcpy(detail.title, "Race One", sizeof(detail.title));
  q_strlcpy(detail.author, "Mapper", sizeof(detail.author));
  q_strlcpy(detail.record_holder, "Runner", sizeof(detail.record_holder));
  q_strlcpy(detail.times[0].player, "Runner",
            sizeof(detail.times[0].player));
  detail.times[0].time_ms = 98765;
  q_strlcpy(detail.times[1].player, "Local", sizeof(detail.times[1].player));
  detail.times[1].time_ms = 102300;

  char encoded[MAX_STRING_CHARS];
  ck_assert(Race_MapBrowserWire_EncodeDetail(&detail, encoded,
                                              sizeof(encoded)));
  ck_assert_str_eq(encoded,
    "3\\race-one\\Race One\\Mapper\\Runner\\98765\\102300\\15"
    "\\1773532800\\2\\2\\Runner\\98765\\Local\\102300");

  race_map_browser_detail_t decoded;
  memset(&decoded, 0xa5, sizeof(decoded));
  ck_assert(Race_MapBrowserWire_DecodeDetail(encoded, &decoded));
  ck_assert(decoded.valid);
  ck_assert_str_eq(decoded.name, "race-one");
  ck_assert_str_eq(decoded.record_holder, "Runner");
  ck_assert_int_eq(decoded.best_ms, 98765);
  ck_assert_int_eq(decoded.pb_ms, 102300);
  ck_assert_int_eq(decoded.ranked_runs, 15);
  ck_assert_uint_eq(decoded.record_date_unix_s, UINT64_C(1773532800));
  ck_assert_int_eq(decoded.local_rank, 2);
  ck_assert_int_eq(decoded.num_times, 2);
  ck_assert_str_eq(decoded.times[1].player, "Local");
  ck_assert_int_eq(decoded.times[1].time_ms, 102300);
} END_TEST

START_TEST(_Race_MapBrowserWireRejectsMalformedInput) {
  race_map_browser_page_t page;
  memset(&page, 0, sizeof(page));
  ck_assert(!Race_MapBrowserWire_DecodePage(NULL, &page));
  /* The superseded version is refused rather than read as v3. */
  ck_assert(!Race_MapBrowserWire_DecodePage("2\\1\\1\\\\all\\0", &page));
  /* More rows than one page may carry. */
  ck_assert(!Race_MapBrowserWire_DecodePage("3\\1\\1\\11\\\\all\\11", &page));
  /* A total that cannot account for the rows it ships with. */
  ck_assert(!Race_MapBrowserWire_DecodePage(
    "3\\1\\1\\0\\\\all\\1\\map\\title\\author\\0\\0", &page));
  ck_assert(!Race_MapBrowserWire_DecodePage(
    "3\\1\\1\\0\\bad\nfilter\\all\\0", &page));
  /* A negative personal best. */
  ck_assert(!Race_MapBrowserWire_DecodePage(
    "3\\1\\1\\1\\\\all\\1\\map\\title\\author\\1000\\-1", &page));

  race_map_browser_detail_t detail;
  memset(&detail, 0, sizeof(detail));
  /* A rank pointing past the rows carried. */
  ck_assert(!Race_MapBrowserWire_DecodeDetail(
    "3\\map\\title\\author\\holder\\1000\\0\\1\\0\\2\\1\\holder\\1000",
    &detail));
  /* Fewer ranked runs than rows. */
  ck_assert(!Race_MapBrowserWire_DecodeDetail(
    "3\\map\\title\\author\\holder\\1000\\0\\0\\0\\1\\1\\holder\\1000",
    &detail));
  /* A signed date. */
  ck_assert(!Race_MapBrowserWire_DecodeDetail(
    "3\\map\\title\\author\\holder\\1000\\0\\1\\+1\\0\\0", &detail));
  /* A truncated header. */
  ck_assert(!Race_MapBrowserWire_DecodeDetail(
    "3\\map\\title\\author\\holder\\1000\\0\\1\\0\\0", &detail));

  char tiny[8] = "stable";
  page.page = page.pages = 1;
  q_strlcpy(page.scope, "all", sizeof(page.scope));
  ck_assert(!Race_MapBrowserWire_EncodePage(&page, tiny, sizeof(tiny)));
} END_TEST

START_TEST(_Race_MapCatalogMetadataAndDefaults) {
  static const char contents[] =
    "# Race map metadata\n"
    "{ name race-one message \"Race One\" author Mapper "
    "description \"Technical course\" tags \"source:official type:bonus\" "
    "difficulty 8 time_limit 12.5 gravity 800 }\n"
    "{ name race-two }\n"
    "{ name ../unsafe difficulty 2 }\n";

  race_map_catalog_t catalog;
  memset(&catalog, 0xa5, sizeof(catalog));
  ck_assert(Race_MapCatalog_Parse(contents, &catalog));
  ck_assert_uint_eq(catalog.count, 2);

  const race_map_catalog_entry_t *first =
    Race_MapCatalog_Find(&catalog, "race-one");
  ck_assert_ptr_nonnull(first);
  ck_assert_str_eq(first->title, "Race One");
  ck_assert_str_eq(first->author, "Mapper");
  ck_assert_str_eq(first->description, "Technical course");
  ck_assert_str_eq(first->tags, "source:official type:bonus");
  ck_assert_int_eq(first->difficulty, 8);
  ck_assert_float_eq(first->time_limit, 12.5f);

  const race_map_catalog_entry_t *second =
    Race_MapCatalog_Find(&catalog, "race-two");
  ck_assert_ptr_nonnull(second);
  ck_assert_int_eq(second->difficulty, 0);
  ck_assert_float_eq(second->time_limit, -1.f);
  ck_assert_ptr_null(Race_MapCatalog_Find(&catalog, "../unsafe"));
} END_TEST

START_TEST(_Race_MapCatalogRejectsMalformedInput) {
  race_map_catalog_t catalog;
  memset(&catalog, 0, sizeof(catalog));
  ck_assert(!Race_MapCatalog_Parse("{ name map difficulty 0 }", &catalog));
  ck_assert(!Race_MapCatalog_Parse("{ name map difficulty 11 }", &catalog));
  ck_assert(!Race_MapCatalog_Parse("{ name map difficulty nope }", &catalog));
  ck_assert(!Race_MapCatalog_Parse("{ name map", &catalog));
  ck_assert(!Race_MapCatalog_Parse("}", &catalog));
  ck_assert(!Race_MapCatalog_Parse(NULL, &catalog));
} END_TEST

void Race_MapBrowser_AddTests(TCase *tcase) {
  tcase_add_test(tcase, _Race_MapBrowserPageWireV3);
  tcase_add_test(tcase, _Race_MapBrowserFullPageFitsOneMessage);
  tcase_add_test(tcase, _Race_MapBrowserDetailWireV3);
  tcase_add_test(tcase, _Race_MapBrowserWireRejectsMalformedInput);
  tcase_add_test(tcase, _Race_MapCatalogMetadataAndDefaults);
  tcase_add_test(tcase, _Race_MapCatalogRejectsMalformedInput);
}
