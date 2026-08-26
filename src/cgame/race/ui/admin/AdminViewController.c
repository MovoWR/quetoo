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
#include "cg_race_admin_command.h"
#include "race_admin_types.h"
#include "race_settings.h"

#include "AdminViewController.h"
#include "WeaponLabViewController.h"

#include "DialogViewController.h"
#include "MainViewController.h"

#define _Class _AdminViewController

/**
 * @brief Sections, in flow order.
 * @details The first four are ColumnsView slots; the last two are full width,
 * which is what the design's `sec.wide` means - a roster table and the
 * configuration actions both read badly in a 460px column.
 */
typedef enum {
  AdminSectionServer,
  AdminSectionAddMap,
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
  [AdminSectionServer] = { "Server control", NULL, false },
  [AdminSectionAddMap] = { "Add map", NULL, false },
  [AdminSectionRules] = { "Race rules", NULL, false },
  [AdminSectionVoting] = { "Voting", NULL, false },
  [AdminSectionSession] = { "Session", NULL, false },
  [AdminSectionPlayers] = { "Players", NULL, true },
  [AdminSectionAdvanced] = { "Advanced", NULL, true },
};

/**
 * @brief The subtab strip, and which sections each tab carries.
 * @details The design groups the six sections under five tabs so the route
 * opens on one readable page instead of six stacked ones. The grouping is the
 * design's; `Add map` is deliberately absent - see the note on adminRows.
 */
typedef struct {
  const char *label;
  const char *outlet;

  /**
   * @brief The sections this tab shows, terminated by ADMIN_SECTION_COUNT.
   */
  size_t sections[3];

  /**
   * @brief True for a tab that hosts a panel instead of grouping sections.
   */
  bool lab;
} AdminTabDescriptor;

/**
 * @brief The weapon lab's tab.
 */
#define ADMIN_TAB_WEAPONS 5

static const AdminTabDescriptor adminTabs[ADMIN_TAB_COUNT] = {
  { "Server", "adminTabServer",
    { AdminSectionServer, AdminSectionAddMap, ADMIN_SECTION_COUNT }, false },
  { "Rules", "adminTabRules",
    { AdminSectionRules, AdminSectionVoting, ADMIN_SECTION_COUNT }, false },
  { "Players", "adminTabPlayers",
    { AdminSectionPlayers, ADMIN_SECTION_COUNT, ADMIN_SECTION_COUNT }, false },
  { "Session", "adminTabSession",
    { AdminSectionSession, ADMIN_SECTION_COUNT, ADMIN_SECTION_COUNT }, false },
  { "Advanced", "adminTabAdvanced",
    { AdminSectionAdvanced, ADMIN_SECTION_COUNT, ADMIN_SECTION_COUNT }, false },
  // Weapons carries no sections. Its rows are one authoritative GAME snapshot
  // rather than settings rows, so it hosts WeaponLabViewController instead of
  // declaring entries in adminRows - which is also why its hit count comes from
  // that panel's catalog rather than from this route's descriptor table.
  { "Weapons", "adminTabWeapons",
    { ADMIN_SECTION_COUNT, ADMIN_SECTION_COUNT, ADMIN_SECTION_COUNT }, true },
};

/**
 * @brief The tab a section belongs to.
 */
static size_t adminTabForSection(size_t section) {

  for (size_t tab = 0; tab < ADMIN_TAB_COUNT; tab++) {
    for (size_t i = 0; i < lengthof(adminTabs[tab].sections); i++) {
      if (adminTabs[tab].sections[i] == section) {
        return tab;
      }
    }
  }

  return 0;
}

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
  AdminActionGlobalGet,
  AdminActionGlobalSet,
  AdminActionGlobalClear,
  AdminActionMapGet,
  AdminActionMapSet,
  AdminActionMapClear,
  AdminActionValidateMap
} AdminAction;

/**
 * @brief The free-text field a text row owns, so the route can reach it by name
 * when an action row needs its contents.
 */
typedef enum {
  AdminFieldNone,
  AdminFieldMapName,
  AdminFieldPlayerSlot,
  AdminFieldSettingsKey,
  AdminFieldSettingsValue,
  AdminFieldNewMapName
} AdminField;

