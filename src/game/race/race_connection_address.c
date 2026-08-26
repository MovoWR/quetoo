/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_connection_address.h"

#include <stdint.h>
#include <string.h>

static bool Race_ConnectionAddressPort(const char *text) {
  if (!text || !*text) {
    return false;
  }
  uint32_t value = 0u;
  while (*text) {
    if (*text < '0' || *text > '9') {
      return false;
    }
    value = value * 10u + (uint32_t) (*text++ - '0');
    if (value > 65535u) {
      return false;
    }
  }
  return value > 0u;
}

bool Race_ConnectionAddressKey(
  const char *source, char output[RACE_CONNECTION_ADDRESS_SIZE]) {
  if (!output) {
    return false;
  }
  output[0] = '\0';
  if (!source) {
    return false;
  }

  const size_t length = strnlen(source, RACE_CONNECTION_ADDRESS_SIZE);
  if (!length || length >= RACE_CONNECTION_ADDRESS_SIZE) {
    return false;
  }

  const char *begin = source;
  size_t address_length = length;
  if (source[0] == '[') {
    const char *closing = strchr(source + 1, ']');
    if (!closing || closing == source + 1 ||
        (closing[1] && (closing[1] != ':' ||
                       !Race_ConnectionAddressPort(closing + 2)))) {
      return false;
    }
    begin = source + 1;
    address_length = (size_t) (closing - begin);
  } else {
    const char *first_colon = strchr(source, ':');
    const char *last_colon = strrchr(source, ':');
    if (first_colon && first_colon == last_colon &&
        Race_ConnectionAddressPort(last_colon + 1)) {
      address_length = (size_t) (last_colon - source);
    }
  }

  if (!address_length || address_length >= RACE_CONNECTION_ADDRESS_SIZE) {
    return false;
  }
  for (size_t i = 0u; i < address_length; i++) {
    unsigned char c = (unsigned char) begin[i];
    if (c <= 0x20u || c >= 0x7fu || c == '\\' || c == '"' || c == ';') {
      output[0] = '\0';
      return false;
    }
    if (c >= 'A' && c <= 'F') {
      c = (unsigned char) (c - 'A' + 'a');
    }
    output[i] = (char) c;
  }
  output[address_length] = '\0';
  return true;
}
