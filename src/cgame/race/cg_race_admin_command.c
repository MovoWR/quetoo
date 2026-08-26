/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#include "cg_race_admin_command.h"

#include <ctype.h>
#include <string.h>

#include "race_settings.h"

static bool Cg_RaceAdminCommand_EqualsIgnoringCase(const char *left,
                                                   const char *right,
                                                   size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (tolower((unsigned char) left[i]) !=
        tolower((unsigned char) right[i])) {
      return false;
    }
  }
  return true;
}

bool Cg_RaceAdminCommand_TokenValid(const char *value) {
  if (!value || !*value || strlen(value) > RACE_SETTING_NAME_MAX) {
    return false;
  }

  for (const unsigned char *c = (const unsigned char *) value; *c; c++) {
    if (!isalnum(*c) && *c != '_' && *c != '-' && *c != '.') {
      return false;
    }
  }

  return true;
}

bool Cg_RaceAdminCommand_MapToken(const char *bsp,
                                  char output[MAX_QPATH]) {
  if (!output) {
    return false;
  }
  output[0] = '\0';

  if (!bsp || !*bsp) {
    return false;
  }

  const size_t bsp_length = strlen(bsp);
  const char *name = bsp_length >= 5u &&
                     Cg_RaceAdminCommand_EqualsIgnoringCase(bsp, "maps/", 5u)
    ? bsp + 5 : bsp;
  const size_t length = strlen(name);
  if (length <= 4u || length - 4u >= MAX_QPATH ||
      !Cg_RaceAdminCommand_EqualsIgnoringCase(name + length - 4u,
                                              ".bsp", 4u)) {
    return false;
  }

  memcpy(output, name, length - 4u);
  output[length - 4u] = '\0';
  if (!Cg_RaceAdminCommand_TokenValid(output)) {
    output[0] = '\0';
    return false;
  }

  return true;
}
