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

#include "cg_local.h"

#include <ObjectivelyMVC/Label.h>
#include <ObjectivelyMVC/Select.h>
#include <ObjectivelyMVC/StackView.h>
#include <ObjectivelyMVC/TextView.h>

#include "ControlsViewController.h"
#include "CvarCheckbox.h"
#include "CvarSelect.h"
#include "CvarSlider.h"
#include "MovementCombatViewController.h"
#include "RaceBindTextView.h"

#define _Class _MovementCombatViewController

#pragma mark - The roster

/**
 * @brief The page strip, in strip order.
 */
static const char *controlsPageNames[CONTROLS_PAGE_COUNT] = {
  "Movement", "Combat", "Ghosts", "Replay", "Modes", "Comms", "Mouse"
};

typedef enum {
  ControlsPageMovement,
  ControlsPageCombat,
  ControlsPageGhosts,
  ControlsPageReplay,
  ControlsPageModes,
  ControlsPageComms,
  ControlsPageMouse
} ControlsPage;

typedef enum {
  ControlsSectionMove,
  ControlsSectionJump,
  ControlsSectionWeapons,
  ControlsSectionHook,
  ControlsSectionRaceAgainst,
  ControlsSectionRacelines,
  ControlsSectionPlayback,
  ControlsSectionVoting,
  ControlsSectionRun,
  ControlsSectionSwitchMode,
  ControlsSectionChat,
  ControlsSectionCapture,
  ControlsSectionMouse
} ControlsSection;

typedef struct {
  ControlsPage page;
  const char *label;
} ControlSectionDescriptor;

/**
 * @brief Sections, in flow order. Two per page keeps a page readable as a pair
 * of columns at any viewport the flow can afford.
 */
static const ControlSectionDescriptor controlSections[CONTROLS_SECTION_COUNT] = {
  { ControlsPageMovement, "Move" },
  { ControlsPageMovement, "Jump & crouch" },
  { ControlsPageCombat, "Weapons" },
  { ControlsPageCombat, "Hook" },
  { ControlsPageGhosts, "Race against" },
  { ControlsPageGhosts, "Racelines & markers" },
  { ControlsPageReplay, "Playback" },
  { ControlsPageReplay, "Voting" },
  { ControlsPageModes, "Run" },
  { ControlsPageModes, "Switch mode" },
  { ControlsPageComms, "Chat" },
  { ControlsPageComms, "Capture" },
  { ControlsPageMouse, "Mouse & view" },
};

typedef enum {
  ControlRowBind,
  ControlRowToggle,
  ControlRowSelect,
  ControlRowSlider
} ControlRowKind;

typedef struct {
  ControlsSection section;
  const char *label;
  ControlRowKind kind;

  /**
   * @brief The bind command for a bind row, else the cvar name.
   */
  const char *command;

  /**
   * @brief The client-shipped keys for this command, primary first.
   * @details SDL_SCANCODE_UNKNOWN means the slot ships empty. Race commands
   * ship unbound by design, so the whole Race half of the roster is
   * defaults-empty, and any key on it reads as modified.
   */
  SDL_Scancode defaults[CONTROLS_SLOT_COUNT];
} ControlDescriptor;

typedef struct {
  const char *command;
  double min, max, step;
} ControlSliderRange;

/**
 * @brief Ranges for the slider rows, which are the only rows that need one.
 */
static const ControlSliderRange controlSliderRanges[] = {
  { "m_sensitivity", 0.5, 10.0, 0.1 },
  { "m_sensitivity_zoom", 0.1, 2.0, 0.05 },
};

/**
 * @brief Every row in the route, grouped by section.
 * @details Defaults mirror the route-scoped subset of DEFAULT_BINDS in
 * client/cl_binds.h, so Restore defaults can never reach a binding the player
 * made outside Controls.
 */
