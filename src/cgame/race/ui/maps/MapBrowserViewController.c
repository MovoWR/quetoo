/*
 * Copyright(c) 2026 Quetoo Race Module
 */

#include "cg_local.h"

#include "MapBrowserViewController.h"

#include <time.h>

#include <ObjectivelyMVC/ScrollView.h>

#define _Class _MapBrowserViewController

/**
 * @brief Column identifiers double as CSS ids, so they carry no spaces; the
 * two that read differently in the header get their title set explicitly.
 */
static const char *_map = "Map";
static const char *_title = "Title";
static const char *_author = "Author";
static const char *_best = "WR";
static const char *_pb = "PB";
static const char *_rank = "Rank";
static const char *_player = "Player";
static const char *_time = "Time";

/**
 * @brief Stands in for every unset time, count and field on the route.
 */
static const char *_unset = "—";

/**
 * @brief One row of either table, as `#Maps .mapListPane TableCellView` and
 * `#Maps .mapDetailsPane TableCellView` state it in
 * MapBrowserViewController.css. Stated here as well because the row heights
 * are counted rather than measured - see sizeTableToRows.
 */
#define RACE_MAP_ROW_HEIGHT 34

/**
 * @brief The tallest each table may become: a full page of rows under its
 * header, which is the `max-height` its rule in MapBrowserViewController.css
 * states. Past this the table scrolls its own rows rather than growing the
 * route.
 */
#define RACE_MAP_TABLE_HEIGHT(rows) (((rows) + 1) * RACE_MAP_ROW_HEIGHT)
#define RACE_MAP_LIST_MAX_HEIGHT RACE_MAP_TABLE_HEIGHT(RACE_MAP_BROWSER_MAX_ROWS)
#define RACE_MAP_TIMES_MAX_HEIGHT RACE_MAP_TABLE_HEIGHT(RACE_MAP_BROWSER_MAX_TIMES)

static MapBrowserViewController *activeMapBrowserViewController;

/**
 * @brief Gives the Maps document vertical overflow while MainView reflows its panes.
 */
static ScrollView *MapBrowser_WrapPage(View *page) {
  SDL_Rect viewport = page->frame;
  ScrollView *scrollView = $(alloc(ScrollView), initWithFrame, &viewport);
  assert(scrollView);

  View *scrollViewView = (View *) scrollView;
  scrollViewView->autoresizingMask = ViewAutoresizingWidth | ViewAutoresizingHeight;
  scrollViewView->minSize = MakeSize(0, 0);
  scrollViewView->maxSize = MakeSize(INT32_MAX, INT32_MAX);
  scrollViewView->clipsSubviews = true;
  scrollViewView->stylesheet = $$(Stylesheet, stylesheetWithResourceName,
                                  "ui/maps/MapBrowserViewController.css");
  assert(scrollViewView->stylesheet);
  $(scrollViewView, addClassName, "esc-page-scroll");
  scrollView->step = 40.f;

  page->alignment = ViewAlignmentTopLeft;
  page->autoresizingMask |= ViewAutoresizingWidth | ViewAutoresizingContain;
  page->frame.x = page->frame.y = 0;
  page->minSize.w = 0;
  page->maxSize.w = INT32_MAX;
  $(scrollView, setContentView, page);

  return scrollView;
}

/**
 * @brief Marks a view and every ancestor above it as needing layout.
 * @details View::layoutIfNeeded lays out only a view that carries needsLayout,
 * and StackView::layoutSubviews flows `visibleSubviews` - so hiding or showing
 * a child changes the whole column beneath it, and nothing about writing the
 * flag says so. Marking upward is what reaches the stack that has to re-flow.
 */
static void invalidateLayoutChain(View *view) {

  while (view) {
    view->needsLayout = true;
    view = view->superview;
  }
}

