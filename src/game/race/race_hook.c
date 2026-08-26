/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#include "race_hook.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>

bool Race_HookPullSpeed_Parse(const char *text, float *speed) {
  if (!text || !*text || isspace((unsigned char) *text) || !speed) {
    return false;
  }

  errno = 0;
  char *end;
  const float parsed = strtof(text, &end);
  if (errno == ERANGE || end == text || *end || !isfinite(parsed) ||
      parsed < RACE_HOOK_PULL_SPEED_MIN ||
      parsed > RACE_HOOK_PULL_SPEED_MAX) {
    return false;
  }

  *speed = parsed;
  return true;
}
