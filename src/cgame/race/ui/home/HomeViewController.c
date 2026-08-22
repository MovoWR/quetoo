/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"

#include "HomeViewController.h"

#include "DialogViewController.h"
#include "race_wire.h"

#include <ctype.h>
#include <inttypes.h>

#define _Class _HomeViewController

static const char *recordRankColumn = "Rank";
static const char *recordPlayerColumn = "Player";
static const char *recordTimeColumn = "Time";

static const char *rosterRunnerColumn = "Runner";
static const char *rosterStatusColumn = "Status";

static HomeViewController *activeHomeViewController;

static void alignTableHeader(TableView *tableView) {
  View *view = (View *) tableView;
  TableRowView *headerRow = (TableRowView *) tableView->headerView;
  View *header = (View *) headerRow;
  SDL_Size headerSize = $(header, size);
  const SDL_Size tableSize = $(view, size);

  if (headerSize.w != tableSize.w) {
    headerSize.w = tableSize.w;
    $(header, resize, &headerSize);
  }

  const Array *rows = (Array *) tableView->rows;
  if (!rows->count) {
    return;
  }

  const TableRowView *row = $(rows, objectAtIndex, 0);
  const Array *headerCells = (Array *) headerRow->cells;
  const Array *cells = (Array *) row->cells;
  const size_t count = Minz(headerCells->count, cells->count);
  for (size_t i = 0; i < count; i++) {
    View *headerCell = (View *) $(headerCells, objectAtIndex, i);
    const View *cell = (View *) $(cells, objectAtIndex, i);
    SDL_Size size = $(headerCell, size);
    if (size.w != cell->frame.w) {
      size.w = cell->frame.w;
      $(headerCell, resize, &size);
    }
  }
}

static void setButtonState(Button *button, const bool selected,
                           const bool enabled) {
  Control *control = (Control *) button;
  const unsigned int oldState = control->state;
  if (selected) {
    control->state |= ControlStateSelected;
  } else {
    control->state &= ~ControlStateSelected;
  }
  if (enabled) {
    control->state &= ~ControlStateDisabled;
  } else {
    control->state |= ControlStateDisabled;
  }
  if (oldState != control->state) {
    $(control, stateDidChange);
  }
}

static void setLabelText(Label *label, const char *text) {
  if (q_strcmp(label->text->text, text)) {
    $(label->text, setText, text);
    label->view.needsLayout = true;
  }
}

static race_mode_t currentMode(const player_state_t *ps) {
  if (ps->stats[STAT_SPECTATOR]) {
    return RACE_MODE_SPECTATOR;
  }
  const race_mode_t mode = (race_mode_t) ps->stats[STAT_RACE_MODE];
  return mode == RACE_MODE_PRACTICE ? RACE_MODE_PRACTICE : RACE_MODE_RACE;
}

static const char *modeCommand(const race_mode_t mode) {
  switch (mode) {
    case RACE_MODE_RACE:
      return "mode race\n";
    case RACE_MODE_PRACTICE:
      return "mode practice\n";
    case RACE_MODE_SPECTATOR:
      return "mode spectator\n";
    default:
      return NULL;
  }
}

static void executeModeCommand(ident data) {
  cgi.Cbuf(data);
}

static race_mode_t modeForButton(const Button *button) {
  const char *identifier = button->control.view.identifier;
  if (!q_strcmp(identifier, "mode_race")) {
    return RACE_MODE_RACE;
  }
  if (!q_strcmp(identifier, "mode_practice")) {
    return RACE_MODE_PRACTICE;
  }
  return RACE_MODE_SPECTATOR;
}

static void didClickModeAction(Button *button) {
  HomeViewController *this = button->delegate.self;
  if (!cgi.client) {
    return;
  }

  const player_state_t *ps = &cgi.client->frame.ps;
  const race_mode_t current = currentMode(ps);
  const race_mode_t requested = modeForButton(button);
  const char *command = modeCommand(requested);
  if (!command || requested == current) {
    return;
  }

  if (!ps->stats[STAT_SPECTATOR] &&
      ps->stats[STAT_RACE_RUN_STATE] == RACE_RUN_ACTIVE) {
    const Dialog dialog = {
      .data = (ident) command,
      .message = "Switch modes? Your active run will end.",
      .ok = "Switch Mode",
      .cancel = "Cancel",
      .okFunction = executeModeCommand
    };
    ViewController *dialogViewController = (ViewController *)
      $(alloc(DialogViewController), initWithDialog, &dialog);
    $((ViewController *) this, addChildViewController, dialogViewController);
  } else {
    cgi.Cbuf(command);
  }
}

