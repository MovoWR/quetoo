/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"

#include "CvarCheckbox.h"
#include "CvarSelect.h"
#include "CvarSlider.h"
#include "RaceSlider.h"
#include "SettingsViewController.h"
#include "StrafeHelperPreview.h"

#include <ctype.h>

#define _Class _SettingsViewController

/**
 * @brief The page strip, in strip order.
 */
static const char *settingsPageNames[SETTINGS_PAGE_COUNT] = {
  "Display", "Lighting & shadows", "Effects", "Audio", "Mouse", "HUD",
  "Strafe helper", "Network"
};

typedef enum {
  SettingsPageDisplay,
  SettingsPageLighting,
  SettingsPageEffects,
  SettingsPageAudio,
  SettingsPageMouse,
  SettingsPageHud,
  SettingsPageStrafe,
  SettingsPageNetwork
} SettingsPage;

typedef enum {
  SettingsSectionPreset,
  SettingsSectionWindow,
  SettingsSectionOutput,
  SettingsSectionShadows,
  SettingsSectionWorldLighting,
  SettingsSectionMaterials,
  SettingsSectionPost,
  SettingsSectionAudio,
  SettingsSectionMouse,
  SettingsSectionRaceClarity,
  SettingsSectionStrafePreview,
  SettingsSectionStrafeHelper,
  SettingsSectionStrafeBar,
  SettingsSectionStrafeMarkers,
  SettingsSectionStrafeColors,
  SettingsSectionStrafeSpeed,
  SettingsSectionStrafeMaxSpeed,
  SettingsSectionStrafeAngle,
  SettingsSectionNetwork
} SettingsSection;

typedef struct {
  SettingsPage page;
  const char *label;
} SettingsSectionDescriptor;

/**
 * @brief Sections, in flow order, which is also the page strip order.
 */
static const SettingsSectionDescriptor settingsSections[SETTINGS_SECTION_COUNT] = {
  { SettingsPageDisplay, "Quality preset" },
  { SettingsPageDisplay, "Window & scaling" },
  { SettingsPageDisplay, "Performance & output" },
  { SettingsPageLighting, "Shadows" },
  { SettingsPageLighting, "World lighting" },
  { SettingsPageEffects, "Materials" },
  { SettingsPageEffects, "Post-processing" },
  { SettingsPageAudio, "Audio" },
  { SettingsPageMouse, "Mouse & view" },
  { SettingsPageHud, "Race clarity" },
  { SettingsPageStrafe, "Live preview" },
  { SettingsPageStrafe, "Helper" },
  { SettingsPageStrafe, "Bar" },
  { SettingsPageStrafe, "Markers" },
  { SettingsPageStrafe, "Colours" },
  { SettingsPageStrafe, "Speed readout" },
  { SettingsPageStrafe, "Max speed readout" },
  { SettingsPageStrafe, "Velocity angle readout" },
  { SettingsPageNetwork, "Interface & network" },
};

/**
 * @brief The strafe helper sub-tab strip, in strip order.
 */
static const char *settingsStrafeTabNames[SETTINGS_STRAFE_TAB_COUNT] = {
  "Bar & markers", "Colours", "Readouts"
};

typedef enum {
  SettingsStrafeTabBar,
  SettingsStrafeTabColours,
  SettingsStrafeTabReadouts,

  /**
   * @brief Not a tab: the preview and the preset strip, which every tab shows.
   */
  SettingsStrafeTabAlways
} SettingsStrafeTab;

/**
 * @brief Returns the sub-tab a strafe section belongs to.
 * @details `SettingsStrafeTabAlways` for the preview section and for anything
 * off the strafe page, so the one test in refreshFilter covers both.
 */
static SettingsStrafeTab strafeTabForSection(SettingsSection section) {

  switch (section) {
    case SettingsSectionStrafeHelper:
    case SettingsSectionStrafeBar:
    case SettingsSectionStrafeMarkers:
      return SettingsStrafeTabBar;
    case SettingsSectionStrafeColors:
      return SettingsStrafeTabColours;
    case SettingsSectionStrafeSpeed:
    case SettingsSectionStrafeMaxSpeed:
    case SettingsSectionStrafeAngle:
      return SettingsStrafeTabReadouts;
    default:
      return SettingsStrafeTabAlways;
  }
}

/**
 * @brief Returns true for a section the strafe helper owns.
 * @details Everything on this page is downstream of `cg_race_strafe_helper_draw`: with the helper
 * off, none of it draws, so none of it is live. The preview and the preset
 * strip are the exceptions - they are how the helper gets turned back on.
 */
static bool isStrafeSection(SettingsSection section) {
  return section >= SettingsSectionStrafePreview && section <= SettingsSectionStrafeAngle;
}

typedef enum {
  SettingRowSlider,
  SettingRowSelect,
  SettingRowToggle,
  SettingRowValue,
  SettingRowPreset,
  SettingRowStrafeColors,
  SettingRowStrafePreset,
  SettingRowStrafePreview
} SettingRowKind;

typedef enum {
  SettingRestartLive,
  SettingRestartRenderer,
  SettingRestartSound
} SettingRestartClass;

/**
 * @brief The option rosters a Select row can carry.
 * @details A resolution is two cvars behind one control, so those two are the
 * only Select rows the route drives by hand; the rest are CvarSelects that bind
 * themselves.
 */
typedef enum {
  SettingSelectNone,
  SettingSelectWindowMode,
  SettingSelectFullscreenRes,
  SettingSelectWindowRes,
  SettingSelectSwapInterval,
  SettingSelectAntialias,
  SettingSelectAnisotropy,
  SettingSelectScreenshot,
  SettingSelectShadowTile,
  SettingSelectSampleRate,
  SettingSelectBarStyle,
  SettingSelectUpsFormat,
  SettingSelectUpsColorMode,
  SettingSelectVelocityAngleFormat
} SettingSelectKind;

typedef struct {
  SettingsSection section;
  const char *label;

  /**
   * @brief The cvar this row reads and writes, and its second half for a
   * composite row. NULL for the preset and link rows, which own no cvar.
   */
  const char *var;
  const char *var2;

  SettingRowKind kind;
  SettingRestartClass restart;
  SettingSelectKind select;

  double min, max, step;

  /**
   * @brief Slider readout format. Ignored when `step` is 1 or more, because
   * CvarSlider::updateBindings forces "%g" on those - see makeControl.
   */
  const char *format;

  /**
   * @brief False for a row this build cannot drive, which is drawn disabled and
   * is never written, counted, reverted or captured.
   */
  bool mutable;

  /**
   * @brief The cvar this row is downstream of, or NULL for a row that always
   * applies. Resolved transitively, so a row nested two toggles deep names only
   * its immediate parent.
   * @remarks Declared after `mutable` deliberately: every row above the strafe
   * page initializes this table positionally, and this trailing field is the
   * one those rows leave to the zero initializer.
   */
  const char *dependsOn;
} SettingDescriptor;

/**
 * @brief Every row in the route, grouped by section.
 * @details One table: the page strip, the filter, the modified dot, the row
 * revert and the footer commit pair all read it, so a row cannot appear in the
 * route without also being searchable and revertible.
 */