static const ControlDescriptor controlDescriptors[CONTROLS_ROW_COUNT] = {
  { ControlsSectionMove, "Forward", ControlRowBind, "+forward", { SDL_SCANCODE_W } },
  { ControlsSectionMove, "Back", ControlRowBind, "+back", { SDL_SCANCODE_S } },
  { ControlsSectionMove, "Move left", ControlRowBind, "+move_left", { SDL_SCANCODE_A } },
  { ControlsSectionMove, "Move right", ControlRowBind, "+move_right", { SDL_SCANCODE_D } },
  { ControlsSectionMove, "Walk / run modifier", ControlRowBind, "+speed", { SDL_SCANCODE_LSHIFT } },

  { ControlsSectionJump, "Jump", ControlRowBind, "+move_up",
    { SDL_SCANCODE_SPACE, (SDL_Scancode) SDL_SCANCODE_MOUSE3 } },
  { ControlsSectionJump, "Crouch", ControlRowBind, "+move_down", { SDL_SCANCODE_C } },
  { ControlsSectionJump, "Double jump", ControlRowBind, "+double_jump", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionJump, "Always run", ControlRowToggle, "cg_run", { SDL_SCANCODE_UNKNOWN } },

  { ControlsSectionWeapons, "Attack", ControlRowBind, "+attack",
    { (SDL_Scancode) SDL_SCANCODE_MOUSE1 } },
  { ControlsSectionWeapons, "Next weapon", ControlRowBind, "cg_weapon_next",
    { (SDL_Scancode) SDL_SCANCODE_MWHEELDOWN } },
  { ControlsSectionWeapons, "Previous weapon", ControlRowBind, "cg_weapon_previous",
    { (SDL_Scancode) SDL_SCANCODE_MWHEELUP } },
  { ControlsSectionWeapons, "Last weapon", ControlRowBind, "weapon_last", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionWeapons, "Grenade launcher", ControlRowBind, "use grenade launcher", { SDL_SCANCODE_5 } },
  { ControlsSectionWeapons, "Rocket launcher", ControlRowBind, "use rocket launcher", { SDL_SCANCODE_6 } },
  { ControlsSectionWeapons, "Hyperblaster / Super nailgun", ControlRowBind, "use hyperblaster", { SDL_SCANCODE_7 } },

  { ControlsSectionHook, "Fire hook", ControlRowBind, "+hook",
    { (SDL_Scancode) SDL_SCANCODE_MOUSE2 } },
  { ControlsSectionHook, "Hook style", ControlRowSelect, "hook_style", { SDL_SCANCODE_UNKNOWN } },

  { ControlsSectionRaceAgainst, "Race personal best", ControlRowBind, "race pb", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionRaceAgainst, "Race world record", ControlRowBind, "race wr", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionRaceAgainst, "Watch personal best", ControlRowBind, "replay pb", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionRaceAgainst, "Watch world record", ControlRowBind, "replay wr", { SDL_SCANCODE_UNKNOWN } },

  { ControlsSectionRacelines, "Personal best line", ControlRowBind, "raceline pb", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionRacelines, "World record line", ControlRowBind, "raceline wr", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionRacelines, "Hide race line", ControlRowBind, "raceline off", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionRacelines, "Show markers", ControlRowBind, "markers", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionRacelines, "Add marker", ControlRowBind, "marker_add", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionRacelines, "Remove nearest marker", ControlRowBind, "marker_remove", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionRacelines, "Save markers", ControlRowBind, "markers_save", { SDL_SCANCODE_UNKNOWN } },

  { ControlsSectionPlayback, "Pause / resume", ControlRowBind, "replay_control pause", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionPlayback, "Restart replay", ControlRowBind, "replay_control restart", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionPlayback, "Seek back 5 seconds", ControlRowBind, "replay_control back", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionPlayback, "Seek forward 5 seconds", ControlRowBind, "replay_control forward", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionPlayback, "Previous sample", ControlRowBind, "replay_control step_back", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionPlayback, "Next sample", ControlRowBind, "replay_control step_forward", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionPlayback, "Slower replay", ControlRowBind, "replay_control slower", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionPlayback, "Faster replay", ControlRowBind, "replay_control faster", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionPlayback, "Exit replay", ControlRowBind, "replay_cancel", { SDL_SCANCODE_UNKNOWN } },

  { ControlsSectionVoting, "Vote yes", ControlRowBind, "race vote yes", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionVoting, "Vote no", ControlRowBind, "race vote no", { SDL_SCANCODE_UNKNOWN } },

  // One key covers both readings: in Race mode `kill` restarts the run, and in
  // Practice mode it drops the player back at their stored spawn - see
  // Race_PrepareClientSpawn. The design draws these as two rows, but there is no
  // load command to bind the second one to, and a second row on `kill` would
  // mirror this one's keys and clobber them on capture.
  { ControlsSectionRun, "Restart run / load spawn", ControlRowBind, "kill", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionRun, "Store practice spawn", ControlRowBind, "store", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionRun, "Practice noclip", ControlRowBind, "no_clip", { SDL_SCANCODE_UNKNOWN } },

  { ControlsSectionSwitchMode, "Race mode", ControlRowBind, "mode race", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionSwitchMode, "Practice mode", ControlRowBind, "mode practice", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionSwitchMode, "Spectator mode", ControlRowBind, "mode spectator", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionSwitchMode, "Show / hide runners", ControlRowBind, "jumpers", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionSwitchMode, "Toggle chase camera", ControlRowBind, "chase_toggle", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionSwitchMode, "Previous player", ControlRowBind, "chase_previous", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionSwitchMode, "Next player", ControlRowBind, "chase_next", { SDL_SCANCODE_UNKNOWN } },

  { ControlsSectionChat, "Chat", ControlRowBind, "cl_message_mode", { SDL_SCANCODE_T, SDL_SCANCODE_RETURN } },
  { ControlsSectionChat, "Team chat", ControlRowBind, "cl_message_mode_2", { SDL_SCANCODE_Y } },
  { ControlsSectionChat, "Show score", ControlRowBind, "+score", { SDL_SCANCODE_TAB } },

  { ControlsSectionCapture, "Take screenshot", ControlRowBind, "r_screenshot", { SDL_SCANCODE_F12 } },
  { ControlsSectionCapture, "Take screenshot (3D only)", ControlRowBind, "r_screenshot view", { SDL_SCANCODE_F11 } },

  { ControlsSectionMouse, "Sensitivity", ControlRowSlider, "m_sensitivity", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionMouse, "Zoom sensitivity", ControlRowSlider, "m_sensitivity_zoom", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionMouse, "Invert mouse", ControlRowToggle, "m_invert", { SDL_SCANCODE_UNKNOWN } },
  { ControlsSectionMouse, "Smooth mouse", ControlRowToggle, "m_interpolate", { SDL_SCANCODE_UNKNOWN } },
};

