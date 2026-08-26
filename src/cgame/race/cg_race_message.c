/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_race_message.h"

#include <stdint.h>
#include <string.h>

#include "quetoo.h"

bool Cg_RaceMessage_StringComplete(const char *payload, size_t *length) {
  if (length) {
    *length = 0u;
  }
  if (!payload) {
    return false;
  }

  const size_t payload_length = strnlen(payload, MAX_STRING_CHARS);
  if (length) {
    *length = payload_length;
  }
  return payload_length < MAX_STRING_CHARS - 1u;
}

bool Cg_RaceMessage_Drain(const cg_race_message_read_data_t read_data,
                          const size_t length) {
  if (!read_data) {
    return false;
  }

  uint8_t discard[64];
  size_t remaining = length;
  while (remaining) {
    const size_t count = remaining < sizeof(discard)
      ? remaining : sizeof(discard);
    read_data(discard, count);
    remaining -= count;
  }
  return true;
}