static const SettingDescriptor settingDescriptors[SETTINGS_ROW_COUNT] = {

  { SettingsSectionPreset, "Preset", NULL, NULL, SettingRowPreset,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, false, NULL },

  { SettingsSectionWindow, "Window mode", "r_fullscreen", NULL, SettingRowSelect,
    SettingRestartRenderer, SettingSelectWindowMode, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionWindow, "Fullscreen resolution", "r_fullscreen_width", "r_fullscreen_height",
    SettingRowSelect, SettingRestartRenderer, SettingSelectFullscreenRes, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionWindow, "Windowed size", "r_window_width", "r_window_height",
    SettingRowSelect, SettingRestartRenderer, SettingSelectWindowRes, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionWindow, "Render scale", "r_framebuffer_scale", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0.25, 2, 0.05, "%0.2f", true, NULL },
  { SettingsSectionWindow, "HUD & 2D scale", "r_draw_scale", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0.5, 3, 0.1, "%0.1f", true, NULL },
  { SettingsSectionWindow, "Vertical sync", "r_swap_interval", NULL, SettingRowSelect,
    SettingRestartLive, SettingSelectSwapInterval, 0, 0, 0, NULL, true, NULL },

  { SettingsSectionOutput, "Frame limiter", "cl_max_fps", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, -1, 250, 1, NULL, true, NULL },
  { SettingsSectionOutput, "Antialiasing", "r_antialias", NULL, SettingRowSelect,
    SettingRestartLive, SettingSelectAntialias, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionOutput, "Texture filtering", "r_anisotropy", NULL, SettingRowSelect,
    SettingRestartLive, SettingSelectAnisotropy, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionOutput, "Screenshot format", "r_screenshot_format", NULL, SettingRowSelect,
    SettingRestartLive, SettingSelectScreenshot, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionOutput, "GPU backend", "r_gpu_driver", NULL, SettingRowValue,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, false, NULL },

  { SettingsSectionShadows, "Dynamic shadows", "r_shadows", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionShadows, "Shadow atlas detail", "r_shadow_tile_size", NULL, SettingRowSelect,
    SettingRestartRenderer, SettingSelectShadowTile, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionShadows, "Shadow distance", "r_shadow_distance", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 4096, 128, NULL, false, NULL },

  { SettingsSectionWorldLighting, "Lighting distance", "r_lighting_distance", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 1024, 8192, 512, NULL, true, NULL },
  { SettingsSectionWorldLighting, "Ambient light", "r_ambient", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 2, 0.1, "%0.1f", true, NULL },
  { SettingsSectionWorldLighting, "Ambient occlusion", "r_ambient_occlusion", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 1, 0.05, "%0.2f", true, NULL },
  { SettingsSectionWorldLighting, "Static-light brightness", "r_modulate", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0.1, 2, 0.05, "%0.2f", true, NULL },

  { SettingsSectionMaterials, "Parallax depth", "r_parallax", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 2, 0.1, "%0.1f", true, NULL },
  { SettingsSectionMaterials, "Parallax self-shadow", "r_parallax_shadow", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 2, 0.1, "%0.1f", true, NULL },
  { SettingsSectionMaterials, "Specular highlights", "r_specularity", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 2, 0.1, "%0.1f", true, NULL },
  { SettingsSectionMaterials, "Surface roughness", "r_roughness", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 2, 0.1, "%0.1f", true, NULL },
  { SettingsSectionMaterials, "Normal-map hardness", "r_hardness", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 2, 0.1, "%0.1f", true, NULL },

  { SettingsSectionPost, "Bloom intensity", "r_bloom", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 8, 0.25, "%0.2f", true, NULL },
  { SettingsSectionPost, "Bloom threshold", "r_bloom_threshold", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 4, 0.1, "%0.1f", true, NULL },
  { SettingsSectionPost, "Bloom softness", "r_bloom_iterations", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 1, 8, 1, NULL, true, NULL },
  { SettingsSectionPost, "Color saturation", "r_saturation", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 2, 0.1, "%0.1f", true, NULL },
  { SettingsSectionPost, "Liquid caustics", "r_caustics", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 1, 0.1, "%0.1f", true, NULL },

  { SettingsSectionAudio, "Master volume", "s_volume", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 1, 0.05, "%0.2f", true, NULL },
  { SettingsSectionAudio, "Effects volume", "s_effects_volume", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 1, 0.05, "%0.2f", true, NULL },
  { SettingsSectionAudio, "Ambient volume", "s_ambient_volume", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 1, 0.05, "%0.2f", true, NULL },
  { SettingsSectionAudio, "Music volume", "s_music_volume", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 1, 0.05, "%0.2f", true, NULL },
  { SettingsSectionAudio, "Doppler intensity", "s_doppler", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 2, 0.1, "%0.1f", true, NULL },
  { SettingsSectionAudio, "Sample rate", "s_rate", NULL, SettingRowSelect,
    SettingRestartSound, SettingSelectSampleRate, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionAudio, "Enhanced effects", "s_effects", NULL, SettingRowToggle,
    SettingRestartSound, SettingSelectNone, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionAudio, "HRTF (headphones)", "s_hrtf", NULL, SettingRowToggle,
    SettingRestartSound, SettingSelectNone, 0, 0, 0, NULL, true, NULL },

  { SettingsSectionMouse, "Sensitivity", "m_sensitivity", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0.5, 10, 0.1, "%0.1f", true, NULL },
  { SettingsSectionMouse, "Zoom sensitivity", "m_sensitivity_zoom", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0.1, 5, 0.1, "%0.1f", true, NULL },
  { SettingsSectionMouse, "Invert mouse", "m_invert", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionMouse, "Interpolate mouse", "m_interpolate", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionMouse, "Zoomed field of view", "cg_fov_zoom", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 40, 110, 1, NULL, true, NULL },
  { SettingsSectionMouse, "FOV interpolation", "cg_fov_interpolate", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 10, 0.5, "%0.1f", true, NULL },

  { SettingsSectionRaceClarity, "Field of view", "cg_fov", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 90, 160, 1, NULL, true, NULL },
  { SettingsSectionRaceClarity, "Weather intensity", "cg_add_weather", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 1, 0.1, "%0.1f", true, NULL },
  { SettingsSectionRaceClarity, "Atmospheric effects", "cg_add_atmospheric", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 1, 0.1, "%0.1f", true, NULL },
  { SettingsSectionRaceClarity, "Light flares", "cg_add_flares", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 1, 0.1, "%0.1f", true, NULL },
  { SettingsSectionRaceClarity, "Impact decals", "cg_add_decals", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 1, 0.1, "%0.1f", true, NULL },

  // The strafe helper page, from the design's "Settings - Strafe helper". Every
  // row here is an `cg_race_strafe_helper_*` cvar `Cg_InitStrafeHelper` registers, and every one of
  // them is a screen-space quantity - which is why the page leads with the
  // preview rather than the roster.
  // No label: the section it opens is already headed "Live preview", and a row
  // captioned "Preview" directly under that eyebrow reads as two headings for
  // one picture. It is not a setting either, so it has nothing to be found by.
  { SettingsSectionStrafePreview, "", NULL, NULL, SettingRowStrafePreview,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, false, NULL },
  { SettingsSectionStrafePreview, "Preset", NULL, NULL, SettingRowStrafePreset,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, false, NULL },

  { SettingsSectionStrafeHelper, "Enable helper", "cg_race_strafe_helper_draw", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionStrafeHelper, "Centre on current angle", "cg_race_strafe_helper_center", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, "cg_race_strafe_helper_draw" },
  { SettingsSectionStrafeHelper, "Opacity", "cg_race_strafe_helper_alpha", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0, 1, 0.05, "%0.2f", true, "cg_race_strafe_helper_draw" },

  { SettingsSectionStrafeBar, "Style", "cg_race_strafe_helper_bar_style", NULL, SettingRowSelect,
    SettingRestartLive, SettingSelectBarStyle, 0, 0, 0, NULL, true, "cg_race_strafe_helper_draw" },
  { SettingsSectionStrafeBar, "Height", "cg_race_strafe_helper_height", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 2, 40, 1, NULL, true, "cg_race_strafe_helper_draw" },
  { SettingsSectionStrafeBar, "Width", "cg_race_strafe_helper_scale", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0.5, 3, 0.05, "%0.2f", true, "cg_race_strafe_helper_draw" },
  { SettingsSectionStrafeBar, "Vertical offset", "cg_race_strafe_helper_y", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, -200, 200, 5, NULL, true, "cg_race_strafe_helper_draw" },

  { SettingsSectionStrafeMarkers, "Centre marker", "cg_race_strafe_helper_centermarker", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, "cg_race_strafe_helper_draw" },
  { SettingsSectionStrafeMarkers, "Centre marker width", "cg_race_strafe_helper_center_width", NULL,
    SettingRowSlider, SettingRestartLive, SettingSelectNone, 1, 8, 1, NULL, true,
    "cg_race_strafe_helper_centermarker" },
  { SettingsSectionStrafeMarkers, "Optimal marker width", "cg_race_strafe_helper_optimal_width", NULL,
    SettingRowSlider, SettingRestartLive, SettingSelectNone, 1, 8, 1, NULL, true, "cg_race_strafe_helper_draw" },
  { SettingsSectionStrafeMarkers, "Outline optimal marker", "cg_race_strafe_helper_optimal_outline", NULL,
    SettingRowToggle, SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, "cg_race_strafe_helper_draw" },

  // No label: the section it opens is already headed "Colours", and the editor
  // captions itself with the target the strip has selected. One row for all
  // three colours - see `settingsStrafeColorTargets`, which `refreshFilter`
  // also reads, so a query for any of the three cvar names the three old rows
  // were found by still lands on this page and this sub-tab.
  { SettingsSectionStrafeColors, "", NULL, NULL, SettingRowStrafeColors,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, "cg_race_strafe_helper_draw" },

  { SettingsSectionStrafeSpeed, "Show speed", "cg_race_strafe_helper_ups", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, "cg_race_strafe_helper_draw" },
  { SettingsSectionStrafeSpeed, "Format", "cg_race_strafe_helper_ups_format", NULL, SettingRowSelect,
    SettingRestartLive, SettingSelectUpsFormat, 0, 0, 0, NULL, true, "cg_race_strafe_helper_ups" },
  { SettingsSectionStrafeSpeed, "Colour mode", "cg_race_strafe_helper_ups_color_mode", NULL, SettingRowSelect,
    SettingRestartLive, SettingSelectUpsColorMode, 0, 0, 0, NULL, true, "cg_race_strafe_helper_ups" },
  { SettingsSectionStrafeSpeed, "Scale", "cg_race_strafe_helper_ups_scale", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0.5, 3, 0.05, "%0.2f", true, "cg_race_strafe_helper_ups" },
  { SettingsSectionStrafeSpeed, "Vertical offset", "cg_race_strafe_helper_ups_y", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, -200, 200, 5, NULL, true, "cg_race_strafe_helper_ups" },
  { SettingsSectionStrafeSpeed, "Shadow", "cg_race_strafe_helper_ups_shadow", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, "cg_race_strafe_helper_ups" },
  { SettingsSectionStrafeSpeed, "Hide at zero speed", "cg_race_strafe_helper_ups_hide_zero", NULL,
    SettingRowToggle, SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, "cg_race_strafe_helper_ups" },
  { SettingsSectionStrafeSpeed, "Include vertical speed", "cg_race_strafe_helper_ups_3d", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, "cg_race_strafe_helper_ups" },

  { SettingsSectionStrafeMaxSpeed, "Show max speed", "cg_race_strafe_helper_max_speed", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, "cg_race_strafe_helper_draw" },
  { SettingsSectionStrafeMaxSpeed, "Scale", "cg_race_strafe_helper_max_speed_scale", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0.5, 3, 0.05, "%0.2f", true, "cg_race_strafe_helper_max_speed" },
  { SettingsSectionStrafeMaxSpeed, "Vertical offset", "cg_race_strafe_helper_max_speed_y", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, -200, 200, 5, NULL, true, "cg_race_strafe_helper_max_speed" },
  { SettingsSectionStrafeMaxSpeed, "Shadow", "cg_race_strafe_helper_max_speed_shadow", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, "cg_race_strafe_helper_max_speed" },

  { SettingsSectionStrafeAngle, "Show velocity angle", "cg_race_strafe_helper_velocity_angle", NULL,
    SettingRowToggle, SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, "cg_race_strafe_helper_draw" },
  { SettingsSectionStrafeAngle, "Format", "cg_race_strafe_helper_velocity_angle_format", NULL, SettingRowSelect,
    SettingRestartLive, SettingSelectVelocityAngleFormat, 0, 0, 0, NULL, true,
    "cg_race_strafe_helper_velocity_angle" },
  { SettingsSectionStrafeAngle, "Scale", "cg_race_strafe_helper_velocity_angle_scale", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 0.5, 3, 0.05, "%0.2f", true, "cg_race_strafe_helper_velocity_angle" },
  { SettingsSectionStrafeAngle, "Vertical offset", "cg_race_strafe_helper_velocity_angle_y", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, -200, 200, 5, NULL, true, "cg_race_strafe_helper_velocity_angle" },
  { SettingsSectionStrafeAngle, "Shadow", "cg_race_strafe_helper_velocity_angle_shadow", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, "cg_race_strafe_helper_velocity_angle" },

  { SettingsSectionNetwork, "Frame counters", "cl_draw_counters", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionNetwork, "Net graph", "cl_draw_net_graph", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionNetwork, "Media keys", "cl_capture_media_keys", NULL, SettingRowToggle,
    SettingRestartLive, SettingSelectNone, 0, 0, 0, NULL, true, NULL },
  { SettingsSectionNetwork, "Connection timeout", "cl_timeout", NULL, SettingRowSlider,
    SettingRestartLive, SettingSelectNone, 5, 60, 1, NULL, true, NULL },
};

/**
 * @brief The quality preset segments. `Custom` is the fifth, and is a readout
 * rather than an action: it is what the strip shows when the cvars match no
 * preset, and there is nothing for clicking it to set.
 */
static const char *settingsPresetNames[SETTINGS_PRESET_COUNT] = {
  "Low", "Medium", "High", "Highest", "Custom"
};

typedef struct {
  int32_t shadows;
  int32_t shadowTileSize;
  int32_t lightingDistance;
  float parallax;
  float parallaxShadow;
  float caustics;
  float addWeather;
  float addAtmospheric;
} QualityPreset;

/**
 * @brief The cvars a preset drives, and the four presets themselves.
 */
static const QualityPreset qualityPresets[] = {
  [0] = {
    .shadows = 0, .shadowTileSize = 128,
    .lightingDistance = 1024, .parallax = 0, .parallaxShadow = 0,
    .caustics = 0, .addWeather = 0, .addAtmospheric = 0,
  },
  [1] = {
    .shadows = 1, .shadowTileSize = 128,
    .lightingDistance = 2048, .parallax = 0, .parallaxShadow = 0,
    .caustics = 0, .addWeather = 1, .addAtmospheric = 1,
  },
  [2] = {
    .shadows = 1, .shadowTileSize = 256,
    .lightingDistance = 4096, .parallax = 1, .parallaxShadow = 0,
    .caustics = 1, .addWeather = 1, .addAtmospheric = 1,
  },
  [3] = {
    .shadows = 1, .shadowTileSize = 512,
    .lightingDistance = 8192, .parallax = 1, .parallaxShadow = 1,
    .caustics = 1, .addWeather = 1, .addAtmospheric = 1,
  },
};

static char screenshotJpg[] = "jpg";
static char screenshotPng[] = "png";
static char screenshotTga[] = "tga";

/**
 * @brief The string values the strafe helper's enumerated cvars accept.
 * @details Option values on a CvarSelect with `expectsStringValue` are written
 * to the cvar verbatim, so these are the helper's own spellings rather than the
 * labels beside them - `Cg_StrafeHelper_UpsText` matches "prefix", not
 * "prefixed", and falls back to plain for anything it does not recognise.
 */
static char barStyleGradient[] = "gradient";
static char barStyleSolid[] = "solid";
static char barStyleOutline[] = "outline";
static char barStyleMinimal[] = "minimal";

static char upsFormatPlain[] = "plain";
static char upsFormatSuffix[] = "suffix";
static char upsFormatPrefix[] = "prefix";

static char upsColorDynamic[] = "dynamic";
static char upsColorStatic[] = "static";
static char upsColorRainbow[] = "rainbow";
static char upsColorThreshold[] = "threshold";
static char upsColorGradient[] = "gradient";
static char upsColorStrafing[] = "strafing";

static char velocityAngleDiff[] = "diff";
static char velocityAngleAbsolute[] = "absolute";

#pragma mark - Strafe helper presets

/**
 * @brief The strafe helper preset strip, from the design's "Live preview".
 * @details `Custom` is the fifth, and is a readout rather than an action, on
 * the same terms as the quality strip's: it is what the strip shows when the
 * cvars spell no preset, and there is nothing for clicking it to set. A segment
 * rather than a label beside the strip, because it is the state the strip is
 * in - a caption floating off the end of the control cannot be read as the
 * selected one, which is exactly the one the player needs to find.
 */
static const char *settingsStrafePresetNames[SETTINGS_STRAFE_PRESET_COUNT] = {
  "Off", "Minimal", "CGAZ classic", "Everything", "Custom"
};

/**
 * @brief The cvars a strafe preset spells out.
 * @details A preset is a whole state, not a patch: applying one restores every
 * `cg_race_strafe_helper_*` row to what the client ships and then writes these over the top. That
 * is what makes the strip readable in both directions - a preset the player can
 * click is also a preset the strip can recognise, without "Minimal plus one
 * edited offset" reading as Minimal.
 */
typedef struct {
  int32_t draw;
  const char *barStyle;
  int32_t center;
  float height;
  float scale;
  float alpha;
  int32_t centerMarker;
  int32_t optimalOutline;
  int32_t ups;
  int32_t ups3d;
  int32_t maxSpeed;
  int32_t velocityAngle;
} StrafePreset;

static const StrafePreset strafePresets[] = {
  [0] = { // Off: the shipped state with the helper switched off.
    .draw = 0, .barStyle = "gradient", .center = 1, .height = 15.f, .scale = 1.5f,
    .alpha = 0.5f, .centerMarker = 1, .optimalOutline = 1, .ups = 1, .ups3d = 0,
    .maxSpeed = 0, .velocityAngle = 0,
  },
  [1] = { // Minimal: a solid bar and the speed, nothing else.
    .draw = 1, .barStyle = "solid", .center = 1, .height = 8.f, .scale = 1.2f,
    .alpha = 0.4f, .centerMarker = 1, .optimalOutline = 0, .ups = 1, .ups3d = 0,
    .maxSpeed = 0, .velocityAngle = 0,
  },
  [2] = { // CGAZ classic: the shipped defaults plus the velocity angle.
    .draw = 1, .barStyle = "gradient", .center = 1, .height = 15.f, .scale = 1.5f,
    .alpha = 0.5f, .centerMarker = 1, .optimalOutline = 1, .ups = 1, .ups3d = 0,
    .maxSpeed = 0, .velocityAngle = 1,
  },
  [3] = { // Everything: every readout on, and a bar sized to be read at speed.
    .draw = 1, .barStyle = "gradient", .center = 1, .height = 22.f, .scale = 2.f,
    .alpha = 0.7f, .centerMarker = 1, .optimalOutline = 1, .ups = 1, .ups3d = 1,
    .maxSpeed = 1, .velocityAngle = 1,
  },
};

#pragma mark - Strafe helper colours