/**
 * @brief The copy the filter row carries when nothing is capturing.
 */
static const char *controlsIdleHint =
  "Select a slot, then press a key or mouse button. Backspace unbinds.";

/**
 * @brief The copy it carries while a slot waits for input.
 */
static const char *controlsCaptureHint =
  "Press a key or mouse button. Esc cancels, Backspace unbinds.";

#pragma mark - Row state

static bool isBindRow(size_t row) {
  return controlDescriptors[row].kind == ControlRowBind;
}

/**
 * @brief Returns the cvar default for a cvar-backed row, or the empty string.
 */
static const char *cvarDefault(size_t row) {

  const cvar_t *var = cgi.GetCvar(controlDescriptors[row].command);
  return var && var->default_string ? var->default_string : "";
}

/**
 * @brief Returns the current cvar spelling, or the empty string.
 */
static const char *cvarString(const char *command) {

  const char *value = command ? cgi.GetCvarString(command) : NULL;
  return value ? value : "";
}

/**
 * @brief Returns true when the row differs from what the client ships.
 * @remarks This drives the row dot and the row revert, not the footer commit
 * pair - that one measures against route entry instead.
 */
static bool isRowModified(size_t row) {

  const ControlDescriptor *descriptor = &controlDescriptors[row];

  if (isBindRow(row)) {
    for (int slot = 0; slot < CONTROLS_SLOT_COUNT; slot++) {
      if (RaceBindTextView_KeyForSlot(descriptor->command, slot) != descriptor->defaults[slot]) {
        return true;
      }
    }
    return false;
  }

  return strcmp(cvarString(descriptor->command), cvarDefault(row)) != 0;
}

/**
 * @brief Returns true when the row differs from the value it had on entry.
 */
static bool isRowDirty(const MovementCombatViewController *self, size_t row) {

  const ControlDescriptor *descriptor = &controlDescriptors[row];

  if (isBindRow(row)) {
    for (int slot = 0; slot < CONTROLS_SLOT_COUNT; slot++) {
      if (RaceBindTextView_KeyForSlot(descriptor->command, slot) != self->openingKeys[row][slot]) {
        return true;
      }
    }
    return false;
  }

  return strcmp(cvarString(descriptor->command), self->openingValues[row]) != 0;
}

static void captureOpeningValues(MovementCombatViewController *self) {

  for (size_t row = 0; row < CONTROLS_ROW_COUNT; row++) {
    const ControlDescriptor *descriptor = &controlDescriptors[row];

    if (isBindRow(row)) {
      for (int slot = 0; slot < CONTROLS_SLOT_COUNT; slot++) {
        self->openingKeys[row][slot] = RaceBindTextView_KeyForSlot(descriptor->command, slot);
      }
      self->openingValues[row][0] = '\0';
    } else {
      q_strlcpy(self->openingValues[row], cvarString(descriptor->command),
                CONTROLS_VALUE_SIZE);
    }
  }
}