/**
 * @brief Hides or shows a view, and tells its flow when that actually changed.
 * @details `View::hidden` is a plain field. Writing it is the whole of what the
 * route used to do, and it is half the job: the details pane opens with the
 * preview and the pages hidden, so the body's vertical flow never placed them,
 * and View::layoutSubviews' alignment pass had left both at the container's
 * origin. Un-hiding them on the first selection put the preview, the title row
 * and the info rows all at y=0 of the pane - the map's title vanished behind
 * the preview's fill and "Preview unavailable" drew straight through "Runs".
 * Nothing ever asked the body to flow again, so it stayed that way for as long
 * as the route was open.
 */
static void setViewHidden(View *view, const bool hidden) {

  if (view->hidden != hidden) {
    view->hidden = hidden;
    invalidateLayoutChain(view);
  }
}

/**
 * @brief Sizes a table to the rows it actually holds, up to a ceiling.
 * @details A table given a fixed height reserves that height whether or not it
 * has rows for it. With two maps in the list, the route drew two rows and then
 * ~290 points of empty canvas before its own footer rule - which reads as rows
 * that failed to load rather than as a short list.
 *
 * Counted, not measured. `TableView::naturalSize` sums what the rows occupy
 * right now, and a row `reloadData` has just built has been through neither a
 * theme pass nor a layout pass: the framework stylesheet is what gives
 * `TableRowView` its `contain` mask and `TableCellView` its height, and it does
 * that during the render pass that follows. The wire callback that reloads this
 * table runs before that pass, so the content measured zero and the table
 * pinned itself to its header alone - a heading, no rows, and a footer
 * correctly counting the maps that were in the page. Join's two tables count
 * their rows for the same reason.
 *
 * Pinned through `minSize`/`maxSize` and not just the frame, because
 * `View::resize` clamps against those two; the stylesheet names no height for
 * either table, so nothing restyles the count away.
 */
static void sizeTableToRows(TableView *tableView, const size_t rows,
                            const int32_t ceiling) {

  View *view = (View *) tableView;

  const int32_t height = min(RACE_MAP_TABLE_HEIGHT((int32_t) rows), ceiling);

  if (view->frame.h != height || view->minSize.h != height ||
      view->maxSize.h != height) {
    view->frame.h = view->minSize.h = view->maxSize.h = height;
    invalidateLayoutChain(view);
  }
}

/**
 * @brief Reloads a table and sizes it to what the reload just built.
 * @details The count comes from the data source the reload itself read, so the
 * height and the rows cannot disagree.
 */
static void reloadTable(TableView *tableView, const int32_t ceiling) {

  $(tableView, reloadData);

  sizeTableToRows(tableView, tableView->dataSource.numberOfRows(tableView),
                  ceiling);
}

static const char *formatTimeMs(const int32_t time_ms) {
  if (time_ms <= 0) {
    return _unset;
  }
  return va("%d:%02d.%03d", time_ms / 60000,
            time_ms / 1000 % 60, time_ms % 1000);
}

/**
 * @brief A map nobody has run has no world record, and a personal best on it
 * would be meaningless - so the pair is rendered unset together.
 */
static const char *formatPersonalTimeMs(const int32_t best_ms,
                                        const int32_t pb_ms) {
  return best_ms > 0 ? formatTimeMs(pb_ms) : _unset;
}

/**
 * @brief The record date reads as day and month, spelled here rather than
 * through strftime so the route does not follow the host's locale.
 */
static const char *formatRecordDate(const uint64_t date_unix_s) {
  static const char *months[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
  };
  /* A 32-bit time_t cannot carry a date past 2038; drop rather than wrap. */
  static const uint64_t max_date_unix_s =
    sizeof(time_t) >= 8 ? RACE_MAP_BROWSER_MAX_DATE_UNIX_S
                        : (uint64_t) INT32_MAX;
  if (!date_unix_s || date_unix_s > max_date_unix_s) {
    return NULL;
  }

  const time_t seconds = (time_t) date_unix_s;
  const struct tm *utc = gmtime(&seconds);
  if (!utc || utc->tm_mon < 0 || utc->tm_mon >= (int32_t) lengthof(months)) {
    return NULL;
  }
  return va("%d %s", utc->tm_mday, months[utc->tm_mon]);
}

static bool isSafeFilter(const char *filter) {
  return filter && q_strlen(filter) <
         sizeof(((MapBrowserViewController *) 0)->prefix) &&
         strpbrk(filter, "\r\n;\"\\") == NULL;
}