/**
 * @brief The colour cvars the Colours sub-tab edits, in strip order.
 * @details `paints` is the editor's own copy rather than the cvar's
 * description: a colour cvar's name says what it is, not what it draws, and
 * "Inner edge of each zone" is the thing a player looking at the bar is
 * actually trying to find. `element` ties each target to the preview above, so
 * selecting a chip isolates the same thing the picker is editing.
 */
static const struct {
  const char *label;
  const char *paints;
  const char *var;
  const char *dependency;
  sh_color_element_t element;
} settingsStrafeColorTargets[SETTINGS_STRAFE_COLOR_COUNT] = {
  { "Accelerating zone", "Bar fill, both zones", "cg_race_strafe_helper_color_accelerating",
    "cg_race_strafe_helper_draw", SH_COLOR_ACCELERATING },
  { "Optimal angle", "Inner edge of each zone", "cg_race_strafe_helper_color_optimal",
    "cg_race_strafe_helper_draw", SH_COLOR_OPTIMAL },
  { "Centre marker", "Your current view angle", "cg_race_strafe_helper_color_centermarker",
    "cg_race_strafe_helper_centermarker", SH_COLOR_CENTER_MARKER },
};

#pragma mark - Row state

/**
 * @brief Collects the cvars a row stands for, and returns how many there are.
 * @details One for most rows, two for a resolution, three for the Colours
 * editor. Everything downstream - the modified dot, the row revert, the opening
 * capture and the footer's dirty count - reads the row through this, so a row
 * that owns more than one cvar is revertible without any of them special-casing
 * how many.
 */
static size_t settingRowVars(size_t row, const char *vars[SETTINGS_ROW_VAR_COUNT]) {

  const SettingDescriptor *descriptor = &settingDescriptors[row];

  for (size_t i = 0; i < SETTINGS_ROW_VAR_COUNT; i++) {
    vars[i] = NULL;
  }

  if (!descriptor->mutable) {
    return 0;
  }

  if (descriptor->kind == SettingRowStrafeColors) {
    for (size_t i = 0; i < SETTINGS_STRAFE_COLOR_COUNT; i++) {
      vars[i] = settingsStrafeColorTargets[i].var;
    }
    return SETTINGS_STRAFE_COLOR_COUNT;
  }

  if (descriptor->var == NULL ||
      (descriptor->kind != SettingRowSlider &&
       descriptor->kind != SettingRowSelect &&
       descriptor->kind != SettingRowToggle)) {
    return 0;
  }

  vars[0] = descriptor->var;
  if (descriptor->var2 == NULL) {
    return 1;
  }

  vars[1] = descriptor->var2;
  return 2;
}

/**
 * @brief Returns the client-shipped default for a cvar, or the empty string.
 * @remarks GetCvar never registers, so this stays safe for the row this build
 * draws but does not own - `r_shadow_distance` above.
 */
static const char *cvarDefault(const char *var) {

  const cvar_t *cvar = var ? cgi.GetCvar(var) : NULL;
  return cvar && cvar->default_string ? cvar->default_string : "";
}

/**
 * @brief Returns the current cvar spelling, or the empty string.
 */
static const char *cvarString(const char *var) {

  const char *value = var ? cgi.GetCvarString(var) : NULL;
  return value ? value : "";
}

/**
 * @brief Returns true when the row differs from what the client ships.
 * @remarks This drives the row dot and the row revert, not the footer commit
 * pair - that one measures against route entry instead.
 */
static bool isRowModified(size_t row) {

  const char *vars[SETTINGS_ROW_VAR_COUNT];
  const size_t count = settingRowVars(row, vars);

  for (size_t i = 0; i < count; i++) {
    if (q_strcmp(cvarString(vars[i]), cvarDefault(vars[i]))) {
      return true;
    }
  }

  return false;
}

/**
 * @brief Returns true when the row differs from the value it had on entry.
 */
static bool isRowDirty(const SettingsViewController *self, size_t row) {

  const char *vars[SETTINGS_ROW_VAR_COUNT];
  const size_t count = settingRowVars(row, vars);

  for (size_t i = 0; i < count; i++) {
    if (q_strcmp(cvarString(vars[i]), self->openingValues[row][i])) {
      return true;
    }
  }

  return false;
}

static void captureOpeningValues(SettingsViewController *self) {

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {

    const char *vars[SETTINGS_ROW_VAR_COUNT];
    const size_t count = settingRowVars(row, vars);

    for (size_t i = 0; i < SETTINGS_ROW_VAR_COUNT; i++) {
      self->openingValues[row][i][0] = '\0';
    }

    for (size_t i = 0; i < count; i++) {
      q_strlcpy(self->openingValues[row][i], cvarString(vars[i]),
                SETTINGS_VALUE_SIZE);
    }
  }
}

/**
 * @brief Restores one row to what the client ships.
 */
static void restoreRowDefaults(size_t row) {

  const char *vars[SETTINGS_ROW_VAR_COUNT];
  const size_t count = settingRowVars(row, vars);

  for (size_t i = 0; i < count; i++) {
    cgi.SetCvarString(vars[i], cvarDefault(vars[i]));
  }
}

/**
 * @brief Restores one row to the value it held when the route was entered.
 */
static void restoreRowOpening(const SettingsViewController *self, size_t row) {

  const char *vars[SETTINGS_ROW_VAR_COUNT];
  const size_t count = settingRowVars(row, vars);

  for (size_t i = 0; i < count; i++) {
    cgi.SetCvarString(vars[i], self->openingValues[row][i]);
  }
}

#pragma mark - Quality preset

static void applyQualityPreset(const QualityPreset *preset) {

  cgi.SetCvarInteger("r_shadows", preset->shadows);
  cgi.SetCvarInteger("r_shadow_tile_size", preset->shadowTileSize);
  cgi.SetCvarInteger("r_lighting_distance", preset->lightingDistance);
  cgi.SetCvarValue("r_parallax", preset->parallax);
  cgi.SetCvarValue("r_parallax_shadow", preset->parallaxShadow);
  cgi.SetCvarValue("r_caustics", preset->caustics);
  cgi.SetCvarValue("cg_add_weather", preset->addWeather);
  cgi.SetCvarValue("cg_add_atmospheric", preset->addAtmospheric);
}

/**
 * @brief Returns the preset the current cvars spell, or -1 for none of them.
 */
static intptr_t detectQualityPreset(void) {

  const QualityPreset current = {
    .shadows = cgi.GetCvarInteger("r_shadows"),
    .shadowTileSize = cgi.GetCvarInteger("r_shadow_tile_size"),
    .lightingDistance = cgi.GetCvarInteger("r_lighting_distance"),
    .parallax = cgi.GetCvarValue("r_parallax"),
    .parallaxShadow = cgi.GetCvarValue("r_parallax_shadow"),
    .caustics = cgi.GetCvarValue("r_caustics"),
    .addWeather = cgi.GetCvarValue("cg_add_weather"),
    .addAtmospheric = cgi.GetCvarValue("cg_add_atmospheric"),
  };

  for (size_t i = 0; i < lengthof(qualityPresets); i++) {
    const QualityPreset *preset = &qualityPresets[i];
    if (current.shadows == preset->shadows &&
        current.shadowTileSize == preset->shadowTileSize &&
        current.lightingDistance == preset->lightingDistance &&
        fabsf(current.parallax - preset->parallax) < 0.001f &&
        fabsf(current.parallaxShadow - preset->parallaxShadow) < 0.001f &&
        fabsf(current.caustics - preset->caustics) < 0.001f &&
        fabsf(current.addWeather - preset->addWeather) < 0.001f &&
        fabsf(current.addAtmospheric - preset->addAtmospheric) < 0.001f) {
      return (intptr_t) i;
    }
  }

  return -1;
}

#pragma mark - Strafe helper preset

/**
 * @brief Restores every strafe row to what the client ships, then spells the
 * preset over the top.
 * @remarks The reset is the point: `Minimal` means the same thing whichever
 * page state it is clicked from, which is also what lets `detectStrafePreset`
 * recognise it afterwards.
 */
static void applyStrafePreset(const StrafePreset *preset) {

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {
    if (isStrafeSection(settingDescriptors[row].section)) {
      restoreRowDefaults(row);
    }
  }

  cgi.SetCvarInteger("cg_race_strafe_helper_draw", preset->draw);
  cgi.SetCvarString("cg_race_strafe_helper_bar_style", preset->barStyle);
  cgi.SetCvarInteger("cg_race_strafe_helper_center", preset->center);
  cgi.SetCvarValue("cg_race_strafe_helper_height", preset->height);
  cgi.SetCvarValue("cg_race_strafe_helper_scale", preset->scale);
  cgi.SetCvarValue("cg_race_strafe_helper_alpha", preset->alpha);
  cgi.SetCvarInteger("cg_race_strafe_helper_centermarker", preset->centerMarker);
  cgi.SetCvarInteger("cg_race_strafe_helper_optimal_outline", preset->optimalOutline);
  cgi.SetCvarInteger("cg_race_strafe_helper_ups", preset->ups);
  cgi.SetCvarInteger("cg_race_strafe_helper_ups_3d", preset->ups3d);
  cgi.SetCvarInteger("cg_race_strafe_helper_max_speed", preset->maxSpeed);
  cgi.SetCvarInteger("cg_race_strafe_helper_velocity_angle", preset->velocityAngle);
}

/**
 * @brief Returns the strafe preset the current cvars spell, or -1 for none.
 * @details Read from the cvars rather than from the last click, the same way
 * `detectQualityPreset` is: the console and the row controls can both spell a
 * preset without anyone touching the strip, and a strip that only remembered
 * its last click would keep claiming `Minimal` after a row moved underneath it.
 * @remarks The rows this compares are exactly the rows `applyStrafePreset`
 * writes. Rows outside that set - the offsets, the marker widths, the colours -
 * are deliberately not compared: they are reset by an apply, so a preset that
 * demanded them too would still match, but a player who then nudged one offset
 * would lose the whole strip rather than one row's worth of it.
 */
static intptr_t detectStrafePreset(void) {

  for (size_t i = 0; i < lengthof(strafePresets); i++) {

    const StrafePreset *preset = &strafePresets[i];

    if (cgi.GetCvarInteger("cg_race_strafe_helper_draw") == preset->draw &&
        !q_strcmp(cvarString("cg_race_strafe_helper_bar_style"), preset->barStyle) &&
        cgi.GetCvarInteger("cg_race_strafe_helper_center") == preset->center &&
        fabsf(cgi.GetCvarValue("cg_race_strafe_helper_height") - preset->height) < 0.001f &&
        fabsf(cgi.GetCvarValue("cg_race_strafe_helper_scale") - preset->scale) < 0.001f &&
        fabsf(cgi.GetCvarValue("cg_race_strafe_helper_alpha") - preset->alpha) < 0.001f &&
        cgi.GetCvarInteger("cg_race_strafe_helper_centermarker") == preset->centerMarker &&
        cgi.GetCvarInteger("cg_race_strafe_helper_optimal_outline") == preset->optimalOutline &&
        cgi.GetCvarInteger("cg_race_strafe_helper_ups") == preset->ups &&
        cgi.GetCvarInteger("cg_race_strafe_helper_ups_3d") == preset->ups3d &&
        cgi.GetCvarInteger("cg_race_strafe_helper_max_speed") == preset->maxSpeed &&
        cgi.GetCvarInteger("cg_race_strafe_helper_velocity_angle") == preset->velocityAngle) {
      return (intptr_t) i;
    }
  }

  return -1;
}

#pragma mark - Row dependencies

/**
 * @brief Returns the row that owns a cvar, or SETTINGS_ROW_COUNT.
 */
static size_t rowForVar(const char *var) {

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {
    if (settingDescriptors[row].var && !q_strcmp(settingDescriptors[row].var, var)) {
      return row;
    }
  }

  return SETTINGS_ROW_COUNT;
}

/**
 * @brief Returns true when a row's dependency chain is satisfied all the way up.
 * @details `cg_race_strafe_helper_ups_scale` names only `cg_race_strafe_helper_ups`, and `cg_race_strafe_helper_ups` names `cg_race_strafe_helper_draw`, so
 * switching the helper off dims the whole page in one write rather than
 * requiring every nested row to repeat the parent it already has. The depth cap
 * is a cycle guard, not a limit anybody is expected to reach.
 */
static bool isDependencySatisfied(const char *var, const int32_t depth) {

  if (var == NULL) {
    return true;
  }

  if (cgi.GetCvarInteger(var) == 0) {
    return false;
  }

  if (depth <= 0) {
    return true;
  }

  const size_t owner = rowForVar(var);
  if (owner == SETTINGS_ROW_COUNT) {
    return true;
  }

  return isDependencySatisfied(settingDescriptors[owner].dependsOn, depth - 1);
}

/**
 * @brief Returns true for a row whose parent toggle is currently off.
 */
static bool isRowInert(size_t row) {
  return !isDependencySatisfied(settingDescriptors[row].dependsOn, 4);
}

#pragma mark - Refresh

static bool containsIgnoringCase(const char *text, const char *query) {

  if (*query == '\0') {
    return true;
  }
  if (text == NULL) {
    return false;
  }

  for (const char *start = text; *start; start++) {
    const char *a = start, *b = query;
    while (*a && *b && tolower((unsigned char) *a) == tolower((unsigned char) *b)) {
      a++;
      b++;
    }
    if (*b == '\0') {
      return true;
    }
  }

  return false;
}

static void invalidateLayoutChain(View *view) {

  while (view) {
    view->needsLayout = true;
    view = view->superview;
  }
}