/**
 * @brief Clears every key bound to a row's command.
 */
static void clearRowBindings(size_t row) {

  const char *command = controlDescriptors[row].command;

  SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
  while ((key = cgi.KeyForBind(key, command)) != SDL_SCANCODE_UNKNOWN) {
    cgi.BindKey(key, NULL);
  }
}

/**
 * @brief Rebinds a row's command to the given key set, primary first.
 */
static void setRowBindings(size_t row, const SDL_Scancode *keys) {

  clearRowBindings(row);

  for (int slot = 0; slot < CONTROLS_SLOT_COUNT; slot++) {
    if (keys[slot] != SDL_SCANCODE_UNKNOWN) {
      cgi.BindKey(keys[slot], controlDescriptors[row].command);
    }
  }
}

/**
 * @brief Restores one row to what the client ships.
 * @details Race commands ship unbound, so restoring a Race row clears it. That
 * is deliberate and settled: "factory" means the first-run state, including the
 * Race rows that arrive with no key at all. The narrower policy - skipping rows
 * whose `defaults[0]` is `SDL_SCANCODE_UNKNOWN` - was considered and rejected,
 * because a reset that silently leaves half the roster alone is harder to
 * reason about than one that resets everything.
 *
 * What protects the player is the route chrome rather than this function: the
 * action is named "Reset to factory bindings", it sits at the far end of the
 * footer away from the commit pair, and it asks for confirmation first - see
 * ControlsViewController::didClickRestoreDefaults. It also deliberately does
 * not re-baseline, so Revert changes still returns the player to the last
 * values they applied.
 */
static void restoreRowDefaults(size_t row) {

  if (isBindRow(row)) {
    setRowBindings(row, controlDescriptors[row].defaults);
  } else {
    cgi.SetCvarString(controlDescriptors[row].command, cvarDefault(row));
  }
}

static void restoreRowOpening(const MovementCombatViewController *self, size_t row) {

  if (isBindRow(row)) {
    setRowBindings(row, self->openingKeys[row]);
  } else {
    cgi.SetCvarString(controlDescriptors[row].command, self->openingValues[row]);
  }
}

#pragma mark - Refresh

static void invalidateLayoutChain(View *view) {

  while (view) {
    view->needsLayout = true;
    view = view->superview;
  }
}

/**
 * @brief Repaints the shared bind pill: which slots show, and whether it reads
 * as capturing.
 * @details Slot 1 is discovery order rather than a persisted "alternate" flag,
 * so an empty alternate is not a slot the player left blank - it is a slot that
 * does not exist yet. It and its comma stay hidden until a key lands there, or
 * until the player aims at it: only a bound alternate earns a comma.
 *
 * The capture fill has to move up with them. The border and background now sit
 * on the pill, so a focused slot's own box is transparent and `:focused` alone
 * would leave capture invisible. The dialect has no `:has`, so the pill carries
 * a class the route toggles rather than a selector that reaches into it.
 */
static void refreshCapture(MovementCombatViewController *self) {

  for (size_t row = 0; row < CONTROLS_ROW_COUNT; row++) {

    View *pill = self->rowPills[row];
    if (pill == NULL) {
      continue;
    }

    bool capturing = false, altFocused = false;
    for (int slot = 0; slot < CONTROLS_SLOT_COUNT; slot++) {
      const Control *field = (Control *) self->rowFields[row][slot];
      if (field && (field->state & ControlStateFocused)) {
        capturing = true;
        if (slot > 0) {
          altFocused = true;
        }
      }
    }

    const char *command = controlDescriptors[row].command;

    const bool hideAlt = !altFocused &&
      RaceBindTextView_KeyForSlot(command, 1) == SDL_SCANCODE_UNKNOWN;

    // The empty fill belongs to the pill, and no selector can reach an ancestor
    // from the slot that knows it is empty - so the route hands it up. Slot 0 is
    // the first key discovered, so an empty slot 0 means the row is unbound.
    const bool unbound = RaceBindTextView_KeyForSlot(command, 0) == SDL_SCANCODE_UNKNOWN;

    View *alt = self->rowFields[row][1];
    View *separator = self->rowSeparators[row];

    if ((alt && alt->hidden != hideAlt) || (separator && separator->hidden != hideAlt)) {
      if (alt) {
        alt->hidden = hideAlt;
      }
      if (separator) {
        separator->hidden = hideAlt;
      }
      invalidateLayoutChain(pill);
    }

    if (capturing) {
      $(pill, addClassName, "capturing");
    } else {
      $(pill, removeClassName, "capturing");
    }

    if (unbound) {
      $(pill, addClassName, "unbound");
    } else {
      $(pill, removeClassName, "unbound");
    }
  }
}

