/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_leaderboard.h"

#include <string.h>

static bool Race_Leaderboard_BoundedLength(const char *string, size_t maximum,
                                           size_t *length) {
  if (!string) {
    return false;
  }

  size_t len = 0;
  while (len <= maximum && string[len]) {
    len++;
  }

  if (len > maximum) {
    return false;
  }

  if (length) {
    *length = len;
  }

  return true;
}

static int32_t Race_Leaderboard_CompareTop(const race_leaderboard_record_t *left,
                                           const race_leaderboard_record_t *right) {
  if (left->elapsed_time != right->elapsed_time) {
    return left->elapsed_time < right->elapsed_time ? -1 : 1;
  }

  return strcmp(left->uid, right->uid);
}

bool Race_Leaderboard_RecordInit(race_leaderboard_record_t *record,
                                 const char *uid, const char *display_name,
                                 uint32_t elapsed_time,
                                 const uint32_t *checkpoint_times,
                                 size_t checkpoint_count) {
  if (!record) {
    return false;
  }

  memset(record, 0, sizeof(*record));

  char canonical[RACE_PROFILE_UID_SIZE];
  size_t display_name_length;
  if (!Race_Profile_CanonicalizeUid(uid, canonical) || strcmp(uid, canonical) ||
      !Race_Leaderboard_BoundedLength(display_name, RACE_PROFILE_NAME_MAX,
                                      &display_name_length) ||
      elapsed_time < 1u || elapsed_time > RACE_LEADERBOARD_MAX_TIME_MS ||
      checkpoint_count > RACE_MAX_CHECKPOINTS ||
      (checkpoint_count && !checkpoint_times)) {
    return false;
  }

  uint32_t previous = 0;
  for (size_t i = 0; i < checkpoint_count; i++) {
    const uint32_t split = checkpoint_times[i];
    if (!split || split <= previous || split > elapsed_time) {
      return false;
    }
    previous = split;
  }

  memcpy(record->uid, canonical, sizeof(record->uid));
  memcpy(record->display_name, display_name, display_name_length + 1);
  record->elapsed_time = elapsed_time;
  record->checkpoint_count = (uint16_t) checkpoint_count;
  if (checkpoint_count) {
    memcpy(record->checkpoint_times, checkpoint_times,
           checkpoint_count * sizeof(*checkpoint_times));
  }

  return true;
}

static bool Race_Leaderboard_RecordValid(const race_leaderboard_record_t *record) {
  if (!record) {
    return false;
  }

  race_leaderboard_record_t canonical;
  if (!Race_Leaderboard_RecordInit(&canonical,
                                   record->uid,
                                   record->display_name,
                                   record->elapsed_time,
                                   record->checkpoint_times,
                                   record->checkpoint_count)) {
    return false;
  }

  if (!Race_Leaderboard_RecordSetSplits(&canonical, record->split_times,
                                        record->split_count,
                                        record->split_layout)) {
    return false;
  }

  return !strcmp(canonical.uid, record->uid) &&
         !strcmp(canonical.display_name, record->display_name) &&
         canonical.elapsed_time == record->elapsed_time &&
         record->date_unix_s <= RACE_LEADERBOARD_MAX_DATE_UNIX_S &&
         canonical.checkpoint_count == record->checkpoint_count &&
         !memcmp(canonical.checkpoint_times, record->checkpoint_times,
                 sizeof(canonical.checkpoint_times)) &&
         canonical.split_count == record->split_count &&
         canonical.split_layout == record->split_layout &&
         !memcmp(canonical.split_times, record->split_times,
                 sizeof(canonical.split_times));
}

bool Race_Leaderboard_RecordSetSplits(race_leaderboard_record_t *record,
                                      const uint32_t *split_times,
                                      const size_t split_count,
                                      const uint64_t split_layout) {
  if (!record || split_count > RACE_MAX_CHECKPOINTS ||
      (split_count && (!split_times || !split_layout)) ||
      (!split_count && split_layout)) {
    return false;
  }

  uint32_t previous = 0u;
  for (size_t i = 0; i < split_count; i++) {
    if (!split_times[i] || split_times[i] <= previous ||
        split_times[i] > record->elapsed_time) {
      return false;
    }
    previous = split_times[i];
  }

  record->split_count = (uint16_t) split_count;
  record->split_layout = split_layout;
  memset(record->split_times, 0, sizeof(record->split_times));
  if (split_count) {
    memcpy(record->split_times, split_times,
           split_count * sizeof(*split_times));
  }
  return true;
}

bool Race_Leaderboard_RecordSetDate(race_leaderboard_record_t *record,
                                    const uint64_t date_unix_s) {
  if (!record || !date_unix_s ||
      date_unix_s > RACE_LEADERBOARD_MAX_DATE_UNIX_S ||
      !Race_Leaderboard_RecordValid(record)) {
    return false;
  }

  record->date_unix_s = date_unix_s;
  return true;
}

bool Race_Leaderboard_RecordAttachReplay(race_leaderboard_record_t *record,
                                         uint64_t replay_id) {
  if (!record || !replay_id || !Race_Leaderboard_RecordValid(record)) {
    return false;
  }

  record->replay_id = replay_id;
  return true;
}

