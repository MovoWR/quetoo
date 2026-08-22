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

#include <ctype.h>

// The Race cg_score.h, not the one cg_local.h pulls from cgame/common - the
// roster snapshot the Players section draws is a Race extension.
#include "cg_score.h"
#include "race_admin_types.h"

#include "AdminViewController.h"

#define _Class _AdminViewController

/**
 * @brief Sections, in flow order.
 * @details The first four are ColumnsView slots; the last two are full width,
 * which is what the design's `sec.wide` means - a roster table and a raw
 * command form both read badly in a 460px column.
 */
typedef enum {
  AdminSectionServer,
  AdminSectionRules,
  AdminSectionVoting,
  AdminSectionSession,
  AdminSectionPlayers,
  AdminSectionAdvanced
} AdminSection;

typedef struct {
  const char *label;

  /**
   * @brief The section annotation, drawn right of the eyebrow. NULL for none.
   */
  const char *metric;

  /**
   * @brief True for a section that leaves the column flow and runs full width.
   */
  bool wide;
} AdminSectionDescriptor;

static const AdminSectionDescriptor adminSections[ADMIN_SECTION_COUNT] = {
  { "Server control", NULL, false },
  { "Race rules", NULL, false },
  { "Voting", "applies on next map", false },
  { "Session", NULL, false },
  { "Players", NULL, true },
  { "Advanced", NULL, true },
};

typedef enum {
  AdminRowToggle,
  AdminRowSlider,
  AdminRowSelect,
  AdminRowText,
  AdminRowAction
} AdminRowKind;

/**
 * @brief The command an action row posts.
 */
typedef enum {
  AdminActionNone,
  AdminActionChangeMap,
  AdminActionRestartMap,
  AdminActionCancelVote,
  AdminActionStatus,
  AdminActionHelp,
  AdminActionLogout,
  AdminActionKick,
  AdminActionSettingsGet,
  AdminActionSettingsSource,
  AdminActionSettingsSet,
  AdminActionSettingsReset
} AdminAction;

/**
 * @brief The free-text field a text row owns, so the route can reach it by name
 * when an action row needs its contents.
 */
typedef enum {
  AdminFieldNone,
  AdminFieldMapName,
  AdminFieldPlayerSlot,
  AdminFieldSettingsScope,
  AdminFieldSettingsKey,
  AdminFieldSettingsValue
} AdminField;

typedef enum {
  AdminSelectNone,
  AdminSelectCheckpointFeedback
} AdminSelectKind;

typedef struct {
  AdminSection section;
  const char *label;
  AdminRowKind kind;

  /**
   * @brief For a settings row: the catalog key it writes, and the scope it
   * writes it in. A key whose `runtime_mutable` is false is rejected at runtime
   * scope by the server, so those rows write `global` and the section says so.
   * @details These mirror the catalog in `src/game/race/race_settings.c`. The
   * cgame does not compile that translation unit, so the two are kept in step by
   * hand - `check_race` asserts the catalog, this table follows it.
   */
  const char *key;
  const char *scope;
  double min, max, step;

  /**
   * @brief Slider readout format. Consumes one double - see Slider::formatLabel.
   */
  const char *format;

  /**
   * @brief The value the shipped catalog defaults this key to, as the server
   * would parse it. Also what the row's revert restores.
   */
  const char *initial;

  AdminSelectKind select;
  AdminField field;
  AdminAction action;
  const char *placeholder;

  /**
   * @brief The capability this row requires, or 0 for a row any admin may use.
   */
  uint16_t capability;
} AdminRowDescriptor;

/**
 * @brief Every row in the route, grouped by section.
 * @details One table: the filter, the modified dot, the row revert, the
 * capability gate and the footer hint all read it, so a row cannot appear in the
 * route without also being searchable and gated.
 *
 * The design's Replay and Record cues sections are deliberately absent. Neither
 * has a setting behind it in the server's catalog, and a control the server
 * cannot honour is worse than no control at all.
 */
