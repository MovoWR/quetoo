/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <errno.h>

#include "cg_local.h"
#include "cg_race_client_file.h"
#include "cg_race_practice_markers.h"

#define CG_MARKERS_FORMAT_V1 "RACE_MARKERS_V1"
#define CG_MARKERS_FORMAT_V2 "RACE_MARKERS_V2"
#define CG_MARKERS_MAX 128u
#define CG_MARKERS_MAX_FILE_SIZE (64u * 1024u)
#define CG_MARKERS_MIN_SIZE 4.f
#define CG_MARKERS_MAX_SIZE 64.f
#define CG_MARKERS_CLEAR_CONFIRM_MILLIS 5000u
#define CG_MARKERS_SURFACE_OFFSET 1.5f

typedef enum {
  CG_MARKER_POINT,
  CG_MARKER_TAKEOFF,
  CG_MARKER_LANDING,
  CG_MARKER_AIM,
  CG_MARKER_TYPES
} cg_marker_type_t;

typedef struct {
  uint32_t id;
  vec3_t origin;
  vec3_t normal;
  cg_marker_type_t type;
  color_t color;
  float size;
} cg_marker_t;

typedef struct {
  cg_marker_t markers[CG_MARKERS_MAX];
  size_t count;
  uint32_t next_id;
  char map[MAX_QPATH];
  char path[MAX_OS_PATH];
  uint64_t generation;
  uint8_t active_slot;
  bool has_active_slot;
  bool dirty;
  bool clear_pending;
  uint32_t clear_armed_at;
} cg_markers_state_t;

static cg_markers_state_t cg_markers_state;

static cvar_t *cg_markers;
static cvar_t *cg_marker_type;
static cvar_t *cg_marker_color;
static cvar_t *cg_marker_alpha;
static cvar_t *cg_marker_size;

/**
 * @brief Returns true when the current key destination permits view-based marker edits.
 */
static bool Cg_Markers_CanEditView(void) {
  if (*cgi.state != CL_ACTIVE) {
    cgi.Print("Practice markers require an active map.\n");
    return false;
  }

  if (cgi.GetKeyDest() != KEY_GAME) {
    cgi.Print("Close the console or menu before editing practice markers.\n");
    return false;
  }

  return true;
}

/**
 * @brief Returns true when the map basename is safe for use as a local filename.
 */
static bool Cg_Markers_IsSafeMapName(const char *map) {
  if (!map || !*map) {
    return false;
  }

  for (const char *c = map; *c; c++) {
    if (!((*c >= 'a' && *c <= 'z') ||
          (*c >= 'A' && *c <= 'Z') ||
          (*c >= '0' && *c <= '9') ||
          *c == '_' || *c == '-')) {
      return false;
    }
  }

  return true;
}

/**
 * @brief Derives the current map identity and private marker path from CS_BSP.
 */
static bool Cg_Markers_CurrentMap(char *map, size_t map_size, char *path, size_t path_size) {
  const char *bsp = cgi.ConfigString(CS_BSP);
  if (!bsp || !*bsp) {
    return false;
  }

  char basename[MAX_QPATH];
  StripExtension(Basename(bsp), basename);

  if (!Cg_Markers_IsSafeMapName(basename)) {
    return false;
  }

  if (q_strlcpy(map, basename, map_size) >= map_size) {
    return false;
  }

  const int32_t written = snprintf(path, path_size,
                                   "markers/%s.markers", basename);
  return written > 0 && (size_t) written < path_size;
}

/**
 * @brief Prints the unsaved-state warning required before marker state is discarded.
 */
static void Cg_Markers_WarnDirty(void) {
  if (cg_markers_state.dirty) {
    cgi.Print("^3Practice markers for %s have unsaved changes.^7 Use markers_save to keep them.\n",
              *cg_markers_state.map ? cg_markers_state.map : "the current map");
  }
}

/**
 * @brief Clears storage without producing a warning.
 */
static void Cg_Markers_Reset(void) {
  memset(&cg_markers_state, 0, sizeof(cg_markers_state));
  cg_markers_state.next_id = 1u;
}

/**
 * @brief Returns true when the given local marker ID is already in use.
 */
static bool Cg_Markers_HasId(uint32_t id) {
  for (size_t i = 0; i < cg_markers_state.count; i++) {
    if (cg_markers_state.markers[i].id == id) {
      return true;
    }
  }

  return false;
}

