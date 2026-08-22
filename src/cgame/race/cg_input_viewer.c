/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"
#include "cg_input_viewer.h"

#include "cg_input_viewer_math.h"
#include "cg_race_hud.h"
#include "cg_race_replay.h"

// §7 geometry, in 1440p pixels.
#define CG_INPUT_VIEWER_CHEVRON 46.f
#define CG_INPUT_VIEWER_GAP 7.f
#define CG_INPUT_VIEWER_GROUP_GAP 32.f
#define CG_INPUT_VIEWER_ACTION_GAP 12.f

static cvar_t *cg_input_viewer;
static int16_t cg_input_viewer_local_flags;
static struct {
  r_image_t *left;
  r_image_t *up;
  r_image_t *down;
  r_image_t *right;
} cg_input_viewer_images;

/**
 * @brief Loads the directional chevrons.
 * @remarks The attack, crouch and jump wordmarks are deliberately absent: at
 * 64x64 with no fully opaque pixels they read as smudges at HUD size, so §7
 * draws those three as text instead.
 */
static void Cg_InputViewer_LoadMedia(void) {
  if (cg_input_viewer_images.left) {
    return;
  }

  cg_input_viewer_images.left = cgi.LoadImage("ui/hud_input_left", IMG_PIC);
  cg_input_viewer_images.up = cgi.LoadImage("ui/hud_input_up", IMG_PIC);
  cg_input_viewer_images.down = cgi.LoadImage("ui/hud_input_down", IMG_PIC);
  cg_input_viewer_images.right = cgi.LoadImage("ui/hud_input_right", IMG_PIC);
}

/**
 * @return The tint for a key: full opacity when held, half when idle.
 */
static color_t Cg_InputViewer_KeyColor(const bool active) {
  return active
    ? Color4b(0xf4, 0xf8, 0xfb, 0xff)
    : Color4b(0xf4, 0xf8, 0xfb, 0x80);
}

static void Cg_InputViewer_DrawChevron(const int32_t x, const int32_t y,
                                       const int32_t size,
                                       const r_image_t *image,
                                       const bool active) {
  if (image) {
    cgi.Draw2DImage(x, y, size, size, image,
                    Cg_InputViewer_KeyColor(active));
  }
}

void Cg_InputViewer_DrawCluster(const int32_t x, const int32_t bottom,
                                const int16_t flags) {
  Cg_InputViewer_LoadMedia();

  const int32_t chevron = Cg_RaceHud_Scale(CG_INPUT_VIEWER_CHEVRON);
  const int32_t gap = Cg_RaceHud_Scale(CG_INPUT_VIEWER_GAP);
  const int32_t group_gap = Cg_RaceHud_Scale(CG_INPUT_VIEWER_GROUP_GAP);
  const int32_t action_gap = Cg_RaceHud_Scale(CG_INPUT_VIEWER_ACTION_GAP);

  const int32_t height = Cg_InputViewer_ChevronsHeight(chevron, gap);
  const int32_t top = bottom - height;

  int32_t text_height;
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &text_height);
  const int32_t attack_width = cgi.StringWidth("ATTACK");
  const int32_t duck_width = cgi.StringWidth("DUCK");
  const int32_t actions_width = Maxi(attack_width, duck_width);

  // ATTACK over DUCK, the pair centered on the chevron cluster.
  const int32_t actions_height = text_height * 2 + action_gap;
  const int32_t actions_y = top + (height - actions_height) / 2;
  Cg_RaceHud_DrawShadowedString(
    x, actions_y, "ATTACK",
    Cg_InputViewer_KeyColor(flags & RACE_INPUT_ATTACK));
  Cg_RaceHud_DrawShadowedString(
    x, actions_y + text_height + action_gap, "DUCK",
    Cg_InputViewer_KeyColor(flags & RACE_INPUT_CROUCH));

  // The chevrons: up centered above left, down and right.
  const int32_t chevrons_x = x + actions_width + group_gap;
  const int32_t row_y = top + chevron + gap;
  Cg_InputViewer_DrawChevron(chevrons_x + chevron + gap, top, chevron,
                             cg_input_viewer_images.up,
                             flags & RACE_INPUT_FORWARD);
  Cg_InputViewer_DrawChevron(chevrons_x, row_y, chevron,
                             cg_input_viewer_images.left,
                             flags & RACE_INPUT_LEFT);
  Cg_InputViewer_DrawChevron(chevrons_x + chevron + gap, row_y, chevron,
                             cg_input_viewer_images.down,
                             flags & RACE_INPUT_BACK);
  Cg_InputViewer_DrawChevron(chevrons_x + (chevron + gap) * 2, row_y, chevron,
                             cg_input_viewer_images.right,
                             flags & RACE_INPUT_RIGHT);

  const int32_t jump_x = chevrons_x +
                         Cg_InputViewer_ChevronsWidth(chevron, gap) +
                         group_gap;
  Cg_RaceHud_DrawShadowedString(jump_x, top + (height - text_height) / 2,
                                "JUMP",
                                Cg_InputViewer_KeyColor(
                                  flags & RACE_INPUT_JUMP));
}

