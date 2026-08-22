/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_publication.h"

race_publication_result_t Race_Publication_Commit(
  const race_publication_ops_t *ops) {
  if (!ops || !ops->commit_replay || !ops->commit_map_state ||
      !ops->remove_replay) {
    return RACE_PUBLICATION_INVALID_ARGUMENT;
  }

  bool newly_created = false;
  if (!ops->commit_replay(ops->context, &newly_created)) {
    return RACE_PUBLICATION_REPLAY_FAILED;
  }
  if (ops->commit_map_state(ops->context)) {
    return RACE_PUBLICATION_OK;
  }
  if (newly_created && !ops->remove_replay(ops->context)) {
    return RACE_PUBLICATION_ORPHAN_RETAINED;
  }
  return RACE_PUBLICATION_MAP_STATE_FAILED;
}

const char *Race_Publication_ResultName(race_publication_result_t result) {
  switch (result) {
    case RACE_PUBLICATION_OK:
      return "ok";
    case RACE_PUBLICATION_REPLAY_FAILED:
      return "replay failed";
    case RACE_PUBLICATION_MAP_STATE_FAILED:
      return "map state failed";
    case RACE_PUBLICATION_ORPHAN_RETAINED:
      return "map state failed; orphan retained";
    case RACE_PUBLICATION_INVALID_ARGUMENT:
      return "invalid argument";
  }
  return "unknown";
}
