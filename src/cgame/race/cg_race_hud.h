/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "cg_types.h"

/**
 * @brief The design names for the Race HUD type scale.
 *
 * Every Race HUD call site binds a font by one of these names rather than by
 * an engine font name, so the mapping from design intent to what the 2D draw
 * path can actually render lives in exactly one table.
 */
#define RACE_FONT_TIMER "race_timer"
#define RACE_FONT_SPEED "race_speed"
#define RACE_FONT_ROUTE "race_route"
#define RACE_FONT_VALUE "race_value"
#define RACE_FONT_RECORD "race_record"
#define RACE_FONT_BODY "race_body"
#define RACE_FONT_LABEL "race_label"

/**
 * @brief The design height every Race HUD constant is expressed in.
 */
#define RACE_HUD_DESIGN_HEIGHT 1440.f

/**
 * @brief Top stack geometry, shared by the live HUD and the replay HUD so the
 * two lockups sit on the same rows.
 */
#define RACE_HUD_STACK_GAP 10.f
#define RACE_HUD_MODE_GAP 16.f

/**
 * @brief The gap between the map clock's value and its caption.
 * @remarks Shared with the finish bar, which draws the same pair in the same
 * slot for as long as it is up, so the clock does not shift when the bar
 * appears.
 */
#define RACE_HUD_CLOCK_CAPTION_GAP 6.f

/**
 * @brief The separator between run in the route and replay lockups.
 * @remarks The design uses U+00B7. The 2D draw path indexes a 16x8 bitmap
 * atlas by raw byte, so anything outside ASCII samples off the atlas and
 * renders as noise.
 */
#define RACE_HUD_SEPARATOR " - "

/**
 * @brief Binds a Race HUD font by design name, optionally returning its
 * height in pixels.
 */
void Cg_RaceHud_BindFont(const char *name, int32_t *height);

/**
 * @return The design pixel value `value` scaled to the current viewport.
 */
int32_t Cg_RaceHud_Scale(float value);

/**
 * @return The edge inset shared by every corner cluster, already scaled.
 */
int32_t Cg_RaceHud_Edge(void);

/**
 * @brief Draws `text` per the active `cg_race_hud_legibility` treatment:
 * `none` (the default, glyphs only), `shadow`, `stroke` or `plates`.
 */
void Cg_RaceHud_DrawShadowedString(int32_t x, int32_t y, const char *text,
                                   color_t color);

/**
 * @brief Draws `text` with its right edge at `right`.
 */
void Cg_RaceHud_DrawRightAligned(int32_t right, int32_t y, const char *text,
                                 color_t color);

/**
 * @brief Draws `text` centered on `center_x`.
 */
void Cg_RaceHud_DrawCentered(int32_t center_x, int32_t y, const char *text,
                             color_t color);

/**
 * @brief Formats a signed millisecond delta as `+s.mmm` / `-s.mmm`.
 * @remarks Shared with the finish bar so a delta reads the same wherever the
 * HUD prints one.
 */
void Cg_RaceHud_FormatDelta(int32_t delta, char *output, size_t size);

/**
 * @brief The saturated in-world trio, plus the two accents that ride with
 * them. The menu golds and greens are a step softer and lose against a lit
 * 3D scene, so the HUD does not share them.
 */
color_t Cg_RaceHud_Gold(void);
color_t Cg_RaceHud_Green(void);
color_t Cg_RaceHud_Cyan(void);
color_t Cg_RaceHud_Warn(void);

/**
 * @return The y coordinate immediately below the top lockup, which anything
 * riding it -- the vote line, the no-checkpoint delta line -- hangs from.
 * @remarks Falls back to the edge inset on frames where the lockup was not
 * drawn, so a caller never lands off-screen.
 */
int32_t Cg_RaceHud_TopStackBottom(void);

/**
 * @brief Resolves the `\\n` escapes a mapper types into a map title.
 * @details `CS_MESSAGE` carries the worldspawn `message` verbatim, and mappers
 * write line breaks into it as the two characters backslash and `n`. Nothing
 * upstream resolves them, so every surface that prints a map title was showing
 * them literally - "Potato jumps\\n\\nby Mako".
 * @param multiline True for a surface that can carry a second line, which
 * resolves the escape to a real newline; false for one that composes its text
 * into a single line, which resolves it to one space instead. A run of escapes
 * collapses to a single separator either way, so a title written with a blank
 * line between its halves does not gain an empty one.
 * @param output The destination, always NUL-terminated.
 */
void Cg_RaceHud_ResolveEscapes(const char *input, char *output, size_t size,
                               bool multiline);

void Cg_RaceHud_Init(void);
bool Cg_RaceHud_ParseMessage(int32_t command);
void Cg_RaceHud_Clear(void);
