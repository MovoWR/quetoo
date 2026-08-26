/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#pragma once

#include "cg_strafe_helper.h"
#include "cg_types.h"

#include <ObjectivelyMVC.h>

/**
 * @file
 * @brief Settings ViewController.
 */

typedef struct SettingsViewController SettingsViewController;
typedef struct SettingsViewControllerInterface SettingsViewControllerInterface;

#define SETTINGS_PAGE_COUNT 8
#define SETTINGS_SECTION_COUNT 18
#define SETTINGS_ROW_COUNT 83
#define SETTINGS_PRESET_COUNT 5
#define SETTINGS_VALUE_SIZE 128

/**
 * @brief The most cvars any one row stands for.
 * @details Two for a resolution, which is one control over a width and a
 * height; three for the strafe helper's Colours editor, which is one control
 * over the three `cg_race_strafe_helper_color_*` cvars. Every row that reads
 * and writes cvars is captured, reverted and counted through this one bound,
 * so a composite row cannot appear in the route without also being revertible.
 */
#define SETTINGS_ROW_VAR_COUNT 3

/**
 * @brief The colour targets the strafe helper's Colours editor selects between.
 */
#define SETTINGS_STRAFE_COLOR_COUNT 3

/**
 * @brief The axes the colour editor offers: hue, saturation, value, alpha.
 * @details A picker rather than a curated roster: every `cg_race_strafe_helper_color_*`
 * cvar is a free-form "R G B A" string, so a chip strip could only ever offer a
 * handful of the colours the cvar accepts, and a value no chip named read as
 * three unselected chips and no explanation. Four sliders reach the whole
 * space. Alpha is one of them because all three cvars carry a meaningful alpha
 * of their own and none of it was reachable from the menu - the helper's own
 * opacity multiplies these rather than replacing them, which is exactly why the
 * editor states the product.
 */
#define SETTINGS_HSVA_COUNT 4

/**
 * @brief The strafe helper sub-tab strip.
 * @details The strafe page carries six sections, which is more than the other
 * seven pages put together, so the design splits them three ways behind their
 * own strip. The preview and the preset strip sit outside it: they are how the
 * helper gets turned on, and they have to stay reachable from every sub-tab.
 */
#define SETTINGS_STRAFE_TAB_COUNT 3

/**
 * @brief The strafe helper preset strip.
 * @details Four segments and a readout beside them, rather than the quality
 * strip's five segments: "CGAZ classic" and "Everything" are long enough that a
 * fifth `Custom` segment would not fit a column, and `Custom` was never
 * clickable in the first place.
 */
#define SETTINGS_STRAFE_PRESET_COUNT 5

/**
 * @brief The Settings route.
 * @details The route is one page strip over one flow of sections, the same
 * shape the Controls roster uses: every section from every page lives in the
 * same flow and the C side shows or hides them, so switching pages and running
 * a filter query are the same pass. Rows are built from one descriptor table,
 * which is what makes every row searchable, revertible and countable toward the
 * footer's commit pair without being declared three times.
 * @extends ViewController
 */
struct SettingsViewController {

  /**
   * @brief The superclass.
   * @private
   */
  ViewController viewController;

  /**
   * @brief The interface.
   * @private
   */
  SettingsViewControllerInterface *interface;

  /**
   * @brief The page strip, in strip order.
   * @private
   */
  Button *pageButtons[SETTINGS_PAGE_COUNT];
  size_t selectedPage;

  /**
   * @brief The one filter slot, and the state it leaves behind.
   * @private
   */
  TextView *filter;
  Label *emptyState;

  /**
   * @brief Sections, and the chip naming an off-page section's home.
   * @private
   */
  /**
   * @brief The filter bar's quality-preset host.
   * @details The design puts the preset control in the filter bar rather than
   * in the document, so it is not a row and has no section of its own: the
   * five segments are built once and parented here.
   */
  View *presetHost;

  View *sectionViews[SETTINGS_SECTION_COUNT];
  Label *sectionTags[SETTINGS_SECTION_COUNT];

  /**
   * @brief Each section's eyebrow-and-rule heading, so it can be dropped when
   * the sub-tab above it already says the same word.
   */
  View *sectionHeads[SETTINGS_SECTION_COUNT];
  View *sectionRules[SETTINGS_SECTION_COUNT];

  /**
   * @brief Rows, and the parts of a row the route drives per refresh.
   * @details `rowModified` caches the last painted state so a refresh only
   * touches a view when its state actually changed - the refresh runs on
   * pointer motion, and adding or removing a class name re-applies the
   * stylesheet to the whole subtree.
   * @private
   */
  View *rowViews[SETTINGS_ROW_COUNT];
  View *rowRules[SETTINGS_ROW_COUNT];
  View *rowDots[SETTINGS_ROW_COUNT];
  Button *rowReverts[SETTINGS_ROW_COUNT];
  View *rowControls[SETTINGS_ROW_COUNT];
  bool rowModified[SETTINGS_ROW_COUNT];