static const AdminRowDescriptor adminRows[ADMIN_ROW_COUNT] = {

  { AdminSectionServer, "Map name", AdminRowText, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldMapName, AdminActionNone, "Exact map name",
    RACE_ADMIN_CAP_MAP_CHANGE },
  { AdminSectionServer, "Change map", AdminRowAction, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldNone, AdminActionChangeMap, NULL,
    RACE_ADMIN_CAP_MAP_CHANGE },
  { AdminSectionServer, "Restart current map", AdminRowAction, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldNone, AdminActionRestartMap, NULL,
    RACE_ADMIN_CAP_MAP_CHANGE },
  { AdminSectionServer, "Cancel active vote", AdminRowAction, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldNone, AdminActionCancelVote, NULL,
    RACE_ADMIN_CAP_VOTE_ADMIN },

  { AdminSectionRules, "Weapons", AdminRowToggle, "weapons", "global", 0, 0, 0, NULL, "1",
    AdminSelectNone, AdminFieldNone, AdminActionNone, NULL,
    RACE_ADMIN_CAP_SETTINGS_MUTATE },
  { AdminSectionRules, "Finish cue", AdminRowToggle, "finish_cue_enabled", "runtime",
    0, 0, 0, NULL, "1",
    AdminSelectNone, AdminFieldNone, AdminActionNone, NULL,
    RACE_ADMIN_CAP_SETTINGS_MUTATE },
  { AdminSectionRules, "Finish cue volume", AdminRowSlider, "finish_cue_gain", "runtime",
    1, 100, 1, "%.0f%%", "100",
    AdminSelectNone, AdminFieldNone, AdminActionNone, NULL,
    RACE_ADMIN_CAP_SETTINGS_MUTATE },
  { AdminSectionRules, "Checkpoint feedback", AdminRowSelect, "checkpoint_feedback", "runtime",
    0, 0, 0, NULL, "time",
    AdminSelectCheckpointFeedback, AdminFieldNone, AdminActionNone, NULL,
    RACE_ADMIN_CAP_SETTINGS_MUTATE },

  { AdminSectionVoting, "Voting time", AdminRowSlider, "voting_time", "global",
    0, 300, 5, "%.0f s", "30",
    AdminSelectNone, AdminFieldNone, AdminActionNone, NULL,
    RACE_ADMIN_CAP_SETTINGS_MUTATE },
  { AdminSectionVoting, "Max vote starts", AdminRowSlider, "max_votes", "global",
    0, 100, 1, "%.0f", "3",
    AdminSelectNone, AdminFieldNone, AdminActionNone, NULL,
    RACE_ADMIN_CAP_SETTINGS_MUTATE },
  { AdminSectionVoting, "Vote menu duration", AdminRowSlider, "vote_menu_duration", "global",
    0, 300, 5, "%.0f s", "20",
    AdminSelectNone, AdminFieldNone, AdminActionNone, NULL,
    RACE_ADMIN_CAP_SETTINGS_MUTATE },
  { AdminSectionVoting, "Vote menu choices", AdminRowSlider, "vote_menu_choices", "global",
    0, 8, 1, "%.0f", "3",
    AdminSelectNone, AdminFieldNone, AdminActionNone, NULL,
    RACE_ADMIN_CAP_SETTINGS_MUTATE },
  { AdminSectionVoting, "Spectator voting", AdminRowToggle, "vote_allow_spectators", "global",
    0, 0, 0, NULL, "0",
    AdminSelectNone, AdminFieldNone, AdminActionNone, NULL,
    RACE_ADMIN_CAP_SETTINGS_MUTATE },

  { AdminSectionSession, "Admin status", AdminRowAction, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldNone, AdminActionStatus, NULL, 0 },
  { AdminSectionSession, "Command help", AdminRowAction, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldNone, AdminActionHelp, NULL, 0 },
  { AdminSectionSession, "Log out", AdminRowAction, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldNone, AdminActionLogout, NULL, 0 },

  { AdminSectionPlayers, "Client slot", AdminRowText, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldPlayerSlot, AdminActionNone, "Slot number",
    RACE_ADMIN_CAP_PLAYER_KICK },
  { AdminSectionPlayers, "Kick player", AdminRowAction, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldNone, AdminActionKick, NULL,
    RACE_ADMIN_CAP_PLAYER_KICK },

  { AdminSectionAdvanced, "Scope", AdminRowText, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldSettingsScope, AdminActionNone, "global | map | runtime",
    RACE_ADMIN_CAP_SETTINGS_MUTATE },
  { AdminSectionAdvanced, "Key", AdminRowText, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldSettingsKey, AdminActionNone, "Setting key", 0 },
  { AdminSectionAdvanced, "Value", AdminRowText, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldSettingsValue, AdminActionNone, "Setting value",
    RACE_ADMIN_CAP_SETTINGS_MUTATE },
  { AdminSectionAdvanced, "Inspect value", AdminRowAction, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldNone, AdminActionSettingsGet, NULL, 0 },
  { AdminSectionAdvanced, "Inspect source", AdminRowAction, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldNone, AdminActionSettingsSource, NULL, 0 },
  { AdminSectionAdvanced, "Set value", AdminRowAction, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldNone, AdminActionSettingsSet, NULL,
    RACE_ADMIN_CAP_SETTINGS_MUTATE },
  { AdminSectionAdvanced, "Reset value", AdminRowAction, NULL, NULL, 0, 0, 0, NULL, NULL,
    AdminSelectNone, AdminFieldNone, AdminActionSettingsReset, NULL,
    RACE_ADMIN_CAP_SETTINGS_MUTATE },
};

static const char *const adminCheckpointFeedback[] = { "time", "silent" };

/**
 * @brief The option roster a Select row carries, and its length.
 * @details One accessor rather than the roster inlined at each use, so a second
 * enum setting only has to be named here and in the row's descriptor.
 */
static const char *const *adminSelectOptions(AdminSelectKind kind, size_t *count) {

  switch (kind) {

    case AdminSelectCheckpointFeedback:
      *count = lengthof(adminCheckpointFeedback);
      return adminCheckpointFeedback;

    default:
      *count = 0;
      return NULL;
  }
}

#pragma mark - Validation

/**
 * @brief The token shape the server's own parsers accept.
 * @details Validating here is a courtesy to the admin, not a security boundary:
 * `race admin` revalidates authority and every argument server-side. What it
 * buys is a disabled control instead of a console error.
 */
static bool isSafeToken(const char *value) {

  if (!value || !*value || q_strlen(value) > 64u) {
    return false;
  }

  for (const unsigned char *c = (const unsigned char *) value; *c; c++) {
    if (!isalnum(*c) && *c != '_' && *c != '-' && *c != '.') {
      return false;
    }
  }

  return true;
}

static bool isScope(const char *value) {
  return value && (!q_strcmp(value, "global") || !q_strcmp(value, "map") ||
                   !q_strcmp(value, "runtime"));
}

static const char *textValue(const TextView *textView) {

  if (!textView || !textView->attributedText) {
    return NULL;
  }

  return ((const String *) textView->attributedText)->chars;
}

/**
 * @brief The bare name of the running map, as `race admin map` wants it.
 * @details CS_BSP carries "maps/<name>.bsp"; the command takes the name alone.
 */
