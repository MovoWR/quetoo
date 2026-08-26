/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"
#include "cg_race_markers.h"
#include "cg_race_presentation.h"

#define CG_RACE_MARKER_LABEL_SIZE 12.f

typedef struct {
  cg_race_marker_descriptor_t descriptor;
  box3_t bounds;
} cg_race_marker_t;

typedef struct {
  const cm_bsp_t *bsp;
  char bsp_name[MAX_QPATH];
  cg_race_marker_t markers[MAX_BSP_ENTITIES];
  size_t count;
  bool loaded;
} cg_race_markers_state_t;

static cg_race_markers_state_t cg_race_markers_state;
static cvar_t *cg_race_markers;

static color_t Cg_RaceMarker_Color(cg_race_marker_type_t type) {
  switch (type) {
    case CG_RACE_MARKER_START:
      return Color4f(.1f, 1.f, .25f, .9f);
    case CG_RACE_MARKER_CHECKPOINT:
      return Color4f(.1f, .8f, 1.f, .9f);
    case CG_RACE_MARKER_FINISH:
      return Color4f(1.f, .35f, .1f, .9f);
    default:
      return color_white;
  }
}

static void Cg_RaceMarkers_Load(void) {
  const cm_bsp_t *bsp = cgi.Bsp();
  const char *bsp_name = cgi.ConfigString(CS_BSP);

  if (!bsp || !bsp_name) {
    return;
  }

  if (cg_race_markers_state.loaded && cg_race_markers_state.bsp == bsp &&
      strcmp(cg_race_markers_state.bsp_name, bsp_name) == 0) {
    return;
  }

  memset(&cg_race_markers_state, 0, sizeof(cg_race_markers_state));
  cg_race_markers_state.bsp = bsp;
  q_strlcpy(cg_race_markers_state.bsp_name, bsp_name,
            sizeof(cg_race_markers_state.bsp_name));

  for (int32_t i = 0; i < bsp->num_entities; i++) {
    const cm_entity_t *entity = bsp->entities[i];
    const cm_entity_t *classname = cgi.EntityValue(entity, "classname");
    const cm_entity_t *checkpoint = cgi.EntityValue(entity, "cp");
    cg_race_marker_descriptor_t descriptor;

    if (!Cg_Race_DescribeMarker(classname->nullable_string,
                                checkpoint->parsed & ENTITY_INTEGER,
                                checkpoint->integer, &descriptor)) {
      continue;
    }

    box3_t bounds = Box3_Null();
    Vector *brushes = cgi.EntityBrushes(entity);

    for (size_t j = 0; j < brushes->count; j++) {
      const cm_bsp_brush_t *brush = VectorValue(brushes, cm_bsp_brush_t *, j);
      bounds = Box3_Union(bounds, brush->bounds);
    }

    release(brushes);

    if (Box3_IsNull(bounds)) {
      continue;
    }

    cg_race_markers_state.markers[cg_race_markers_state.count++] = (cg_race_marker_t) {
      .descriptor = descriptor,
      .bounds = bounds
    };
  }

  cg_race_markers_state.loaded = true;
  Cg_Debug("Loaded %zu Race course marker volumes from %s\n",
           cg_race_markers_state.count, bsp_name);
}

static vec3_t Cg_RaceMarker_Point(vec3_t origin, vec3_t right, vec3_t up,
                                  float x, float y) {
  return Vec3_Fmaf(Vec3_Fmaf(origin, x, right), y, up);
}

static void Cg_RaceMarker_DrawCheckpoint(uint16_t checkpoint, vec3_t origin,
                                         vec3_t right, vec3_t up, color_t color) {
  static const uint8_t digits[] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
  };
  static const vec2_t segments[][2] = {
    { { .x = -.5f, .y =  1.f }, { .x =  .5f, .y =  1.f } },
    { { .x =  .5f, .y =  1.f }, { .x =  .5f, .y =  0.f } },
    { { .x =  .5f, .y =  0.f }, { .x =  .5f, .y = -1.f } },
    { { .x =  .5f, .y = -1.f }, { .x = -.5f, .y = -1.f } },
    { { .x = -.5f, .y = -1.f }, { .x = -.5f, .y =  0.f } },
    { { .x = -.5f, .y =  0.f }, { .x = -.5f, .y =  1.f } },
    { { .x = -.5f, .y =  0.f }, { .x =  .5f, .y =  0.f } }
  };

  const uint16_t values[] = { checkpoint / 10u, checkpoint % 10u };
  const size_t first = checkpoint >= 10u ? 0u : 1u;
  const float center = first ? 0.f : -.7f * CG_RACE_MARKER_LABEL_SIZE;
  vec3_t points[28];
  size_t count = 0u;

  for (size_t i = first; i < 2u; i++) {
    const float offset = center + (float) (i - first) * 1.4f * CG_RACE_MARKER_LABEL_SIZE;
    const uint8_t mask = digits[values[i]];

    for (size_t segment = 0; segment < lengthof(segments); segment++) {
      if (!(mask & (1u << segment))) {
        continue;
      }

      points[count++] = Cg_RaceMarker_Point(
        origin, right, up,
        offset + segments[segment][0].x * CG_RACE_MARKER_LABEL_SIZE,
        segments[segment][0].y * CG_RACE_MARKER_LABEL_SIZE);
      points[count++] = Cg_RaceMarker_Point(
        origin, right, up,
        offset + segments[segment][1].x * CG_RACE_MARKER_LABEL_SIZE,
        segments[segment][1].y * CG_RACE_MARKER_LABEL_SIZE);
    }
  }

  cgi.Draw3DLines(SDL_GPU_PRIMITIVETYPE_LINELIST, points, count, color, true);
}