/**
 * @brief Allocates the next unused nonzero local marker ID.
 */
static uint32_t Cg_Markers_NextId(void) {
  uint32_t id = cg_markers_state.next_id ? cg_markers_state.next_id : 1u;

  for (size_t attempts = 0; attempts <= CG_MARKERS_MAX; attempts++) {
    if (!Cg_Markers_HasId(id)) {
      cg_markers_state.next_id = id + 1u;
      if (!cg_markers_state.next_id) {
        cg_markers_state.next_id = 1u;
      }
      return id;
    }

    id++;
    if (!id) {
      id = 1u;
    }
  }

  return 0u;
}

/**
 * @brief Validates a six-digit RGB cvar before using the shared color parser.
 */
static bool Cg_Markers_ParseColor(const char *value, color_t *color) {
  if (!value || strlen(value) != 6u) {
    return false;
  }

  for (const char *c = value; *c; c++) {
    if (!((*c >= '0' && *c <= '9') ||
          (*c >= 'a' && *c <= 'f') ||
          (*c >= 'A' && *c <= 'F'))) {
      return false;
    }
  }

  return Color_Parse(value, color);
}

/**
 * @brief Resolves the current marker style with conservative clamping.
 */
static void Cg_Markers_CurrentStyle(cg_marker_type_t *type, color_t *color, float *size) {
  *type = (cg_marker_type_t) Maxi(0,
                                  Mini(cg_marker_type->integer, CG_MARKER_TYPES - 1));

  if (!Cg_Markers_ParseColor(cg_marker_color->string, color)) {
    *color = Color3b(0, 229, 255);
    cgi.Print("^3Invalid cg_marker_color; using 00e5ff.^7\n");
  }

  color->a = Clampf(cg_marker_alpha->value, 0.1f, 1.f);
  *size = Clampf(cg_marker_size->value, CG_MARKERS_MIN_SIZE, CG_MARKERS_MAX_SIZE);
}

/**
 * @brief Adds a surface-aligned marker at the current view trace.
 */
static void Cg_MarkerAdd_f(void) {
  if (!Cg_Markers_CanEditView()) {
    return;
  }

  if (cg_markers_state.count == CG_MARKERS_MAX) {
    cgi.Print("Practice marker limit reached (%u).\n", CG_MARKERS_MAX);
    return;
  }

  const vec3_t start = cgi.view->origin;
  const vec3_t end = Vec3_Fmaf(start, MAX_WORLD_DIST, cgi.view->forward);
  const cm_trace_t trace = cgi.Trace(start, end, Box3_Zero(), NULL, CONTENTS_MASK_SOLID);

  if (trace.start_solid || trace.all_solid || trace.fraction >= 1.f) {
    cgi.Print("No solid surface found for practice marker.\n");
    return;
  }

  cg_marker_t marker = {
    .id = Cg_Markers_NextId(),
    .origin = trace.end,
    .normal = Vec3_Normalize(trace.plane.normal)
  };

  if (!marker.id || Vec3_LengthSquared(marker.normal) < 0.5f) {
    cgi.Print("Unable to create practice marker at that surface.\n");
    return;
  }

  marker.origin = Vec3_Fmaf(marker.origin, CG_MARKERS_SURFACE_OFFSET, marker.normal);
  Cg_Markers_CurrentStyle(&marker.type, &marker.color, &marker.size);

  cg_markers_state.markers[cg_markers_state.count++] = marker;
  cg_markers_state.dirty = true;
  cg_markers_state.clear_pending = false;

  cgi.Print("Added practice marker %u (%zu/%u).\n",
            marker.id, cg_markers_state.count, CG_MARKERS_MAX);
}

/**
 * @brief Removes the deterministically closest visible marker to the current view ray.
 */