static const char *currentMapName(void) {

  const char *bsp = cgi.ConfigString(CS_BSP);
  if (!bsp || !*bsp) {
    return NULL;
  }

  const char *name = strrchr(bsp, '/');
  name = name ? name + 1 : bsp;

  static char buffer[MAX_QPATH];
  q_strlcpy(buffer, name, sizeof(buffer));

  char *extension = strrchr(buffer, '.');
  if (extension) {
    *extension = '\0';
  }

  return *buffer ? buffer : NULL;
}

static bool containsIgnoringCase(const char *text, const char *query) {

  if (!query || !*query) {
    return true;
  }

  if (!text) {
    return false;
  }

  const size_t length = q_strlen(query);
  for (const char *c = text; *c; c++) {
    size_t i = 0;
    while (i < length && c[i] &&
           tolower((unsigned char) c[i]) == tolower((unsigned char) query[i])) {
      i++;
    }
    if (i == length) {
      return true;
    }
  }

  return false;
}

#pragma mark - View helpers

/**
 * @details View::layoutIfNeeded lays out a view only if that view carries
 * needsLayout, so marking the route root alone leaves the flow beneath it
 * untouched and a newly revealed section keeps geometry it never received.
 */
static void invalidateLayoutChain(View *view) {

  while (view) {
    view->needsLayout = true;
    view = view->superview;
  }
}

static void setControlFlag(Control *control, ControlState flag, bool enabled) {

  if (!control) {
    return;
  }

  const unsigned int state = control->state;
  if (enabled) {
    control->state |= flag;
  } else {
    control->state &= ~flag;
  }

  if (state != control->state) {
    $(control, stateDidChange);
  }
}

static void setTextIfChanged(Text *text, const char *string) {

  if (!text) {
    return;
  }

  const char *current = text->text;
  if (current && string && !q_strcmp(current, string)) {
    return;
  }

  if (!current && (!string || !*string)) {
    return;
  }

  $(text, setText, string);
}

#pragma mark - Capabilities and row state

static uint16_t adminCapabilities(void) {

  if (!cgi.client) {
    return 0u;
  }

  return (uint16_t) cgi.client->frame.ps.stats[STAT_RACE_ADMIN_CAPABILITIES];
}

static bool isSettingRow(size_t row) {
  return adminRows[row].key != NULL;
}

/**
 * @brief Whether the client may currently use this row.
 * @details Capability first, then the row's own precondition - an action that
 * consumes a free-text field is dead until that field holds a token the server
 * would accept.
 */
static bool isRowEnabled(const AdminViewController *self, size_t row) {

  const AdminRowDescriptor *descriptor = &adminRows[row];

  if (descriptor->capability && !(self->capabilities & descriptor->capability)) {
    return false;
  }

  switch (descriptor->action) {

    case AdminActionChangeMap:
      return isSafeToken(textValue(self->mapName));

    case AdminActionRestartMap:
      return currentMapName() != NULL;

    case AdminActionKick:
      return isSafeToken(textValue(self->playerSlot));

    case AdminActionSettingsGet:
    case AdminActionSettingsSource:
      return isSafeToken(textValue(self->settingsKey));

    case AdminActionSettingsSet:
      return isScope(textValue(self->settingsScope)) &&
             isSafeToken(textValue(self->settingsKey)) &&
             isSafeToken(textValue(self->settingsValue));

    case AdminActionSettingsReset:
      return isScope(textValue(self->settingsScope)) &&
             isSafeToken(textValue(self->settingsKey));

    default:
      return true;
  }
}

/**
 * @brief A settings row is marked when it no longer shows the shipped default.
 */
static bool isRowModified(const AdminViewController *self, size_t row) {

  if (!isSettingRow(row)) {
    return false;
  }

  return q_strcmp(self->rowValues[row], adminRows[row].initial) != 0;
}

#pragma mark - Commands

static void printInputError(void) {
  cgi.Print("Invalid Race administrator input; use the documented single-token values\n");
}

/**
 * @brief Posts every settings row whose displayed value has outrun what this
 * route last sent, and records that the row now holds a value this admin set.
 * @details A Slider notifies on every motion, so writing from the delegate would
 * post one admin command per pixel of drag. Toggles and Selects move once and
 * call this immediately; a Slider waits for the button to come back up.
 */
static void flushPendingWrites(AdminViewController *self) {

  for (size_t row = 0; row < ADMIN_ROW_COUNT; row++) {

    if (!isSettingRow(row) || !q_strcmp(self->rowValues[row], self->rowSent[row])) {
      continue;
    }

    // Capability can be revoked while the route is open. Rolling the display
    // back is the honest outcome: the write will not happen, so the control
    // must not keep showing the value as though it had.
    if (!(self->capabilities & adminRows[row].capability)) {
      q_strlcpy(self->rowValues[row], self->rowSent[row], ADMIN_VALUE_SIZE);
      continue;
    }

    cgi.Cbuf(va("race admin settings set %s %s %s\n", adminRows[row].scope,
                adminRows[row].key, self->rowValues[row]));

    q_strlcpy(self->rowSent[row], self->rowValues[row], ADMIN_VALUE_SIZE);
    self->rowConfirmed[row] = true;
  }
}