static const char *formatRaceTime(const uint32_t timeMilliseconds) {
  if (!timeMilliseconds) {
    return "--:--.---";
  }
  return va("%u:%02u.%03u", timeMilliseconds / 60000u,
            timeMilliseconds / 1000u % 60u, timeMilliseconds % 1000u);
}

static const char *chaseTargetName(const cg_roster_entry_t *entries,
                                   const size_t count, const int16_t target) {
  if (target < 0) {
    return "Free camera";
  }
  for (size_t i = 0; i < count; i++) {
    if (entries[i].client == target) {
      return entries[i].name;
    }
  }
  return "No target";
}

static size_t numberOfSummaryRecordRows(const TableView *tableView) {
  const HomeViewController *this = tableView->dataSource.self;
  return Minz(this->numRecords, 3u);
}

static TableCellView *recordCellForColumnAndRow(const TableView *tableView,
                                                const TableColumn *column,
                                                const size_t row) {
  const HomeViewController *this = tableView->dataSource.self;
  const cg_leaderboard_snapshot_entry_t *entry = this->records + row;
  TableCellView *cell = $(alloc(TableCellView), initWithFrame, NULL);
  if (!row) {
    $((View *) cell, addClassName, "recordWorld");
  }
  if (entry->local_pb) {
    $((View *) cell, addClassName, "recordLocal");
  }

  if (!q_strcmp(column->identifier, recordRankColumn)) {
    $(cell->text, setText, va("%zu", row + 1u));
  } else if (!q_strcmp(column->identifier, recordPlayerColumn)) {
    $(cell->text, setText, entry->name);
    cell->text->colorEscapes = true;
  } else if (!q_strcmp(column->identifier, recordTimeColumn)) {
    // TableColumn stamps its identifier onto the header cell only, so a data
    // cell needs its own class to pick up the column's alignment.
    $((View *) cell, addClassName, "recordNumeric");
    $(cell->text, setText, formatRaceTime(entry->time_ms));
  }
  return cell;
}

static bool isLocalRosterEntry(const cg_roster_entry_t *entry) {
  return cgi.client && entry->client == cgi.client->frame.ps.client;
}

/**
 * @brief Describes what a roster entry is doing right now. The local client is
 * called out by name so the runner can find themselves in a full session.
 */
static const char *rosterStatusText(const HomeViewController *this,
                                    const cg_roster_entry_t *entry) {
  const bool local = isLocalRosterEntry(entry);

  switch (entry->group) {
    case CG_ROSTER_SPECTATOR: {
      if (entry->spectator_target < 0) {
        return local ? "You · free camera" : "Free camera";
      }
      const char *target = chaseTargetName(this->players, this->numPlayers,
                                           entry->spectator_target);
      return local ? va("You · watching %s", target)
                   : va("Watching %s", target);
    }
    case CG_ROSTER_PRACTICE_MODE:
      return local ? "You · practicing" : "Practicing";
    default:
      return local ? "You · racing" : "Racing";
  }
}

/**
 * @brief Folds `src` into `dst` the way the filter compares text: lowercased,
 * with Quetoo's `^N` color escapes dropped. A roster name carries its escapes
 * inline (`^2sata`), and the Runner cell renders them rather than printing
 * them, so the visible name and the stored name are not the same string.
 */
static const char *rosterPlainText(const char *src, char *dst,
                                   const size_t size) {
  size_t out = 0;
  for (const char *p = src ? src : ""; *p && out < size - 1; p++) {
    if (p[0] == '^' && p[1] >= '0' && p[1] <= '9') {
      p++;
      continue;
    }
    dst[out++] = (char) tolower((unsigned char) *p);
  }
  dst[out] = '\0';
  return dst;
}

/**
 * @brief Decides whether a roster entry survives the route's filter. `this->
 * filter` is already folded by rosterPlainText, and is never empty here.
 *
 * The filter matches the Runner column alone, exactly as the "Filter runners"
 * placeholder promises. Local entries follow the same rule as every other
 * entry so every visible result has an explicit name match.
 */
static bool rosterMatchesFilter(const HomeViewController *this,
                                const cg_roster_entry_t *entry) {
  char name[sizeof(entry->name)];
  rosterPlainText(entry->name, name, sizeof(name));
  return strstr(name, this->filter) != NULL;
}

/**
 * @brief Rebuilds the surviving row map. An empty filter admits everything, so
 * the table's default state costs no comparisons.
 */