void Cg_InputViewer_DrawUnavailable(const int32_t x, const int32_t bottom) {
  const int32_t chevron = Cg_RaceHud_Scale(CG_INPUT_VIEWER_CHEVRON);
  const int32_t gap = Cg_RaceHud_Scale(CG_INPUT_VIEWER_GAP);
  const int32_t height = Cg_InputViewer_ChevronsHeight(chevron, gap);

  int32_t text_height;
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &text_height);
  Cg_RaceHud_DrawShadowedString(x, bottom - height + (height - text_height) / 2,
                                "INPUT NOT RECORDED",
                                Color4f(1.f, 1.f, 1.f, .44f));
}

void Cg_InputViewer_Init(void) {
  cg_input_viewer = cgi.AddCvar(
    "cg_input_viewer", "1", CVAR_ARCHIVE,
    "Draw effective gameplay actions during live, chase, and replay play.");
  Cg_InputViewer_Clear();
}

void Cg_InputViewer_Clear(void) {
  cg_input_viewer_local_flags = RACE_INPUT_FORMAT_V1;
  memset(&cg_input_viewer_images, 0, sizeof(cg_input_viewer_images));
}

void Cg_InputViewer_CaptureCommand(const pm_cmd_t *cmd) {
  cg_input_viewer_local_flags = Race_InputFlags(cmd);
}

void Cg_InputViewer_Draw(const player_state_t *ps) {
  if (!ps || !Cg_InputViewer_Visible(
        cg_input_viewer && cg_input_viewer->integer,
        cgi.GetKeyDest() == KEY_GAME,
        ps->pm_state.type == PM_DEAD,
        ps->stats[STAT_SPECTATOR] && !ps->stats[STAT_CHASE],
        ps->stats[STAT_SCORES] != 0)) {
    return;
  }

  int16_t replay_flags = 0;
  const bool replay_active = Cg_ReplayActive();
  if (replay_active) {
    Cg_RaceReplay_Telemetry(NULL, NULL, &replay_flags);
  }

  const cg_input_viewer_state_t state = Cg_InputViewer_Select(
    cg_input_viewer_local_flags, ps->stats[STAT_RACE_INPUT], replay_flags,
    replay_active, ps->stats[STAT_CHASE] != 0);

  const int32_t x = Cg_RaceHud_Edge();
  const int32_t bottom = cgi.context->h - Cg_RaceHud_Edge();

  if (!state.valid) {
    if (state.source != CG_INPUT_VIEWER_LOCAL) {
      Cg_InputViewer_DrawUnavailable(x, bottom);
    }
    cgi.BindFont(NULL, NULL, NULL);
    return;
  }

  Cg_InputViewer_DrawCluster(x, bottom, state.flags);
  cgi.BindFont(NULL, NULL, NULL);
}