static void setControlFlag(Control *control, ControlState flag, bool enabled) {

  const unsigned int previous = control->state;
  if (enabled) {
    control->state |= flag;
  } else {
    control->state &= ~flag;
  }
  if (control->state != previous) {
    $(control, stateDidChange);
  }
}

/**
 * @brief Sets a Text only when the string has actually changed.
 * @details The footer and the readouts are recomputed on pointer motion, and
 * Text::setText re-renders the string into a texture every time it is called.
 */
static bool setTextIfChanged(Text *text, const char *string) {

  if (q_strcmp(text->text ? text->text : "", string ? string : "")) {
    $(text, setText, string);
    return true;
  }

  return false;
}

/**
 * @brief Rewrites the readouts a printf format cannot express.
 * @details Slider draws its own label from a single `labelFormat`, so the two
 * cl_max_fps sentinels - negative for uncapped, zero for the display's refresh
 * rate - have to be written over the top after the Slider has formatted itself.
 */
static void refreshSliderReadouts(SettingsViewController *self) {

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {

    if (settingDescriptors[row].kind != SettingRowSlider ||
        q_strcmp(settingDescriptors[row].var, "cl_max_fps")) {
      continue;
    }

    const Slider *slider = (const Slider *) self->rowControls[row];
    if (slider == NULL) {
      continue;
    }

    const char *sentinel = slider->value < 0.0 ? "uncapped"
      : slider->value < 1.0 ? "refresh" : NULL;

    if (sentinel) {
      setTextIfChanged(slider->label, sentinel);
    }
  }
}

/**
 * @brief Paints the preset strip from the cvars rather than from the last
 * click: a preset can also be spelled by editing its rows one at a time.
 */
static void refreshPresetSegments(SettingsViewController *self) {

  const intptr_t detected = detectQualityPreset();
  const size_t selected = detected >= 0 ? (size_t) detected : SETTINGS_PRESET_COUNT - 1;

  for (size_t i = 0; i < SETTINGS_PRESET_COUNT; i++) {
    setControlFlag((Control *) self->presetButtons[i], ControlStateSelected, i == selected);
  }
}

/**
 * @brief Paints the strafe strip from the cvars, for the same reason the
 * quality strip is painted from them.
 */
static void refreshStrafePresetSegments(SettingsViewController *self) {

  const intptr_t detected = detectStrafePreset();
  const size_t selected = detected >= 0 ? (size_t) detected
                                        : SETTINGS_STRAFE_PRESET_COUNT - 1;

  for (size_t i = 0; i < SETTINGS_STRAFE_PRESET_COUNT; i++) {
    setControlFlag((Control *) self->strafePresetButtons[i], ControlStateSelected,
                   i == selected);
  }

  // The strip now says "Custom" itself, so the caption beside it is left with
  // the one thing the strip cannot say: that a hand-edited page is inert
  // anyway, because `cg_race_strafe_helper_draw` is zero. A detected preset
  // already carries that - preset zero *is* the helper switched off.
  setTextIfChanged(self->strafePresetStatus->text,
                   detected < 0 && cgi.GetCvarInteger("cg_race_strafe_helper_draw") == 0
                     ? "Helper off" : "");
}

/**
 * @brief Splits an "R G B A" cvar string into its four channels.
 * @details Alpha is optional and defaults to opaque: `cg_race_strafe_helper_color_*`
 * is free-form, and a config that writes three numbers is as valid as one that
 * writes four.
 */
static void parseColorString(const char *value, int32_t *rgba) {

  rgba[0] = rgba[1] = rgba[2] = rgba[3] = 255;
  sscanf(value ? value : "", "%d %d %d %d", &rgba[0], &rgba[1], &rgba[2], &rgba[3]);

  for (size_t i = 0; i < 4; i++) {
    rgba[i] = (int32_t) Clampf((float) rgba[i], 0, 255);
  }
}

/**
 * @brief Converts 0-255 RGB into the picker's three axes.
 * @param hsv Hue in degrees, then saturation and value, each in 0..1.
 */
static void colorToHsv(const int32_t *rgba, double *hsv) {

  const double r = rgba[0] / 255.0, g = rgba[1] / 255.0, b = rgba[2] / 255.0;

  const double max = r > g ? (r > b ? r : b) : (g > b ? g : b);
  const double min = r < g ? (r < b ? r : b) : (g < b ? g : b);
  const double chroma = max - min;

  double hue = 0.0;
  if (chroma > 0.0) {
    if (max == r) {
      hue = fmod((g - b) / chroma, 6.0) * 60.0;
    } else if (max == g) {
      hue = ((b - r) / chroma + 2.0) * 60.0;
    } else {
      hue = ((r - g) / chroma + 4.0) * 60.0;
    }
  }

  hsv[0] = hue < 0.0 ? hue + 360.0 : hue;
  hsv[1] = max == 0.0 ? 0.0 : chroma / max;
  hsv[2] = max;
}

/**
 * @brief Converts the picker's three axes back into 0-255 RGB.
 */
static void hsvToColor(const double *hsv, int32_t *rgb) {

  const double hue = fmod(fmod(hsv[0], 360.0) + 360.0, 360.0);
  const double chroma = hsv[2] * hsv[1];
  const double second = chroma * (1.0 - fabs(fmod(hue / 60.0, 2.0) - 1.0));
  const double base = hsv[2] - chroma;

  double r, g, b;
  if (hue < 60.0) {
    r = chroma, g = second, b = 0.0;
  } else if (hue < 120.0) {
    r = second, g = chroma, b = 0.0;
  } else if (hue < 180.0) {
    r = 0.0, g = chroma, b = second;
  } else if (hue < 240.0) {
    r = 0.0, g = second, b = chroma;
  } else if (hue < 300.0) {
    r = second, g = 0.0, b = chroma;
  } else {
    r = chroma, g = 0.0, b = second;
  }

  rgb[0] = (int32_t) Clampf(roundf((float) ((r + base) * 255.0)), 0, 255);
  rgb[1] = (int32_t) Clampf(roundf((float) ((g + base) * 255.0)), 0, 255);
  rgb[2] = (int32_t) Clampf(roundf((float) ((b + base) * 255.0)), 0, 255);
}

/**
 * @brief Converts the alpha axis' percentage into the cvar's 0-255 channel.
 * @details The axis runs 0-100 rather than 0-255 because `Slider::setLabelFormat`
 * is printf over the slider's own value - a 0-255 axis cannot print "50%", and
 * the number a player needs here is a proportion, not a byte. The round trip is
 * stable on every value the client ships (128 to 50 to 128, 255 to 100 to 255)
 * and costs at most one 8-bit step elsewhere, which is not visible through a
 * bar already drawn at half opacity.
 */
static int32_t alphaFromAxis(const double percent) {
  return (int32_t) Clampf((float) lround(percent * 2.55), 0, 255);
}

static double alphaToAxis(const int32_t alpha) {
  return (double) lround(alpha / 2.55);
}

/**
 * @brief Paints one View from a colour the stylesheet cannot name.
 * @details Assigning `backgroundColor` alone is not enough: the framework's own
 * stylesheet opens with `* { background-color: transparent; }`, which matches
 * every View, so the next applyStyle pass binds the attribute back to
 * transparent. The inline style is the dialect's escape hatch - View::applyTheme
 * merges it over the computed style - so the swatch declares its own colour and
 * survives. Guarded on a change because this runs on pointer motion, and every
 * addColorAttribute boxes a new value into the style dictionary.
 */
static void paintSwatch(View *view, const SDL_Color color) {

  if (memcmp(&view->backgroundColor, &color, sizeof(color))) {
    view->backgroundColor = color;
    $(view->style, addColorAttribute, "background-color", &color);
  }
}

/**
 * @brief Returns the opacity the selected colour actually reaches the screen at.
 * @details Every one of the three is multiplied by `cg_race_strafe_helper_alpha`
 * before it is drawn - `Cg_StrafeHelper_ElementColor` passes `helper_alpha` for
 * all three elements. The shipped accelerating green is a 50% alpha under a 50%
 * opacity, which is 25% on screen, and that product is not derivable from
 * anything else the menu shows.
 */
static double strafeColorEffectiveAlpha(const int32_t alpha) {

  return (alpha / 255.0) *
    Clampf(cgi.GetCvarValue("cg_race_strafe_helper_alpha"), 0.f, 1.f);
}

/**
 * @brief Repaints the Colours editor: the strip, the header, the axes when they
 * have drifted, and the meta line.
 * @details The sliders are repositioned only when the colour they express is no
 * longer the colour the cvar holds. Recomputing them every pass would be lossy:
 * hue is undefined on a grey and saturation is undefined on black, so an axis
 * the player deliberately dragged to zero would drag the other two back to zero
 * with it and neither could be moved off again. Comparing colours rather than
 * axes leaves each thumb where it was put for as long as it still spells the
 * right answer - and alpha is compared with them now that it is one of the four,
 * or a value set from the console would never reach its thumb.
 */
static void refreshStrafeColors(SettingsViewController *self) {

  for (size_t i = 0; i < SETTINGS_STRAFE_COLOR_COUNT; i++) {

    int32_t rgba[4];
    parseColorString(cgi.GetCvarString(settingsStrafeColorTargets[i].var), rgba);

    paintSwatch(self->strafeColorChipSwatches[i],
                MakeColor((uint8_t) rgba[0], (uint8_t) rgba[1],
                          (uint8_t) rgba[2], (uint8_t) rgba[3]));

    setControlFlag((Control *) self->strafeColorChips[i], ControlStateSelected,
                   i == self->strafeColorTarget);
  }

  const size_t target = self->strafeColorTarget;

  setTextIfChanged(self->strafeColorName->text, settingsStrafeColorTargets[target].label);
  setTextIfChanged(self->strafeColorPaints->text, settingsStrafeColorTargets[target].paints);

  int32_t rgba[4];
  parseColorString(cgi.GetCvarString(settingsStrafeColorTargets[target].var), rgba);

  double hsv[3];
  for (size_t i = 0; i < 3; i++) {
    hsv[i] = self->strafeColorSliders[i]->value;
  }

  int32_t expressed[3];
  hsvToColor(hsv, expressed);

  if (expressed[0] != rgba[0] || expressed[1] != rgba[1] || expressed[2] != rgba[2] ||
      alphaFromAxis(self->strafeColorSliders[3]->value) != rgba[3]) {

    colorToHsv(rgba, hsv);

    // Slider::setValue notifies its delegate, which would write the cvar
    // straight back - and what it would write is this quantized round trip,
    // not the string the player's config actually holds.
    const bool wasRefreshing = self->refreshing;
    self->refreshing = true;

    for (size_t i = 0; i < 3; i++) {
      $(self->strafeColorSliders[i], setValue, hsv[i]);
    }
    $(self->strafeColorSliders[3], setValue, alphaToAxis(rgba[3]));

    self->refreshing = wasRefreshing;
  }

  const double effective = strafeColorEffectiveAlpha(rgba[3]);

  setTextIfChanged(self->strafeColorMeta->text,
                   va("%s   %.0f%% on screen",
                       cvarString(settingsStrafeColorTargets[target].var),
                      effective * 100.0));

  // The whole HSV space is reachable, which is what a free picker buys and also
  // what it costs: a value low enough, or an alpha thin enough, and the bar is
  // there but cannot be seen over a lit scene. Stated rather than prevented -
  // and emptied rather than hidden, so the line cannot change height under a
  // thumb that is still moving.
  const double value = rgba[0] > rgba[1]
    ? (rgba[0] > rgba[2] ? rgba[0] : rgba[2])
    : (rgba[1] > rgba[2] ? rgba[1] : rgba[2]);

  setTextIfChanged(self->strafeColorWarning->text,
                   (value / 255.0) * effective < 0.22 ? "Hard to see over a lit scene" : "");

  // The live preview sits directly above and draws these same three colours, so
  // the selection points into it rather than the editor carrying a sample of
  // its own. Gated on the section actually being on screen, which is one test
  // for the page, the sub-tab and a running filter query all at once - the
  // preview is every sub-tab's readout, and only this one is editing it.
  const bool isolating = !self->sectionViews[SettingsSectionStrafeColors]->hidden;

  ((StrafeHelperPreview *) self->strafePreview)->isolated =
    isolating ? settingsStrafeColorTargets[target].element : SH_COLOR_NONE;

  setTextIfChanged(self->strafeEditing->text,
                   isolating ? va("Editing · %s", settingsStrafeColorTargets[target].label) : "");
  self->strafeEditing->view.hidden = !isolating;
}

/**
 * @brief Dims and disables the rows whose parent toggle is off.
 * @details Dimmed rather than hidden: the page is a fixed roster and a section
 * that changed height every time a toggle moved would make the rows below it
 * jump under the pointer. Guarded on a change for the usual reason - this runs
 * on pointer motion, and a class name re-applies the stylesheet to the row's
 * whole subtree.
 */
static void refreshRowDependencies(SettingsViewController *self) {

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {

    const bool inert = isRowInert(row);
    if (inert == self->rowInert[row]) {
      continue;
    }

    self->rowInert[row] = inert;

    if (inert) {
      $(self->rowViews[row], addClassName, "inert");
    } else {
      $(self->rowViews[row], removeClassName, "inert");
    }

    View *control = self->rowControls[row];
    if ($((Object *) control, isKindOfClass, _Control())) {
      setControlFlag((Control *) control, ControlStateDisabled, inert);
    }
  }
}