static bool isSafeMapName(const char *name) {
  if (!name || !*name || q_strlen(name) >=
      sizeof(((race_map_browser_row_t *) 0)->name) || q_strstr(name, "..")) {
    return false;
  }
  for (const unsigned char *c = (const unsigned char *) name; *c; c++) {
    if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
          (*c >= '0' && *c <= '9') || *c == '_' || *c == '-' || *c == '.')) {
      return false;
    }
  }
  return true;
}

static void requestMapTimes(const char *name) {
  if (isSafeMapName(name)) {
    cgi.Cbuf(va("maptimes %s\n", name));
  }
}

static void requestMapInfo(const char *name) {
  if (isSafeMapName(name)) {
    cgi.Cbuf(va("mapinfo_ui %s\n", name));
  }
}

static const char *filterText(MapBrowserViewController *self) {
  const String *string = self->filterTextView
    ? (String *) self->filterTextView->attributedText
    : NULL;
  return string && string->chars ? string->chars : "";
}

static const char *ellipsizeText(const char *text, const size_t max_length) {
  if (!text || !*text || q_strlen(text) <= max_length) {
    return text ? text : "";
  }
  return va("%.*s...", (int32_t) max_length - 3, text);
}

static void setButtonEnabled(Button *button, const bool enabled) {
  if (!button) {
    return;
  }
  Control *control = (Control *) button;
  const unsigned int old_state = control->state;
  if (enabled) {
    control->state &= ~ControlStateDisabled;
  } else {
    control->state |= ControlStateDisabled;
  }
  if (old_state != control->state) {
    $(control, stateDidChange);
  }
}

static void setButtonSelected(Button *button, const bool selected) {
  if (!button) {
    return;
  }
  Control *control = (Control *) button;
  if (selected) {
    control->state |= ControlStateSelected;
  } else {
    control->state &= ~ControlStateSelected;
  }
  $(control, stateDidChange);
}

static bool isPersonalScope(const char *scope) {
  return !q_strcmp(scope, "pb");
}

/**
 * @brief The list pane captions the active scope itself, so it is spelled the
 * way its button is rather than as the wire token.
 */
static const char *scopeLabel(const char *scope) {
  return isPersonalScope(scope) ? "My PBs" : "All maps";
}

static void refreshScopeTabs(MapBrowserViewController *self) {
  setButtonSelected(self->allScopeTab, !isPersonalScope(self->scope));
  setButtonSelected(self->personalScopeTab, isPersonalScope(self->scope));
}

static void refreshDetailTabs(MapBrowserViewController *self) {
  setButtonSelected(self->infoTab,
                    self->detailsPages->currentPage == self->infoPage);
  setButtonSelected(self->timesTab,
                    self->detailsPages->currentPage == self->timesPage);
}

/**
 * @brief The route states its count in three places - the footer's two halves
 * and the pager - and every one of them is derived here, from the one page the
 * server sent, so they cannot disagree.
 */
static void setMapListSummary(MapBrowserViewController *self) {
  const race_map_browser_page_t *page = Cg_RaceMapBrowser_Page();
  const char *plural = page->total == 1 ? "" : "s";

  $(self->countLabel->text, setText,
    va("%d map%s · %s", page->total, plural, scopeLabel(self->scope)));
  /* Before the first page lands there is nothing to number, so the pager
     keeps the one page it is showing rather than reading "Page 0 of 0". */
  $(self->pageLabel->text, setText,
    va("Page %d of %d", max(page->page, 1), max(page->pages, 1)));

  if (!page->total) {
    $(self->footerLabel->text, setText, "No maps match the current filter");
    return;
  }

  const int32_t shown = (page->page - 1) * RACE_MAP_BROWSER_MAX_ROWS +
                        page->num_rows;
  const int32_t remaining = page->total - shown;
  if (remaining > 0) {
    $(self->footerLabel->text, setText,
      va("%d on this page · %d more", page->num_rows, remaining));
  } else {
    $(self->footerLabel->text, setText,
      va("End of list · %d map%s", page->total, plural));
  }
}