static void Cg_RaceMarker_DrawStart(vec3_t origin, vec3_t right, vec3_t up,
                                    color_t color) {
  const float size = CG_RACE_MARKER_LABEL_SIZE;
  const vec3_t points[] = {
    Cg_RaceMarker_Point(origin, right, up, 0.f, -size),
    Cg_RaceMarker_Point(origin, right, up, 0.f, size),
    Cg_RaceMarker_Point(origin, right, up, 0.f, size),
    Cg_RaceMarker_Point(origin, right, up, -size * .7f, size * .25f),
    Cg_RaceMarker_Point(origin, right, up, 0.f, size),
    Cg_RaceMarker_Point(origin, right, up, size * .7f, size * .25f),
    Cg_RaceMarker_Point(origin, right, up, -size * .7f, -size),
    Cg_RaceMarker_Point(origin, right, up, size * .7f, -size)
  };

  cgi.Draw3DLines(SDL_GPU_PRIMITIVETYPE_LINELIST,
                  points, lengthof(points), color, true);
}

static void Cg_RaceMarker_DrawFinish(vec3_t origin, vec3_t right, vec3_t up,
                                     color_t color) {
  const float size = CG_RACE_MARKER_LABEL_SIZE;
  const vec3_t points[] = {
    Cg_RaceMarker_Point(origin, right, up, -size * .7f, -size),
    Cg_RaceMarker_Point(origin, right, up, -size * .7f, size),
    Cg_RaceMarker_Point(origin, right, up, -size * .7f, size),
    Cg_RaceMarker_Point(origin, right, up, size * .7f, size),
    Cg_RaceMarker_Point(origin, right, up, size * .7f, size),
    Cg_RaceMarker_Point(origin, right, up, size * .7f, 0.f),
    Cg_RaceMarker_Point(origin, right, up, size * .7f, 0.f),
    Cg_RaceMarker_Point(origin, right, up, -size * .7f, 0.f),
    Cg_RaceMarker_Point(origin, right, up, -size * .7f, size),
    Cg_RaceMarker_Point(origin, right, up, size * .7f, 0.f),
    Cg_RaceMarker_Point(origin, right, up, size * .7f, size),
    Cg_RaceMarker_Point(origin, right, up, -size * .7f, 0.f)
  };

  cgi.Draw3DLines(SDL_GPU_PRIMITIVETYPE_LINELIST,
                  points, lengthof(points), color, true);
}

static void Cg_RaceMarker_Draw(const cg_race_marker_t *marker) {
  const color_t color = Cg_RaceMarker_Color(marker->descriptor.type);
  vec3_t origin = Box3_Center(marker->bounds);
  origin.z = marker->bounds.maxs.z + 24.f;

  const vec3_t right = Vec3_Normalize(cgi.view->right);
  const vec3_t up = Vec3_Normalize(cgi.view->up);

  cgi.Draw3DBox(marker->bounds, color, true);

  switch (marker->descriptor.type) {
    case CG_RACE_MARKER_START:
      Cg_RaceMarker_DrawStart(origin, right, up, color);
      break;
    case CG_RACE_MARKER_CHECKPOINT:
      Cg_RaceMarker_DrawCheckpoint(marker->descriptor.checkpoint, origin, right, up, color);
      break;
    case CG_RACE_MARKER_FINISH:
      Cg_RaceMarker_DrawFinish(origin, right, up, color);
      break;
    default:
      break;
  }
}

void Cg_RaceMarkers_Init(void) {
  memset(&cg_race_markers_state, 0, sizeof(cg_race_markers_state));
  cg_race_markers = cgi.AddCvar(
    "cg_race_markers", "0", CVAR_ARCHIVE,
    "Draw Race course start, checkpoint, and finish markers.");
}

void Cg_RaceMarkers_Draw(void) {

  if (*cgi.state != CL_ACTIVE || !cg_race_markers ||
      !cg_race_markers->integer) {
    return;
  }

  Cg_RaceMarkers_Load();

  for (size_t i = 0; i < cg_race_markers_state.count; i++) {
    Cg_RaceMarker_Draw(&cg_race_markers_state.markers[i]);
  }
}