static void Cg_MarkerRemove_f(void) {
  if (!Cg_Markers_CanEditView()) {
    return;
  }

  if (!cg_markers_state.count) {
    cgi.Print("There are no practice markers on this map.\n");
    return;
  }

  const vec3_t start = cgi.view->origin;
  const vec3_t forward = Vec3_Normalize(cgi.view->forward);
  const vec3_t end = Vec3_Fmaf(start, MAX_WORLD_DIST, forward);
  const cm_trace_t trace = cgi.Trace(start, end, Box3_Zero(), NULL, CONTENTS_MASK_SOLID);
  const float ray_limit = trace.start_solid ? 0.f : MAX_WORLD_DIST * trace.fraction;

  size_t best = SIZE_MAX;
  float best_distance = FLT_MAX;
  float best_along = FLT_MAX;

  for (size_t i = 0; i < cg_markers_state.count; i++) {
    const cg_marker_t *marker = &cg_markers_state.markers[i];
    const vec3_t relative = Vec3_Subtract(marker->origin, start);
    const float along = Vec3_Dot(relative, forward);
    const float radius = Clampf(marker->size, 12.f, 48.f);

    if (along < 0.f || along > ray_limit + radius) {
      continue;
    }

    const vec3_t closest = Vec3_Fmaf(start, along, forward);
    const float distance = Vec3_Distance(marker->origin, closest);
    if (distance > radius) {
      continue;
    }

    if (distance < best_distance ||
        (distance == best_distance && along < best_along) ||
        (distance == best_distance && along == best_along &&
         (best == SIZE_MAX || marker->id < cg_markers_state.markers[best].id))) {
      best = i;
      best_distance = distance;
      best_along = along;
    }
  }

  if (best == SIZE_MAX) {
    cgi.Print("Aim closer to a visible practice marker to remove it.\n");
    return;
  }

  const uint32_t id = cg_markers_state.markers[best].id;
  memmove(&cg_markers_state.markers[best],
          &cg_markers_state.markers[best + 1u],
          (cg_markers_state.count - best - 1u) * sizeof(cg_marker_t));
  cg_markers_state.count--;
  cg_markers_state.dirty = true;
  cg_markers_state.clear_pending = false;

  cgi.Print("Removed practice marker %u (%zu remaining).\n", id, cg_markers_state.count);
}

/**
 * @brief Toggles local marker rendering.
 */
static void Cg_Markers_f(void) {
  cgi.ToggleCvar(cg_markers->name);
  cgi.Print("Practice markers are %s.\n", cg_markers->integer ? "visible" : "hidden");
}

/**
 * @brief Clears the current map set after a second invocation within five seconds.
 */
static void Cg_MarkersClear_f(void) {
  if (!cg_markers_state.count) {
    cgi.Print("There are no practice markers to clear.\n");
    cg_markers_state.clear_pending = false;
    return;
  }

  const uint32_t now = cgi.client->unclamped_time;
  if (!cg_markers_state.clear_pending ||
      now - cg_markers_state.clear_armed_at > CG_MARKERS_CLEAR_CONFIRM_MILLIS) {
    cg_markers_state.clear_pending = true;
    cg_markers_state.clear_armed_at = now;
    cgi.Print("Run markers_clear again within five seconds to clear %zu markers.\n",
              cg_markers_state.count);
    return;
  }

  cg_markers_state.count = 0u;
  cg_markers_state.next_id = 1u;
  cg_markers_state.dirty = true;
  cg_markers_state.clear_pending = false;
  cgi.Print("Cleared practice markers for %s.\n", cg_markers_state.map);
}

/**
 * @brief Returns the next line from a bounded mutable buffer.
 */
static char *Cg_Markers_NextLine(char **cursor, char *end) {
  if (*cursor >= end) {
    return NULL;
  }

  char *line = *cursor;
  while (*cursor < end && **cursor != '\n') {
    (*cursor)++;
  }

  if (*cursor < end) {
    *(*cursor)++ = '\0';
  }

  const size_t length = strlen(line);
  if (length && line[length - 1u] == '\r') {
    line[length - 1u] = '\0';
  }

  return line;
}

/**
 * @brief Skips horizontal whitespace within one marker-file line.
 */
static void Cg_Markers_SkipSpace(char **cursor) {
  while (**cursor == ' ' || **cursor == '\t') {
    (*cursor)++;
  }
}

/**
 * @brief Parses one bounded unsigned integer token.
 */
static bool Cg_Markers_ParseUint(char **cursor, uint32_t *value) {
  Cg_Markers_SkipSpace(cursor);
  if (!**cursor || **cursor == '-' || **cursor == '+') {
    return false;
  }

  errno = 0;
  char *end;
  const unsigned long long parsed = strtoull(*cursor, &end, 10);
  if (end == *cursor || errno == ERANGE || parsed > UINT32_MAX ||
      (*end && *end != ' ' && *end != '\t')) {
    return false;
  }

  *cursor = end;
  *value = (uint32_t) parsed;
  return true;
}