static void applyRosterFilter(HomeViewController *this) {
  this->numMatches = 0;
  for (size_t i = 0; i < this->numPlayers; i++) {
    if (!*this->filter || rosterMatchesFilter(this, this->players + i)) {
      this->matches[this->numMatches++] = i;
    }
  }
}

static size_t numberOfRosterRows(const TableView *tableView) {
  const HomeViewController *this = tableView->dataSource.self;
  return this->numMatches;
}

static TableCellView *rosterCellForColumnAndRow(const TableView *tableView,
                                                const TableColumn *column,
                                                const size_t row) {
  const HomeViewController *this = tableView->dataSource.self;
  const cg_roster_entry_t *entry = this->players + this->matches[row];
  TableCellView *cell = $(alloc(TableCellView), initWithFrame, NULL);
  if (isLocalRosterEntry(entry)) {
    $((View *) cell, addClassName, "rosterLocal");
  }

  if (!q_strcmp(column->identifier, rosterRunnerColumn)) {
    $(cell->text, setText, entry->name);
    cell->text->colorEscapes = true;
  } else if (!q_strcmp(column->identifier, rosterStatusColumn)) {
    $((View *) cell, addClassName, "rosterStatus");
    $(cell->text, setText, rosterStatusText(this, entry));
  }
  return cell;
}

static size_t countRosterGroup(const HomeViewController *this,
                               const cg_roster_group_t group) {
  size_t count = 0;
  for (size_t i = 0; i < this->numPlayers; i++) {
    if (this->players[i].group == group) {
      count++;
    }
  }
  return count;
}

static const char *rosterSummaryText(const char *label, const size_t count) {
  if (!count) {
    return va("%s · No players", label);
  }
  return va("%s · %zu player%s", label, count, count == 1u ? "" : "s");
}

static const char *configStringOr(const int32_t index, const char *fallback) {
  const char *value = cgi.ConfigString(index);
  return value && *value ? value : fallback;
}

/**
 * @brief Reflects the filter over the roster: the surviving rows, the header
 * count, and the empty state that stands in for a table with nothing in it.
 */
static void refreshRosterFilter(HomeViewController *this) {
  applyRosterFilter(this);
  $(this->rosterTable, reloadData);
  alignTableHeader(this->rosterTable);

  // A filtered-out roster shows the empty state alone: no rows, and no column
  // header band above them. The section head keeps its label and count.
  const bool empty = this->numMatches == 0;
  if (this->rosterEmpty->view.hidden == empty) {
    this->rosterEmpty->view.hidden = !empty;
    this->rosterTable->control.view.hidden = empty;
    this->rosterHeaderBand->hidden = empty;
    this->rosterPanel->needsLayout = true;
  }

  // Only blame the filter when there is one. An unfiltered roster comes up
  // empty until the server sends scores, which it only does while a client
  // holds the scoreboard open (G_ClientScores), so this is the common case.
  setLabelText(this->rosterEmpty, *this->filter
    ? "No runners match that filter."
    : "No runners to show yet.");

  // The design counts what the filter admits against the server's capacity,
  // so the section metric moves as the runner types.
  setLabelText(this->rosterCount, va("%zu / %d", this->numMatches,
                                     cg_state.max_clients));
}

static void didEditRosterFilter(TextView *textView) {
  HomeViewController *this = textView->delegate.self;
  rosterPlainText(textView->attributedText->chars, this->filter,
                  sizeof(this->filter));
  refreshRosterFilter(this);
}