static void runAction(AdminViewController *self, size_t row) {

  if (!isRowEnabled(self, row)) {
    printInputError();
    return;
  }

  switch (adminRows[row].action) {

    case AdminActionChangeMap:
      cgi.Cbuf(va("race admin map %s\n", textValue(self->mapName)));
      break;

    case AdminActionRestartMap:
      cgi.Cbuf(va("race admin map %s\n", currentMapName()));
      break;

    case AdminActionCancelVote:
      cgi.Cbuf("race admin vote cancel\n");
      break;

    case AdminActionStatus:
      cgi.Cbuf("race admin status\n");
      break;

    case AdminActionHelp:
      cgi.Cbuf("race admin help\n");
      break;

    case AdminActionLogout:
      cgi.Cbuf("race admin_logout\n");
      break;

    case AdminActionKick:
      cgi.Cbuf(va("race admin kick %s\n", textValue(self->playerSlot)));
      break;

    case AdminActionSettingsGet:
      cgi.Cbuf(va("race admin settings get %s\n", textValue(self->settingsKey)));
      break;

    case AdminActionSettingsSource:
      cgi.Cbuf(va("race admin settings source %s\n", textValue(self->settingsKey)));
      break;

    case AdminActionSettingsSet:
      cgi.Cbuf(va("race admin settings set %s %s %s\n", textValue(self->settingsScope),
                  textValue(self->settingsKey), textValue(self->settingsValue)));
      break;

    case AdminActionSettingsReset:
      cgi.Cbuf(va("race admin settings reset %s %s\n", textValue(self->settingsScope),
                  textValue(self->settingsKey)));
      break;

    default:
      break;
  }
}

#pragma mark - Refresh

/**
 * @brief Pushes `rowValues` back into the controls, after a revert or an entry.
 */
static void syncControlsFromValues(AdminViewController *self) {

  self->refreshing = true;

  for (size_t row = 0; row < ADMIN_ROW_COUNT; row++) {

    if (!isSettingRow(row) || !self->rowControls[row]) {
      continue;
    }

    switch (adminRows[row].kind) {

      case AdminRowToggle:
        setControlFlag((Control *) self->rowControls[row], ControlStateSelected,
                       !q_strcmp(self->rowValues[row], "1"));
        break;

      case AdminRowSlider:
        $((Slider *) self->rowControls[row], setValue, strtod(self->rowValues[row], NULL));
        break;

      case AdminRowSelect: {

        size_t count;
        const char *const *options = adminSelectOptions(adminRows[row].select, &count);

        for (size_t i = 0; i < count; i++) {
          if (!q_strcmp(self->rowValues[row], options[i])) {
            $((Select *) self->rowControls[row], selectOptionWithValue,
              (ident) (intptr_t) i);
            break;
          }
        }
        break;
      }

      default:
        break;
    }
  }

  self->refreshing = false;
}

/**
 * @brief Repaints the per-row chrome: the changed dot, its revert, and whether
 * the control is live at all.
 * @details Every write is guarded on a change, because this runs on pointer
 * motion and adding or removing a class name re-applies the stylesheet to the
 * whole subtree.
 */
static void refreshRows(AdminViewController *self) {

  self->capabilities = adminCapabilities();

  for (size_t row = 0; row < ADMIN_ROW_COUNT; row++) {

    const bool modified = isRowModified(self, row);
    if (modified != self->rowPainted[row]) {

      self->rowPainted[row] = modified;
      self->rowDots[row]->hidden = !modified;
      self->rowReverts[row]->control.view.hidden = !modified;

      if (modified) {
        $(self->rowViews[row], addClassName, "modified");
      } else {
        $(self->rowViews[row], removeClassName, "modified");
      }
    }

    Control *control = NULL;
    if (self->rowControls[row] &&
        $((Object *) self->rowControls[row], isKindOfClass, _Control())) {
      control = (Control *) self->rowControls[row];
    }

    setControlFlag(control, ControlStateDisabled, !isRowEnabled(self, row));
  }
}

/**
 * @brief Names how many settings rows are still showing a value nobody
 * confirmed, which is the whole of what this route can honestly say about them.
 */
static void refreshProvenance(AdminViewController *self) {

  size_t unconfirmed = 0, total = 0;

  for (size_t row = 0; row < ADMIN_ROW_COUNT; row++) {
    if (!isSettingRow(row)) {
      continue;
    }
    total++;
    if (!self->rowConfirmed[row]) {
      unconfirmed++;
    }
  }

  if (unconfirmed == 0) {
    setTextIfChanged(self->provenance->text, "");
    return;
  }

  char text[192];
  snprintf(text, sizeof(text),
           "%zu of %zu server settings shown at shipped defaults - a value set "
           "elsewhere is not reflected here", unconfirmed, total);

  setTextIfChanged(self->provenance->text, text);
}

/**
 * @brief The query the filter slot currently holds.
 * @details An empty Objectively String never allocates, so `chars` is NULL
 * rather than "" - and TextView always holds a String, so testing
 * attributedText alone guards the wrong pointer. An empty filter is the state
 * this route opens in.
 */
static const char *filterQuery(const AdminViewController *self) {

  const String *text = self->filter->attributedText;

  return text && text->chars ? text->chars : "";
}

/**
 * @brief Redraws the connected roster, which is the Players section's table.
 * @details Every cell is written through setTextIfChanged: the roster refreshes
 * on pointer motion, and rewriting four Labels per client every frame would
 * re-apply the stylesheet across the section for nothing.
 */