static void setMapDetailInfo(MapBrowserViewController *self,
                             const char *author, const int32_t best_ms,
                             const int32_t pb_ms, const int32_t ranked_runs) {
  $(self->mapAuthorLabel->text, setText,
    author && *author ? author : "Unknown");
  $(self->mapPersonalBestLabel->text, setText,
    formatPersonalTimeMs(best_ms, pb_ms));
  $(self->mapRunsLabel->text, setText,
    ranked_runs > 0 ? va("%d ranked", ranked_runs) : _unset);
}

static void setMapDetailTimes(MapBrowserViewController *self,
                              const int32_t best_ms, const int32_t pb_ms,
                              const int32_t ranked_runs,
                              const char *record_holder,
                              const uint64_t record_date_unix_s) {
  $(self->worldRecordChipLabel->text, setText, formatTimeMs(best_ms));
  $(self->personalRecordChipLabel->text, setText,
    formatPersonalTimeMs(best_ms, pb_ms));

  if (best_ms > 0 && record_holder && *record_holder) {
    const char *since = formatRecordDate(record_date_unix_s);
    $(self->mapTimesLabel->text, setText,
      since ? va("Held by %s since %s", record_holder, since)
            : va("Held by %s", record_holder));
  } else {
    $(self->mapTimesLabel->text, setText, "No ranked completions");
  }

  $(self->mapStatsLabel->text, setText,
    ranked_runs > 0
      ? va("%d ranked run%s", ranked_runs, ranked_runs == 1 ? "" : "s")
      : _unset);
}

static void refreshMapShot(MapBrowserViewController *self, const char *name) {
  $(self->mapShot, setImage, NULL);
  setViewHidden((View *) self->mapShot, true);
  setViewHidden((View *) self->mapShotPlaceholder, false);
  List *mapshots = name && *name ? cgi.Mapshots(va("maps/%s.bsp", name)) : NULL;
  if (mapshots && mapshots->head) {
    const char *resource = mapshots->head->element;
    SDL_Surface *surface = resource ? cgi.LoadSurface(resource) : NULL;
    if (surface) {
      $(self->mapShot, setImageWithSurface, surface);
      setViewHidden((View *) self->mapShot, false);
      setViewHidden((View *) self->mapShotPlaceholder, true);
      SDL_DestroySurface(surface);
    }
  }
  release(mapshots);
}

/**
 * @brief Shows or hides everything in the details pane that only means
 * something once a map is chosen.
 * @details The pane used to answer "no selection" with a full skeleton: the
 * heading read "Select a map" while `Author: Unknown`, `Your best: -` and
 * `Runs: -` were drawn beneath it, and the preview reported itself
 * unavailable - which says a map is targeted and has no mapshot, not that
 * nothing is targeted at all. Both of those read as a pane that failed to load
 * rather than one waiting for a click. Join's details pane had the same bug and
 * settled on a prompt and a line; this is that fix, applied here.
 */
static void setDetailsPopulated(MapBrowserViewController *self, const bool populated) {

  setViewHidden((View *) self->mapDetailHint, populated);

  setViewHidden((View *) self->mapPathLabel, !populated);
  setViewHidden(self->mapPreviewFrame, !populated);
  setViewHidden((View *) self->detailsPages, !populated);

  // The tabs switch between two pages that are not on screen, so they are
  // inert rather than merely unhelpful while nothing is selected.
  setButtonEnabled(self->infoTab, populated);
  setButtonEnabled(self->timesTab, populated);
}