static void layoutConnectedDashboard(HomeViewController *this) {
  View *page = this->viewController.view;
  const SDL_Rect bounds = $(page, bounds);
  const int32_t width = Maxi(1, bounds.w);
  const int32_t height = Maxi(1, bounds.h);

  View *container = this->dashboardRoot->view.superview;
  while (container && container != page) {
    container->frame.w = width;
    container->frame.h = height;
    container->minSize = MakeSize(width, height);
    container->maxSize = MakeSize(width, height);
    container->needsLayout = true;
    container = container->superview;
  }

  View *dashboard = (View *) this->dashboardRoot;
  View *session = (View *) this->sessionDashboard;
  dashboard->frame = MakeRect(0, 0, width, height);
  dashboard->minSize = MakeSize(width, height);
  dashboard->maxSize = MakeSize(width, height);
  session->frame = MakeRect(0, 0, width, height);
  session->minSize = MakeSize(width, height);
  session->maxSize = MakeSize(width, height);

  const Array *columns = (Array *) session->subviews;
  if (columns->count) {
    View *sessionColumn = columns->elements[0];
    sessionColumn->minSize = MakeSize(width, height);
    sessionColumn->maxSize = MakeSize(width, height);
  }

  // The design separates stacked sections by clamp(16px, 2.4vh, 32px) and the
  // two overview columns by clamp(24px, 4vw, 64px). The page very nearly spans
  // the viewport, so its own bounds stand in for vh/vw.
  const int32_t sectionGap = Mini(Maxi(height * 24 / 1000, 16), 32);
  const int32_t columnGap = Mini(Maxi(width / 25, 24), 64);

  // The route's one filter slot: a 34px field on the left, the route hint on
  // the right, separated from the first section by clamp(8px, 1.2vh, 14px).
  const int32_t filterHeight = 34;
  const int32_t filterGap = Mini(Maxi(height * 12 / 1000, 8), 14);
  const int32_t filterFieldWidth = Mini(260, width);
  const int32_t hintX = filterFieldWidth + 16;
  const int32_t hintWidth = Maxi(0, width - hintX);
  this->routeFilter->frame = MakeRect(0, 0, width, filterHeight);
  this->routeFilter->minSize = MakeSize(width, filterHeight);
  this->routeFilter->maxSize = MakeSize(width, filterHeight);
  this->rosterFilter->control.view.frame = MakeRect(0, 0, filterFieldWidth,
                                                    filterHeight);
  this->rosterFilter->control.view.minSize = MakeSize(filterFieldWidth,
                                                      filterHeight);
  this->rosterFilter->control.view.maxSize = this->rosterFilter->control.view.minSize;
  this->routeHint->view.frame = MakeRect(hintX, 0, hintWidth, filterHeight);
  this->routeHint->view.minSize = MakeSize(hintWidth, filterHeight);
  this->routeHint->view.maxSize = this->routeHint->view.minSize;

  const int32_t mapY = filterHeight + filterGap;
  const int32_t mapHeight = 102;
  const int32_t overviewY = mapY + mapHeight + sectionGap;
  const int32_t overviewHeight = Maxi(0, height - overviewY);
  this->sessionColumn->frame = MakeRect(0, 0, width, height);
  this->mapSummary->frame = MakeRect(0, mapY, width, mapHeight);
  this->mapSummary->minSize = MakeSize(width, mapHeight);
  this->mapSummary->maxSize = MakeSize(width, mapHeight);
  this->mapSectionTitle->view.frame = MakeRect(0, 0, width, 20);
  this->mapSectionTitle->view.minSize = MakeSize(width, 20);
  this->mapSectionTitle->view.maxSize = MakeSize(width, 20);
  this->mapRule->frame = MakeRect(0, 27, width, 1);
  this->mapRule->minSize = MakeSize(width, 1);
  this->mapRule->maxSize = MakeSize(width, 1);
  const int32_t mapContentY = 38;
  const int32_t mapContentHeight = Maxi(0, mapHeight - mapContentY);
  this->mapHeadingRow->view.frame = MakeRect(0, mapContentY, width,
                                             mapContentHeight);
  this->mapHeadingRow->view.minSize = MakeSize(width, mapContentHeight);
  this->mapHeadingRow->view.maxSize = MakeSize(width, mapContentHeight);

  const int32_t modeWidth = Mini(Maxi(0, width - 24), 300);
  const int32_t mapCopyWidth = Maxi(0, width - modeWidth - 24);
  const int32_t modeHeight = 41;
  this->mapCopy->view.minSize = MakeSize(mapCopyWidth, mapContentHeight);
  this->mapCopy->view.maxSize = MakeSize(mapCopyWidth, mapContentHeight);
  this->modePanel->view.minSize = MakeSize(modeWidth, modeHeight);
  this->modePanel->view.maxSize = MakeSize(modeWidth, modeHeight);

  this->overviewRow->frame = MakeRect(0, overviewY, width, overviewHeight);
  this->overviewRow->minSize = MakeSize(width, overviewHeight);
  this->overviewRow->maxSize = MakeSize(width, overviewHeight);

  int32_t summaryWidth;
  int32_t summaryHeight;
  int32_t rosterX;
  int32_t rosterY;
  int32_t rosterWidth;
  int32_t rosterHeight;
  if (width >= 720) {
    summaryWidth = (width - columnGap) / 2;
    summaryHeight = Mini(207, overviewHeight);
    rosterX = summaryWidth + columnGap;
    rosterY = 0;
    rosterWidth = Maxi(0, width - rosterX);
    rosterHeight = summaryHeight;
  } else {
    summaryWidth = width;
    summaryHeight = Mini(overviewHeight, Maxi(140,
      (overviewHeight - columnGap) / 2));
    rosterX = 0;
    rosterY = summaryHeight + columnGap;
    rosterWidth = width;
    rosterHeight = Maxi(0, overviewHeight - rosterY);
  }

  this->recordsSummaryPanel->frame = MakeRect(0, 0, summaryWidth,
                                               summaryHeight);
  this->recordsSummaryPanel->minSize = MakeSize(summaryWidth, summaryHeight);
  this->recordsSummaryPanel->maxSize = MakeSize(summaryWidth, summaryHeight);
  this->rosterPanel->frame = MakeRect(rosterX, rosterY, rosterWidth,
                                      rosterHeight);
  this->rosterPanel->minSize = MakeSize(rosterWidth, rosterHeight);
  this->rosterPanel->maxSize = MakeSize(rosterWidth, rosterHeight);

  const int32_t summaryHeaderHeight = 52;
  const int32_t summaryButtonWidth = 0;
  const int32_t worldRecordWidth = 130;

  // One height for all four header labels, because the two panels sit side by
  // side and their header lines have to read as one line. The stylesheet says
  // 36 for `.summaryTitle`, `.rosterTitle`, `.worldRecord` and `.rosterCount`
  // alike; the roster's two were the only ones measured here instead, at
  // `rosterHeaderHeight - 8`. Pinning min and max to 44 is what made that
  // stick - `resize` clamps, so the stylesheet's 36 came back out as 44 - and a
  // label centred in 44 sits 4 points below the same label centred in 36. That
  // is the step between "Records / WR" and "Runners / 1 / 64".
  const int32_t summaryTitleHeight = 36;
  const int32_t summaryTableHeight = Maxi(0, summaryHeight -
    summaryHeaderHeight);
  this->recordsSummaryHeader->frame = MakeRect(0, 0, summaryWidth,
                                                summaryHeaderHeight);
  this->recordsSummaryRule->frame = MakeRect(0, summaryHeaderHeight - 13,
                                              summaryWidth, 1);
  this->recordsSummaryRule->minSize = MakeSize(summaryWidth, 1);
  this->recordsSummaryRule->maxSize = MakeSize(summaryWidth, 1);
  this->recordsSummaryTitle->view.frame = MakeRect(0, 4,
    Maxi(0, summaryWidth - summaryButtonWidth - worldRecordWidth - 12),
    summaryTitleHeight);
  this->recordsSummaryTitle->view.minSize = MakeSize(
    this->recordsSummaryTitle->view.frame.w, summaryTitleHeight);
  this->recordsSummaryTitle->view.maxSize =
    this->recordsSummaryTitle->view.minSize;
  this->worldRecord->view.frame = MakeRect(
    Maxi(0, summaryWidth - summaryButtonWidth - worldRecordWidth), 4,
    worldRecordWidth, summaryTitleHeight);
  this->worldRecord->view.minSize = MakeSize(worldRecordWidth, summaryTitleHeight);
  this->worldRecord->view.maxSize = MakeSize(worldRecordWidth, summaryTitleHeight);
  this->summaryRecordsHeaderBand->frame = MakeRect(0, summaryHeaderHeight,
    summaryWidth, 44);
  this->summaryRecordsHeaderBand->minSize = MakeSize(summaryWidth, 44);
  this->summaryRecordsHeaderBand->maxSize = MakeSize(summaryWidth, 44);
  this->summaryRecordsTable->control.view.frame = MakeRect(0,
    summaryHeaderHeight, summaryWidth, summaryTableHeight);
  this->summaryRecordsTable->control.view.minSize = MakeSize(summaryWidth,
                                                              summaryTableHeight);
  this->summaryRecordsTable->control.view.maxSize = MakeSize(summaryWidth,
                                                              summaryTableHeight);

  const int32_t rosterHeaderHeight = 52;
  const int32_t rosterFooterHeight = 0;
  this->rosterFooter->view.hidden = true;
  const int32_t rosterTableHeight = Maxi(0, rosterHeight -
    rosterHeaderHeight - rosterFooterHeight);
  const int32_t rosterCountWidth = 100;
  this->rosterHeader->frame = MakeRect(0, 0, rosterWidth, rosterHeaderHeight);
  this->rosterRule->frame = MakeRect(0, rosterHeaderHeight - 13,
                                     rosterWidth, 1);
  this->rosterRule->minSize = MakeSize(rosterWidth, 1);
  this->rosterRule->maxSize = MakeSize(rosterWidth, 1);
  this->rosterTitle->view.frame = MakeRect(0, 4,
    Maxi(0, rosterWidth - rosterCountWidth), summaryTitleHeight);
  this->rosterTitle->view.minSize = MakeSize(
    this->rosterTitle->view.frame.w, summaryTitleHeight);
  this->rosterTitle->view.maxSize = this->rosterTitle->view.minSize;
  this->rosterCount->view.frame = MakeRect(
    Maxi(0, rosterWidth - rosterCountWidth), 4, rosterCountWidth,
    summaryTitleHeight);
  this->rosterCount->view.minSize = MakeSize(rosterCountWidth,
                                             summaryTitleHeight);
  this->rosterCount->view.maxSize = this->rosterCount->view.minSize;
  this->rosterHeaderBand->frame = MakeRect(0, rosterHeaderHeight,
                                           rosterWidth, 44);
  this->rosterHeaderBand->minSize = MakeSize(rosterWidth, 44);
  this->rosterHeaderBand->maxSize = MakeSize(rosterWidth, 44);
  this->rosterTable->control.view.frame = MakeRect(0, rosterHeaderHeight,
                                                   rosterWidth, rosterTableHeight);
  this->rosterTable->control.view.minSize = MakeSize(rosterWidth,
                                                     rosterTableHeight);
  this->rosterTable->control.view.maxSize = MakeSize(rosterWidth,
                                                     rosterTableHeight);
  this->rosterFooter->view.frame = MakeRect(0,
    rosterHeaderHeight + rosterTableHeight, rosterWidth, rosterFooterHeight);
  this->rosterFooter->view.minSize = MakeSize(rosterWidth, rosterFooterHeight);
  this->rosterFooter->view.maxSize = MakeSize(rosterWidth, rosterFooterHeight);

  // The empty state stands where the header band and rows would be, so a
  // filtered-out roster reads as one line of copy under the section rule.
  const int32_t emptyHeight = Mini(36, rosterTableHeight);
  this->rosterEmpty->view.frame = MakeRect(0, rosterHeaderHeight, rosterWidth,
                                           emptyHeight);
  this->rosterEmpty->view.minSize = MakeSize(rosterWidth, emptyHeight);
  this->rosterEmpty->view.maxSize = this->rosterEmpty->view.minSize;

  this->mapSummary->needsLayout = true;
  this->mapHeadingRow->view.needsLayout = true;
  this->modePanel->view.needsLayout = true;
  this->recordsSummaryPanel->needsLayout = true;
  this->recordsSummaryHeader->needsLayout = true;
  this->rosterPanel->needsLayout = true;
  this->rosterHeader->needsLayout = true;
  this->rosterFooter->view.needsLayout = true;

  dashboard->needsLayout = session->needsLayout = true;
  $(dashboard, layoutIfNeeded);
}