typedef struct {
  AdminSection section;
  const char *label;
  AdminRowKind kind;

  /**
   * @brief For a settings row, the shared catalog alias it writes globally.
   * @details Type, default, bounds, enum values, map support and activation all
   * come from `Race_Settings_Catalog`; this table carries presentation only.
   */
  const char *setting;
  double step;

  /**
   * @brief Slider readout format. Consumes one double - see Slider::formatLabel.
   */
  const char *format;

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

  { AdminSectionServer, "Map name", AdminRowText, NULL, 0, NULL,
    AdminFieldMapName, AdminActionNone, "Exact map name",
    RACE_ADMIN_CAP_MAP_CHANGE },
  { AdminSectionServer, "Change map", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionChangeMap, NULL,
    RACE_ADMIN_CAP_MAP_CHANGE },
  { AdminSectionServer, "Restart current map", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionRestartMap, NULL,
    RACE_ADMIN_CAP_MAP_CHANGE },
  { AdminSectionServer, "Cancel active vote", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionCancelVote, NULL,
    RACE_ADMIN_CAP_VOTE_ADMIN },

  { AdminSectionAddMap, "Map name", AdminRowText, NULL, 0, NULL,
    AdminFieldNewMapName, AdminActionNone, "Map to look for", 0 },
  { AdminSectionAddMap, "Validate", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionValidateMap, NULL, 0 },

  { AdminSectionRules, "Weapons", AdminRowToggle, "weapons", 0, NULL,
    AdminFieldNone, AdminActionNone, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionRules, "Finish cue", AdminRowToggle, "finish_cue_enabled", 0, NULL,
    AdminFieldNone, AdminActionNone, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionRules, "Finish cue volume", AdminRowSlider, "finish_cue_gain", 1, "%.0f%%",
    AdminFieldNone, AdminActionNone, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionRules, "Checkpoint feedback", AdminRowSelect, "checkpoint_feedback", 0, NULL,
    AdminFieldNone, AdminActionNone, NULL, RACE_ADMIN_CAP_SERVER_CVAR },

  { AdminSectionVoting, "Voting time", AdminRowSlider, "voting_time", 5, "%.0f s",
    AdminFieldNone, AdminActionNone, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionVoting, "Max vote starts", AdminRowSlider, "max_votes", 1, "%.0f",
    AdminFieldNone, AdminActionNone, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionVoting, "Vote menu duration", AdminRowSlider, "vote_menu_duration", 5, "%.0f s",
    AdminFieldNone, AdminActionNone, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionVoting, "Vote menu choices", AdminRowSlider, "vote_menu_choices", 1, "%.0f",
    AdminFieldNone, AdminActionNone, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionVoting, "Spectator voting", AdminRowToggle, "vote_allow_spectators", 0, NULL,
    AdminFieldNone, AdminActionNone, NULL, RACE_ADMIN_CAP_SERVER_CVAR },

  { AdminSectionSession, "Admin status", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionStatus, NULL, 0 },
  { AdminSectionSession, "Command help", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionHelp, NULL, 0 },
  { AdminSectionSession, "Log out", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionLogout, NULL, 0 },

  { AdminSectionPlayers, "Client slot", AdminRowText, NULL, 0, NULL,
    AdminFieldPlayerSlot, AdminActionNone, "Slot number",
    RACE_ADMIN_CAP_PLAYER_KICK },
  { AdminSectionPlayers, "Kick player", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionKick, NULL,
    RACE_ADMIN_CAP_PLAYER_KICK },

  { AdminSectionAdvanced, "Setting or cvar", AdminRowText, NULL, 0, NULL,
    AdminFieldSettingsKey, AdminActionNone, "Alias or canonical cvar",
    RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionAdvanced, "Value", AdminRowText, NULL, 0, NULL,
    AdminFieldSettingsValue, AdminActionNone, "Value (spaces allowed)",
    RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionAdvanced, "Inspect global value", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionGlobalGet, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionAdvanced, "Set global value", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionGlobalSet, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionAdvanced, "Clear global value", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionGlobalClear, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionAdvanced, "Inspect map override", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionMapGet, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionAdvanced, "Set map override", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionMapSet, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
  { AdminSectionAdvanced, "Clear map override", AdminRowAction, NULL, 0, NULL,
    AdminFieldNone, AdminActionMapClear, NULL, RACE_ADMIN_CAP_SERVER_CVAR },
};

#pragma mark - Validation

/**
 * @brief The token shape the server's own parsers accept.
 * @details Validating here is a courtesy to the admin, not a security boundary:
 * GAME resolves names, revalidates authority and validates every argument.
 * What this buys is a disabled control instead of a console error.
 */
/**
 * @brief Whether a settings value can be represented as one console argument.
 * @details The command parser copies backslash pairs literally, so a trailing
 * backslash would consume the closing quote. It also expands tokens beginning
 * with `$`. Reject those two ambiguous shapes along with quotes and controls;
 * spaces, slashes, commas and semicolons remain safe inside the quotes.
 */
static bool isSafeSettingValue(const char *value) {

  if (!value || q_strlen(value) > RACE_SETTING_VALUE_MAX || *value == '$') {
    return false;
  }

  const unsigned char *c = (const unsigned char *) value;
  for (; *c; c++) {
    if (*c < 32u || *c == 127u || *c == '"') {
      return false;
    }
  }

  return c == (const unsigned char *) value || c[-1] != '\\';
}

static bool quoteSettingValue(const char *value, char *output,
                              size_t output_size) {

  if (!output || !output_size || !isSafeSettingValue(value)) {
    return false;
  }

  const int32_t written = q_snprintf(output, output_size, "\"%s\"", value);
  return written >= 0 && (size_t) written < output_size;
}

static const char *textValue(const TextView *textView) {

  if (!textView || !textView->attributedText) {
    return NULL;
  }

  return ((const String *) textView->attributedText)->chars;
}

/**
 * @brief Resolves a shared setting alias or its canonical cvar name.
 * @details This lookup is presentation-only. GAME resolves the name again
 * before capability checks and remains authoritative for every command.
 */
static const race_setting_descriptor_t *adminSettingForName(const char *name) {

  if (!name || !*name) {
    return NULL;
  }

  size_t count;
  const race_setting_descriptor_t *settings = Race_Settings_Catalog(&count);

  for (size_t i = 0; i < count; i++) {
    if (!q_strcmp(name, settings[i].alias) || !q_strcmp(name, settings[i].cvar)) {
      return &settings[i];
    }
  }

  return NULL;
}

static const race_setting_descriptor_t *adminSettingForRow(size_t row) {

  const char *name = adminRows[row].setting;

  return name ? adminSettingForName(name) : NULL;
}

static bool isMapSettingName(const char *name) {

  const race_setting_descriptor_t *setting = adminSettingForName(name);

  return setting && setting->map_overridable;
}

static const char *adminActivationLabel(race_setting_activation_t activation) {

  switch (activation) {
    case RACE_SETTING_ACTIVATION_IMMEDIATE:
      return "active now";
    case RACE_SETTING_ACTIVATION_RESTART:
      return "requires restart";
    case RACE_SETTING_ACTIVATION_NEXT_MAP:
      return "next map";
  }

  return "activation unknown";
}

/**
 * @brief The bare name of the running map, as `race admin map` wants it.
 * @details CS_BSP carries "maps/<name>.bsp"; the command takes the name alone.
 */
static const char *currentMapName(void) {

  static char buffer[MAX_QPATH];
  return Cg_RaceAdminCommand_MapToken(cgi.ConfigString(CS_BSP), buffer)
    ? buffer : NULL;
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
  return adminRows[row].setting != NULL;
}

/**
 * @brief Whether this client holds the capability the row requires.
 * @details The design gates by rendering, not by dimming: "a row whose
 * capability bit is clear is not rendered, and a section with no visible rows
 * is not rendered either". That is the stronger statement of the two - a
 * disabled control still tells an unprivileged client which levers exist and
 * invites a support question about a button that will never work - so this is
 * separated from isRowEnabled, which decides only whether a *permitted* row is
 * currently usable.
 * @remarks Presentation only. GAME revalidates authority on every command.
 */
static bool isRowPermitted(const AdminViewController *self, size_t row) {

  const AdminRowDescriptor *descriptor = &adminRows[row];

  return !descriptor->capability ||
         (self->capabilities & descriptor->capability) == descriptor->capability;
}

/**
 * @brief Whether the client may currently use this row.
 * @details Capability first, then the row's own precondition - an action that
 * consumes a free-text field is dead until that field has a representable
 * value. GAME still performs the authoritative type and capability checks.
 */
static bool isRowEnabled(const AdminViewController *self, size_t row) {

  const AdminRowDescriptor *descriptor = &adminRows[row];

  if (descriptor->capability && !(self->capabilities & descriptor->capability)) {
    return false;
  }

  switch (descriptor->action) {

    case AdminActionChangeMap:
      return Cg_RaceAdminCommand_TokenValid(textValue(self->mapName));

    case AdminActionRestartMap:
      return currentMapName() != NULL;

    case AdminActionKick:
      return Cg_RaceAdminCommand_TokenValid(textValue(self->playerSlot));

    case AdminActionValidateMap:
      return Cg_RaceAdminCommand_TokenValid(textValue(self->newMapName));

    case AdminActionGlobalGet:
    case AdminActionGlobalClear:
      return Cg_RaceAdminCommand_TokenValid(textValue(self->settingsKey));

    case AdminActionGlobalSet:
      return Cg_RaceAdminCommand_TokenValid(textValue(self->settingsKey)) &&
             isSafeSettingValue(textValue(self->settingsValue));

    case AdminActionMapGet:
    case AdminActionMapClear:
      return Cg_RaceAdminCommand_TokenValid(textValue(self->settingsKey)) &&
             isMapSettingName(textValue(self->settingsKey));

    case AdminActionMapSet:
      return Cg_RaceAdminCommand_TokenValid(textValue(self->settingsKey)) &&
             isMapSettingName(textValue(self->settingsKey)) &&
             isSafeSettingValue(textValue(self->settingsValue));

    default:
      return true;
  }
}

/**
 * @brief Recomputes which rows, sections and tabs this mask and query leave
 * standing. Declared here because the subtab strip drives it directly.
 */
static void refreshFilter(AdminViewController *self);

/**
 * @brief Leaves the Weapons tab, throwing away whatever it had staged.
 * @details Local state only. A discarded draft has never been sent, so nothing
 * reaches GAME and the accepted snapshot is exactly where it was.
 */
static void discardDraftAndSwitchTab(ident data) {

  AdminViewController *self = data;

  WeaponLabViewController_DiscardDraft(self->weaponLab);

  self->openTab = self->pendingTab;
  refreshFilter(self);
}

/**
 * @brief ButtonDelegate for the subtab strip.
 * @details Leaving Weapons with a staged draft asks first. The draft is work -
 * a set of values somebody chose and has not applied - and the tab strip is one
 * click away from every other tab on the route.
 */
static void didClickTab(Button *button) {

  AdminViewController *self = button->delegate.self;

  const size_t tab = (size_t) (intptr_t) button->delegate.data;
  if (tab == self->openTab) {
    return;
  }

  if (adminTabs[self->openTab].lab &&
      WeaponLabViewController_HasPendingDraft(self->weaponLab)) {

    self->pendingTab = tab;

    const Dialog dialog = {
      .data = self,
      .message = "Leave Weapons and discard the staged values? They have not "
                 "been applied, so the server keeps the snapshot it published "
                 "and nothing is sent.",
      .ok = "Discard draft",
      .cancel = "Keep editing",
      .okFunction = discardDraftAndSwitchTab
    };

    ViewController *viewController =
      (ViewController *) $(alloc(DialogViewController), initWithDialog, &dialog);
    $((ViewController *) self, addChildViewController, viewController);

    View *confirm = $(viewController->view, descendantWithIdentifier, "ok");
    if (confirm) {
      $(confirm, addClassName, "dangerButton");
    }
    return;
  }

  self->openTab = tab;
  refreshFilter(self);
}

/**
 * @brief Sends one admin command and records it in the response log.
 * @details The design's response log prints the command *and* the audit line it
 * produced. A client can only supply the first half honestly: the audit line is
 * a GAME-side console print - `account=... slot=... action=... subject=...
 * result=...` - and there is no wire channel carrying it back to the menu. So
 * the command is echoed exactly as sent, and the reply half says where the
 * outcome is written rather than the menu asserting an outcome it never saw.
 * @param command The command, without its trailing newline.
 */
static void postCommand(AdminViewController *self, const char *command) {

  cgi.Cbuf(va("%s\n", command));

  if (self->responseCommand == NULL) {
    return;
  }

  setTextIfChanged(self->responseCommand->text, va("> %s", command));
  setTextIfChanged(self->responseReply->text,
                   "Sent. The server writes the audit line - account, slot, "
                   "action, subject, result - to the console, and revalidates "
                   "authority and arguments before applying anything.");

  self->responseView->hidden = false;
  invalidateLayoutChain(self->responseView);
}

/**
 * @brief WeaponLabDelegate: the lab writes into this route's response block.
 * @details The design gives the lab no log of its own - "each accepted action
 * prints into the Admin route's existing response block" - so the panel hands
 * the command and what it can honestly say about it back here, and the block
 * is raised exactly as it is for a `gset`.
 */
static void didPostWeaponLabCommand(ident data, const char *command,
                                    const char *reply) {

  AdminViewController *self = data;

  if (self->responseCommand == NULL) {
    return;
  }

  setTextIfChanged(self->responseCommand->text, va("> %s", command));
  setTextIfChanged(self->responseReply->text, reply);

  self->responseView->hidden = false;
  invalidateLayoutChain(self->responseView);
}

/**
 * @brief A settings row is marked when it no longer shows its registry default.
 */
static bool isRowModified(const AdminViewController *self, size_t row) {

  if (!isSettingRow(row)) {
    return false;
  }

  return q_strcmp(self->rowValues[row], self->rowDefaults[row]) != 0;
}

#pragma mark - Commands

static void printInputError(void) {
  cgi.Print("Invalid Race administrator input; check the field format and length\n");
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

    const race_setting_descriptor_t *setting = adminSettingForRow(row);
    assert(setting);

    postCommand(self, va("gset %s %s", setting->alias, self->rowValues[row]));

    q_strlcpy(self->rowSent[row], self->rowValues[row], ADMIN_VALUE_SIZE);
    self->rowConfirmed[row] = true;
  }
}