/**
 * @brief Repaints the row chrome the route owns: the modified dot, its revert,
 * and the footer dirty count.
 */
static void refreshRows(MovementCombatViewController *self) {

  size_t dirtyCount = 0;

  for (size_t row = 0; row < CONTROLS_ROW_COUNT; row++) {

    const bool modified = isRowModified(row);
    self->rowDots[row]->hidden = !modified;
    self->rowReverts[row]->control.view.hidden = !modified;

    if (modified) {
      $(self->rowViews[row], addClassName, "modified");
    } else {
      $(self->rowViews[row], removeClassName, "modified");
    }

    if (isRowDirty(self, row)) {
      dirtyCount++;
    }
  }

  refreshCapture(self);

  ControlsViewController_UpdateStatus(self->viewController.view, dirtyCount);
}

/**
 * @brief Pulls every bind field back from the engine, then repaints the chrome.
 */
static void refreshBindings(MovementCombatViewController *self) {

  ControlsViewController_RefreshBindings(self->viewController.view);
  refreshRows(self);
}

#pragma mark - Filter and pages

static bool containsIgnoringCase(const char *text, const char *query) {

  if (*query == '\0') {
    return true;
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

/**
 * @brief Shows the rows and sections that survive the current query.
 * @details A query reaches every page, not just the open one - otherwise a
 * binding one tab over reads as "does not exist". Off-page sections that match
 * are shown in place and tagged with the page they belong to, and each tab
 * carries its own hit count, so the player can see where the rest of the
 * matches are without opening every page in turn.
 */
static void refreshFilter(MovementCombatViewController *self) {

  // An empty Objectively String never allocates, so `chars` is NULL rather than
  // "" - and TextView always holds a String, so testing attributedText alone
  // guards the wrong pointer. An empty filter is the state this route opens in.
  const String *text = self->filter->attributedText;
  const char *query = text && text->chars ? text->chars : "";
  const bool searching = *query != '\0';

  size_t pageHits[CONTROLS_PAGE_COUNT] = { 0 };
  size_t sectionHits[CONTROLS_SECTION_COUNT] = { 0 };
  size_t lastVisibleRow[CONTROLS_SECTION_COUNT];

  for (size_t section = 0; section < CONTROLS_SECTION_COUNT; section++) {
    lastVisibleRow[section] = CONTROLS_ROW_COUNT;
  }

  for (size_t row = 0; row < CONTROLS_ROW_COUNT; row++) {
    const ControlDescriptor *descriptor = &controlDescriptors[row];
    const bool hit = containsIgnoringCase(descriptor->label, query) ||
                     containsIgnoringCase(descriptor->command, query);

    self->rowViews[row]->hidden = !hit;
    if (hit) {
      sectionHits[descriptor->section]++;
      pageHits[controlSections[descriptor->section].page]++;
      lastVisibleRow[descriptor->section] = row;
    }
  }

  // A separator under the last visible row of a section would draw a line to
  // nowhere, and which row is last moves with the query.
  for (size_t row = 0; row < CONTROLS_ROW_COUNT; row++) {
    const size_t last = lastVisibleRow[controlDescriptors[row].section];
    self->rowRules[row]->hidden = last == row;
  }

  bool any = false;
  for (size_t section = 0; section < CONTROLS_SECTION_COUNT; section++) {

    const ControlsPage page = controlSections[section].page;
    const bool onPage = (size_t) page == self->selectedPage;
    const bool visible = sectionHits[section] > 0 && (searching || onPage);
    const bool tagged = visible && searching && !onPage;

    self->sectionViews[section]->hidden = !visible;
    self->sectionTags[section]->view.hidden = !tagged;
    if (tagged) {
      $(self->sectionTags[section]->text, setText, controlsPageNames[page]);
    }

    any = any || visible;
  }

  for (size_t page = 0; page < CONTROLS_PAGE_COUNT; page++) {

    Button *button = self->pageButtons[page];
    if (searching) {
      $(button->title, setTextWithFormat, "%s  %zu", controlsPageNames[page], pageHits[page]);
    } else {
      $(button->title, setText, controlsPageNames[page]);
    }

    if (searching && pageHits[page] == 0) {
      $((View *) button, addClassName, "noHits");
    } else {
      $((View *) button, removeClassName, "noHits");
    }
  }

  self->emptyState->view.hidden = any;

  // View::layoutIfNeeded lays out a view only if that view carries needsLayout,
  // so marking the route root alone leaves the flow beneath it untouched and a
  // newly revealed section keeps geometry it never received. Marking upward
  // from the sections reaches the ColumnsView, which re-places its columns and
  // re-marks each one's subtree - see ColumnsView::placeColumn. The rows below
  // are its business, not this route's.
  for (size_t section = 0; section < CONTROLS_SECTION_COUNT; section++) {
    invalidateLayoutChain(self->sectionViews[section]);
  }
  invalidateLayoutChain((View *) self->emptyState);
}

/**
 * @brief Names the command under the pointer in the footer hint.
 * @details The raw command - `+move_up`, `replay_control step_back`, `cg_run` -
 * never appears in a row, so hovering one is how a player learns the console
 * name for the key they just bound.
 */
static void refreshHint(MovementCombatViewController *self) {

  float mouseX, mouseY;
  SDL_GetMouseState(&mouseX, &mouseY);

  const SDL_Point point = MakePoint(mouseX, mouseY);
  const char *hint = NULL;

  for (size_t row = 0; row < CONTROLS_ROW_COUNT && hint == NULL; row++) {
    if (self->rowViews[row]->hidden) {
      continue;
    }
    for (int slot = 0; slot < CONTROLS_SLOT_COUNT; slot++) {
      View *field = self->rowFields[row][slot];
      if (field && $(field, containsPoint, &point)) {
        hint = controlDescriptors[row].command;
        break;
      }
    }
  }

  ControlsViewController_SetHint(self->viewController.view, hint);
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

static void selectPage(MovementCombatViewController *self, size_t page) {

  if (page >= CONTROLS_PAGE_COUNT) {
    return;
  }

  self->selectedPage = page;
  for (size_t i = 0; i < CONTROLS_PAGE_COUNT; i++) {
    setControlFlag((Control *) self->pageButtons[i], ControlStateSelected, i == page);
  }

  refreshFilter(self);
}

#pragma mark - Delegates

/**
 * @brief TextViewDelegate callback for a bind slot that finished capturing.
 */
static void didBindKey(TextView *textView) {

  MovementCombatViewController *self = textView->delegate.self;

  refreshBindings(self);
  refreshHint(self);
}

static void didClickPage(Button *button) {

  MovementCombatViewController *self = button->delegate.self;

  $(self->filter, setAttributedText, "");
  selectPage(self, (size_t) (intptr_t) button->delegate.data);
}

static void didEditFilter(TextView *textView) {

  MovementCombatViewController *self = textView->delegate.self;

  refreshFilter(self);
}

static void didClickRowRevert(Button *button) {

  MovementCombatViewController *self = button->delegate.self;
  const size_t row = (size_t) (intptr_t) button->delegate.data;

  restoreRowDefaults(row);
  refreshBindings(self);
}

#pragma mark - Row construction

/**
 * @brief Builds the control a non-bind row shows on its right-hand side.
 */
static View *rowControl(size_t row) {

  const ControlDescriptor *descriptor = &controlDescriptors[row];
  cvar_t *var = cgi.GetCvar(descriptor->command);

  switch (descriptor->kind) {
    case ControlRowToggle:
      return (View *) $(alloc(CvarCheckbox), initWithVariable, var);

    case ControlRowSelect: {
      CvarSelect *select = $(alloc(CvarSelect), initWithVariable, var);
      select->expectsStringValue = true;
      $((Select *) select, addOption, "Pull", "pull");
      $((Select *) select, addOption, "Manual swing", "swing_manual");
      $((Select *) select, addOption, "Automatic swing", "swing_auto");
      return (View *) select;
    }

    case ControlRowSlider:
      for (size_t i = 0; i < lengthof(controlSliderRanges); i++) {
        const ControlSliderRange *range = &controlSliderRanges[i];
        if (strcmp(range->command, descriptor->command) == 0) {
          return (View *) $(alloc(CvarSlider), initWithVariable, var,
                            range->min, range->max, range->step);
        }
      }
      break;

    case ControlRowBind:
      break;
  }

  return NULL;
}

/**
 * @brief Builds one row: dot and label pinned left, fields pinned right.
 * @details The row is a plain View rather than a StackView so that the two
 * groups pin to opposite edges however long the action name runs - a horizontal
 * StackView would lay them out end to end instead, and the field column would
 * step in and out with every label.
 */
static View *makeRow(MovementCombatViewController *self, size_t row) {

  const ControlDescriptor *descriptor = &controlDescriptors[row];

  View *view = $(alloc(View), initWithFrame, NULL);
  $(view, addClassName, "controlRow");

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

  // A slider is dragged, not clicked, so it gets a wider cell than the bind
  // pills beside it. The cell is right-pinned, so the extra width runs leftward
  // and the field column still ends on one edge.
  if (descriptor->kind == ControlRowSlider) {
    $((View *) right, addClassName, "sliderCell");
  }

  if (descriptor->kind == ControlRowBind) {

    // Both slots read as one pill - "Q, 6" rather than two boxes - so the
    // border and fill live on the shared rowRight and the slots inside it are
    // transparent. The comma has to be a view of its own: the dialect has no
    // generated content, and a layout gap cannot be punctuation.
    $((View *) right, addClassName, "bindPill");
    self->rowPills[row] = (View *) right;

    for (int slot = 0; slot < CONTROLS_SLOT_COUNT; slot++) {

      if (slot > 0) {
        Label *separator = $(alloc(Label), initWithText, ",", NULL);
        $((View *) separator, addClassName, "bindSeparator");
        $((View *) right, addSubview, (View *) separator);
        self->rowSeparators[row] = (View *) separator;
        release(separator);
      }

      RaceBindTextView *field =
        $(alloc(RaceBindTextView), initWithBindAndSlot, descriptor->command, slot);

      if (slot > 0) {
        $((View *) field, addClassName, "altBind");
      }

      field->bindTextView.textView.delegate = (TextViewDelegate) {
        .self = self,
        .didEndEditing = didBindKey
      };

      $((View *) right, addSubview, (View *) field);
      self->rowFields[row][slot] = (View *) field;
      release(field);
    }
  } else {
    View *control = rowControl(row);
    assert(control);
    $((View *) right, addSubview, control);
    self->rowFields[row][0] = control;
    self->rowFields[row][1] = NULL;
    self->rowPills[row] = NULL;
    self->rowSeparators[row] = NULL;
    release(control);
  }

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
static View *makeSection(MovementCombatViewController *self, size_t section) {

  StackView *view = $(alloc(StackView), initWithFrame, NULL);
  $((View *) view, addClassName, "controlSection");

  StackView *head = $(alloc(StackView), initWithFrame, NULL);
  $((View *) head, addClassName, "sectionHead");
  head->axis = StackViewAxisHorizontal;

  Label *eyebrow = $(alloc(Label), initWithText, controlSections[section].label, NULL);
  $((View *) eyebrow, addClassName, "sectionEyebrow");
  $((View *) head, addSubview, (View *) eyebrow);
  release(eyebrow);

  Label *tag = $(alloc(Label), initWithText, "", NULL);
  $((View *) tag, addClassName, "sectionTag");
  tag->view.hidden = true;
  $((View *) head, addSubview, (View *) tag);
  self->sectionTags[section] = tag;
  release(tag);

  $((View *) view, addSubview, (View *) head);
  release(head);

  View *rule = $(alloc(View), initWithFrame, NULL);
  $(rule, addClassName, "sectionRule");
  $((View *) view, addSubview, rule);
  release(rule);

  StackView *rows = $(alloc(StackView), initWithFrame, NULL);
  $((View *) rows, addClassName, "controlRows");

  for (size_t row = 0; row < CONTROLS_ROW_COUNT; row++) {
    if ((size_t) controlDescriptors[row].section != section) {
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

#pragma mark - Route actions

void MovementCombatViewController_RestoreDefaults(ViewController *viewController) {

  MovementCombatViewController *self = (MovementCombatViewController *) viewController;

  for (size_t row = 0; row < CONTROLS_ROW_COUNT; row++) {
    restoreRowDefaults(row);
  }

  // No captureOpeningValues here, unlike Apply: the factory set is written live
  // but stays measured against the last applied values, so it lands in the dirty
  // count and Revert changes can take it back.
  refreshBindings(self);
}

void MovementCombatViewController_RevertChanges(ViewController *viewController) {

  MovementCombatViewController *self = (MovementCombatViewController *) viewController;

  for (size_t row = 0; row < CONTROLS_ROW_COUNT; row++) {
    if (isRowDirty(self, row)) {
      restoreRowOpening(self, row);
    }
  }

  refreshBindings(self);
}

void MovementCombatViewController_ApplyChanges(ViewController *viewController) {

  MovementCombatViewController *self = (MovementCombatViewController *) viewController;

  // Bindings and mouse cvars are live the moment they are set - there is no
  // deferred half to flush here, and the same is true of the Settings route.
  // Apply accepts the current state as the new baseline, which is what clears
  // the count and re-arms Revert changes against the next batch of edits.
  captureOpeningValues(self);
  refreshRows(self);
}

#pragma mark - ViewController

static void resolveOutlets(MovementCombatViewController *self) {

  Outlet outlets[] = MakeOutlets(
    MakeOutlet("controlsFilter", &self->filter),
    MakeOutlet("controlsEmptyState", &self->emptyState)
  );

  $(self->viewController.view, resolve, outlets);
  assert(self->filter);
  assert(self->emptyState);
}

/**
 * @see ViewController::loadView(ViewController *)
 */
static void loadView(ViewController *viewController) {

  super(ViewController, viewController, loadView);

  MovementCombatViewController *self = (MovementCombatViewController *) viewController;

  $(viewController->view, awakeWithResourceName, "ui/controls/MovementCombatViewController.json");
  resolveOutlets(self);

  View *tabs = $(viewController->view, descendantWithIdentifier, "controlsPageTabs");
  assert(tabs);

  for (size_t page = 0; page < CONTROLS_PAGE_COUNT; page++) {

    Button *button = $(alloc(Button), initWithTitle, controlsPageNames[page]);
    $((View *) button, addClassName, "controlsPageTab");
    button->delegate = (ButtonDelegate) {
      .self = self,
      .data = (ident) (intptr_t) page,
      .didClick = didClickPage
    };

    $(tabs, addSubview, (View *) button);
    self->pageButtons[page] = button;
    release(button);
  }

  // The sections are built here rather than authored in the JSON because every
  // one of them is the same shape over the same descriptor table that the
  // filter, the dot and the commit pair all read. One table means a row cannot
  // appear in the roster without also being searchable and revertible.
  View *columns = $(viewController->view, descendantWithIdentifier, "controlsColumns");
  assert(columns);

  for (size_t section = 0; section < CONTROLS_SECTION_COUNT; section++) {
    View *sectionView = makeSection(self, section);
    $(columns, addSubview, sectionView);
    self->sectionViews[section] = sectionView;
    release(sectionView);
  }

  self->filter->delegate = (TextViewDelegate) {
    .self = self,
    .didEdit = didEditFilter
  };

  captureOpeningValues(self);
  selectPage(self, ControlsPageMovement);
}

/**
 * @see ViewController::viewWillAppear(ViewController *)
 */
static void viewWillAppear(ViewController *viewController) {

  super(ViewController, viewController, viewWillAppear);

  MovementCombatViewController *self = (MovementCombatViewController *) viewController;

  captureOpeningValues(self);
  refreshFilter(self);
  refreshBindings(self);
  refreshHint(self);

}

/**
 * @brief Returns true while a bind slot is waiting for a key.
 */
static bool isCapturing(const MovementCombatViewController *self) {

  for (size_t row = 0; row < CONTROLS_ROW_COUNT; row++) {
    if (!isBindRow(row)) {
      continue;
    }
    for (int slot = 0; slot < CONTROLS_SLOT_COUNT; slot++) {
      const Control *field = (Control *) self->rowFields[row][slot];
      if (field && (field->state & ControlStateFocused)) {
        return true;
      }
    }
  }

  return false;
}

/**
 * @see ViewController::respondToEvent(ViewController *, const SDL_Event *)
 */
static void respondToEvent(ViewController *viewController, const SDL_Event *event) {

  super(ViewController, viewController, respondToEvent, event);

  MovementCombatViewController *self = (MovementCombatViewController *) viewController;

  if (event->type == SDL_EVENT_MOUSE_MOTION ||
      (event->type == MVC_VIEW_EVENT &&
       (event->user.code == ViewEventMouseEnter || event->user.code == ViewEventMouseLeave))) {
    refreshHint(self);
  }

  // Capture opens on a click and closes on the next key, so the copy only has
  // to move on those two events. The roster itself is refreshed by the bind
  // delegate and the filter by its own didEdit - not from here.
  if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
      event->type == SDL_EVENT_MOUSE_BUTTON_UP) {

    Label *hint = (Label *) $(viewController->view, descendantWithIdentifier, "controlsCaptureHint");
    if (hint) {
      $(hint->text, setText, isCapturing(self) ? controlsCaptureHint : controlsIdleHint);
    }

    // Capture opens on the click itself, which never reaches refreshBindings -
    // so the pill's fill and its alternate slot are repainted from here too.
    refreshCapture(self);
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
 * @fn Class *MovementCombatViewController::_MovementCombatViewController(void)
 * @memberof MovementCombatViewController
 */
Class *_MovementCombatViewController(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "MovementCombatViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(MovementCombatViewController),
      .interfaceOffset = offsetof(MovementCombatViewController, interface),
      .interfaceSize = sizeof(MovementCombatViewControllerInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