static void refreshPlayerActions(HomeViewController *this,
                                 const player_state_t *ps) {
  const bool isActive = *cgi.state == CL_ACTIVE && ps;
  const bool wasHidden = this->dashboardRoot->view.hidden;
  this->dashboardRoot->view.hidden = !isActive;
  if (!isActive) {
    if (!wasHidden) {
      this->viewController.view->needsLayout = true;
    }
    return;
  }

  layoutConnectedDashboard(this);
  alignTableHeader(this->summaryRecordsTable);
  alignTableHeader(this->rosterTable);
  setLabelText(this->mapTitle, configStringOr(CS_MESSAGE, "Unknown map"));
  setLabelText(this->mapIdentifier, configStringOr(CS_BSP, "unknown_map"));

  const race_mode_t mode = currentMode(ps);
  const bool spectator = mode == RACE_MODE_SPECTATOR;
  const bool chasing = spectator && ps->stats[STAT_CHASE] > 0;
  const uint32_t currentMilliseconds = Race_WireElapsed(
    ps->stats[STAT_RACE_ELAPSED_LOW], ps->stats[STAT_RACE_ELAPSED_HIGH]);
  const bool invalid = ps->stats[STAT_RACE_INVALID_FLAGS] != 0;

  setButtonState(this->modeRaceButton, mode == RACE_MODE_RACE, true);
  setButtonState(this->modePracticeButton, mode == RACE_MODE_PRACTICE, true);
  setButtonState(this->modeSpectatorButton, mode == RACE_MODE_SPECTATOR, true);

  char status[128];
  if (spectator) {
    const int16_t target = ps->stats[STAT_CHASE] - 1;
    q_snprintf(status, sizeof(status), "Spectating · %s",
               chasing ? chaseTargetName(this->players, this->numPlayers, target)
                       : "free camera");
  } else if (invalid) {
    q_snprintf(status, sizeof(status), "%s · invalid run",
               mode == RACE_MODE_PRACTICE ? "Practice Mode" : "Race Mode");
  } else if (ps->stats[STAT_RACE_RUN_STATE] == RACE_RUN_ACTIVE) {
    q_snprintf(status, sizeof(status), "%s · running %s",
               mode == RACE_MODE_PRACTICE ? "Practice Mode" : "Race Mode",
               formatRaceTime(currentMilliseconds));
  } else {
    q_snprintf(status, sizeof(status), "You are in %s",
               mode == RACE_MODE_PRACTICE ? "Practice Mode" : "Race Mode");
  }

  setLabelText(this->modeStatus, status);

  if (wasHidden) {
    this->viewController.view->needsLayout = true;
  }
}