static void refreshRoster(AdminViewController *self, const char *query) {

  cg_roster_entry_t entries[MAX_CLIENTS];
  const size_t count = Cg_RosterSnapshot(entries, lengthof(entries));

  size_t shown = 0;

  for (size_t i = 0; i < count && shown < MAX_CLIENTS; i++) {

    if (!containsIgnoringCase(entries[i].name, query)) {
      continue;
    }

    const char *mode = "Spectating";
    switch (entries[i].group) {
      case CG_ROSTER_RACE_MODE:
        mode = "Racing";
        break;
      case CG_ROSTER_PRACTICE_MODE:
        mode = "Practice";
        break;
      default:
        break;
    }

    char slot[16], ping[16];
    snprintf(slot, sizeof(slot), "%u", entries[i].client);
    snprintf(ping, sizeof(ping), "%d ms", entries[i].ping);

    self->rosterRows[shown]->hidden = false;
    setTextIfChanged(self->rosterName[shown]->text, entries[i].name);
    setTextIfChanged(self->rosterSlot[shown]->text, slot);
    setTextIfChanged(self->rosterMode[shown]->text, mode);
    setTextIfChanged(self->rosterPing[shown]->text, ping);

    shown++;
  }

  for (size_t i = shown; i < MAX_CLIENTS; i++) {
    self->rosterRows[i]->hidden = true;
  }

  self->rosterEmpty->view.hidden = shown > 0;
  self->rosterShown = shown;

  // The section metric mirrors the design's "n / 64" readout, and counts what
  // the query left standing rather than the whole roster.
  char metric[32];
  snprintf(metric, sizeof(metric), "%zu / %u", shown, (unsigned) MAX_CLIENTS);
  setTextIfChanged(self->sectionMetrics[AdminSectionPlayers]->text, metric);
  self->sectionMetrics[AdminSectionPlayers]->view.hidden = false;
}

/**
 * @brief One pass over the table: hide the rows the query missed, then the
 * sections left with nothing.
 */
static void refreshFilter(AdminViewController *self) {

  const char *query = filterQuery(self);

  size_t sectionHits[ADMIN_SECTION_COUNT] = { 0 };
  size_t lastVisibleRow[ADMIN_SECTION_COUNT];

  for (size_t section = 0; section < ADMIN_SECTION_COUNT; section++) {
    lastVisibleRow[section] = ADMIN_ROW_COUNT;
  }

  for (size_t row = 0; row < ADMIN_ROW_COUNT; row++) {

    const AdminRowDescriptor *descriptor = &adminRows[row];
    const bool hit = containsIgnoringCase(descriptor->label, query) ||
                     containsIgnoringCase(descriptor->key, query);

    self->rowViews[row]->hidden = !hit;
    if (hit) {
      sectionHits[descriptor->section]++;
      lastVisibleRow[descriptor->section] = row;
    }
  }

  // A separator under the last visible row of a section would draw a line to
  // nowhere, and which row is last moves with the query.
  for (size_t row = 0; row < ADMIN_ROW_COUNT; row++) {
    self->rowRules[row]->hidden = lastVisibleRow[adminRows[row].section] == row;
  }

  refreshRoster(self, query);

  bool any = false;
  for (size_t section = 0; section < ADMIN_SECTION_COUNT; section++) {

    // Players keeps its table for a query that matched a name but no row label,
    // which is the only section whose content is not in the descriptor table.
    const bool visible = sectionHits[section] > 0 ||
                         (section == AdminSectionPlayers && self->rosterShown > 0);

    self->sectionViews[section]->hidden = !visible;
    any = any || visible;
  }

  self->emptyState->view.hidden = any;

  for (size_t section = 0; section < ADMIN_SECTION_COUNT; section++) {
    invalidateLayoutChain(self->sectionViews[section]);
  }
  invalidateLayoutChain((View *) self->emptyState);
}

/**
 * @brief Names the hovered row's command or setting key in the footer.
 * @details The dialect has no tooltip, and an admin command is exactly the kind
 * of thing that should be legible before it is clicked.
 */
static void refreshHint(AdminViewController *self) {

  float mouseX, mouseY;
  SDL_GetMouseState(&mouseX, &mouseY);

  const SDL_Point point = MakePoint(mouseX, mouseY);

  for (size_t row = 0; row < ADMIN_ROW_COUNT; row++) {

    const AdminRowDescriptor *descriptor = &adminRows[row];
    if (self->rowViews[row]->hidden ||
        self->sectionViews[descriptor->section]->hidden ||
        !$(self->rowViews[row], containsPoint, &point)) {
      continue;
    }

    char hint[256];

    if (isSettingRow(row)) {
      snprintf(hint, sizeof(hint), "race admin settings set %s %s%s", descriptor->scope,
               descriptor->key, self->rowConfirmed[row] ? "" : " - value unconfirmed");
    } else if (!isRowEnabled(self, row)) {
      snprintf(hint, sizeof(hint), "%s", descriptor->capability &&
               !(self->capabilities & descriptor->capability)
                 ? "Your admin role does not carry this capability"
                 : "Fill the field above with a single token first");
    } else {
      switch (descriptor->action) {
        case AdminActionChangeMap:
          snprintf(hint, sizeof(hint), "race admin map %s", textValue(self->mapName));
          break;
        case AdminActionRestartMap:
          snprintf(hint, sizeof(hint), "race admin map %s", currentMapName());
          break;
        case AdminActionCancelVote:
          snprintf(hint, sizeof(hint), "race admin vote cancel");
          break;
        case AdminActionStatus:
          snprintf(hint, sizeof(hint), "race admin status");
          break;
        case AdminActionHelp:
          snprintf(hint, sizeof(hint), "race admin help");
          break;
        case AdminActionLogout:
          snprintf(hint, sizeof(hint), "race admin_logout");
          break;
        case AdminActionKick:
          snprintf(hint, sizeof(hint), "race admin kick %s", textValue(self->playerSlot));
          break;
        case AdminActionSettingsGet:
        case AdminActionSettingsSource:
        case AdminActionSettingsSet:
        case AdminActionSettingsReset:
          snprintf(hint, sizeof(hint), "Replies print to the console");
          break;
        default:
          snprintf(hint, sizeof(hint), "%s", descriptor->placeholder
                     ? descriptor->placeholder : "");
          break;
      }
    }

    setTextIfChanged(self->hint->text, hint);
    return;
  }

  setTextIfChanged(self->hint->text, "");
}