/**
 * @brief Parses one bounded 64-bit unsigned integer token.
 */
static bool Cg_Markers_ParseUint64(char **cursor, uint64_t *value) {
  Cg_Markers_SkipSpace(cursor);
  if (!**cursor || **cursor == '-' || **cursor == '+') {
    return false;
  }

  errno = 0;
  char *end;
  const unsigned long long parsed = strtoull(*cursor, &end, 10);
  if (end == *cursor || errno == ERANGE ||
      (*end && *end != ' ' && *end != '\t')) {
    return false;
  }

  *cursor = end;
  *value = (uint64_t) parsed;
  return true;
}

/**
 * @brief Parses one finite-range floating-point token.
 */
static bool Cg_Markers_ParseFloat(char **cursor, float *value) {
  Cg_Markers_SkipSpace(cursor);
  if (!**cursor) {
    return false;
  }

  errno = 0;
  char *end;
  const float parsed = strtof(*cursor, &end);
  if (end == *cursor || errno == ERANGE ||
      (*end && *end != ' ' && *end != '\t')) {
    return false;
  }

  *cursor = end;
  *value = parsed;
  return true;
}

/**
 * @brief Returns true when no non-whitespace tokens remain on the line.
 */
static bool Cg_Markers_AtLineEnd(char *cursor) {
  Cg_Markers_SkipSpace(&cursor);
  return *cursor == '\0';
}

/**
 * @brief Validates one parsed marker before it can reach live state.
 */
static bool Cg_Markers_ValidateMarker(cg_marker_t *marker) {
  if (!marker->id ||
      marker->type >= CG_MARKER_TYPES ||
      !isfinite(marker->origin.x) || !isfinite(marker->origin.y) || !isfinite(marker->origin.z) ||
      fabsf(marker->origin.x) > MAX_WORLD_COORD ||
      fabsf(marker->origin.y) > MAX_WORLD_COORD ||
      fabsf(marker->origin.z) > MAX_WORLD_COORD ||
      !isfinite(marker->normal.x) || !isfinite(marker->normal.y) || !isfinite(marker->normal.z) ||
      fabsf(marker->normal.x) > 1.f || fabsf(marker->normal.y) > 1.f || fabsf(marker->normal.z) > 1.f ||
      !isfinite(marker->color.r) || !isfinite(marker->color.g) ||
      !isfinite(marker->color.b) || !isfinite(marker->color.a) ||
      marker->color.r < 0.f || marker->color.r > 1.f ||
      marker->color.g < 0.f || marker->color.g > 1.f ||
      marker->color.b < 0.f || marker->color.b > 1.f ||
      marker->color.a < 0.1f || marker->color.a > 1.f ||
      !isfinite(marker->size) ||
      marker->size < CG_MARKERS_MIN_SIZE || marker->size > CG_MARKERS_MAX_SIZE) {
    return false;
  }

  const float normal_length = Vec3_Length(marker->normal);
  if (!isfinite(normal_length) || normal_length < 0.99f || normal_length > 1.01f) {
    return false;
  }

  marker->normal = Vec3_Normalize(marker->normal);
  return true;
}

/**
 * @brief Parses a complete marker file transactionally into temporary storage.
 */