/**
 * @brief Dims the chips and the picker whose target the helper is not drawing.
 * @details A chip whose dependency is unmet dims but stays selectable: the
 * colour is still worth seeing, and the centre marker is switched on one
 * sub-tab over rather than here. The picker follows the selected chip, so what
 * dims is the editor for a thing that is currently not drawn - not the strip
 * that reaches the other two.
 */
static void refreshStrafeColorDependencies(SettingsViewController *self) {

  for (size_t i = 0; i < SETTINGS_STRAFE_COLOR_COUNT; i++) {

    const bool inert = !isDependencySatisfied(settingsStrafeColorTargets[i].dependency, 4);
    View *chip = (View *) self->strafeColorChips[i];

    if (inert != $(chip, hasClassName, "inert")) {
      if (inert) {
        $(chip, addClassName, "inert");
      } else {
        $(chip, removeClassName, "inert");
      }
    }
  }

  const bool inert =
    !isDependencySatisfied(settingsStrafeColorTargets[self->strafeColorTarget].dependency, 4);

  if (inert != $(self->strafeColorPicker, hasClassName, "inert")) {
    if (inert) {
      $(self->strafeColorPicker, addClassName, "inert");
    } else {
      $(self->strafeColorPicker, removeClassName, "inert");
    }
  }

  // The picker is a stack, not a Control, so the four axes are disabled
  // individually - the same thing `refreshRowDependencies` does for every other
  // row through the one control its right-hand track holds.
  for (size_t i = 0; i < SETTINGS_HSVA_COUNT; i++) {
    setControlFlag((Control *) self->strafeColorSliders[i], ControlStateDisabled, inert);
  }
}

/**
 * @brief Repaints the row chrome, the footer status and the commit pair.
 * @details Every view touch here is guarded on a change, because this runs on
 * pointer motion: a class name added unconditionally re-applies the stylesheet
 * to that row's whole subtree on every frame the mouse moves.
 */
static void refreshRows(SettingsViewController *self) {

  size_t dirtyCount = 0;
  bool rendererRestart = false;
  bool soundRestart = false;

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {

    const SettingDescriptor *descriptor = &settingDescriptors[row];
    const bool modified = isRowModified(row);

    if (modified != self->rowModified[row]) {

      self->rowModified[row] = modified;
      self->rowDots[row]->hidden = !modified;
      self->rowReverts[row]->control.view.hidden = !modified;

      if (modified) {
        $(self->rowViews[row], addClassName, "modified");
      } else {
        $(self->rowViews[row], removeClassName, "modified");
      }
    }

    if (isRowDirty(self, row)) {
      dirtyCount++;
      rendererRestart |= descriptor->restart == SettingRestartRenderer;
      soundRestart |= descriptor->restart == SettingRestartSound;
    }
  }

  // Every touch that can change the commit group's width is tracked, because
  // the footer bar has to be told to re-place the group afterwards; see below.
  bool commitDidResize = self->dirtyStatus->view.hidden != (dirtyCount == 0);

  // Hidden rather than merely emptied: the commit group hugs its contents, and
  // a blank label still costs the group one span of spacing - which is a gap
  // opening between the cost caption and the buttons for no stated reason.
  self->dirtyStatus->view.hidden = dirtyCount == 0;

  if (dirtyCount == 0) {
    commitDidResize |= setTextIfChanged(self->dirtyStatus->text, "");
  } else {
    const char *restart = rendererRestart && soundRestart
      ? " · Renderer and sound restart required"
      : rendererRestart ? " · Renderer restart required"
      : soundRestart ? " · Sound restart required" : "";

    char status[128];
    snprintf(status, sizeof(status), "%zu setting%s changed%s",
               dirtyCount, dirtyCount == 1 ? "" : "s", restart);
    commitDidResize |= setTextIfChanged(self->dirtyStatus->text, status);
  }

  // The commit group hugs its contents and pins to the footer's right edge, so
  // a caption that grew or shrank moves the whole group. View::resize only
  // dirties a superview that is itself a container, and the footer bar is a
  // plain View - it has to pin both halves to opposite edges, which a StackView
  // cannot do - so nothing here would ever ask it to re-place the group. The
  // group would keep the x it was given at whatever width the caption used to
  // be, and the buttons would walk off the right edge while the caption ran on
  // underneath them. That is the overlap this footer has been reported for.
  if (commitDidResize) {
    self->footerBar->needsLayout = true;
  }

  // The commit pair is only reachable while there is something to commit, so a
  // stale count cannot survive a revert or a page change.
  setControlFlag((Control *) self->revertChanges, ControlStateDisabled, dirtyCount == 0);
  setControlFlag((Control *) self->apply, ControlStateDisabled, dirtyCount == 0);

  refreshPresetSegments(self);
  refreshStrafePresetSegments(self);
  refreshStrafeColors(self);
  refreshRowDependencies(self);
  refreshStrafeColorDependencies(self);
  refreshSliderReadouts(self);

  // The preview is the page's readout: every strafe row changes what it draws,
  // and it has no cvar of its own to bind through updateBindings.
  $(self->strafePreview, updateBindings);
}

/**
 * @brief Pulls every control back from its cvar, then repaints the chrome.
 * @details CvarSlider, CvarSelect and CvarCheckbox each bind themselves in
 * updateBindings; the two resolution rows are one control over two cvars, which
 * no Cvar* control models, so they are selected by hand.
 */
static void syncControlsFromCvars(SettingsViewController *self) {

  const bool wasRefreshing = self->refreshing;
  self->refreshing = true;

  $(self->viewController.view, updateBindings);

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {

    const SettingDescriptor *descriptor = &settingDescriptors[row];
    if (descriptor->select != SettingSelectFullscreenRes &&
        descriptor->select != SettingSelectWindowRes) {
      continue;
    }

    const int32_t width = cgi.GetCvarInteger(descriptor->var);
    const int32_t height = cgi.GetCvarInteger(descriptor->var2);
    $((Select *) self->rowControls[row], selectOptionWithValue,
      (ident) (intptr_t) ((width << 16) | height));
  }

  const char *driver = cgi.GetCvarString("r_gpu_driver");
  setTextIfChanged(self->gpuBackend->text, driver && *driver ? driver : "SDL auto");

  self->refreshing = wasRefreshing;
  refreshRows(self);
}

#pragma mark - Filter and pages

/**
 * @brief Shows the rows and sections that survive the current query.
 * @details A query reaches every page, not just the open one - otherwise a
 * setting one tab over reads as "does not exist". Off-page sections that match
 * are shown in place and tagged with the page they belong to, and each tab
 * carries its own hit count, so the player can see where the rest of the
 * matches are without opening every page in turn.
 */
static void refreshFilter(SettingsViewController *self) {

  // An empty Objectively String never allocates, so `chars` is NULL rather than
  // "" - and TextView always holds a String, so testing attributedText alone
  // guards the wrong pointer. An empty filter is the state this route opens in.
  const String *text = self->filter->attributedText;
  const char *query = text && text->chars ? text->chars : "";
  const bool searching = *query != '\0';

  size_t pageHits[SETTINGS_PAGE_COUNT] = { 0 };
  size_t sectionHits[SETTINGS_SECTION_COUNT] = { 0 };
  size_t lastVisibleRow[SETTINGS_SECTION_COUNT];

  for (size_t section = 0; section < SETTINGS_SECTION_COUNT; section++) {
    lastVisibleRow[section] = SETTINGS_ROW_COUNT;
  }

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {

    const SettingDescriptor *descriptor = &settingDescriptors[row];
    bool hit = containsIgnoringCase(descriptor->label, query) ||
               containsIgnoringCase(descriptor->var, query) ||
               containsIgnoringCase(descriptor->var2, query);

    // One row, three cvars. The three colours were roster rows with cvar names
    // as match text until the sub-tab became one editor, and a player who types
    // `cg_race_strafe_helper_color_optimal` - or "Optimal angle" - is looking
    // for the page that edits it. Reaching *into* the tab and selecting the
    // chip is a separate, unbuilt piece of cross-tab work.
    for (size_t i = 0; !hit && descriptor->kind == SettingRowStrafeColors &&
                       i < SETTINGS_STRAFE_COLOR_COUNT; i++) {
      hit = containsIgnoringCase(settingsStrafeColorTargets[i].var, query) ||
            containsIgnoringCase(settingsStrafeColorTargets[i].label, query);
    }

    self->rowViews[row]->hidden = !hit;
    if (hit) {
      sectionHits[descriptor->section]++;
      pageHits[settingsSections[descriptor->section].page]++;
      lastVisibleRow[descriptor->section] = row;
    }
  }

  // A separator under the last visible row of a section would draw a line to
  // nowhere, and which row is last moves with the query.
  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {
    const size_t last = lastVisibleRow[settingDescriptors[row].section];
    self->rowRules[row]->hidden = last == row;
  }

  bool any = false;
  for (size_t section = 0; section < SETTINGS_SECTION_COUNT; section++) {

    const SettingsPage page = settingsSections[section].page;
    const SettingsStrafeTab tab = strafeTabForSection((SettingsSection) section);
    const bool onPage = (size_t) page == self->selectedPage;

    // A query reaches every page, so it reaches every sub-tab too: a setting
    // one sub-tab over must not read as "does not exist" either.
    const bool onTab = tab == SettingsStrafeTabAlways ||
                       (size_t) tab == self->selectedStrafeTab;

    const bool visible = sectionHits[section] > 0 && (searching || (onPage && onTab));
    const bool tagged = visible && searching && !onPage;

    self->sectionViews[section]->hidden = !visible;
    self->sectionTags[section]->view.hidden = !tagged;

    // A section whose name is the name of the sub-tab that reveals it says the
    // word twice: the strip reads "Bar & markers | Colours | Readouts", and the
    // page under it opened with a "Colours" eyebrow and its rule. The strip is
    // the heading in that case, so the section drops its own.
    const bool duplicateHead = !searching && onPage && onTab &&
      tab != SettingsStrafeTabAlways &&
      !strcmp(settingsSections[section].label, settingsStrafeTabNames[tab]);

    // `hidden` notifies nobody, so a change here is only picked up because the
    // loop below re-marks every section's chain upward regardless.
    self->sectionHeads[section]->hidden = duplicateHead;
    self->sectionRules[section]->hidden = duplicateHead;
    if (tagged) {
      setTextIfChanged(self->sectionTags[section]->text, settingsPageNames[page]);
    }

    any = any || visible;
  }

  for (size_t page = 0; page < SETTINGS_PAGE_COUNT; page++) {

    Button *button = self->pageButtons[page];
    if (searching) {
      $(button->title, setTextWithFormat, "%s  %zu", settingsPageNames[page], pageHits[page]);
    } else {
      setTextIfChanged(button->title, settingsPageNames[page]);
    }

    if (searching && pageHits[page] == 0) {
      $((View *) button, addClassName, "noHits");
    } else {
      $((View *) button, removeClassName, "noHits");
    }
  }

  self->emptyState->view.hidden = any;

  // The strip governs the strafe page's sections, and a query dissolves the
  // page boundary - so while one is running there is nothing left for it to
  // govern.
  self->strafeTabs->hidden = searching || self->selectedPage != SettingsPageStrafe;

  // The host carries the flow's spacing, so it has to collapse with the one
  // section it holds rather than leaving a gap on every other page.
  self->strafePreviewHost->hidden = self->sectionViews[SettingsSectionStrafePreview]->hidden;

  // View::layoutIfNeeded lays out a view only if that view carries needsLayout,
  // so marking the route root alone leaves the flow beneath it untouched and a
  // newly revealed section keeps geometry it never received. Marking upward
  // from the sections reaches the ColumnsView, which re-places its columns and
  // re-marks each one's subtree - see ColumnsView::placeColumn.
  for (size_t section = 0; section < SETTINGS_SECTION_COUNT; section++) {
    invalidateLayoutChain(self->sectionViews[section]);
  }
  invalidateLayoutChain((View *) self->emptyState);
  invalidateLayoutChain(self->strafePreviewHost);
  invalidateLayoutChain(self->strafeTabs);
}

static void selectPage(SettingsViewController *self, size_t page) {

  if (page >= SETTINGS_PAGE_COUNT) {
    return;
  }

  self->selectedPage = page;
  for (size_t i = 0; i < SETTINGS_PAGE_COUNT; i++) {
    setControlFlag((Control *) self->pageButtons[i], ControlStateSelected, i == page);
  }

  refreshFilter(self);
}

/**
 * @brief Shows the strafe helper sections the given sub-tab owns.
 * @details The tab survives leaving and re-entering the page, the same way the
 * design's does: a player tuning readouts comes back to readouts.
 */
static void selectStrafeTab(SettingsViewController *self, size_t tab) {

  if (tab >= SETTINGS_STRAFE_TAB_COUNT) {
    return;
  }

  self->selectedStrafeTab = tab;
  for (size_t i = 0; i < SETTINGS_STRAFE_TAB_COUNT; i++) {
    setControlFlag((Control *) self->strafeTabButtons[i], ControlStateSelected, i == tab);
  }

  refreshFilter(self);
}

/**
 * @brief Names the hovered row's cvar, description and restart class in the
 * footer, or clears it when nothing is hovered.
 * @details The dialect has no tooltip, and these annotations used to ride
 * inline in the row labels. Descriptions come from the engine's own `Cvar_Add`
 * text rather than being duplicated here.
 */