#pragma mark - Delegates

static void didEditFilter(TextView *textView) {

  AdminViewController *self = textView->delegate.self;

  refreshFilter(self);
}

static void didEditField(TextView *textView) {

  AdminViewController *self = textView->delegate.self;

  refreshRows(self);
}

static void didClickAction(Button *button) {

  AdminViewController *self = button->delegate.self;

  runAction(self, (size_t) (intptr_t) button->delegate.data);
  refreshRows(self);
}

static void didClickRowRevert(Button *button) {

  AdminViewController *self = button->delegate.self;
  const size_t row = (size_t) (intptr_t) button->delegate.data;

  if (!isSettingRow(row) || !(self->capabilities & adminRows[row].capability)) {
    return;
  }

  // `reset` rather than a `set` back to the shipped value: it clears this
  // scope's override, so the server ends up at the default rather than carrying
  // an override that merely happens to equal it. A map override at a narrower
  // scope can still win, which is the same thing the provenance line is about.
  cgi.Cbuf(va("race admin settings reset %s %s\n", adminRows[row].scope,
              adminRows[row].key));

  q_strlcpy(self->rowValues[row], adminRows[row].initial, ADMIN_VALUE_SIZE);
  q_strlcpy(self->rowSent[row], adminRows[row].initial, ADMIN_VALUE_SIZE);
  self->rowConfirmed[row] = true;

  syncControlsFromValues(self);
  refreshRows(self);
  refreshProvenance(self);
}

static void didToggleSetting(Checkbox *checkbox) {

  AdminViewController *self = checkbox->delegate.self;
  if (self->refreshing) {
    return;
  }

  const size_t row = (size_t) (intptr_t) checkbox->delegate.data;
  const bool checked = (checkbox->control.state & ControlStateSelected) != 0;

  q_strlcpy(self->rowValues[row], checked ? "1" : "0", ADMIN_VALUE_SIZE);

  flushPendingWrites(self);
  refreshRows(self);
  refreshProvenance(self);
}

/**
 * @brief The row a control belongs to.
 * @details SliderDelegate and SelectDelegate carry only a self-reference - no
 * user data slot - so those two find their row by identity rather than being
 * told it. Checkbox and Button delegates do carry one, and use it.
 */
static size_t rowForControl(const AdminViewController *self, const View *control) {

  for (size_t row = 0; row < ADMIN_ROW_COUNT; row++) {
    if (self->rowControls[row] == control) {
      return row;
    }
  }

  return ADMIN_ROW_COUNT;
}

static void didSetSettingValue(Slider *slider, double value) {

  AdminViewController *self = slider->delegate.self;
  if (self->refreshing) {
    return;
  }

  const size_t row = rowForControl(self, (const View *) slider);
  if (row == ADMIN_ROW_COUNT) {
    return;
  }

  // Only the displayed value moves here; the write waits for the drag to end.
  // See flushPendingWrites.
  snprintf(self->rowValues[row], ADMIN_VALUE_SIZE, "%d", (int32_t) (value + 0.5));

  refreshRows(self);
}

static void didSelectSetting(Select *select, Option *option) {

  AdminViewController *self = select->delegate.self;
  if (self->refreshing || !option) {
    return;
  }

  const size_t row = rowForControl(self, (const View *) select);
  if (row == ADMIN_ROW_COUNT) {
    return;
  }

  size_t count;
  const char *const *options = adminSelectOptions(adminRows[row].select, &count);

  const size_t index = (size_t) (intptr_t) option->value;
  if (index >= count) {
    return;
  }

  q_strlcpy(self->rowValues[row], options[index], ADMIN_VALUE_SIZE);

  flushPendingWrites(self);
  refreshRows(self);
  refreshProvenance(self);
}

#pragma mark - Construction

static View *makeControl(AdminViewController *self, size_t row) {

  const AdminRowDescriptor *descriptor = &adminRows[row];

  switch (descriptor->kind) {

    case AdminRowToggle: {

      Checkbox *checkbox = $(alloc(Checkbox), initWithFrame, NULL);
      checkbox->delegate = (CheckboxDelegate) {
        .self = self,
        .data = (ident) (intptr_t) row,
        .didToggle = didToggleSetting
      };
      return (View *) checkbox;
    }

    case AdminRowSlider: {

      Slider *slider = $(alloc(Slider), initWithFrame, NULL);
      slider->min = descriptor->min;
      slider->max = descriptor->max;
      slider->step = descriptor->step;
      slider->snapToStep = true;
      slider->delegate = (SliderDelegate) {
        .self = self,
        .didSetValue = didSetSettingValue
      };

      if (descriptor->format) {
        $(slider, setLabelFormat, descriptor->format);
      }

      return (View *) slider;
    }

    case AdminRowSelect: {

      Select *select = $(alloc(Select), initWithFrame, NULL);
      select->delegate = (SelectDelegate) {
        .self = self,
        .didSelectOption = didSelectSetting
      };

      size_t count;
      const char *const *options = adminSelectOptions(descriptor->select, &count);

      for (size_t i = 0; i < count; i++) {
        $(select, addOption, options[i], (ident) (intptr_t) i);
      }

      return (View *) select;
    }

    case AdminRowText: {

      TextView *textView = $(alloc(TextView), initWithFrame, NULL);
      $(textView, setDefaultText, descriptor->placeholder);
      $((View *) textView, addClassName, "rowField");
      textView->delegate = (TextViewDelegate) {
        .self = self,
        .didEdit = didEditField,
        .didEndEditing = didEditField
      };

      switch (descriptor->field) {
        case AdminFieldMapName:
          self->mapName = textView;
          break;
        case AdminFieldPlayerSlot:
          self->playerSlot = textView;
          break;
        case AdminFieldSettingsScope:
          self->settingsScope = textView;
          break;
        case AdminFieldSettingsKey:
          self->settingsKey = textView;
          break;
        case AdminFieldSettingsValue:
          self->settingsValue = textView;
          break;
        default:
          break;
      }

      return (View *) textView;
    }

    case AdminRowAction: {

      Button *button = $(alloc(Button), initWithTitle, descriptor->label);
      $((View *) button, addClassName, "rowAction");

      if (descriptor->action == AdminActionKick ||
          descriptor->action == AdminActionLogout) {
        $((View *) button, addClassName, "dangerButton");
      } else if (descriptor->action == AdminActionChangeMap ||
                 descriptor->action == AdminActionSettingsSet) {
        $((View *) button, addClassName, "primaryButton");
      }

      button->delegate = (ButtonDelegate) {
        .self = self,
        .data = (ident) (intptr_t) row,
        .didClick = didClickAction
      };

      return (View *) button;
    }
  }

  return NULL;
}