bool Race_Leaderboard_RecordsValid(const race_leaderboard_record_t *records,
                                   size_t count) {
  if (count && !records) {
    return false;
  }

  for (size_t i = 0; i < count; i++) {
    if (!Race_Leaderboard_RecordValid(records + i) ||
        (i && strcmp(records[i - 1].uid, records[i].uid) >= 0)) {
      return false;
    }
  }

  return true;
}

static size_t Race_Leaderboard_LowerBound(const race_leaderboard_record_t *records,
                                          size_t count, const char *uid) {
  size_t low = 0;
  size_t high = count;

  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    if (strcmp(records[middle].uid, uid) < 0) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }

  return low;
}

const race_leaderboard_record_t *Race_Leaderboard_FindSorted(
    const race_leaderboard_record_t *records, const size_t count,
    const char *uid) {
  char canonical[RACE_PROFILE_UID_SIZE];
  if ((!records && count) || !uid ||
      !Race_Profile_CanonicalizeUid(uid, canonical) ||
      strcmp(uid, canonical)) {
    return NULL;
  }

  const size_t index = Race_Leaderboard_LowerBound(records, count, uid);
  return index < count && !strcmp(records[index].uid, uid)
    ? records + index
    : NULL;
}

const race_leaderboard_record_t *Race_Leaderboard_Find(
  const race_leaderboard_record_t *records, size_t count, const char *uid) {
  if (!uid || !Race_Leaderboard_RecordsValid(records, count)) {
    return NULL;
  }

  return Race_Leaderboard_FindSorted(records, count, uid);
}

bool Race_Leaderboard_Evaluate(const race_leaderboard_record_t *records,
                               size_t count, size_t capacity,
                               const race_leaderboard_record_t *candidate,
                               race_leaderboard_evaluation_t *evaluation) {
  if (!evaluation) {
    return false;
  }

  memset(evaluation, 0, sizeof(*evaluation));

  if (count > capacity || !Race_Leaderboard_RecordsValid(records, count) ||
      !Race_Leaderboard_RecordValid(candidate)) {
    return false;
  }

  evaluation->valid = true;

  const race_leaderboard_record_t *existing = Race_Leaderboard_Find(records, count,
                                                                     candidate->uid);
  if (existing && candidate->elapsed_time >= existing->elapsed_time) {
    return true;
  }

  if (!existing && count == capacity) {
    return true;
  }

  evaluation->would_accept = true;
  evaluation->personal_best = true;
  evaluation->first_completion = count == 0;

  uint32_t best_time = UINT32_MAX;
  size_t faster = 0;

  for (size_t i = 0; i < count; i++) {
    if (records[i].elapsed_time < best_time) {
      best_time = records[i].elapsed_time;
    }

    if (&records[i] != existing &&
        Race_Leaderboard_CompareTop(records + i, candidate) < 0) {
      faster++;
    }
  }

  evaluation->world_record = !count || candidate->elapsed_time < best_time;
  evaluation->top_rank = faster + 1;
  evaluation->top = evaluation->top_rank <= RACE_LEADERBOARD_TOP_MAX;

  return true;
}

bool Race_Leaderboard_Apply(race_leaderboard_record_t *records,
                            size_t *count, size_t capacity,
                            const race_leaderboard_record_t *candidate,
                            race_leaderboard_evaluation_t *evaluation) {
  race_leaderboard_evaluation_t local;
  race_leaderboard_evaluation_t *result = evaluation ? evaluation : &local;

  if (!records || !count ||
      !Race_Leaderboard_Evaluate(records, *count, capacity, candidate, result) ||
      !result->would_accept) {
    return false;
  }

  const size_t index = Race_Leaderboard_LowerBound(records, *count, candidate->uid);
  if (index < *count && !strcmp(records[index].uid, candidate->uid)) {
    records[index] = *candidate;
    return true;
  }

  memmove(records + index + 1, records + index,
          (*count - index) * sizeof(*records));
  records[index] = *candidate;
  (*count)++;
  return true;
}

size_t Race_Leaderboard_Top(const race_leaderboard_record_t *records,
                            size_t count,
                            const race_leaderboard_record_t **top,
                            size_t top_capacity) {
  if (!top || !top_capacity ||
      !Race_Leaderboard_RecordsValid(records, count)) {
    return 0;
  }

  const size_t limit = top_capacity < RACE_LEADERBOARD_TOP_MAX
    ? top_capacity
    : RACE_LEADERBOARD_TOP_MAX;
  size_t top_count = 0;

  for (size_t i = 0; i < count; i++) {
    size_t position = 0;
    while (position < top_count &&
           Race_Leaderboard_CompareTop(top[position], records + i) < 0) {
      position++;
    }

    if (position >= limit) {
      continue;
    }

    const size_t move_count = top_count < limit
      ? top_count - position
      : limit - position - 1;
    memmove(top + position + 1, top + position,
            move_count * sizeof(*top));
    top[position] = records + i;
    if (top_count < limit) {
      top_count++;
    }
  }

  return top_count;
}