static bool Cg_Markers_Parse(char *text, size_t length, const char *expected_map,
                             cg_marker_t *markers, size_t *count,
                             uint32_t *next_id, uint64_t *generation) {
  char *cursor = text;
  char *end = text + length;
  char *line = Cg_Markers_NextLine(&cursor, end);

  if (!line) {
    return false;
  }

  if (strcmp(line, CG_MARKERS_FORMAT_V2) == 0) {
    line = Cg_Markers_NextLine(&cursor, end);
    if (!line || strncmp(line, "generation ", 11u) != 0) {
      return false;
    }
    char *generation_cursor = line + 11u;
    if (!Cg_Markers_ParseUint64(&generation_cursor, generation) ||
        !*generation || !Cg_Markers_AtLineEnd(generation_cursor)) {
      return false;
    }
  } else if (strcmp(line, CG_MARKERS_FORMAT_V1) == 0) {
    *generation = 0u;
  } else {
    return false;
  }

  line = Cg_Markers_NextLine(&cursor, end);
  if (!line || strncmp(line, "map ", 4u) != 0 ||
      strcmp(line + 4u, expected_map) != 0) {
    return false;
  }

  uint32_t advertised_count;
  line = Cg_Markers_NextLine(&cursor, end);
  if (!line || strncmp(line, "count ", 6u) != 0) {
    return false;
  }

  char *count_cursor = line + 6u;
  if (!Cg_Markers_ParseUint(&count_cursor, &advertised_count) ||
      !Cg_Markers_AtLineEnd(count_cursor) || advertised_count > CG_MARKERS_MAX) {
    return false;
  }

  uint32_t highest_id = 0u;
  for (uint32_t i = 0; i < advertised_count; i++) {
    line = Cg_Markers_NextLine(&cursor, end);
    if (!line) {
      return false;
    }

    cg_marker_t marker = { 0 };
    uint32_t type;
    char *row = line;

    if (!Cg_Markers_ParseUint(&row, &marker.id) ||
        !Cg_Markers_ParseFloat(&row, &marker.origin.x) ||
        !Cg_Markers_ParseFloat(&row, &marker.origin.y) ||
        !Cg_Markers_ParseFloat(&row, &marker.origin.z) ||
        !Cg_Markers_ParseFloat(&row, &marker.normal.x) ||
        !Cg_Markers_ParseFloat(&row, &marker.normal.y) ||
        !Cg_Markers_ParseFloat(&row, &marker.normal.z) ||
        !Cg_Markers_ParseUint(&row, &type) ||
        !Cg_Markers_ParseFloat(&row, &marker.color.r) ||
        !Cg_Markers_ParseFloat(&row, &marker.color.g) ||
        !Cg_Markers_ParseFloat(&row, &marker.color.b) ||
        !Cg_Markers_ParseFloat(&row, &marker.color.a) ||
        !Cg_Markers_ParseFloat(&row, &marker.size) ||
        !Cg_Markers_AtLineEnd(row) || type >= CG_MARKER_TYPES) {
      return false;
    }
    marker.type = (cg_marker_type_t) type;

    if (!Cg_Markers_ValidateMarker(&marker)) {
      return false;
    }

    for (uint32_t j = 0; j < i; j++) {
      if (markers[j].id == marker.id) {
        return false;
      }
    }

    markers[i] = marker;
    if (marker.id > highest_id) {
      highest_id = marker.id;
    }
  }

  while ((line = Cg_Markers_NextLine(&cursor, end))) {
    if (*line) {
      return false;
    }
  }

  *count = advertised_count;
  *next_id = highest_id + 1u;
  if (!*next_id) {
    *next_id = 1u;
  }
  return true;
}

/**
 * @brief Loads the current map marker set without publishing partial or malformed data.
 */