  /**
   * @brief Whether the row's dependency was satisfied as of the last repaint.
   * @details A row whose parent toggle is off is dimmed and made unclickable
   * rather than hidden, so the page keeps its shape while a section is switched
   * off. Cached for the same reason `rowModified` is.
   * @private
   */
  bool rowInert[SETTINGS_ROW_COUNT];

  /**
   * @brief The Colours editor: one target strip over one picker.
   * @details Not three rows of three sliders. The three colours are one editor
   * with a selector, the shape the preview and preset rows on the same page
   * already establish - a colour cvar is not a setting a roster row can show,
   * because the row's control cell is sized for one control and a picker is
   * four rails, a swatch strip and two captions.
   * @private
   */
  size_t strafeColorTarget;
  Button *strafeColorChips[SETTINGS_STRAFE_COLOR_COUNT];
  View *strafeColorChipSwatches[SETTINGS_STRAFE_COLOR_COUNT];
  Label *strafeColorName, *strafeColorPaints, *strafeColorMeta, *strafeColorWarning;

  /**
   * @brief The four axes, and the block they are laid out in.
   * @details The sliders carry no target of their own the way a Button delegate
   * does - SliderDelegate has no `data` slot - so they are matched back to
   * their axis by index. The block is held so its dependency state can be
   * painted; its geometry is the stylesheet's, not this route's - see
   * `.strafeColorPicker`.
   * @private
   */
  Slider *strafeColorSliders[SETTINGS_HSVA_COUNT];
  View *strafeColorPicker;

  /**
   * @brief The caption naming the element the preview is isolating.
   * @private
   */
  Label *strafeEditing;

  /**
   * @brief The quality preset segments, Low through Custom.
   * @private
   */
  Button *presetButtons[SETTINGS_PRESET_COUNT];

  /**
   * @brief The strafe helper sub-tab strip, and the page it belongs to.
   * @details The strip is a slot in the resource rather than a section in the
   * flow: sections land in ColumnsView slots, and a strip that governs the
   * whole page cannot live inside one column of it.
   * @private
   */
  View *strafeTabs;
  Button *strafeTabButtons[SETTINGS_STRAFE_TAB_COUNT];
  size_t selectedStrafeTab;

  /**
   * @brief The full-width host the preview section lives in.
   * @details The preview is a picture of the screen, so it reads at the width
   * the screen has - which a ColumnsView slot cannot give it, because a slot is
   * one column of however many the viewport affords. It sits above the strip
   * for the same reason the design does: it is what the sub-tabs are tuning.
   * @private
   */
  View *strafePreviewHost;

  /**
   * @brief The strafe helper preset segments, and the readout beside them.
   * @private
   */
  Button *strafePresetButtons[SETTINGS_STRAFE_PRESET_COUNT];
  Label *strafePresetStatus;

  /**
   * @brief The strafe helper preview, which redraws itself from the cvars.
   * @private
   */
  View *strafePreview;

  /**
   * @brief The read-only renderer backend readout.
   * @private
   */
  Label *gpuBackend;

  /**
   * @brief The footer: the hovered row's cvar.
   * @details The commit status and the Apply/Revert pair moved to the shell
   * footer, which the design gives one of - see MainViewController_SetCommitDelegate.
   * @private
   */
  View *footerBar;
  Label *hint;

  /**
   * @brief Row values as of route entry, for the footer's commit pair.
   * @details One slot per cvar the row stands for: a resolution is two, and the
   * strafe helper's Colours editor is three.
   * @private
   */
  char openingValues[SETTINGS_ROW_COUNT][SETTINGS_ROW_VAR_COUNT][SETTINGS_VALUE_SIZE];

  /**
   * @brief Storage for the "from config" option a string-valued Select adds
   * when the cvar holds a spelling its roster does not name.
   * @details An Option holds the pointer it is handed and never copies it, and
   * a cvar's own string is freed the first time anything writes that cvar - so
   * the route has to own this one.
   * @private
   */
  char legacyValues[SETTINGS_ROW_COUNT][SETTINGS_VALUE_SIZE];

  /**
   * @brief Callback synchronization guard.
   * @private
   */
  bool refreshing;
};

/**
 * @brief The SettingsViewController interface.
 */
struct SettingsViewControllerInterface {

  /**
   * @brief The superclass interface.
   */
  ViewControllerInterface viewControllerInterface;
};

/**
 * @fn Class *SettingsViewController::_SettingsViewController(void)
 * @brief The SettingsViewController archetype.
 * @return The SettingsViewController Class.
 * @memberof SettingsViewController
 */
CGAME_EXPORT Class *_SettingsViewController(void);
