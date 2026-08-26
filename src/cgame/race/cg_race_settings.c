/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"
#include "cg_race_settings.h"

/**
 * @brief The last payload that decoded cleanly.
 */
static struct {
  race_settings_status_t status;
  bool valid;
} cg_race_settings;

void Cg_RaceSettings_Clear(void) {
  memset(&cg_race_settings, 0, sizeof(cg_race_settings));
}

void Cg_RaceSettings_UpdateStatus(const char *status) {

  // An empty ConfigString is what a server that predates the mirror publishes,
  // and it is also what the level reset leaves behind. Both mean "nothing known
  // yet", which is not the same as "nothing assigned".
  if (!status || !*status) {
    Cg_RaceSettings_Clear();
    return;
  }

  race_settings_status_t decoded;
  if (!Race_Settings_StatusDecode(status, &decoded)) {
    // Half-applying a malformed payload would put fresh rows next to stale ones
    // with nothing to tell them apart. Drop the whole mirror instead.
    Cg_RaceSettings_Clear();
    return;
  }

  cg_race_settings.status = decoded;
  cg_race_settings.valid = true;
}

void Cg_RaceSettings_Init(void) {
  Cg_RaceSettings_Clear();
  if (cgi.ConfigString) {
    Cg_RaceSettings_UpdateStatus(cgi.ConfigString(CS_RACE_SETTINGS_STATUS));
  }
}

bool Cg_RaceSettings_Valid(void) {
  return cg_race_settings.valid;
}

const race_settings_status_t *Cg_RaceSettings_Status(void) {
  return cg_race_settings.valid ? &cg_race_settings.status : NULL;
}

const char *Cg_RaceSettings_Map(void) {
  return cg_race_settings.valid && cg_race_settings.status.map[0]
    ? cg_race_settings.status.map
    : NULL;
}

bool Cg_RaceSettings_Truncated(void) {
  return cg_race_settings.valid && cg_race_settings.status.truncated;
}

bool Cg_RaceSettings_Assigned(const race_setting_id_t id, const bool map_store) {

  if (!cg_race_settings.valid || id >= RACE_SETTING_TOTAL) {
    return false;
  }

  const uint32_t mask = map_store ? cg_race_settings.status.map_mask
                                  : cg_race_settings.status.global_mask;
  return (mask & (1u << (uint16_t) id)) != 0u;
}

const char *Cg_RaceSettings_Assignment(const race_setting_id_t id,
                                       const bool map_store) {

  if (!Cg_RaceSettings_Assigned(id, map_store)) {
    return NULL;
  }

  const char *value = map_store ? cg_race_settings.status.overrides[id]
                                : cg_race_settings.status.global[id];
  return *value ? value : NULL;
}

const char *Cg_RaceSettings_Effective(const race_setting_id_t id,
                                      bool *assigned, bool *from_map) {

  if (!cg_race_settings.valid) {
    if (assigned) {
      *assigned = false;
    }
    if (from_map) {
      *from_map = false;
    }
    return NULL;
  }

  return Race_Settings_StatusEffective(&cg_race_settings.status, id, assigned,
                                       from_map);
}

size_t Cg_RaceSettings_AssignedCount(const bool map_store) {

  if (!cg_race_settings.valid) {
    return 0u;
  }

  const uint32_t mask = map_store ? cg_race_settings.status.map_mask
                                  : cg_race_settings.status.global_mask;
  size_t count = 0u;
  for (uint16_t id = 0u; id < RACE_SETTING_TOTAL; id++) {
    if (mask & (1u << id)) {
      count++;
    }
  }
  return count;
}