static bool Cg_Markers_LoadCurrent(void) {
  char map[MAX_QPATH];
  char path[MAX_OS_PATH];

  if (!Cg_Markers_CurrentMap(map, sizeof(map), path, sizeof(path))) {
    Cg_Warn("Invalid or missing CS_BSP; practice markers are unavailable\n");
    return false;
  }

  const bool map_changed = strcmp(cg_markers_state.map, map) != 0;
  if (map_changed) {
    Cg_Markers_WarnDirty();
    Cg_Markers_Reset();
    q_strlcpy(cg_markers_state.map, map, sizeof(cg_markers_state.map));
    q_strlcpy(cg_markers_state.path, path, sizeof(cg_markers_state.path));
  } else if (cg_markers_state.dirty) {
    Cg_Markers_WarnDirty();
  }

  cg_marker_t best_markers[CG_MARKERS_MAX];
  size_t best_count = 0u;
  uint32_t best_next_id = 1u;
  uint64_t best_generation = 0u;
  int32_t best_slot = -1;
  bool found_file = false;
  bool found_valid = false;

  for (int32_t candidate = -1; candidate < 2; candidate++) {
    char candidate_path[MAX_OS_PATH];
    if (candidate < 0) {
      q_strlcpy(candidate_path, path, sizeof(candidate_path));
    } else {
      const int32_t candidate_length = snprintf(candidate_path,
        sizeof(candidate_path), "%s.%d", path, candidate);
      if (candidate_length <= 0 ||
          (size_t) candidate_length >= sizeof(candidate_path)) {
        continue;
      }
    }

    void *loaded = NULL;
    const int64_t loaded_length = cgi.LoadFile(candidate_path, &loaded);
    if (loaded_length == -1) {
      continue;
    }
    found_file = true;

    cg_marker_t parsed_markers[CG_MARKERS_MAX];
    size_t parsed_count = 0u;
    uint32_t parsed_next_id = 1u;
    uint64_t parsed_generation = 0u;
    bool valid = loaded_length > 0 &&
                 loaded_length <= (int64_t) CG_MARKERS_MAX_FILE_SIZE && loaded &&
                 !memchr(loaded, '\0', (size_t) loaded_length);
    if (valid) {
      char text[CG_MARKERS_MAX_FILE_SIZE + 1u];
      memcpy(text, loaded, (size_t) loaded_length);
      text[(size_t) loaded_length] = '\0';
      valid = Cg_Markers_Parse(text, (size_t) loaded_length, map,
                               parsed_markers, &parsed_count, &parsed_next_id,
                               &parsed_generation);
    }
    if (loaded) {
      cgi.FreeFile(loaded);
    }

    if (!valid) {
      Cg_Warn("Ignored invalid practice marker slot %s\n", candidate_path);
      continue;
    }

    if (!found_valid || parsed_generation > best_generation ||
        (parsed_generation == best_generation && candidate > best_slot)) {
      memcpy(best_markers, parsed_markers,
             parsed_count * sizeof(*parsed_markers));
      best_count = parsed_count;
      best_next_id = parsed_next_id;
      best_generation = parsed_generation;
      best_slot = candidate;
      found_valid = true;
    }
  }

  if (!found_file) {
    cg_markers_state.count = 0u;
    cg_markers_state.next_id = 1u;
    cg_markers_state.generation = 0u;
    cg_markers_state.has_active_slot = false;
    cg_markers_state.dirty = false;
    cg_markers_state.clear_pending = false;
    cgi.Print("No saved practice markers for %s.\n", map);
    return true;
  }

  if (!found_valid) {
    Cg_Warn("Rejected all practice marker slots for %s; %s\n", path,
            map_changed ? "the marker set remains empty"
                        : "current markers were preserved");
    return false;
  }

  memcpy(cg_markers_state.markers, best_markers,
         best_count * sizeof(*best_markers));
  cg_markers_state.count = best_count;
  cg_markers_state.next_id = best_next_id;
  cg_markers_state.generation = best_generation;
  cg_markers_state.has_active_slot = best_slot >= 0;
  cg_markers_state.active_slot = best_slot >= 0 ? (uint8_t) best_slot : 0u;
  cg_markers_state.dirty = false;
  cg_markers_state.clear_pending = false;
  q_strlcpy(cg_markers_state.map, map, sizeof(cg_markers_state.map));
  q_strlcpy(cg_markers_state.path, path, sizeof(cg_markers_state.path));

  cgi.Print("Loaded %zu practice marker%s for %s.\n",
            best_count, best_count == 1u ? "" : "s", map);
  return true;
}

/**
 * @brief Explicitly persists the complete current map marker set.
 */