static void runAction(AdminViewController *self, size_t row) {

  if (!isRowEnabled(self, row)) {
    printInputError();
    return;
  }

  char quotedValue[RACE_SETTING_VALUE_SIZE + 3u] = { 0 };
  if ((adminRows[row].action == AdminActionGlobalSet ||
       adminRows[row].action == AdminActionMapSet) &&
      !quoteSettingValue(textValue(self->settingsValue), quotedValue,
                         sizeof(quotedValue))) {
    printInputError();
    return;
  }

  switch (adminRows[row].action) {

    case AdminActionChangeMap:
      postCommand(self, va("race admin map %s", textValue(self->mapName)));
      break;

    case AdminActionRestartMap:
      postCommand(self, va("race admin map %s", currentMapName()));
      break;

    case AdminActionCancelVote:
      postCommand(self, "race admin vote cancel");
      break;

    case AdminActionStatus:
      postCommand(self, "race admin status");
      break;

    case AdminActionHelp:
      postCommand(self, "race admin help");
      break;

    case AdminActionLogout:
      postCommand(self, "race admin_logout");
      break;

    case AdminActionKick:
      postCommand(self, va("race admin kick %s", textValue(self->playerSlot)));
      break;

    case AdminActionGlobalGet:
      postCommand(self, va("gget %s", textValue(self->settingsKey)));
      break;

    case AdminActionGlobalSet:
      postCommand(self, va("gset %s %s", textValue(self->settingsKey), quotedValue));
      break;

    case AdminActionGlobalClear:
      postCommand(self, va("gclear %s", textValue(self->settingsKey)));
      break;

    case AdminActionMapGet:
      postCommand(self, va("mget %s", textValue(self->settingsKey)));
      break;

    case AdminActionMapSet:
      postCommand(self, va("mset %s %s", textValue(self->settingsKey), quotedValue));
      break;

    case AdminActionMapClear:
      postCommand(self, va("mclear %s", textValue(self->settingsKey)));
      break;

    case AdminActionValidateMap: {

      // The design normalizes `maps/` and `.bsp` off the input before asking.
      // GAME normalizes again - Race_MapState_CanonicalizeMap is the same call
      // a map change makes - so this is presentation, not the contract.
      char name[MAX_QPATH];
      q_strlcpy(name, textValue(self->newMapName), sizeof(name));

      char *cursor = name;
      if (!q_strncasecmp(cursor, "maps/", 5)) {
        cursor += 5;
      }
      char *dot = strrchr(cursor, '.');
      if (dot && !q_strcasecmp(dot, ".bsp")) {
        *dot = '\0';
      }

      postCommand(self, va("race admin map validate %s", cursor));

      // The server answers on the console; the row says what was asked, and
      // the console carries present / not installed. Saying more here would be
      // the menu asserting a result it never saw.
      if (self->newMapResult) {
        setTextIfChanged(self->newMapResult->text,
                         va("Asked the server about maps/%s.bsp - the answer "
                            "is printed to the console.", cursor));
        self->newMapResult->view.hidden = false;
        invalidateLayoutChain((View *) self->newMapResult);
      }
      break;
    }


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

        const race_setting_descriptor_t *setting = adminSettingForRow(row);
        assert(setting && setting->type == RACE_SETTING_ENUM);

        for (size_t i = 0; i < setting->enum_count; i++) {
          if (!q_strcmp(self->rowValues[row], setting->enum_values[i])) {
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

  for (size_t i = 0; i < ADMIN_CAPABILITY_COUNT; i++) {

    const bool held = (self->capabilities & (1u << i)) != 0;
    View *chip = (View *) self->capabilityChips[i];

    if (chip == NULL) {
      continue;
    }
    if (held) {
      $(chip, addClassName, "adminCapabilityHeld");
    } else {
      $(chip, removeClassName, "adminCapabilityHeld");
    }
  }

  // "The eyebrow flips to 'No administrator session' in brand red." The session
  // state can change while the route is open, so this rides the same mask read
  // as the chips rather than being written once when the route is built.
  MainViewController_SetRouteEyebrow(
    self->capabilities ? "Authenticated session" : "No administrator session",
    self->capabilities == 0);

  if (self->capabilityValue) {
    char mask[16];
    snprintf(mask, sizeof(mask), "0x%02x", (unsigned) (self->capabilities & 0xffu));
    setTextIfChanged(self->capabilityValue->text, mask);
  }

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

  // The design's legend closes with "n of m controls visible at 0x...", which
  // is the one line that explains why two administrators looking at the same
  // route see different numbers of rows. The unconfirmed clause precedes it
  // when there is one; on a signed-out connection neither applies.
  if (self->capabilities == 0) {
    setTextIfChanged(self->provenance->text, "");
    return;
  }

  // The weapon lab counts its own values in its footer. Its catalog, runtime
  // baseline and current values arrive only through complete GAME-authored
  // cache transactions; drafts stay local until Apply and the correlated
  // result plus following broadcast both arrive.
  if (adminTabs[self->openTab].lab) {
    setTextIfChanged(self->provenance->text,
                      "Weapon Lab verifies complete GAME-authored cache "
                      "transactions; server values are authoritative and local "
                      "drafts remain pending until the next verified broadcast");
    return;
  }

  char text[256];

  if (unconfirmed == 0) {
    snprintf(text, sizeof(text), "%zu of %zu controls visible at 0x%02x",
             self->visibleRows, self->totalRows,
             (unsigned) (self->capabilities & 0xffu));
  } else {
    snprintf(text, sizeof(text),
             "%zu of %zu controls show registry defaults - use gget or mget "
             "for authoritative server state · "
             "%zu of %zu controls visible at 0x%02x",
             unconfirmed, total, self->visibleRows, self->totalRows,
             (unsigned) (self->capabilities & 0xffu));
  }

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
    if (entries[i].ping >= 0 && entries[i].ping < 999) {
      q_snprintf(ping, sizeof(ping), "%d ms", entries[i].ping);
    } else {
      q_strlcpy(ping, "--", sizeof(ping));
    }

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
}

/**
 * @brief The JSON slot the weapon lab was hosted in.
 * @details Held through the panel rather than as an outlet of its own: the host
 * is a plain View that nothing else addresses, and the lab is always its only
 * child. Hiding the host rather than the panel is what keeps the document stack
 * from leaving its spacing behind on the five tabs that are not Weapons.
 */
static View *weaponLabHost(const AdminViewController *self) {

  return self->weaponLab ? self->weaponLab->view->superview : NULL;
}

/**
 * @brief Puts the route's shared chrome into Weapon Lab mode, or back.
 * @details The intro and the filter placeholder are written for `adminRows`,
 * and both are wrong on Weapons: nothing there applies immediately, and the
 * filter searches a catalog rather than a control list. Phase 5 asks for one
 * unambiguous mode and for ordinary Admin chrome on the way out.
 */
static void refreshChrome(AdminViewController *self, bool lab) {

  Label *intro = (Label *) $(self->viewController.view, descendantWithIdentifier,
                             "adminIntro");
  if (intro) {
    setTextIfChanged(intro->text, lab
      ? "Weapon values are the server's. A slider or a typed number is staged "
        "locally until Apply. Reset All restores the server defaults, and any "
        "custom values are automatically unranked."
      : "Admin actions apply immediately and are logged. The server "
        "revalidates your authority and every command before applying "
        "changes.");
  }

  $(self->filter, setDefaultText,
    lab ? "Filter weapon values" : "Filter admin controls");
}

/**
 * @brief One pass over the table: hide the rows the query missed, then the
 * sections left with nothing.
 */
static void refreshFilter(AdminViewController *self) {

  // "Signed out (0x00) replaces the whole panel with a centered dismissal."
  // Nothing on this route is reachable without a session, so the document is
  // taken down rather than left standing with every row gated out - which
  // would otherwise read as "nothing matches that filter".
  const bool signedOut = self->capabilities == 0;
  const Array *children = (Array *) self->document->subviews;

  for (size_t i = 0; i < children->count; i++) {

    View *child = children->elements[i];

    if (child == self->signedOutPanel) {
      child->hidden = !signedOut;
    } else if (signedOut) {
      child->hidden = true;
    } else if (child != (View *) self->emptyState &&
               child != self->responseView) {
      // The empty state is the filter pass's to decide, below; the response log
      // is the first command's, and stays down until then.
      child->hidden = false;
    }
  }

  if (signedOut) {
    self->visibleRows = 0;
    self->totalRows = ADMIN_ROW_COUNT;
    View *host = weaponLabHost(self);
    if (host) {
      host->hidden = true;
    }
    invalidateLayoutChain(self->signedOutPanel);
    return;
  }

  const char *query = filterQuery(self);

  size_t sectionHits[ADMIN_SECTION_COUNT] = { 0 };
  size_t lastVisibleRow[ADMIN_SECTION_COUNT];

  for (size_t section = 0; section < ADMIN_SECTION_COUNT; section++) {
    lastVisibleRow[section] = ADMIN_ROW_COUNT;
  }

  for (size_t row = 0; row < ADMIN_ROW_COUNT; row++) {

    const AdminRowDescriptor *descriptor = &adminRows[row];
    const race_setting_descriptor_t *setting = adminSettingForRow(row);
    const bool hit = containsIgnoringCase(descriptor->label, query) ||
                     containsIgnoringCase(descriptor->setting, query) ||
                     (setting && containsIgnoringCase(setting->cvar, query)) ||
                     (setting && containsIgnoringCase(setting->description, query));

    const bool visible = hit && isRowPermitted(self, row);

    self->rowViews[row]->hidden = !visible;
    if (visible) {
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

  // "Row accounting": each section head shows `visible / total`, where the
  // total is every row the section declares - including the ones this mask
  // hides - so the count says how much of the route this account can reach
  // rather than how much of it the query matched.
  size_t sectionRows[ADMIN_SECTION_COUNT] = { 0 };
  for (size_t row = 0; row < ADMIN_ROW_COUNT; row++) {
    sectionRows[adminRows[row].section]++;
  }

  // A query reaches every tab, not just the open one - so while one is active
  // the strip reports per-tab hit counts and the sections show across tabs.
  const bool filtering = *query != '\0';

  bool hasContent[ADMIN_SECTION_COUNT];
  size_t tabHits[ADMIN_TAB_COUNT] = { 0 };
  size_t shownRows = 0, totalRows = 0;

  for (size_t section = 0; section < ADMIN_SECTION_COUNT; section++) {

    // Players keeps its table for a query that matched a name but no row label,
    // which is the only section whose content is not in the descriptor table.
    hasContent[section] = sectionHits[section] > 0 ||
                          (section == AdminSectionPlayers && self->rosterShown > 0);

    if (hasContent[section]) {
      tabHits[adminTabForSection(section)] += sectionHits[section] > 0
        ? sectionHits[section] : 1;
    }

    shownRows += sectionHits[section];
    totalRows += sectionRows[section];

    char metric[32];
    snprintf(metric, sizeof(metric), "%zu / %zu",
             sectionHits[section], sectionRows[section]);
    setTextIfChanged(self->sectionMetrics[section]->text, metric);
    self->sectionMetrics[section]->view.hidden = false;
  }

  // Weapons is offered to any administrator who can reach the route: a viewer
  // sees every accepted value with mutation disabled, which is the doc's
  // permission model, so the tab is gated on the query rather than on a
  // capability bit. Its count is its own catalog's, not this table's.
  tabHits[ADMIN_TAB_WEAPONS] = WeaponLabViewController_Hits(query);

  WeaponLabViewController_SetQuery(self->weaponLab, query);
  WeaponLabViewController_SetCapabilities(self->weaponLab, self->capabilities);

  // "A subtab with no visible sections is not offered, and switching role away
  // from a tab that just emptied falls through to the first tab with content."
  if (tabHits[self->openTab] == 0) {
    for (size_t tab = 0; tab < ADMIN_TAB_COUNT; tab++) {
      if (tabHits[tab]) {
        self->openTab = tab;
        break;
      }
    }
  }

  // Weapons replaces the section flow rather than joining it: a query reaches
  // every tab, but the body still belongs to the tab that is open, so an
  // off-tab hit on Weapons is reported in the strip and read by switching to it.
  const bool onLab = adminTabs[self->openTab].lab;

  refreshChrome(self, onLab);

  View *host = weaponLabHost(self);
  if (host) {
    host->hidden = !onLab;
    invalidateLayoutChain(host);
  }

  bool any = onLab;

  for (size_t section = 0; section < ADMIN_SECTION_COUNT; section++) {

    const bool onOpenTab = adminTabForSection(section) == self->openTab;
    const bool visible = !onLab && hasContent[section] &&
                         (filtering || onOpenTab);

    self->sectionViews[section]->hidden = !visible;
    any = any || visible;
  }

  for (size_t tab = 0; tab < ADMIN_TAB_COUNT; tab++) {

    Button *button = self->tabs[tab];
    if (button == NULL) {
      continue;
    }

    // A tab this mask leaves empty is not offered at all.
    button->control.view.hidden = tabHits[tab] == 0;

    char label[48];
    if (filtering && tabHits[tab]) {
      snprintf(label, sizeof(label), "%s  %zu", adminTabs[tab].label, tabHits[tab]);
    } else {
      q_strlcpy(label, adminTabs[tab].label, sizeof(label));
    }
    setTextIfChanged(button->title, label);

    Control *control = (Control *) button;
    const unsigned int state = control->state;
    if (!filtering && tab == self->openTab) {
      control->state |= ControlStateSelected;
    } else {
      control->state &= ~ControlStateSelected;
    }
    if (state != control->state) {
      $(control, stateDidChange);
    }
  }

  self->visibleRows = shownRows;
  self->totalRows = totalRows;

  self->emptyState->view.hidden = any;

  for (size_t section = 0; section < ADMIN_SECTION_COUNT; section++) {
    invalidateLayoutChain(self->sectionViews[section]);
  }
  invalidateLayoutChain((View *) self->emptyState);

  // The footer's provenance line says which values on screen are confirmed, and
  // that answer is different on Weapons. It is written from here rather than
  // only from the event pass because this is the one place the open tab can
  // change - including when a query empties the open tab and the strip falls
  // through to another one.
  refreshProvenance(self);
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

    if (!isRowEnabled(self, row)) {
      snprintf(hint, sizeof(hint), "%s", descriptor->capability &&
               !(self->capabilities & descriptor->capability)
                 ? "Your admin role does not carry this capability"
                 : "Fill the field above with a single token first");
    } else if (isSettingRow(row)) {
      const race_setting_descriptor_t *setting = adminSettingForRow(row);
      assert(setting);
      snprintf(hint, sizeof(hint), "gset %s - %s%s", setting->alias,
               adminActivationLabel(setting->activation),
               self->rowConfirmed[row] ? "" : " - value unconfirmed");
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
        case AdminActionGlobalGet:
          snprintf(hint, sizeof(hint), "gget %s - reply prints to console",
                   textValue(self->settingsKey));
          break;
        case AdminActionGlobalSet:
          snprintf(hint, sizeof(hint), "gset %s <value>",
                   textValue(self->settingsKey));
          break;
        case AdminActionGlobalClear:
          snprintf(hint, sizeof(hint), "gclear %s", textValue(self->settingsKey));
          break;
        case AdminActionMapGet:
          snprintf(hint, sizeof(hint), "mget %s - reply prints to console",
                   textValue(self->settingsKey));
          break;
        case AdminActionMapSet:
          snprintf(hint, sizeof(hint), "mset %s <value> - persists for next map/restart",
                   textValue(self->settingsKey));
          break;
        case AdminActionMapClear:
          snprintf(hint, sizeof(hint), "mclear %s - clears on next map/restart",
                   textValue(self->settingsKey));
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

  // Clear the persisted global assignment rather than writing the registry
  // default as another override. A current-map override can still win.
  const race_setting_descriptor_t *setting = adminSettingForRow(row);
  assert(setting);
  postCommand(self, va("gclear %s", setting->alias));

  q_strlcpy(self->rowValues[row], self->rowDefaults[row], ADMIN_VALUE_SIZE);
  q_strlcpy(self->rowSent[row], self->rowDefaults[row], ADMIN_VALUE_SIZE);
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

  const race_setting_descriptor_t *setting = adminSettingForRow(row);
  assert(setting && setting->type == RACE_SETTING_ENUM);

  const size_t index = (size_t) (intptr_t) option->value;
  if (index >= setting->enum_count) {
    return;
  }

  q_strlcpy(self->rowValues[row], setting->enum_values[index], ADMIN_VALUE_SIZE);

  flushPendingWrites(self);
  refreshRows(self);
  refreshProvenance(self);
}

#pragma mark - Construction

static View *makeControl(AdminViewController *self, size_t row) {

  const AdminRowDescriptor *descriptor = &adminRows[row];

  switch (descriptor->kind) {

    case AdminRowToggle: {

      const race_setting_descriptor_t *setting = adminSettingForRow(row);
      assert(setting && setting->type == RACE_SETTING_BOOL);

      Checkbox *checkbox = $(alloc(Checkbox), initWithFrame, NULL);
      checkbox->delegate = (CheckboxDelegate) {
        .self = self,
        .data = (ident) (intptr_t) row,
        .didToggle = didToggleSetting
      };
      return (View *) checkbox;
    }

    case AdminRowSlider: {

      const race_setting_descriptor_t *setting = adminSettingForRow(row);
      assert(setting && setting->type == RACE_SETTING_INT);

      Slider *slider = $(alloc(Slider), initWithFrame, NULL);
      slider->min = setting->minimum;
      slider->max = setting->maximum;
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

      const race_setting_descriptor_t *setting = adminSettingForRow(row);
      assert(setting && setting->type == RACE_SETTING_ENUM);

      Select *select = $(alloc(Select), initWithFrame, NULL);
      select->delegate = (SelectDelegate) {
        .self = self,
        .didSelectOption = didSelectSetting
      };

      for (size_t i = 0; i < setting->enum_count; i++) {
        $(select, addOption, setting->enum_values[i], (ident) (intptr_t) i);
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
        case AdminFieldSettingsKey:
          self->settingsKey = textView;
          break;
        case AdminFieldSettingsValue:
          self->settingsValue = textView;
          break;
        case AdminFieldNewMapName:
          self->newMapName = textView;
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
                 descriptor->action == AdminActionGlobalSet ||
                 descriptor->action == AdminActionMapSet) {
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

  // A slider is dragged, not clicked, so it gets a wider cell than the buttons
  // and fields beside it. The cell is right-pinned, so the extra width runs
  // leftward and the control column still ends on one edge.
  if (adminRows[row].kind == AdminRowSlider) {
    $((View *) right, addClassName, "sliderCell");
  }

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
 * @brief The eight capability chips, and the mask they summarize.
 * @details The design puts this in the Session section as a wide row: one chip
 * per bit of STAT_RACE_ADMIN_CAPABILITIES, lit when the bit is held. Two of the
 * eight are named here but can never light a control on this route -
 * `account-manage` and `cvar-allowlist-manage` have no menu surface and are
 * reached from the console - and one, `player-ban`, is reserved: no Race action
 * maps to it (see Race_Admin_ActionCapability), so nothing consumes it at all.
 * Naming them rather than hiding them is what makes the count in the legend
 * add up for someone reading the row and the mask side by side.
 */
static View *makeCapabilityChips(AdminViewController *self) {

  static const char *names[ADMIN_CAPABILITY_COUNT] = {
    "settings-mutate", "map-change", "player-kick", "player-ban",
    "vote-admin", "account-manage", "server-cvar", "cvar-allowlist-manage"
  };

  StackView *view = $(alloc(StackView), initWithFrame, NULL);
  $((View *) view, addClassName, "adminCapabilities");
  view->axis = StackViewAxisHorizontal;

  Label *label = $(alloc(Label), initWithText, "Capability mask", NULL);
  $((View *) label, addClassName, "adminCapabilityLabel");
  $((View *) view, addSubview, (View *) label);
  release(label);

  StackView *chips = $(alloc(StackView), initWithFrame, NULL);
  $((View *) chips, addClassName, "adminCapabilityChips");
  chips->axis = StackViewAxisHorizontal;

  for (size_t i = 0; i < ADMIN_CAPABILITY_COUNT; i++) {

    Label *chip = $(alloc(Label), initWithText, names[i], NULL);
    $((View *) chip, addClassName, "adminCapabilityChip");

    // `player-ban` is the design's dashed chip. The dialect has no border-style,
    // so the reserved bit is carried by its own class and a dimmer fill.
    if ((1u << i) == RACE_ADMIN_CAP_PLAYER_BAN) {
      $((View *) chip, addClassName, "adminCapabilityReserved");
    }

    $((View *) chips, addSubview, (View *) chip);
    self->capabilityChips[i] = chip;
    release(chip);
  }

  $((View *) view, addSubview, (View *) chips);
  release(chips);

  Label *value = $(alloc(Label), initWithText, "0x00", NULL);
  $((View *) value, addClassName, "adminCapabilityValue");
  $((View *) view, addSubview, (View *) value);
  self->capabilityValue = value;
  release(value);

  return (View *) view;
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

  if (section == AdminSectionAddMap) {

    // The design closes this section with a note explaining why it is ungated,
    // and shows the answer under the row that asked for it.
    Label *note = $(alloc(Label), initWithText,
                    "Available to every role: reading whether a .bsp is "
                    "installed grants no authority. Fetching a map from a link "
                    "is not offered - a server-side download would need its own "
                    "capability bit and an engine API the module does not have.",
                    NULL);
    $((View *) note, addClassName, "adminSectionNote");
    note->text->lineWrap = true;
    $((View *) view, addSubview, (View *) note);
    release(note);

    Label *result = $(alloc(Label), initWithText, "", NULL);
    $((View *) result, addClassName, "adminMapResult");
    result->text->lineWrap = true;
    result->view.hidden = true;
    $((View *) view, addSubview, (View *) result);
    self->newMapResult = result;
    release(result);
  }

  if (section == AdminSectionSession) {
    View *chips = makeCapabilityChips(self);
    $((View *) view, addSubview, chips);
    release(chips);
  }

  return (View *) view;
}

#pragma mark - ViewController

static void resolveOutlets(AdminViewController *self) {

  Outlet outlets[] = MakeOutlets(
    MakeOutlet("adminFilter", &self->filter),
    MakeOutlet("adminEmptyState", &self->emptyState),
    MakeOutlet("adminSignedOut", &self->signedOutPanel),
    MakeOutlet("adminResponse", &self->responseView),
    MakeOutlet("adminResponseCommand", &self->responseCommand),
    MakeOutlet("adminResponseReply", &self->responseReply),
    MakeOutlet("adminDocument", &self->document),
    MakeOutlet("adminHint", &self->hint),
    MakeOutlet("adminProvenance", &self->provenance)
  );

  $(self->viewController.view, resolve, outlets);

  for (size_t tab = 0; tab < ADMIN_TAB_COUNT; tab++) {
    self->tabs[tab] = (Button *) $(self->viewController.view,
                                   descendantWithIdentifier, adminTabs[tab].outlet);
    assert(self->tabs[tab]);
  }

  assert(self->filter);
  assert(self->emptyState);
  assert(self->signedOutPanel);
  assert(self->responseView);
  assert(self->responseCommand);
  assert(self->responseReply);

  // Nothing has been sent yet; the log appears with the first command.
  self->responseView->hidden = true;
  assert(self->document);
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
      const race_setting_descriptor_t *setting = adminSettingForRow(row);
      assert(setting && setting->default_value);
      q_strlcpy(self->rowDefaults[row], setting->default_value, ADMIN_VALUE_SIZE);
      q_strlcpy(self->rowValues[row], self->rowDefaults[row], ADMIN_VALUE_SIZE);
      q_strlcpy(self->rowSent[row], self->rowDefaults[row], ADMIN_VALUE_SIZE);
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

  // Weapons is a hosted panel rather than a section group, so it is built here
  // and parked in its own JSON slot. It is created before the tab delegates are
  // wired because the first filter pass hands it the query and the mask.
  View *labHost = $(viewController->view, descendantWithIdentifier,
                    "adminWeaponLabHost");
  assert(labHost);

  self->weaponLab = $((ViewController *) alloc(WeaponLabViewController), init);
  assert(self->weaponLab);

  $(viewController, addChildViewController, self->weaponLab);
  $(labHost, addSubview, self->weaponLab->view);
  release(self->weaponLab);

  WeaponLabViewController_SetDelegate(self->weaponLab,
                                      &(const WeaponLabDelegate) {
    .self = self,
    .didPostCommand = didPostWeaponLabCommand
  });

  for (size_t tab = 0; tab < ADMIN_TAB_COUNT; tab++) {
    self->tabs[tab]->delegate = (ButtonDelegate) {
      .self = self,
      .data = (ident) (intptr_t) tab,
      .didClick = didClickTab
    };
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
 * @see ViewController::viewWillDisappear(ViewController *)
 * @details The eyebrow is shell chrome on loan; every other route expects it to
 * say what the session is.
 */
static void viewWillDisappear(ViewController *viewController) {

  super(ViewController, viewController, viewWillDisappear);

  MainViewController_SetRouteEyebrow(NULL, false);
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
  ((ViewControllerInterface *) clazz->interface)->viewWillDisappear = viewWillDisappear;
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