static void refreshRoster(HomeViewController *this) {
  const bool isActive = *cgi.state == CL_ACTIVE;
  const bool dashboardWasHidden = this->dashboardRoot->view.hidden;
  const bool disconnectedWasHidden = this->disconnectedPanel->view.hidden;
  this->dashboardRoot->view.hidden = !isActive;
  this->disconnectedPanel->view.hidden = isActive;
  if (dashboardWasHidden != this->dashboardRoot->view.hidden ||
      disconnectedWasHidden != this->disconnectedPanel->view.hidden) {
    this->dashboardRoot->view.superview->needsLayout = true;
  }
  if (!isActive) {
    return;
  }

  this->numRecords = Cg_LeaderboardSnapshot(
    this->records, lengthof(this->records));
  this->numPlayers = Cg_RosterSnapshot(
    this->players, lengthof(this->players));
  setLabelText(this->worldRecord, this->numRecords
    ? va("WR %s", formatRaceTime(this->records[0].time_ms))
    : "WR --:--.---");
  $(this->summaryRecordsTable, reloadData);

  setLabelText(this->rosterPracticeSummary,
               rosterSummaryText("Practice Mode",
                                 countRosterGroup(this,
                                                  CG_ROSTER_PRACTICE_MODE)));
  setLabelText(this->rosterSpectatorSummary,
               rosterSummaryText("Spectate",
                                 countRosterGroup(this, CG_ROSTER_SPECTATOR)));
  refreshRosterFilter(this);
  this->dashboardRoot->view.needsLayout = true;
  refreshPlayerActions(this, cgi.client ? &cgi.client->frame.ps : NULL);
}

