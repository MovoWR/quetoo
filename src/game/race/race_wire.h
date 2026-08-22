/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <stdint.h>
#include <string.h>

/**
 * @brief Reinterprets an unsigned wire word as one player-stat slot.
 * @details Player stats are signed 16-bit values. `memcpy` preserves all bits
 * without relying on an implementation-defined out-of-range integer cast.
 */
static inline int16_t Race_WireStat(uint16_t word) {
  int16_t stat;
  memcpy(&stat, &word, sizeof(stat));
  return stat;
}

static inline int16_t Race_WireElapsedLow(uint32_t elapsed) {
  return Race_WireStat((uint16_t) elapsed);
}

static inline int16_t Race_WireElapsedHigh(uint32_t elapsed) {
  return Race_WireStat((uint16_t) (elapsed >> 16u));
}

static inline uint32_t Race_WireElapsed(int16_t low, int16_t high) {
  return (uint32_t) (uint16_t) low | ((uint32_t) (uint16_t) high << 16u);
}