static void refreshHint(SettingsViewController *self) {

  float mouseX, mouseY;
  SDL_GetMouseState(&mouseX, &mouseY);

  const SDL_Point point = MakePoint(mouseX, mouseY);

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {

    const SettingDescriptor *descriptor = &settingDescriptors[row];
    if (self->rowViews[row]->hidden ||
        self->sectionViews[descriptor->section]->hidden ||
        !$(self->rowViews[row], containsPoint, &point)) {
      continue;
    }

    if (descriptor->var == NULL) {
      switch (descriptor->kind) {
        case SettingRowStrafePreview:
          setTextIfChanged(self->hint->text,
            "Stand-in geometry · colours, styles, offsets and scales are read live");
          break;
        case SettingRowStrafePreset:
          setTextIfChanged(self->hint->text,
            "Restores every strafe helper row, then sets the preset");
          break;
        case SettingRowStrafeColors: {
          // The row stands for three cvars, so the footer names the one the
          // strip has selected rather than all three or none.
          const char *var = settingsStrafeColorTargets[self->strafeColorTarget].var;
          const cvar_t *cvar = cgi.GetCvar(var);
          setTextIfChanged(self->hint->text,
            va("%s%s%s · R G B A, 0-255 each", var,
               cvar && cvar->description ? " · " : "",
               cvar && cvar->description ? cvar->description : ""));
          break;
        }
        default:
          setTextIfChanged(self->hint->text,
            "Sets shadows, lighting distance, parallax, caustics and weather at once");
          break;
      }
      return;
    }

    const char *restart = "";
    switch (descriptor->restart) {
      case SettingRestartRenderer:
        restart = " · requires renderer restart";
        break;
      case SettingRestartSound:
        restart = " · requires sound restart";
        break;
      default:
        break;
    }

    if (!descriptor->mutable) {
      restart = " · unavailable in this build";
    } else if (isRowInert(row)) {
      restart = descriptor->dependsOn ? va(" · requires %s", descriptor->dependsOn) : "";
    }

    const cvar_t *var = cgi.GetCvar(descriptor->var);
    const char *description = var && var->description ? var->description : NULL;

    char hint[512];
    if (description) {
      snprintf(hint, sizeof(hint), "%s · %s%s", descriptor->var, description, restart);
    } else {
      snprintf(hint, sizeof(hint), "%s%s", descriptor->var, restart);
    }

    setTextIfChanged(self->hint->text, hint);
    return;
  }

  setTextIfChanged(self->hint->text, "");
}

#pragma mark - Delegates

static void didClickPage(Button *button) {

  SettingsViewController *self = button->delegate.self;

  $(self->filter, setAttributedText, "");
  selectPage(self, (size_t) (intptr_t) button->delegate.data);

  // `refreshFilter` decides what is on screen; `refreshRows` is what reads that
  // back. The preview's isolation is the one piece of chrome that depends on
  // which page and sub-tab are showing rather than on a cvar, so leaving it to
  // the next refresh would leave the preview isolating a colour on a page that
  // is no longer the Colours page - and this route gets no next refresh: see
  // `respondToEvent`.
  refreshRows(self);
  $(self->viewController.view, layoutIfNeeded);
}

static void didClickStrafeTab(Button *button) {

  SettingsViewController *self = button->delegate.self;

  selectStrafeTab(self, (size_t) (intptr_t) button->delegate.data);
  refreshRows(self);
  $(self->viewController.view, layoutIfNeeded);
}

static void didEditFilter(TextView *textView) {

  SettingsViewController *self = textView->delegate.self;

  refreshFilter(self);
}

static void didClickRowRevert(Button *button) {

  SettingsViewController *self = button->delegate.self;

  restoreRowDefaults((size_t) (intptr_t) button->delegate.data);
  syncControlsFromCvars(self);
}

static void didClickPreset(Button *button) {

  SettingsViewController *self = button->delegate.self;
  const intptr_t preset = (intptr_t) button->delegate.data;

  if (preset >= 0 && preset < (intptr_t) lengthof(qualityPresets)) {
    applyQualityPreset(&qualityPresets[preset]);
    syncControlsFromCvars(self);
  } else {
    refreshPresetSegments(self);
  }
}

static void didClickStrafePreset(Button *button) {

  SettingsViewController *self = button->delegate.self;
  const intptr_t preset = (intptr_t) button->delegate.data;

  if (preset >= 0 && preset < (intptr_t) lengthof(strafePresets)) {
    applyStrafePreset(&strafePresets[preset]);
    syncControlsFromCvars(self);
  } else {
    refreshStrafePresetSegments(self);
  }
}

/**
 * @brief Points the editor and the preview at another colour.
 * @details The sliders are rebound rather than rebuilt - the refresh path is
 * the same one a console write goes through, with the target moved.
 */
static void didClickStrafeColorChip(Button *button) {

  SettingsViewController *self = button->delegate.self;

  self->strafeColorTarget = (size_t) (intptr_t) button->delegate.data;

  syncControlsFromCvars(self);

  // The footer names the cvar the strip has selected, so it is stale the moment
  // the selection moves and nothing else on this route will notice.
  refreshHint(self);

  $(self->viewController.view, layoutIfNeeded);
}

/**
 * @brief Writes the four axes of the selected colour back into its cvar.
 * @details Every axis is read from its slider rather than one of them from the
 * moved slider and three from the cvar: the cvar is quantized to 8 bits per
 * channel, so recovering hue and saturation from it would nudge the axes the
 * player did not touch on every drag of the one they did.
 * @remarks SliderDelegate carries no `data` slot the way ButtonDelegate does,
 * so the axis is matched back by identity - which the four-slot array makes
 * unnecessary here, but the delegate is still the one the framework calls.
 */
static void didSetStrafeColorAxis(Slider *slider, double value) {

  SettingsViewController *self = slider->delegate.self;
  if (self->refreshing) {
    return;
  }

  double hsv[3];
  for (size_t i = 0; i < 3; i++) {
    hsv[i] = self->strafeColorSliders[i]->value;
  }

  int32_t rgb[3];
  hsvToColor(hsv, rgb);

  const int32_t alpha = alphaFromAxis(self->strafeColorSliders[3]->value);

  char color[SETTINGS_VALUE_SIZE];
  snprintf(color, sizeof(color), "%d %d %d %d", rgb[0], rgb[1], rgb[2], alpha);

  cgi.SetCvarString(settingsStrafeColorTargets[self->strafeColorTarget].var, color);
  syncControlsFromCvars(self);
}

/**
 * @brief Returns the row a control belongs to, or SETTINGS_ROW_COUNT.
 * @remarks SelectDelegate carries no `data` slot the way ButtonDelegate does,
 * so a Select is matched back to its row by identity.
 */
static size_t rowForControl(const SettingsViewController *self, const View *control) {

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {
    if (self->rowControls[row] == control) {
      return row;
    }
  }

  return SETTINGS_ROW_COUNT;
}

/**
 * @brief SelectDelegate for the two rows that are one control over two cvars.
 */
static void didSelectResolution(Select *select, Option *option) {

  SettingsViewController *self = select->delegate.self;
  const size_t row = rowForControl(self, (const View *) select);
  if (row == SETTINGS_ROW_COUNT) {
    return;
  }

  const intptr_t packed = (intptr_t) option->value;

  cgi.SetCvarInteger(settingDescriptors[row].var, (packed >> 16) & 0xffff);
  cgi.SetCvarInteger(settingDescriptors[row].var2, packed & 0xffff);

  if (!self->refreshing) {
    refreshRows(self);
  }
}

/**
 * @brief Restores every row to the value it held when the route was entered.
 */
static void didClickRevertChanges(Button *button) {

  SettingsViewController *self = button->delegate.self;

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {
    if (isRowDirty(self, row)) {
      restoreRowOpening(self, row);
    }
  }

  syncControlsFromCvars(self);
}

/**
 * @brief Accepts the current values as the new baseline, and restarts whatever
 * the dirty set needs in order to take effect.
 * @details Every cvar here is written the moment its control moves, so there is
 * no deferred half to flush - the same is true of the Controls route. What Apply
 * adds is the restart that the renderer-class and sound-class rows need before
 * their new values are visible or audible, and a new baseline for Revert.
 */
static void didClickApply(Button *button) {

  SettingsViewController *self = button->delegate.self;

  bool rendererRestart = false;
  bool soundRestart = false;

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {
    if (isRowDirty(self, row)) {
      rendererRestart |= settingDescriptors[row].restart == SettingRestartRenderer;
      soundRestart |= settingDescriptors[row].restart == SettingRestartSound;
    }
  }

  captureOpeningValues(self);
  refreshRows(self);

  if (rendererRestart && soundRestart) {
    cgi.Cbuf("r_restart; s_restart");
  } else if (rendererRestart) {
    cgi.Cbuf("r_restart");
  } else if (soundRestart) {
    cgi.Cbuf("s_restart");
  }
}

#pragma mark - Row construction

static void addResolutionOptions(Select *select, bool fullscreen) {

  if (fullscreen) {
    $(select, addOption, "Desktop", (ident) 0);
  }

  int32_t numModes;
  SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(cgi.context->display, &numModes);
  if (!modes) {
    return;
  }

  int32_t lastWidth = 0, lastHeight = 0;
  for (int32_t i = 0; i < numModes; i++) {

    const SDL_DisplayMode *mode = modes[i];
    if (mode->pixel_density > 1.f || (mode->w == lastWidth && mode->h == lastHeight)) {
      continue;
    }

    lastWidth = mode->w;
    lastHeight = mode->h;

    char label[MAX_QPATH];
    snprintf(label, sizeof(label), "%dx%d", mode->w, mode->h);
    $(select, addOption, label, (ident) (intptr_t) ((mode->w << 16) | mode->h));
  }

  SDL_free(modes);
}

/**
 * @brief One entry in a string-valued Select roster: what it reads as, and what
 * it writes.
 */
typedef struct {
  const char *label;
  char *value;
} SettingStringOption;

/**
 * @brief Adds a string-valued roster, terminated by a NULL label.
 * @details The helper accepts spellings its rosters do not offer - `ups` for
 * suffix, `abs` for absolute, and a numeric form of each - so a config written
 * by hand or by an older build can hold a value none of these options name. The
 * roster keeps it rather than dropping it, which is the same courtesy the
 * antialias and anisotropy rows extend: a Select showing nothing selected reads
 * as broken, and rewriting the cvar to make the strip tidy would be the route
 * changing a setting nobody asked it to change.
 */
static void addStringOptions(Select *select, const char *var, char *legacy,
                             const SettingStringOption *options) {

  const char *current = var ? cgi.GetCvarString(var) : NULL;
  bool named = current == NULL || *current == '\0';

  for (const SettingStringOption *option = options; option->label; option++) {
    $(select, addOption, option->label, option->value);
    named = named || !q_strcmp(current, option->value);
  }

  if (!named) {
    // Copied into the route's own storage: an Option holds the pointer it is
    // given and never copies it, and the cvar's string is freed out from under
    // it the first time anything writes that cvar.
    q_strlcpy(legacy, current, SETTINGS_VALUE_SIZE);
    $(select, addOption, va("%s (from config)", legacy), legacy);
  }
}

/**
 * @brief Adds the roster a Select row carries, plus a legacy option when the
 * cvar currently holds a value the roster does not name.
 */
