/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"

#include <string.h>

#include "cg_race_map_browser.h"
#include "cg_race_message.h"
#include "ui/maps/MapBrowserViewController.h"

static race_map_browser_page_t race_map_browser_page;
static race_map_browser_detail_t race_map_browser_detail;

static const char *Cg_RaceMapBrowser_ReadPayload(void) {
  const char *payload = cgi.ReadString();
  if (!Cg_RaceMessage_StringComplete(payload, NULL)) {
    Cg_Error("Unterminated Race map browser payload\n");
  }
  return payload;
}

bool Cg_RaceMapBrowser_ParseMessage(const int32_t command) {
  if (command == SV_CMD_RACE_MAP_BROWSER) {
    race_map_browser_page_t page;
    if (Race_MapBrowserWire_DecodePage(
          Cg_RaceMapBrowser_ReadPayload(), &page)) {
      race_map_browser_page = page;
      MapBrowserViewController_RefreshValues();
    } else {
      Cg_Warn("Invalid Race map browser payload\n");
    }
    return true;
  }
  if (command == SV_CMD_RACE_MAP_BROWSER_DETAIL) {
    race_map_browser_detail_t detail;
    if (Race_MapBrowserWire_DecodeDetail(
          Cg_RaceMapBrowser_ReadPayload(), &detail)) {
      race_map_browser_detail = detail;
      MapBrowserViewController_RefreshDetails();
    } else {
      Cg_Warn("Invalid Race map browser detail payload\n");
    }
    return true;
  }
  return false;
}

void Cg_RaceMapBrowser_Clear(void) {
  memset(&race_map_browser_page, 0, sizeof(race_map_browser_page));
  memset(&race_map_browser_detail, 0, sizeof(race_map_browser_detail));
}

const race_map_browser_page_t *Cg_RaceMapBrowser_Page(void) {
  return &race_map_browser_page;
}

const race_map_browser_detail_t *Cg_RaceMapBrowser_Detail(void) {
  return &race_map_browser_detail;
}