static void Cg_MarkersSave_f(void) {
  char map[MAX_QPATH];
  char path[MAX_OS_PATH];
  if (!Cg_Markers_CurrentMap(map, sizeof(map), path, sizeof(path))) {
    cgi.Print("Practice markers require a valid active map.\n");
    return;
  }

  if (strcmp(cg_markers_state.map, map) != 0) {
    cgi.Print("Practice marker state is not ready for this map.\n");
    return;
  }

  char buffer[CG_MARKERS_MAX_FILE_SIZE + 1u];
  size_t offset = 0u;

  if (cg_markers_state.generation == UINT64_MAX) {
    Cg_Warn("Practice marker generation is exhausted for %s\n", path);
    return;
  }
  const uint64_t generation = cg_markers_state.generation + 1u;

#define CG_MARKERS_APPEND(...) \
  do { \
    const int32_t written = snprintf(buffer + offset, sizeof(buffer) - offset, __VA_ARGS__); \
    if (written < 0 || (size_t) written >= sizeof(buffer) - offset) { \
      cgi.Print("Practice marker data is too large to save.\n"); \
      return; \
    } \
    offset += (size_t) written; \
  } while (0)

  CG_MARKERS_APPEND("%s\ngeneration %" PRIu64 "\nmap %s\ncount %zu\n",
                    CG_MARKERS_FORMAT_V2, generation, map,
                    cg_markers_state.count);

  for (size_t i = 0; i < cg_markers_state.count; i++) {
    const cg_marker_t *marker = &cg_markers_state.markers[i];
    CG_MARKERS_APPEND(
      "%" PRIu32 " %.9g %.9g %.9g %.9g %.9g %.9g %u %.9g %.9g %.9g %.9g %.9g\n",
      marker->id,
      marker->origin.x, marker->origin.y, marker->origin.z,
      marker->normal.x, marker->normal.y, marker->normal.z,
      (uint32_t) marker->type,
      marker->color.r, marker->color.g, marker->color.b, marker->color.a,
      marker->size);
  }

#undef CG_MARKERS_APPEND

  const uint8_t slot = cg_markers_state.has_active_slot
    ? (uint8_t) (cg_markers_state.active_slot ^ 1u) : 0u;
  char candidate_path[MAX_OS_PATH];
  const int32_t candidate_length = snprintf(candidate_path,
    sizeof(candidate_path), "%s.%u", path, slot);
  if (candidate_length <= 0 ||
      (size_t) candidate_length >= sizeof(candidate_path)) {
    Cg_Warn("Failed to construct the practice marker slot path; the previous slot was preserved\n");
    return;
  }
  if (!Cg_RaceClientFile_WriteVerified(candidate_path, buffer, offset)) {
    Cg_Warn("Failed to save practice markers to %s; the previous slot was preserved\n",
            candidate_path);
    return;
  }

  cg_markers_state.generation = generation;
  cg_markers_state.active_slot = slot;
  cg_markers_state.has_active_slot = true;
  cg_markers_state.dirty = false;
  q_strlcpy(cg_markers_state.path, path, sizeof(cg_markers_state.path));
  cgi.Print("Saved %zu practice marker%s to %s.\n",
            cg_markers_state.count,
            cg_markers_state.count == 1u ? "" : "s",
            path);
}

/**
 * @brief Explicitly reloads the current map marker set.
 */
static void Cg_MarkersReload_f(void) {
  if (!Cg_Markers_LoadCurrent()) {
    cgi.Print("Practice markers were not reloaded.\n");
  }
}

/**
 * @brief Builds an orthonormal basis on the impacted surface.
 */
static void Cg_Markers_Basis(const vec3_t normal, vec3_t *right, vec3_t *up) {
  const vec3_t reference = fabsf(normal.z) < 0.9f ? Vec3(0.f, 0.f, 1.f) : Vec3(0.f, 1.f, 0.f);
  *right = Vec3_Normalize(Vec3_Cross(reference, normal));
  *up = Vec3_Normalize(Vec3_Cross(normal, *right));
}

/**
 * @brief Draws one marker using small depth-tested line-list geometry.
 */
static void Cg_Markers_DrawOne(const cg_marker_t *marker) {
  vec3_t right, up;
  Cg_Markers_Basis(marker->normal, &right, &up);

  const float size = marker->size;
  const vec3_t center = marker->origin;
  vec3_t vertices[16];
  size_t count = 0u;

#define CG_MARKERS_LINE(a, b) \
  do { \
    vertices[count++] = (a); \
    vertices[count++] = (b); \
  } while (0)

  switch (marker->type) {
    case CG_MARKER_TAKEOFF: {
      const vec3_t tip = Vec3_Fmaf(center, size, up);
      const vec3_t tail = Vec3_Fmaf(center, -size * 0.7f, up);
      const vec3_t left = Vec3_Fmaf(tail, -size * 0.65f, right);
      const vec3_t right_point = Vec3_Fmaf(tail, size * 0.65f, right);
      CG_MARKERS_LINE(left, tip);
      CG_MARKERS_LINE(tip, right_point);
      CG_MARKERS_LINE(right_point, left);
      CG_MARKERS_LINE(center, Vec3_Fmaf(center, size * 0.5f, marker->normal));
      break;
    }

    case CG_MARKER_LANDING: {
      const vec3_t a = Vec3_Fmaf(Vec3_Fmaf(center, size, right), size, up);
      const vec3_t b = Vec3_Fmaf(Vec3_Fmaf(center, -size, right), size, up);
      const vec3_t c = Vec3_Fmaf(Vec3_Fmaf(center, -size, right), -size, up);
      const vec3_t d = Vec3_Fmaf(Vec3_Fmaf(center, size, right), -size, up);
      CG_MARKERS_LINE(a, b);
      CG_MARKERS_LINE(b, c);
      CG_MARKERS_LINE(c, d);
      CG_MARKERS_LINE(d, a);
      CG_MARKERS_LINE(a, c);
      CG_MARKERS_LINE(b, d);
      break;
    }

    case CG_MARKER_AIM: {
      const vec3_t a = Vec3_Fmaf(center, size, up);
      const vec3_t b = Vec3_Fmaf(center, size, right);
      const vec3_t c = Vec3_Fmaf(center, -size, up);
      const vec3_t d = Vec3_Fmaf(center, -size, right);
      CG_MARKERS_LINE(a, b);
      CG_MARKERS_LINE(b, c);
      CG_MARKERS_LINE(c, d);
      CG_MARKERS_LINE(d, a);
      CG_MARKERS_LINE(center, Vec3_Fmaf(center, size, marker->normal));
      break;
    }

    case CG_MARKER_POINT:
    default:
      CG_MARKERS_LINE(Vec3_Fmaf(center, -size, right), Vec3_Fmaf(center, size, right));
      CG_MARKERS_LINE(Vec3_Fmaf(center, -size, up), Vec3_Fmaf(center, size, up));
      CG_MARKERS_LINE(center, Vec3_Fmaf(center, size * 0.75f, marker->normal));
      break;
  }

#undef CG_MARKERS_LINE

  cgi.Draw3DLines(SDL_GPU_PRIMITIVETYPE_LINELIST,
                  vertices, count, marker->color, true);
}