static void addSelectOptions(SettingsViewController *self, size_t row, Select *select) {

  const SettingSelectKind kind = settingDescriptors[row].select;
  const char *var = settingDescriptors[row].var;
  char *legacy = self->legacyValues[row];


  const int32_t current = var ? cgi.GetCvarInteger(var) : 0;

  switch (kind) {
    case SettingSelectWindowMode:
      $(select, addOption, "Windowed", (ident) 0);
      $(select, addOption, "Borderless", (ident) 1);
      $(select, addOption, "Exclusive fullscreen", (ident) 2);
      break;

    case SettingSelectFullscreenRes:
      addResolutionOptions(select, true);
      break;

    case SettingSelectWindowRes:
      addResolutionOptions(select, false);
      break;

    case SettingSelectSwapInterval:
      $(select, addOption, "Disabled", (ident) 0);
      $(select, addOption, "Enabled", (ident) 1);
      $(select, addOption, "Mailbox", (ident) (intptr_t) -1);
      break;

    case SettingSelectAntialias:
      $(select, addOption, "Disabled", (ident) 0);
      if (current != 0 && current != 2 && current != 4 && current != 8) {
        char label[MAX_QPATH];
        snprintf(label, sizeof(label), "Disabled (legacy %d)", current);
        $(select, addOption, label, (ident) (intptr_t) current);
      }
      $(select, addOption, "MSAA 2x", (ident) 2);
      $(select, addOption, "MSAA 4x", (ident) 4);
      $(select, addOption, "MSAA 8x", (ident) 8);
      break;

    case SettingSelectAnisotropy:
      $(select, addOption, "Disabled", (ident) 0);
      if (current != 0 && current != 2 && current != 4 && current != 8 && current != 16) {
        char label[MAX_QPATH];
        snprintf(label, sizeof(label), "Disabled (legacy %dx)", current);
        $(select, addOption, label, (ident) (intptr_t) current);
      }
      $(select, addOption, "2x", (ident) 2);
      $(select, addOption, "4x", (ident) 4);
      $(select, addOption, "8x", (ident) 8);
      $(select, addOption, "16x", (ident) 16);
      break;

    case SettingSelectScreenshot:
      $(select, addOption, "jpg", screenshotJpg);
      $(select, addOption, "png", screenshotPng);
      $(select, addOption, "tga", screenshotTga);
      break;

    case SettingSelectShadowTile:
      if (current != 128 && current != 256 && current != 512) {
        char label[MAX_QPATH];
        snprintf(label, sizeof(label), "Legacy %d (effective 128)", current);
        $(select, addOption, label, (ident) (intptr_t) current);
      }
      $(select, addOption, "128", (ident) 128);
      $(select, addOption, "256", (ident) 256);
      $(select, addOption, "512", (ident) 512);
      break;

    case SettingSelectSampleRate:
      $(select, addOption, "22050", (ident) 22050);
      $(select, addOption, "44100", (ident) 44100);
      $(select, addOption, "48000", (ident) 48000);
      break;

    // The design's Style control offers gradient and solid; the helper's own
    // Cg_StrafeHelper_BarStyle resolves four. The engine is the source of truth
    // for what a cvar accepts, so the roster carries all four - a player whose
    // config says `outline` should find the row already spelling it rather than
    // silently reading as one of the other two.
    case SettingSelectBarStyle:
      addStringOptions(select, var, legacy, (const SettingStringOption[]) {
        { "Gradient", barStyleGradient },
        { "Solid", barStyleSolid },
        { "Outline", barStyleOutline },
        { "Minimal", barStyleMinimal },
        { NULL, NULL }
      });
      break;

    // Labelled with what each one prints, because "plain" and "prefix" do not
    // say which is which. The values are the helper's spellings, not these.
    case SettingSelectUpsFormat:
      addStringOptions(select, var, legacy, (const SettingStringOption[]) {
        { "712", upsFormatPlain },
        { "712 ups", upsFormatSuffix },
        { "UPS: 712", upsFormatPrefix },
        { NULL, NULL }
      });
      break;

    case SettingSelectUpsColorMode:
      // Six, not the design's two: Cg_StrafeHelper_UpsColor resolves static,
      // rainbow, threshold, gradient and strafing, and treats everything else -
      // including the shipped default - as dynamic.
      addStringOptions(select, var, legacy, (const SettingStringOption[]) {
        { "Dynamic", upsColorDynamic },
        { "Gradient", upsColorGradient },
        { "Strafing", upsColorStrafing },
        { "Threshold", upsColorThreshold },
        { "Static", upsColorStatic },
        { "Rainbow", upsColorRainbow },
        { NULL, NULL }
      });
      break;

    case SettingSelectVelocityAngleFormat:
      addStringOptions(select, var, legacy, (const SettingStringOption[]) {
        { "Difference", velocityAngleDiff },
        { "Absolute", velocityAngleAbsolute },
        { NULL, NULL }
      });
      break;

    case SettingSelectNone:
      break;
  }
}

/**
 * @brief Builds the preset strip: five segments, four of which set cvars.
 */
static View *makePresetSegments(SettingsViewController *self) {

  StackView *segments = $(alloc(StackView), initWithFrame, NULL);
  $((View *) segments, addClassName, "presetSegments");
  segments->axis = StackViewAxisHorizontal;

  for (size_t i = 0; i < SETTINGS_PRESET_COUNT; i++) {

    Button *button = $(alloc(Button), initWithTitle, settingsPresetNames[i]);
    $((View *) button, addClassName, "presetSegment");
    button->delegate = (ButtonDelegate) {
      .self = self,
      .data = (ident) (i < lengthof(qualityPresets) ? (intptr_t) i : (intptr_t) -1),
      .didClick = didClickPreset
    };

    $((View *) segments, addSubview, (View *) button);
    self->presetButtons[i] = button;
    release(button);
  }

  return (View *) segments;
}

/**
 * @brief Builds the strafe helper strip: five segments, four of which apply.
 * @details The fifth carries `-1`, which is what `didClickStrafePreset` reads
 * as "readout, nothing to set" - the same contract the quality strip's `Custom`
 * segment is built on.
 */
static View *makeStrafePresetSegments(SettingsViewController *self) {

  StackView *segments = $(alloc(StackView), initWithFrame, NULL);
  $((View *) segments, addClassName, "presetSegments");
  $((View *) segments, addClassName, "strafePresetSegments");
  segments->axis = StackViewAxisHorizontal;

  for (size_t i = 0; i < SETTINGS_STRAFE_PRESET_COUNT; i++) {

    Button *button = $(alloc(Button), initWithTitle, settingsStrafePresetNames[i]);
    $((View *) button, addClassName, "presetSegment");
    button->delegate = (ButtonDelegate) {
      .self = self,
      .data = (ident) (i < lengthof(strafePresets) ? (intptr_t) i : (intptr_t) -1),
      .didClick = didClickStrafePreset
    };

    $((View *) segments, addSubview, (View *) button);
    self->strafePresetButtons[i] = button;
    release(button);
  }

  Label *status = $(alloc(Label), initWithText, "", NULL);
  $((View *) status, addClassName, "strafePresetStatus");
  $((View *) segments, addSubview, (View *) status);
  self->strafePresetStatus = status;
  release(status);

  return (View *) segments;
}

/**
 * @brief The picker's four axes, in strip order.
 * @details Alpha runs 0-100 and reads as a percentage rather than running the
 * cvar's own 0-255: `Slider::setLabelFormat` is printf over the slider's value,
 * so a 0-255 axis cannot print "50%" - and a proportion is what this number is.
 * `alphaFromAxis` and `alphaToAxis` convert at the edges.
 */
static const struct {
  const char *label;
  double min, max, step;
  const char *format;
} settingsHsvaAxes[SETTINGS_HSVA_COUNT] = {
  { "H", 0, 360, 1, "%0.0f" },
  { "S", 0, 1, 0.01, "%0.2f" },
  { "V", 0, 1, 0.01, "%0.2f" },
  { "A", 0, 100, 1, "%0.0f%%" },
};

/**
 * @brief Builds the Colours editor: a target strip over one picker.
 * @details Not three rows of three sliders. Three colour cvars behind three
 * roster rows put a swatch and three rails into a cell the roster sizes for one
 * control, and what came out was a rail collapsed to a tick with its value
 * clipped against the row's right edge. The sub-tab is not a list of settings -
 * it is one editor with a selector, which is the shape the preview and preset
 * rows on the same page already have.
 *
 * No swatch tile of its own: the live preview sits directly above, drawing
 * these same three colours over the same geometry, and a second sample beside
 * it could only ever disagree with the first. The strip's chips carry the
 * colours instead, because that is also what a player picks a target by.
 */
static View *makeStrafeColors(SettingsViewController *self) {

  StackView *editor = $(alloc(StackView), initWithFrame, NULL);
  $((View *) editor, addClassName, "strafeColors");

  StackView *chips = $(alloc(StackView), initWithFrame, NULL);
  $((View *) chips, addClassName, "strafeColorChips");
  chips->axis = StackViewAxisHorizontal;
  chips->view.alignment = ViewAlignmentTopCenter;

  for (size_t i = 0; i < SETTINGS_STRAFE_COLOR_COUNT; i++) {

    Button *chip = $(alloc(Button), initWithTitle, settingsStrafeColorTargets[i].label);
    $((View *) chip, addClassName, "strafeColorChip");
    chip->delegate = (ButtonDelegate) {
      .self = self,
      .data = (ident) (intptr_t) i,
      .didClick = didClickStrafeColorChip
    };

    // `alignment: internal`, which is the dialect's word for a subview the
    // layout does not see: a Button lays its children out with the framework's
    // own `Button > * { alignment: middle-center }`, so a swatch placed any
    // other way lands under the title. Internal keeps it out of
    // `visibleSubviews` - so out of layout and out of `sizeThatFits` - while
    // `View::draw` still draws it, and `renderFrame` reads its origin in the
    // chip's own coordinates rather than adding the chip's padding. The room
    // for it is that padding: see `.strafeColorChip` in the stylesheet.
    View *swatch = $(alloc(View), initWithFrame, NULL);
    $(swatch, addClassName, "strafeColorChipSwatch");
    $((View *) chip, addSubview, swatch);
    self->strafeColorChipSwatches[i] = swatch;
    release(swatch);

    $((View *) chips, addSubview, (View *) chip);
    self->strafeColorChips[i] = chip;
    release(chip);
  }

  $((View *) editor, addSubview, (View *) chips);
  release(chips);

  StackView *head = $(alloc(StackView), initWithFrame, NULL);
  $((View *) head, addClassName, "strafeColorHead");
  head->axis = StackViewAxisHorizontal;
  head->view.alignment = ViewAlignmentTopCenter;

  Label *name = $(alloc(Label), initWithText, "", NULL);
  $((View *) name, addClassName, "strafeColorName");
  $((View *) head, addSubview, (View *) name);
  self->strafeColorName = name;
  release(name);

  // What the colour paints, not what the cvar is called: the cvar name is
  // already on the meta line, and "Inner edge of each zone" is the thing a
  // player looking at the bar is trying to find.
  Label *paints = $(alloc(Label), initWithText, "", NULL);
  $((View *) paints, addClassName, "strafeColorPaints");
  $((View *) head, addSubview, (View *) paints);
  self->strafeColorPaints = paints;
  release(paints);

  $((View *) editor, addSubview, (View *) head);
  release(head);

  View *rule = $(alloc(View), initWithFrame, NULL);
  $(rule, addClassName, "strafeColorRule");
  $((View *) editor, addSubview, rule);
  release(rule);

  StackView *picker = $(alloc(StackView), initWithFrame, NULL);
  $((View *) picker, addClassName, "strafeColorPicker");

  for (size_t i = 0; i < SETTINGS_HSVA_COUNT; i++) {

    StackView *axis = $(alloc(StackView), initWithFrame, NULL);
    $((View *) axis, addClassName, "strafeColorAxis");
    axis->axis = StackViewAxisHorizontal;
    axis->view.alignment = ViewAlignmentMiddleLeft;

    Label *letter = $(alloc(Label), initWithText, settingsHsvaAxes[i].label, NULL);
    $((View *) letter, addClassName, "strafeColorAxisLabel");
    $((View *) axis, addSubview, (View *) letter);
    release(letter);

    // A plain Slider, not a CvarSlider: the cvar behind this editor is one
    // "R G B A" string rather than four numbers, so no single axis owns it.
    Slider *slider = $(alloc(Slider), initWithFrame, NULL);
    $((View *) slider, addClassName, "strafeColorSlider");
    slider->min = settingsHsvaAxes[i].min;
    slider->max = settingsHsvaAxes[i].max;
    slider->step = settingsHsvaAxes[i].step;
    slider->snapToStep = true;
    $(slider, setLabelFormat, settingsHsvaAxes[i].format);
    slider->delegate = (SliderDelegate) {
      .self = self,
      .didSetValue = didSetStrafeColorAxis
    };

    $((View *) axis, addSubview, (View *) slider);
    self->strafeColorSliders[i] = slider;
    release(slider);

    $((View *) picker, addSubview, (View *) axis);
    release(axis);
  }

  $((View *) editor, addSubview, (View *) picker);
  self->strafeColorPicker = (View *) picker;
  release(picker);

  StackView *meta = $(alloc(StackView), initWithFrame, NULL);
  $((View *) meta, addClassName, "strafeColorMetaLine");
  meta->axis = StackViewAxisHorizontal;
  meta->view.alignment = ViewAlignmentTopCenter;

  Label *value = $(alloc(Label), initWithText, "", NULL);
  $((View *) value, addClassName, "strafeColorMeta");
  $((View *) meta, addSubview, (View *) value);
  self->strafeColorMeta = value;
  release(value);

  // Emptied rather than hidden: the line must not change height while a thumb
  // is still moving, which is exactly when this appears and disappears.
  Label *warning = $(alloc(Label), initWithText, "", NULL);
  $((View *) warning, addClassName, "strafeColorWarning");
  $((View *) meta, addSubview, (View *) warning);
  self->strafeColorWarning = warning;
  release(warning);

  $((View *) editor, addSubview, (View *) meta);
  release(meta);

  return (View *) editor;
}

/**
 * @brief Builds the control a row shows on its right-hand side.
 */