static void dealloc(Object *self) {
  HomeViewController *this = (HomeViewController *) self;
  if (activeHomeViewController == this) {
    activeHomeViewController = NULL;
  }
  super(Object, self, dealloc);
}

static void loadView(ViewController *self) {
  super(ViewController, self, loadView);
  HomeViewController *this = (HomeViewController *) self;
  View *view = $$(View, viewWithResourceName,
                 "ui/home/HomeViewController.json", NULL);
  assert(view);
  view->stylesheet = $$(Stylesheet, stylesheetWithResourceName,
                       "ui/home/HomeViewController.css");
  assert(view->stylesheet);

  Outlet outlets[] = MakeOutlets(
    MakeOutlet("disconnectedPanel", &this->disconnectedPanel),
    MakeOutlet("dashboardRoot", &this->dashboardRoot),
    MakeOutlet("sessionDashboard", &this->sessionDashboard),
    MakeOutlet("sessionColumn", &this->sessionColumn),
    MakeOutlet("routeFilter", &this->routeFilter),
    MakeOutlet("rosterFilter", &this->rosterFilter),
    MakeOutlet("routeHint", &this->routeHint),
    MakeOutlet("mapSummary", &this->mapSummary),
    MakeOutlet("mapSectionTitle", &this->mapSectionTitle),
    MakeOutlet("mapRule", &this->mapRule),
    MakeOutlet("mapHeadingRow", &this->mapHeadingRow),
    MakeOutlet("mapCopy", &this->mapCopy),
    MakeOutlet("modePanel", &this->modePanel),
    MakeOutlet("overviewRow", &this->overviewRow),
    MakeOutlet("recordsSummaryPanel", &this->recordsSummaryPanel),
    MakeOutlet("recordsSummaryHeader", &this->recordsSummaryHeader),
    MakeOutlet("recordsSummaryRule", &this->recordsSummaryRule),
    MakeOutlet("recordsSummaryTitle", &this->recordsSummaryTitle),
    MakeOutlet("worldRecord", &this->worldRecord),
    MakeOutlet("summaryRecordsHeaderBand", &this->summaryRecordsHeaderBand),
    MakeOutlet("summaryRecordsTable", &this->summaryRecordsTable),
    MakeOutlet("rosterPanel", &this->rosterPanel),
    MakeOutlet("rosterHeader", &this->rosterHeader),
    MakeOutlet("rosterRule", &this->rosterRule),
    MakeOutlet("rosterTitle", &this->rosterTitle),
    MakeOutlet("rosterCount", &this->rosterCount),
    MakeOutlet("rosterHeaderBand", &this->rosterHeaderBand),
    MakeOutlet("rosterTable", &this->rosterTable),
    MakeOutlet("rosterEmpty", &this->rosterEmpty),
    MakeOutlet("rosterFooter", &this->rosterFooter),
    MakeOutlet("rosterPracticeSummary", &this->rosterPracticeSummary),
    MakeOutlet("rosterSpectatorSummary", &this->rosterSpectatorSummary),
    MakeOutlet("mapTitle", &this->mapTitle),
    MakeOutlet("mapIdentifier", &this->mapIdentifier),
    MakeOutlet("modeStatus", &this->modeStatus),
    MakeOutlet("mode_race", &this->modeRaceButton),
    MakeOutlet("mode_practice", &this->modePracticeButton),
    MakeOutlet("mode_spectator", &this->modeSpectatorButton)
  );
  $(view, resolve, outlets);
  $(self, setView, view);
  release(view);

  this->sessionDashboard->view.hidden = false;
  this->modeStatus->view.hidden = true;
  this->rosterEmpty->view.hidden = true;
  this->rosterFilter->delegate = (TextViewDelegate) {
    .self = this, .didEdit = didEditRosterFilter
  };
  $(this->summaryRecordsTable, addColumnWithIdentifier, recordRankColumn);
  $(this->summaryRecordsTable, addColumnWithIdentifier, recordPlayerColumn);
  $(this->summaryRecordsTable, addColumnWithIdentifier, recordTimeColumn);
  this->summaryRecordsTable->control.selection = ControlSelectionNone;
  this->summaryRecordsTable->dataSource = (TableViewDataSource) {
    .self = this,
    .numberOfRows = numberOfSummaryRecordRows
  };
  this->summaryRecordsTable->delegate = (TableViewDelegate) {
    .self = this,
    .cellForColumnAndRow = recordCellForColumnAndRow
  };

  $(this->rosterTable, addColumnWithIdentifier, rosterRunnerColumn);
  $(this->rosterTable, addColumnWithIdentifier, rosterStatusColumn);
  this->rosterTable->control.selection = ControlSelectionNone;
  this->rosterTable->dataSource = (TableViewDataSource) {
    .self = this,
    .numberOfRows = numberOfRosterRows
  };
  this->rosterTable->delegate = (TableViewDelegate) {
    .self = this,
    .cellForColumnAndRow = rosterCellForColumnAndRow
  };

  Button *modeButtons[] = {
    this->modeRaceButton, this->modePracticeButton, this->modeSpectatorButton
  };
  for (size_t i = 0; i < lengthof(modeButtons); i++) {
    modeButtons[i]->delegate = (ButtonDelegate) {
      .self = self, .didClick = didClickModeAction
    };
  }
  activeHomeViewController = this;
  refreshRoster(this);
}