/**
 * @brief Clears all session marker state, warning first when it is dirty.
 */
void Cg_RacePracticeMarkers_Clear(void) {
  Cg_Markers_WarnDirty();
  Cg_Markers_Reset();
}

/**
 * @brief Draws all private markers for the current map.
 */
void Cg_RacePracticeMarkers_Draw(void) {
  if (!cg_markers || !cg_markers->integer) {
    return;
  }

  for (size_t i = 0; i < cg_markers_state.count; i++) {
    Cg_Markers_DrawOne(&cg_markers_state.markers[i]);
  }
}

/**
 * @brief Registers private marker cvars and cgame-local commands.
 */
void Cg_RacePracticeMarkers_Init(void) {
  Cg_Markers_Reset();

  cg_markers = cgi.AddCvar("cg_markers", "1", CVAR_ARCHIVE,
                           "Show private practice markers.");
  cg_marker_type = cgi.AddCvar("cg_marker_type", "0", CVAR_ARCHIVE,
                               "Practice marker type: 0 point, 1 takeoff, 2 landing, 3 aim.");
  cg_marker_color = cgi.AddCvar("cg_marker_color", "00e5ff", CVAR_ARCHIVE,
                                "Practice marker RGB color in rrggbb format.");
  cg_marker_alpha = cgi.AddCvar("cg_marker_alpha", "0.85", CVAR_ARCHIVE,
                                "Practice marker opacity from 0.1 to 1.");
  cg_marker_size = cgi.AddCvar("cg_marker_size", "16", CVAR_ARCHIVE,
                               "Practice marker size from 4 to 64 units.");

  cgi.AddCmd("markers", Cg_Markers_f, CMD_CGAME, "Toggle private practice markers.");
  cgi.AddCmd("marker_add", Cg_MarkerAdd_f, CMD_CGAME,
             "Add a private practice marker at the aimed surface.");
  cgi.AddCmd("marker_remove", Cg_MarkerRemove_f, CMD_CGAME,
             "Remove the private practice marker nearest the view ray.");
  cgi.AddCmd("markers_clear", Cg_MarkersClear_f, CMD_CGAME,
             "Clear private practice markers after confirmation.");
  cgi.AddCmd("markers_save", Cg_MarkersSave_f, CMD_CGAME,
             "Save private practice markers for the current map.");
  cgi.AddCmd("markers_reload", Cg_MarkersReload_f, CMD_CGAME,
             "Reload private practice markers for the current map.");
}

/**
 * @brief Automatically loads the current map set after map media is ready.
 */
void Cg_RacePracticeMarkers_Load(void) {
  (void) Cg_Markers_LoadCurrent();
}

/**
 * @brief Warns about unsaved private marker state during cgame shutdown.
 */
void Cg_RacePracticeMarkers_Shutdown(void) {
  Cg_Markers_WarnDirty();
}