static View *makeControl(SettingsViewController *self, size_t row) {

  const SettingDescriptor *descriptor = &settingDescriptors[row];
  cvar_t *var = descriptor->var ? cgi.GetCvar(descriptor->var) : NULL;

  switch (descriptor->kind) {

    case SettingRowPreset:
      return makePresetSegments(self);

    case SettingRowValue: {
      Label *value = $(alloc(Label), initWithText, "", NULL);
      $((View *) value, addClassName, "rowValue");
      self->gpuBackend = value;
      return (View *) value;
    }

    case SettingRowStrafePreview: {
      StrafeHelperPreview *preview = $(alloc(StrafeHelperPreview), initWithFrame, NULL);
      self->strafePreview = (View *) preview;
      return (View *) preview;
    }

    case SettingRowStrafePreset:
      return makeStrafePresetSegments(self);

    case SettingRowStrafeColors:
      return makeStrafeColors(self);

    case SettingRowToggle:
      return (View *) $(alloc(CvarCheckbox), initWithVariable, var);

    case SettingRowSelect: {

      // A resolution is two cvars behind one control, which CvarSelect cannot
      // model - those two rows take a plain Select and a delegate instead.
      if (descriptor->var2) {

        Select *select = $(alloc(Select), initWithFrame, NULL);
        select->delegate = (SelectDelegate) {
          .self = self,
          .didSelectOption = didSelectResolution
        };
        addSelectOptions(self, row, select);
        return (View *) select;
      }

      CvarSelect *select = $(alloc(CvarSelect), initWithVariable, var);
      select->expectsStringValue =
        descriptor->select == SettingSelectScreenshot ||
        descriptor->select == SettingSelectBarStyle ||
        descriptor->select == SettingSelectUpsFormat ||
        descriptor->select == SettingSelectUpsColorMode ||
        descriptor->select == SettingSelectVelocityAngleFormat;
      addSelectOptions(self, row, (Select *) select);
      return (View *) select;
    }

    case SettingRowSlider: {

      // A row this build does not own gets a plain Slider: CvarSlider wants a
      // cvar, and asking the engine for one would register it.
      if (!descriptor->mutable) {

        Slider *slider = $(alloc(Slider), initWithFrame, NULL);
        slider->min = descriptor->min;
        slider->max = descriptor->max;
        slider->step = descriptor->step;
        slider->snapToStep = true;
        setControlFlag((Control *) slider, ControlStateDisabled, true);
        return (View *) slider;
      }

      // RaceSlider overrides CvarSlider::initWithVariable but does not redeclare
      // it, so the cast is what names the interface the dispatch reads - the
      // slot still holds RaceSlider's implementation.
      CvarSlider *slider = $((CvarSlider *) alloc(RaceSlider), initWithVariable, var,
                             descriptor->min, descriptor->max, descriptor->step);
      ((Slider *) slider)->snapToStep = true;

      // CvarSlider::updateBindings forces "%g" whenever the step is 1 or more,
      // and it runs on every refresh - so only the sub-unit rows can carry a
      // readout format of their own.
      if (descriptor->format) {
        $((Slider *) slider, setLabelFormat, descriptor->format);
      }

      return (View *) slider;
    }
  }

  return NULL;
}

/**
 * @brief Builds one row: dot and label pinned left, control pinned right.
 * @details The row is a plain View rather than a StackView so that the two
 * groups pin to opposite edges however long the setting name runs - a
 * horizontal StackView would lay them out end to end instead, and the control
 * column would step in and out with every label.
 */
static View *makeRow(SettingsViewController *self, size_t row) {

  const SettingDescriptor *descriptor = &settingDescriptors[row];

  View *view = $(alloc(View), initWithFrame, NULL);
  $(view, addClassName, "settingRow");
  switch (descriptor->kind) {
    case SettingRowPreset:
      $(view, addClassName, "presetRow");
      break;
    case SettingRowStrafePreset:
      $(view, addClassName, "presetRow");
      $(view, addClassName, "strafePresetRow");
      break;
    case SettingRowStrafePreview:
      $(view, addClassName, "previewRow");
      break;
    case SettingRowStrafeColors:
      $(view, addClassName, "strafeColorsRow");
      break;
    default:
      break;
  }

  StackView *left = $(alloc(StackView), initWithFrame, NULL);
  $((View *) left, addClassName, "rowLeft");
  left->axis = StackViewAxisHorizontal;
  left->view.alignment = ViewAlignmentMiddleLeft;

  View *dot = $(alloc(View), initWithFrame, NULL);
  $(dot, addClassName, "rowDot");
  dot->hidden = true;
  $((View *) left, addSubview, dot);
  self->rowDots[row] = dot;
  release(dot);

  Label *label = $(alloc(Label), initWithText, descriptor->label, NULL);
  $((View *) label, addClassName, "rowLabel");
  $((View *) left, addSubview, (View *) label);
  release(label);

  Button *revert = $(alloc(Button), initWithTitle, "revert");
  $((View *) revert, addClassName, "rowRevert");
  revert->control.view.hidden = true;
  revert->delegate = (ButtonDelegate) {
    .self = self,
    .data = (ident) (intptr_t) row,
    .didClick = didClickRowRevert
  };
  $((View *) left, addSubview, (View *) revert);
  self->rowReverts[row] = revert;
  release(revert);

  $(view, addSubview, (View *) left);
  release(left);

  StackView *right = $(alloc(StackView), initWithFrame, NULL);
  $((View *) right, addClassName, "rowRight");
  right->axis = StackViewAxisHorizontal;
  right->view.alignment = ViewAlignmentMiddleRight;

  View *control = makeControl(self, row);
  assert(control);

  // The preview is a picture of the screen rather than a control beside a
  // label, and the Colours editor is a strip over a picker rather than one
  // control - so both take the whole row instead of the row's right-hand track.
  if (descriptor->kind == SettingRowStrafePreview ||
      descriptor->kind == SettingRowStrafeColors) {
    $(view, addSubview, control);
  } else {
    $((View *) right, addSubview, control);
  }

  self->rowControls[row] = control;
  release(control);

  $(view, addSubview, (View *) right);
  release(right);

  // The dialect has no per-side borders, so the row separator is an explicit
  // hairline pinned to the bottom edge. Riding inside the row means the filter
  // hides a row and its rule together.
  View *rule = $(alloc(View), initWithFrame, NULL);
  $(rule, addClassName, "rowRule");
  rule->alignment = ViewAlignmentBottomLeft;
  $(view, addSubview, rule);
  self->rowRules[row] = rule;
  release(rule);

  return view;
}

/**
 * @brief Builds one section: eyebrow, page chip, rule, and its rows.
 */
static View *makeSection(SettingsViewController *self, size_t section) {

  StackView *view = $(alloc(StackView), initWithFrame, NULL);
  $((View *) view, addClassName, "settingSection");

  StackView *head = $(alloc(StackView), initWithFrame, NULL);
  $((View *) head, addClassName, "sectionHead");
  head->axis = StackViewAxisHorizontal;

  Label *eyebrow = $(alloc(Label), initWithText, settingsSections[section].label, NULL);
  $((View *) eyebrow, addClassName, "sectionEyebrow");
  $((View *) head, addSubview, (View *) eyebrow);
  release(eyebrow);

  Label *tag = $(alloc(Label), initWithText, "", NULL);
  $((View *) tag, addClassName, "sectionTag");
  tag->view.hidden = true;
  $((View *) head, addSubview, (View *) tag);
  self->sectionTags[section] = tag;
  release(tag);

  // The preview's eyebrow is also where the Colours editor says what it has
  // isolated: the caption belongs to the picture it changes, not to the strip
  // two sections down that selected it.
  if (section == SettingsSectionStrafePreview) {

    Label *editing = $(alloc(Label), initWithText, "", NULL);
    $((View *) editing, addClassName, "strafeEditing");
    editing->view.hidden = true;
    $((View *) head, addSubview, (View *) editing);
    self->strafeEditing = editing;
    release(editing);
  }

  $((View *) view, addSubview, (View *) head);
  self->sectionHeads[section] = (View *) head;
  release(head);

  View *rule = $(alloc(View), initWithFrame, NULL);
  $(rule, addClassName, "sectionRule");
  $((View *) view, addSubview, rule);
  self->sectionRules[section] = rule;
  release(rule);

  StackView *rows = $(alloc(StackView), initWithFrame, NULL);
  $((View *) rows, addClassName, "settingRows");

  for (size_t row = 0; row < SETTINGS_ROW_COUNT; row++) {
    if ((size_t) settingDescriptors[row].section != section) {
      continue;
    }

    View *rowView = makeRow(self, row);
    $((View *) rows, addSubview, rowView);
    self->rowViews[row] = rowView;
    release(rowView);
  }

  $((View *) view, addSubview, (View *) rows);
  release(rows);

  return (View *) view;
}

#pragma mark - ViewController

static void resolveOutlets(SettingsViewController *self) {

  Outlet outlets[] = MakeOutlets(
    MakeOutlet("settingsFilter", &self->filter),
    MakeOutlet("settingsStrafePreview", &self->strafePreviewHost),
    MakeOutlet("settingsStrafeTabs", &self->strafeTabs),
    MakeOutlet("settingsEmptyState", &self->emptyState),
    MakeOutlet("settingsFooterBar", &self->footerBar),
    MakeOutlet("settingsHint", &self->hint),
    MakeOutlet("settingsDirtyStatus", &self->dirtyStatus),
    MakeOutlet("revertChanges", &self->revertChanges),
    MakeOutlet("applyChanges", &self->apply)
  );

  $(self->viewController.view, resolve, outlets);

  assert(self->filter);
  assert(self->strafePreviewHost);
  assert(self->strafeTabs);
  assert(self->emptyState);
  assert(self->footerBar);
  assert(self->hint);
  assert(self->dirtyStatus);
  assert(self->revertChanges);
  assert(self->apply);
}

/**
 * @see ViewController::loadView(ViewController *)
 */
static void loadView(ViewController *viewController) {

  super(ViewController, viewController, loadView);

  SettingsViewController *self = (SettingsViewController *) viewController;

  View *view = $$(View, viewWithResourceName,
                  "ui/settings/SettingsViewController.json", NULL);
  assert(view);
  assert(view->identifier && !q_strcmp(view->identifier, "raceSettingsRoot"));

  view->stylesheet = $$(Stylesheet, stylesheetWithResourceName,
                        "ui/settings/SettingsViewController.css");
  assert(view->stylesheet);

  ((View *) ((Panel *) view)->accessoryView)->hidden = false;

  $(viewController, setView, view);
  release(view);

  resolveOutlets(self);

  View *tabs = $(viewController->view, descendantWithIdentifier, "settingsPageTabs");
  assert(tabs);

  for (size_t page = 0; page < SETTINGS_PAGE_COUNT; page++) {

    Button *button = $(alloc(Button), initWithTitle, settingsPageNames[page]);
    $((View *) button, addClassName, "settingsPageTab");
    button->delegate = (ButtonDelegate) {
      .self = self,
      .data = (ident) (intptr_t) page,
      .didClick = didClickPage
    };

    $(tabs, addSubview, (View *) button);
    self->pageButtons[page] = button;
    release(button);
  }

  // The strafe page carries six sections, so the design splits them behind a
  // strip of their own. It is a slot in the resource rather than a section in
  // the flow, because sections land in ColumnsView slots and a strip that
  // governs the whole page cannot live inside one column of it.
  for (size_t tab = 0; tab < SETTINGS_STRAFE_TAB_COUNT; tab++) {

    Button *button = $(alloc(Button), initWithTitle, settingsStrafeTabNames[tab]);
    $((View *) button, addClassName, "settingsStrafeTab");
    button->delegate = (ButtonDelegate) {
      .self = self,
      .data = (ident) (intptr_t) tab,
      .didClick = didClickStrafeTab
    };

    $(self->strafeTabs, addSubview, (View *) button);
    self->strafeTabButtons[tab] = button;
    release(button);
  }

  // The sections are built here rather than authored in the JSON because every
  // one of them is the same shape over the same descriptor table that the
  // filter, the dot, the hint and the commit pair all read. One table means a
  // row cannot appear in the route without also being searchable and revertible.
  View *columns = $(viewController->view, descendantWithIdentifier, "settingsColumns");
  assert(columns);

  for (size_t section = 0; section < SETTINGS_SECTION_COUNT; section++) {

    View *sectionView = makeSection(self, section);

    // The preview is the one section that does not go into the flow: a
    // ColumnsView slot is one column of however many the viewport affords, and
    // a picture of the screen has to be the width of the content area to read
    // as one. Its host sits above the sub-tab strip, which is where the design
    // puts it - it is what the sub-tabs are tuning.
    $(section == SettingsSectionStrafePreview ? self->strafePreviewHost : columns,
      addSubview, sectionView);

    self->sectionViews[section] = sectionView;
    release(sectionView);
  }

  self->filter->delegate = (TextViewDelegate) {
    .self = self,
    .didEdit = didEditFilter
  };
  self->revertChanges->delegate = (ButtonDelegate) {
    .self = self,
    .didClick = didClickRevertChanges
  };
  self->apply->delegate = (ButtonDelegate) {
    .self = self,
    .didClick = didClickApply
  };

  captureOpeningValues(self);
  selectStrafeTab(self, SettingsStrafeTabBar);
  selectPage(self, SettingsPageDisplay);
  syncControlsFromCvars(self);
}

/**
 * @see ViewController::viewWillAppear(ViewController *)
 */
static void viewWillAppear(ViewController *viewController) {

  super(ViewController, viewController, viewWillAppear);

  SettingsViewController *self = (SettingsViewController *) viewController;

  captureOpeningValues(self);
  refreshFilter(self);
  syncControlsFromCvars(self);
  refreshHint(self);
}

/**
 * @see ViewController::respondToEvent(ViewController *, const SDL_Event *)
 * @details The controls write their own cvars, so there is no per-control
 * delegate to hang the chrome off; the route re-reads the descriptor table
 * instead. Every write in refreshRows is guarded on a change, so a still pointer
 * costs one pass of string compares and nothing else.
 */
static void respondToEvent(ViewController *viewController, const SDL_Event *event) {

  super(ViewController, viewController, respondToEvent, event);

  SettingsViewController *self = (SettingsViewController *) viewController;

  switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION:
      refreshHint(self);
      refreshRows(self);
      break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_KEY_UP:
      refreshRows(self);
      break;
    default:
      break;
  }
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
  ((ViewControllerInterface *) clazz->interface)->respondToEvent = respondToEvent;
  ((ViewControllerInterface *) clazz->interface)->viewWillAppear = viewWillAppear;
}

/**
 * @fn Class *SettingsViewController::_SettingsViewController(void)
 * @memberof SettingsViewController
 */
Class *_SettingsViewController(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "SettingsViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(SettingsViewController),
      .interfaceOffset = offsetof(SettingsViewController, interface),
      .interfaceSize = sizeof(SettingsViewControllerInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