static void viewWillAppear(ViewController *self) {
  super(ViewController, self, viewWillAppear);
  activeHomeViewController = (HomeViewController *) self;
  refreshRoster(activeHomeViewController);
}

static void viewWillDisappear(ViewController *self) {
  HomeViewController *this = (HomeViewController *) self;
  if (activeHomeViewController == this) {
    activeHomeViewController = NULL;
  }
  super(ViewController, self, viewWillDisappear);
}

static void initialize(Class *clazz) {
  ((ObjectInterface *) clazz->interface)->dealloc = dealloc;
  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
  ((ViewControllerInterface *) clazz->interface)->viewWillAppear = viewWillAppear;
  ((ViewControllerInterface *) clazz->interface)->viewWillDisappear = viewWillDisappear;
}

void HomeViewController_Refresh(void) {
  if (activeHomeViewController) {
    refreshRoster(activeHomeViewController);
  }
}

void HomeViewController_RefreshPlayerActions(const player_state_t *ps) {
  if (activeHomeViewController) {
    refreshPlayerActions(activeHomeViewController, ps);
  }
}

Class *_HomeViewController(void) {
  static Class *clazz;
  static Once once;
  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "HomeViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(HomeViewController),
      .interfaceOffset = offsetof(HomeViewController, interface),
      .interfaceSize = sizeof(HomeViewControllerInterface),
      .initialize = initialize,
    });
  });
  return clazz;
}

#undef _Class