static void setSelectedMap(MapBrowserViewController *self,
                           const race_map_browser_row_t *row) {
  const bool populated = row && isSafeMapName(row->name);

  setDetailsPopulated(self, populated);

  if (populated) {
    q_strlcpy(self->selectedMap, row->name, sizeof(self->selectedMap));
    $(self->mapTitleLabel->text, setText,
      row->title[0] ? row->title : row->name);
    $(self->mapPathLabel->text, setText, va("maps/%s.bsp", row->name));
    setMapDetailInfo(self, row->author, row->best_ms, row->pb_ms, 0);
    setMapDetailTimes(self, row->best_ms, row->pb_ms, 0, NULL, 0);
    refreshMapShot(self, row->name);
    requestMapInfo(row->name);
  } else {
    self->selectedMap[0] = '\0';
    $(self->mapTitleLabel->text, setText, "Select a map");
    $(self->mapPathLabel->text, setText, "");
    setMapDetailInfo(self, NULL, 0, 0, 0);
    setMapDetailTimes(self, 0, 0, 0, NULL, 0);
    refreshMapShot(self, NULL);
  }
  reloadTable(self->timesTableView, RACE_MAP_TIMES_MAX_HEIGHT);

  const bool selected = *self->selectedMap != '\0';
  setButtonEnabled(self->timesButton, selected);
  setButtonEnabled(self->voteButton, selected);
  setButtonEnabled(self->nominateButton, selected);
}

static void requestPage(MapBrowserViewController *self, int32_t page,
                        const char *prefix, const char *scope) {
  if (!isSafeFilter(prefix)) {
    cgi.Print("Invalid map filter\n");
    return;
  }
  self->page = max(page, 1);
  q_strlcpy(self->prefix, prefix, sizeof(self->prefix));
  q_strlcpy(self->scope, isPersonalScope(scope) ? "pb" : "all",
            sizeof(self->scope));
  setSelectedMap(self, NULL);
  refreshScopeTabs(self);
  setButtonEnabled(self->previousButton, false);
  setButtonEnabled(self->nextButton, false);
  cgi.Cbuf(va("maps_ui %d \"%s\" %s\n", self->page,
              self->prefix, self->scope));
}

static void didClickCommand(Button *button) {
  MapBrowserViewController *self = button->delegate.self;
  const char *identifier = ((View *) button)->identifier;
  const race_map_browser_page_t *page = Cg_RaceMapBrowser_Page();
  if (!q_strcmp(identifier, "maps_scope_all")) {
    requestPage(self, 1, filterText(self), "all");
  } else if (!q_strcmp(identifier, "maps_scope_pb")) {
    requestPage(self, 1, filterText(self), "pb");
  } else if (!q_strcmp(identifier, "maps_previous")) {
    requestPage(self, page->page - 1, self->prefix, self->scope);
  } else if (!q_strcmp(identifier, "maps_next")) {
    requestPage(self, page->page + 1, self->prefix, self->scope);
  } else if (!q_strcmp(identifier, "maps_filter")) {
    requestPage(self, 1, filterText(self), self->scope);
  } else if (!q_strcmp(identifier, "maps_refresh")) {
    requestPage(self, self->page, self->prefix, self->scope);
  } else if (!q_strcmp(identifier, "maps_info_tab")) {
    $(self->detailsPages, setCurrentPage, self->infoPage);
    refreshDetailTabs(self);
  } else if (!q_strcmp(identifier, "maps_times_tab")) {
    $(self->detailsPages, setCurrentPage, self->timesPage);
    refreshDetailTabs(self);
  } else if (!q_strcmp(identifier, "maps_times") && *self->selectedMap) {
    requestMapTimes(self->selectedMap);
  } else if (!q_strcmp(identifier, "maps_vote") && *self->selectedMap) {
    cgi.Cbuf(va("race vote map %s\n", self->selectedMap));
  } else if (!q_strcmp(identifier, "maps_nominate") && *self->selectedMap) {
    cgi.Cbuf(va("nominate %s\n", self->selectedMap));
  }
}

static size_t numberOfRows(const TableView *tableView) {
  (void) tableView;
  return Cg_RaceMapBrowser_Page()->num_rows;
}

