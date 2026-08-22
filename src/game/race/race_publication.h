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

typedef struct {
  bool (*commit_replay)(void *context, bool *newly_created);
  bool (*commit_map_state)(void *context);
  bool (*remove_replay)(void *context);
  void *context;
} race_publication_ops_t;

typedef enum {
  RACE_PUBLICATION_OK,
  RACE_PUBLICATION_REPLAY_FAILED,
  RACE_PUBLICATION_MAP_STATE_FAILED,
  RACE_PUBLICATION_ORPHAN_RETAINED,
  RACE_PUBLICATION_INVALID_ARGUMENT
} race_publication_result_t;

race_publication_result_t Race_Publication_Commit(
  const race_publication_ops_t *ops);
const char *Race_Publication_ResultName(race_publication_result_t result);