/**
 * @brief Builds one row: dot and label pinned left, control pinned right.
 * @details The row is a plain View rather than a StackView so that the two
 * groups pin to opposite edges however long the label runs - a horizontal
 * StackView would lay them out end to end instead, and the control column would
 * step in and out with every label.
 */
static View *makeRow(AdminViewController *self, size_t row) {

  const AdminRowDescriptor *descriptor = &adminRows[row];

  View *view = $(alloc(View), initWithFrame, NULL);
  $(view, addClassName, "adminRow");

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
  $((View *) right, addSubview, control);
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
 * @brief Builds one roster line: name, slot, mode, ping.
 */
static View *makeRosterRow(AdminViewController *self, size_t index, bool header) {

  View *view = $(alloc(View), initWithFrame, NULL);
  $(view, addClassName, header ? "rosterHead" : "rosterRow");

  StackView *left = $(alloc(StackView), initWithFrame, NULL);
  $((View *) left, addClassName, "rosterLeft");
  left->axis = StackViewAxisHorizontal;
  left->view.alignment = ViewAlignmentMiddleLeft;

  Label *name = $(alloc(Label), initWithText, header ? "Player" : "", NULL);
  $((View *) name, addClassName, "rosterName");
  $((View *) left, addSubview, (View *) name);

  Label *slot = $(alloc(Label), initWithText, header ? "Slot" : "", NULL);
  $((View *) slot, addClassName, "rosterSlot");
  $((View *) left, addSubview, (View *) slot);

  $(view, addSubview, (View *) left);
  release(left);

  StackView *right = $(alloc(StackView), initWithFrame, NULL);
  $((View *) right, addClassName, "rosterRight");
  right->axis = StackViewAxisHorizontal;
  right->view.alignment = ViewAlignmentMiddleRight;

  Label *mode = $(alloc(Label), initWithText, header ? "Mode" : "", NULL);
  $((View *) mode, addClassName, "rosterMode");
  $((View *) right, addSubview, (View *) mode);

  Label *ping = $(alloc(Label), initWithText, header ? "Ping" : "", NULL);
  $((View *) ping, addClassName, "rosterPing");
  $((View *) right, addSubview, (View *) ping);

  $(view, addSubview, (View *) right);
  release(right);

  if (!header) {
    self->rosterName[index] = name;
    self->rosterSlot[index] = slot;
    self->rosterMode[index] = mode;
    self->rosterPing[index] = ping;
    view->hidden = true;
  }

  release(name);
  release(slot);
  release(mode);
  release(ping);

  return view;
}

/**
 * @brief Builds the Players section's table, which is the one piece of this
 * route that is not driven by the descriptor table.
 */
static View *makeRoster(AdminViewController *self) {

  StackView *view = $(alloc(StackView), initWithFrame, NULL);
  $((View *) view, addClassName, "rosterTable");

  View *head = makeRosterRow(self, 0, true);
  $((View *) view, addSubview, head);
  release(head);

  for (size_t i = 0; i < MAX_CLIENTS; i++) {
    View *row = makeRosterRow(self, i, false);
    $((View *) view, addSubview, row);
    self->rosterRows[i] = row;
    release(row);
  }

  Label *empty = $(alloc(Label), initWithText, "No players match that filter.", NULL);
  $((View *) empty, addClassName, "rosterEmpty");
  $((View *) view, addSubview, (View *) empty);
  self->rosterEmpty = empty;
  release(empty);

  return (View *) view;
}

/**
 * @brief Builds one section: eyebrow, metric, rule, and its rows.
 */
static View *makeSection(AdminViewController *self, size_t section) {

  StackView *view = $(alloc(StackView), initWithFrame, NULL);
  $((View *) view, addClassName, "adminSection");
  if (adminSections[section].wide) {
    $((View *) view, addClassName, "wideSection");
  }

  StackView *head = $(alloc(StackView), initWithFrame, NULL);
  $((View *) head, addClassName, "sectionHead");
  head->axis = StackViewAxisHorizontal;

  Label *eyebrow = $(alloc(Label), initWithText, adminSections[section].label, NULL);
  $((View *) eyebrow, addClassName, "sectionEyebrow");
  $((View *) head, addSubview, (View *) eyebrow);
  release(eyebrow);

  Label *metric = $(alloc(Label), initWithText,
                    adminSections[section].metric ? adminSections[section].metric : "", NULL);
  $((View *) metric, addClassName, "sectionMetric");
  metric->view.hidden = adminSections[section].metric == NULL;
  $((View *) head, addSubview, (View *) metric);
  self->sectionMetrics[section] = metric;
  release(metric);

  $((View *) view, addSubview, (View *) head);
  release(head);

  View *rule = $(alloc(View), initWithFrame, NULL);
  $(rule, addClassName, "sectionRule");
  $((View *) view, addSubview, rule);
  release(rule);

  StackView *rows = $(alloc(StackView), initWithFrame, NULL);
  $((View *) rows, addClassName, "adminRows");

  for (size_t row = 0; row < ADMIN_ROW_COUNT; row++) {
    if ((size_t) adminRows[row].section != section) {
      continue;
    }

    View *rowView = makeRow(self, row);
    $((View *) rows, addSubview, rowView);
    self->rowViews[row] = rowView;
    release(rowView);
  }

  $((View *) view, addSubview, (View *) rows);
  release(rows);

  // The roster hangs below the Players rows rather than beside them: it is the
  // section's evidence, and the slot an admin types into comes off it.
  if (section == AdminSectionPlayers) {
    View *roster = makeRoster(self);
    $((View *) view, addSubview, roster);
    release(roster);
  }

  return (View *) view;
}

#pragma mark - ViewController

static void resolveOutlets(AdminViewController *self) {

  Outlet outlets[] = MakeOutlets(
    MakeOutlet("adminFilter", &self->filter),
    MakeOutlet("adminEmptyState", &self->emptyState),
    MakeOutlet("adminHint", &self->hint),
    MakeOutlet("adminProvenance", &self->provenance)
  );

  $(self->viewController.view, resolve, outlets);

  assert(self->filter);
  assert(self->emptyState);
  assert(self->hint);
  assert(self->provenance);
}

/**
 * @see ViewController::loadView(ViewController *)
 */
static void loadView(ViewController *viewController) {

  super(ViewController, viewController, loadView);

  AdminViewController *self = (AdminViewController *) viewController;

  View *view = $$(View, viewWithResourceName,
                  "ui/admin/AdminViewController.json", NULL);
  assert(view);
  assert(view->identifier && !q_strcmp(view->identifier, "raceAdminRoot"));

  view->stylesheet = $$(Stylesheet, stylesheetWithResourceName,
                        "ui/admin/AdminViewController.css");
  assert(view->stylesheet);

  ((View *) ((Panel *) view)->accessoryView)->hidden = false;

  $(viewController, setView, view);
  release(view);

  resolveOutlets(self);

  for (size_t row = 0; row < ADMIN_ROW_COUNT; row++) {
    if (isSettingRow(row)) {
      q_strlcpy(self->rowValues[row], adminRows[row].initial, ADMIN_VALUE_SIZE);
      q_strlcpy(self->rowSent[row], adminRows[row].initial, ADMIN_VALUE_SIZE);
    }
  }

  // The sections are built here rather than authored in the JSON because every
  // one of them is the same shape over the same descriptor table that the
  // filter, the dot, the capability gate and the footer hint all read.
  View *columns = $(viewController->view, descendantWithIdentifier, "adminColumns");
  View *wide = $(viewController->view, descendantWithIdentifier, "adminWideSections");
  assert(columns && wide);

  for (size_t section = 0; section < ADMIN_SECTION_COUNT; section++) {
    View *sectionView = makeSection(self, section);
    $(adminSections[section].wide ? wide : columns, addSubview, sectionView);
    self->sectionViews[section] = sectionView;
    release(sectionView);
  }

  self->filter->delegate = (TextViewDelegate) {
    .self = self,
    .didEdit = didEditFilter
  };

  self->capabilities = adminCapabilities();

  syncControlsFromValues(self);
  refreshFilter(self);
  refreshRows(self);
  refreshProvenance(self);
}

/**
 * @see ViewController::viewWillAppear(ViewController *)
 */
static void viewWillAppear(ViewController *viewController) {

  super(ViewController, viewController, viewWillAppear);

  AdminViewController *self = (AdminViewController *) viewController;

  self->capabilities = adminCapabilities();

  refreshFilter(self);
  refreshRows(self);
  refreshProvenance(self);
  refreshHint(self);
}

/**
 * @see ViewController::respondToEvent(ViewController *, const SDL_Event *)
 * @details A Slider notifies its delegate on every motion, so the write it
 * implies is deferred to the button coming back up - see flushPendingWrites.
 * Every write in refreshRows is guarded on a change, so a still pointer costs
 * one pass of string compares and nothing else.
 */
static void respondToEvent(ViewController *viewController, const SDL_Event *event) {

  super(ViewController, viewController, respondToEvent, event);

  AdminViewController *self = (AdminViewController *) viewController;

  switch (event->type) {

    case SDL_EVENT_MOUSE_MOTION: {

      // A join or a part changes what the Players section is worth showing, and
      // that decision belongs to the filter pass rather than to the roster.
      const size_t shown = self->rosterShown;
      refreshRoster(self, filterQuery(self));
      if (shown != self->rosterShown) {
        refreshFilter(self);
      }

      refreshHint(self);
      refreshRows(self);
      break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_KEY_UP:
      flushPendingWrites(self);
      refreshRows(self);
      refreshProvenance(self);
      break;

    case SDL_EVENT_MOUSE_WHEEL:
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
 * @fn Class *AdminViewController::_AdminViewController(void)
 * @memberof AdminViewController
 */
Class *_AdminViewController(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "AdminViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(AdminViewController),
      .interfaceOffset = offsetof(AdminViewController, interface),
      .interfaceSize = sizeof(AdminViewControllerInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