static TableCellView *cellForColumnAndRow(const TableView *tableView,
                                         const TableColumn *column,
                                         const size_t row) {
  (void) tableView;
  const race_map_browser_row_t *entry = Cg_RaceMapBrowser_Page()->rows + row;
  TableCellView *cell = $(alloc(TableCellView), initWithFrame, NULL);
  if (!q_strcmp(column->identifier, _map)) {
    $(cell->text, setText, ellipsizeText(entry->name, 18));
  } else if (!q_strcmp(column->identifier, _title)) {
    $(cell->text, setText, *entry->title ? entry->title : _unset);
  } else if (!q_strcmp(column->identifier, _author)) {
    $(cell->text, setText, *entry->author ? entry->author : "Unknown");
  } else if (!q_strcmp(column->identifier, _best)) {
    $(cell->text, setText, formatTimeMs(entry->best_ms));
  } else if (!q_strcmp(column->identifier, _pb)) {
    $(cell->text, setText,
      formatPersonalTimeMs(entry->best_ms, entry->pb_ms));
  }
  return cell;
}

static void didSelectRowsAtIndexes(TableView *tableView,
                                   const IndexSet *indexes) {
  MapBrowserViewController *self = tableView->delegate.self;
  const race_map_browser_page_t *page = Cg_RaceMapBrowser_Page();
  if (!indexes->count || indexes->indexes[0] >= (size_t) page->num_rows) {
    setSelectedMap(self, NULL);
    return;
  }
  const race_map_browser_row_t *row = page->rows + indexes->indexes[0];
  setSelectedMap(self, row);
}

/**
 * @brief The top-four table only shows the detail the selection asked for, so
 * a payload for a map the player has since clicked away from renders empty.
 */
static const race_map_browser_detail_t *selectedDetail(
  const MapBrowserViewController *self) {
  const race_map_browser_detail_t *detail = Cg_RaceMapBrowser_Detail();
  if (!self || !detail->valid || !*self->selectedMap ||
      q_strcmp(self->selectedMap, detail->name)) {
    return NULL;
  }
  return detail;
}

static size_t numberOfTimeRows(const TableView *tableView) {
  const race_map_browser_detail_t *detail =
    selectedDetail(tableView->dataSource.self);
  return detail ? (size_t) detail->num_times : 0;
}

static TableCellView *timeCellForColumnAndRow(const TableView *tableView,
                                              const TableColumn *column,
                                              const size_t row) {
  TableCellView *cell = $(alloc(TableCellView), initWithFrame, NULL);
  const race_map_browser_detail_t *detail =
    selectedDetail(tableView->delegate.self);
  if (!detail || row >= (size_t) detail->num_times) {
    return cell;
  }

  const race_map_browser_time_t *entry = detail->times + row;
  if (!q_strcmp(column->identifier, _rank)) {
    $(cell->text, setText, va("%d", (int32_t) row + 1));
  } else if (!q_strcmp(column->identifier, _player)) {
    $(cell->text, setText, *entry->player ? entry->player : "Unknown");
  } else if (!q_strcmp(column->identifier, _time)) {
    $(cell->text, setText, formatTimeMs(entry->time_ms));
  }

  /* The design system reserves these two tints; the route only names them. */
  if (!row) {
    $((View *) cell, addClassName, "tableRowWorld");
  } else if (detail->local_rank &&
             (size_t) detail->local_rank == row + 1) {
    $((View *) cell, addClassName, "tableRowLocal");
  }
  return cell;
}

/**
 * @brief Adds a column whose header reads differently from the identifier the
 * cells and the stylesheet address it by.
 */
static void addColumnWithTitle(TableView *tableView, const char *identifier,
                               const char *title) {
  $(tableView, addColumnWithIdentifier, identifier);
  TableColumn *column = $(tableView, columnWithIdentifier, identifier);
  assert(column);
  $(((TableCellView *) column->headerCell)->text, setText, title);
}

static void loadView(ViewController *view_controller) {
  super(ViewController, view_controller, loadView);
  MapBrowserViewController *self = (MapBrowserViewController *) view_controller;
  Button *filter, *refresh;
  Outlet outlets[] = MakeOutlets(
    MakeOutlet("maps", &self->mapsTableView),
    MakeOutlet("maps_times_table", &self->timesTableView),
    MakeOutlet("maps_empty", &self->emptyLabel),
    MakeOutlet("maps_count", &self->countLabel),
    MakeOutlet("maps_footer", &self->footerLabel),
    MakeOutlet("filter", &self->filterTextView),
    MakeOutlet("page", &self->pageLabel),
    MakeOutlet("maps_pager", &self->pagerView),
    MakeOutlet("maps_previous", &self->previousButton),
    MakeOutlet("maps_next", &self->nextButton),
    MakeOutlet("maps_scope_all", &self->allScopeTab),
    MakeOutlet("maps_scope_pb", &self->personalScopeTab),
    MakeOutlet("maps_filter", &filter),
    MakeOutlet("maps_refresh", &refresh),
    MakeOutlet("maps_details_pages", &self->detailsPages),
    MakeOutlet("maps_info_page", &self->infoPage),
    MakeOutlet("maps_times_page", &self->timesPage),
    MakeOutlet("maps_info_tab", &self->infoTab),
    MakeOutlet("maps_times_tab", &self->timesTab),
    MakeOutlet("maps_preview", &self->mapShot),
    MakeOutlet("maps_preview_placeholder", &self->mapShotPlaceholder),
    MakeOutlet("maps_preview_frame", &self->mapPreviewFrame),
    MakeOutlet("maps_detail_hint", &self->mapDetailHint),
    MakeOutlet("maps_detail_title", &self->mapTitleLabel),
    MakeOutlet("maps_detail_path", &self->mapPathLabel),
    MakeOutlet("maps_detail_author", &self->mapAuthorLabel),
    MakeOutlet("maps_detail_pb", &self->mapPersonalBestLabel),
    MakeOutlet("maps_detail_runs", &self->mapRunsLabel),
    MakeOutlet("maps_record_world", &self->worldRecordChipLabel),
    MakeOutlet("maps_record_local", &self->personalRecordChipLabel),
    MakeOutlet("maps_detail_times", &self->mapTimesLabel),
    MakeOutlet("maps_detail_stats", &self->mapStatsLabel),
    MakeOutlet("maps_times", &self->timesButton),
    MakeOutlet("maps_vote", &self->voteButton),
    MakeOutlet("maps_nominate", &self->nominateButton));

  View *view = $$(View, viewWithResourceName,
                  "ui/maps/MapBrowserViewController.json", outlets);
  assert(view);
  view->stylesheet = $$(Stylesheet, stylesheetWithResourceName,
                        "ui/maps/MapBrowserViewController.css");
  assert(view->stylesheet);
  ScrollView *scrollView = MapBrowser_WrapPage(view);
  assert(scrollView);

  $(view_controller, setView, (View *) scrollView);
  release(scrollView);
  release(view);

  $(self->mapsTableView, addColumnWithIdentifier, _map);
  $(self->mapsTableView, addColumnWithIdentifier, _title);
  $(self->mapsTableView, addColumnWithIdentifier, _author);
  $(self->mapsTableView, addColumnWithIdentifier, _best);
  addColumnWithTitle(self->mapsTableView, _pb, "Your PB");
  self->mapsTableView->dataSource = (TableViewDataSource) {
    .self = self, .numberOfRows = numberOfRows
  };
  self->mapsTableView->delegate = (TableViewDelegate) {
    .self = self,
    .cellForColumnAndRow = cellForColumnAndRow,
    .didSelectRowsAtIndexes = didSelectRowsAtIndexes
  };

  $(self->timesTableView, addColumnWithIdentifier, _rank);
  $(self->timesTableView, addColumnWithIdentifier, _player);
  $(self->timesTableView, addColumnWithIdentifier, _time);
  self->timesTableView->dataSource = (TableViewDataSource) {
    .self = self, .numberOfRows = numberOfTimeRows
  };
  self->timesTableView->delegate = (TableViewDelegate) {
    .self = self, .cellForColumnAndRow = timeCellForColumnAndRow
  };
  self->timesTableView->control.selection = ControlSelectionNone;

  Button *buttons[] = {
    self->previousButton, self->nextButton, filter, refresh,
    self->infoTab, self->timesTab,
    self->timesButton, self->voteButton, self->nominateButton,
    self->allScopeTab, self->personalScopeTab
  };
  for (size_t i = 0; i < lengthof(buttons); i++) {
    buttons[i]->delegate = (ButtonDelegate) {
      .self = self, .didClick = didClickCommand
    };
  }

  sizeTableToRows(self->mapsTableView, 0, RACE_MAP_LIST_MAX_HEIGHT);
  sizeTableToRows(self->timesTableView, 0, RACE_MAP_TIMES_MAX_HEIGHT);
  setViewHidden(self->pagerView, true);

  self->page = 1;
  q_strlcpy(self->scope, "all", sizeof(self->scope));
  setButtonEnabled(self->previousButton, false);
  setButtonEnabled(self->nextButton, false);
  setViewHidden((View *) self->emptyLabel, true);
  $(self->detailsPages, setCurrentPage, self->infoPage);
  refreshDetailTabs(self);
  refreshScopeTabs(self);
  setMapListSummary(self);
  setSelectedMap(self, NULL);
}

static void viewWillAppear(ViewController *view_controller) {
  super(ViewController, view_controller, viewWillAppear);
  activeMapBrowserViewController = (MapBrowserViewController *) view_controller;
  requestPage(activeMapBrowserViewController, 1,
              activeMapBrowserViewController->prefix,
              activeMapBrowserViewController->scope);
}

static void viewWillDisappear(ViewController *view_controller) {
  super(ViewController, view_controller, viewWillDisappear);
  if (activeMapBrowserViewController ==
      (MapBrowserViewController *) view_controller) {
    activeMapBrowserViewController = NULL;
  }
}

void MapBrowserViewController_RefreshValues(void) {
  MapBrowserViewController *self = activeMapBrowserViewController;
  const race_map_browser_page_t *page = Cg_RaceMapBrowser_Page();
  if (!self || !self->mapsTableView) {
    return;
  }
  self->page = page->page;
  q_strlcpy(self->prefix, page->prefix, sizeof(self->prefix));
  q_strlcpy(self->scope, isPersonalScope(page->scope) ? "pb" : "all",
            sizeof(self->scope));
  setButtonEnabled(self->previousButton, page->page > 1);
  setButtonEnabled(self->nextButton,
                   page->pages > 0 && page->page < page->pages);
  reloadTable(self->mapsTableView, RACE_MAP_LIST_MAX_HEIGHT);
  setViewHidden((View *) self->emptyLabel, page->num_rows != 0);

  // Dead chrome otherwise: with one page both buttons are disabled and the
  // label reads "Page 1 of 1", which is a row of controls that says only that
  // it has nothing to do.
  setViewHidden(self->pagerView, page->pages <= 1);

  refreshScopeTabs(self);
  setMapListSummary(self);
  setSelectedMap(self, NULL);
}

void MapBrowserViewController_RefreshDetails(void) {
  MapBrowserViewController *self = activeMapBrowserViewController;
  const race_map_browser_detail_t *detail = selectedDetail(self);
  if (!detail) {
    return;
  }
  $(self->mapTitleLabel->text, setText,
    *detail->title ? detail->title : detail->name);
  $(self->mapPathLabel->text, setText, va("maps/%s.bsp", detail->name));
  setMapDetailInfo(self, detail->author, detail->best_ms, detail->pb_ms,
                   detail->ranked_runs);
  setMapDetailTimes(self, detail->best_ms, detail->pb_ms, detail->ranked_runs,
                    detail->record_holder, detail->record_date_unix_s);
  reloadTable(self->timesTableView, RACE_MAP_TIMES_MAX_HEIGHT);
}

static void initialize(Class *clazz) {
  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
  ((ViewControllerInterface *) clazz->interface)->viewWillAppear = viewWillAppear;
  ((ViewControllerInterface *) clazz->interface)->viewWillDisappear = viewWillDisappear;
}

Class *_MapBrowserViewController(void) {
  static Class *clazz;
  static Once once;
  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "MapBrowserViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(MapBrowserViewController),
      .interfaceOffset = offsetof(MapBrowserViewController, interface),
      .interfaceSize = sizeof(MapBrowserViewControllerInterface),
      .initialize = initialize,
    });
  });
  return clazz;
}

#undef _Class
