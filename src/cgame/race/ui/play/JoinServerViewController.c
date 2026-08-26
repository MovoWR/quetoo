/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 * Copyright(c) 2026 Quetoo Race Module.
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

#include "JoinServerViewController.h"
#include "CvarCheckbox.h"
#include "CvarSlider.h"

/**
 * @brief Column identifiers double as CSS ids, so they carry no spaces.
 */
static const char *_server = "Server";
static const char *_map = "Map";
static const char *_players = "Players";
static const char *_ping = "Ping";

static const char *_runner = "Runner";
static const char *_best = "Best";
static const char *_status = "Status";

/**
 * @brief Stands in for every field the selected server did not report.
 */
static const char *_unset = "—";

/**
 * @brief A ping the client never got an answer for.
 * @details `Cl_ParseServerInfo` clamps the measured round trip to 999, so 999
 * is the timeout sentinel rather than a latency. Reading it as "slow but
 * joinable" is the wrong story, so it renders unset and sorts last.
 */
#define JOIN_PING_UNANSWERED 999

/**
 * @brief Row geometry, which the tables are sized against rather than reserving
 * a fixed block of rows.
 * @remarks JOIN_ROW_HEIGHT must match the TableCellView height in the CSS.
 */
#define JOIN_ROW_HEIGHT 34
#define JOIN_SERVERS_MAX_HEIGHT 662
#define JOIN_ROSTER_MAX_HEIGHT 306

static cvar_t *cg_join_server_hide_empty;
static cvar_t *cg_join_server_hide_bots;
static JoinServerViewController *sortingJoinServerViewController;

#define _Class _JoinServerViewController

static const cl_server_info_t *serverAtIndex(const PointerArray *servers, size_t index) {

  if (servers == NULL || index >= servers->count) {
    return NULL;
  }

  return $(servers, get, index);
}

/**
 * @brief Returns the selected server, by hostname rather than by row index, so
 * that the selection survives a re-sort and a refresh.
 */
static const cl_server_info_t *selectedServer(const JoinServerViewController *self) {

  if (self->servers == NULL || *self->selectedHostname == '\0') {
    return NULL;
  }

  for (size_t i = 0; i < self->servers->count; i++) {
    const cl_server_info_t *server = $(self->servers, get, i);
    if (q_strcmp(server->hostname, self->selectedHostname) == 0) {
      return server;
    }
  }

  return NULL;
}

static void setButtonEnabled(Button *button, const bool enabled) {

  if (button == NULL) {
    return;
  }

  Control *control = (Control *) button;
  const unsigned int state = control->state;

  if (enabled) {
    control->state &= ~ControlStateDisabled;
  } else {
    control->state |= ControlStateDisabled;
  }

  if (state != control->state) {
    $(control, stateDidChange);
  }
}

static void setLabelText(Label *label, const char *text) {

  if (label) {
    $(label->text, setText, text && *text ? text : NULL);
  }
}

/**
 * @brief The colour threshold and the quick join threshold are the same number,
 * so that moving the slider visibly splits the list.
 */
static int32_t maxPing(void) {
  return cg_quick_join_max_ping ? Clampf(cg_quick_join_max_ping->integer, 1, 999) : 200;
}

/**
 * @brief True when the client never got an answer from this server.
 */
static bool pingUnanswered(const cl_server_info_t *server) {
  return server->ping <= 0 || server->ping >= JOIN_PING_UNANSWERED;
}

/**
 * @brief Formats a run time the way the rest of the mod spells one.
 */
/**
 * @brief Spells the mode a server is running.
 * @details `g_gameplay` is a deathmatch axis, so a race server reports
 * `default` on it - the one word a player reads as "not configured". The mod
 * itself is named in `game_name`, which is what actually answers the question
 * this row asks.
 */
static const char *gameplayText(const cl_server_info_t *server) {
  const char *gameplay = *server->gameplay ? server->gameplay : NULL;
  return gameplay ? gameplay : _unset;
}

/**
 * @brief One counting rule, enforced here rather than per call site: `clients`
 * counts humans and bots both, and `bots` is a subset of it.
 */
static const char *playersLabel(const cl_server_info_t *server) {

  if (server->bots > 0) {
    return va("%d / %d · %d bot%s", server->clients, server->max_clients,
              server->bots, server->bots == 1 ? "" : "s");
  }

  return va("%d / %d", server->clients, server->max_clients);
}

/**
 * @brief Formats a server address for display.
 * @details `Net_NetaddrToString` lives in the engine's socket layer, which the
 * modules do not link, so the dotted quad is read straight out of the network
 * byte order the address is stored in - no host byte order assumption.
 */
static const char *addressLabel(const net_addr_t *addr) {

  const uint8_t *ip = (const uint8_t *) &addr->addr;
  const uint8_t *port = (const uint8_t *) &addr->port;

  return va("%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3],
            (uint32_t) port[0] << 8 | port[1]);
}

static const char *sourceLabel(const cl_server_info_t *server) {

  switch (server->source) {
    case SERVER_SOURCE_INTERNET:
      return "Internet";
    case SERVER_SOURCE_USER:
      return "User";
    case SERVER_SOURCE_BCAST:
      return "LAN";
  }

  return _unset;
}

#pragma mark - Route state

/**
 * @brief States the sort in words beside the pane title, so that it survives a
 * screenshot.
 */
static void refreshSortLabel(JoinServerViewController *self) {

  const TableColumn *column = self->serversTableView->sortColumn;
  if (column == NULL || column->order == OrderSame) {
    setLabelText(self->sortLabel, "Unsorted");
    return;
  }

  char name[32];
  q_strlcpy(name, column->identifier, sizeof(name));
  for (char *c = name; *c; c++) {
    *c = (char) tolower(*c);
  }

  setLabelText(self->sortLabel, va("Sorted by %s %s", name,
                                   column->order == OrderAscending ? "ascending" : "descending"));
}

/**
 * @brief Marks the sorted column in its own header cell, since the table draws
 * no other affordance for it.
 */
static void refreshSortColumnHeaders(JoinServerViewController *self) {

  const Array *columns = (Array *) self->serversTableView->columns;
  for (size_t i = 0; i < columns->count; i++) {

    const TableColumn *column = $(columns, objectAtIndex, i);
    TableCellView *header = (TableCellView *) column->headerCell;

    if (column == self->serversTableView->sortColumn && column->order != OrderSame) {

      // ASCII markers avoid relying on optional Unicode glyph coverage. The
      // marker also has to lead a right-aligned column, or the header stops
      // lining up with the numbers under it.
      const char *marker = column->order == OrderAscending ? "^" : "v";
      const bool trailing = q_strcmp(column->identifier, _players) &&
                            q_strcmp(column->identifier, _ping);

      $(header->text, setText, trailing
        ? va("%s %s", column->identifier, marker)
        : va("%s %s", marker, column->identifier));

      $((View *) header, addClassName, "sorted");
    } else {
      $(header->text, setText, column->identifier);
      $((View *) header, removeClassName, "sorted");
    }
  }
}

/**
 * @brief Pins a view's height, past the reach of a later restyle.
 * @details The stylesheet deliberately names no height for the two tables or
 * the two pane bodies: `bindInlets` only writes attributes a computed style
 * actually carries, so an attribute absent from the CSS is one the C owns
 * outright. Pinning min and max as well as the frame means nothing resizes it
 * back either.
 */
static void setViewHeight(View *view, const int32_t height) {

  view->frame.h = height;
  view->minSize.h = height;
  view->maxSize.h = height;
  view->needsLayout = true;

  if (view->superview) {
    view->superview->needsLayout = true;
  }
}

/**
 * @brief Sizes a table to its header and rows, up to a reserved maximum.
 */
static void sizeTableToRows(TableView *tableView, const size_t rows, const int32_t maxHeight) {

  const int32_t height = (int32_t) (rows + 1) * JOIN_ROW_HEIGHT;

  setViewHeight((View *) tableView, Mini(height, maxHeight));
}

/**
 * @brief Sizes a pane body to whatever it is currently showing.
 * @details A pane that reserves room for a full table and then draws nine rows
 * reads as still loading, so the body closes up and the pane's rule, legend and
 * actions ride up with it. Measured rather than summed from constants, so the
 * stylesheet stays the one place the rows' own heights are stated.
 */
static void sizeBodyToContent(View *body) {

  const SDL_Size fits = $(body, sizeThatFits);

  // Only the frame, deliberately: the body is `autoresizing-mask: contain`, so
  // a layout pass takes max(frame, sizeThatFits) and grows it back if this ran
  // before the stylesheet had given the children their heights - which is the
  // order on the very first pass, and is what left an unstyled empty state
  // drawn over the footer. Pinning min and max as well would cap that recovery.
  // Shrinking still has to come from here, since `contain` never gets smaller.
  body->frame.h = fits.h;
  body->needsLayout = true;

  if (body->superview) {
    body->superview->needsLayout = true;
  }
}

/**
 * @brief Shows or hides everything in the details pane that only means
 * something once a server is selected.
 */
static void setDetailsPopulated(JoinServerViewController *self, const bool populated) {

  self->hintLabel->view.hidden = populated;

  self->motdLabel->view.hidden = !populated;
  self->detailGrid->hidden = !populated;
  self->rosterRule->hidden = !populated;
  self->rosterHeader->hidden = !populated;

  if (!populated) {
    self->rosterTableView->control.view.hidden = true;
    self->rosterEmptyLabel->view.hidden = true;
  }
}

/**
 * @brief Repopulates the details pane from the current selection.
 */
static void refreshDetails(JoinServerViewController *self) {

  const cl_server_info_t *server = selectedServer(self);

  // With nothing selected the pane is a prompt and a line, not a skeleton: a
  // headline over five dashed rows and an empty roster reads as a page that
  // failed to load rather than as one waiting for a click.
  setDetailsPopulated(self, server != NULL);

  if (server == NULL) {
    setLabelText(self->hostnameLabel, "Select a server");
    setLabelText(self->addressLabel, NULL);
    setLabelText(self->sourceLabel, NULL);
    setLabelText(self->rosterCountLabel, NULL);

    setButtonEnabled(self->connectButton, false);

    $(self->rosterTableView, reloadData);
    sizeTableToRows(self->rosterTableView, 0, JOIN_ROSTER_MAX_HEIGHT);
    sizeBodyToContent(self->detailsBody);
    return;
  }

  setLabelText(self->hostnameLabel, *server->hostname ? server->hostname : _unset);
  setLabelText(self->addressLabel, addressLabel(&server->addr));

  setLabelText(self->motdLabel, "Details available after connecting");

  setLabelText(self->sourceLabel, sourceLabel(server));
  setLabelText(self->mapLabel, *server->name ? server->name : _unset);
  setLabelText(self->gameplayLabel, gameplayText(server));
  setLabelText(self->playersLabel, playersLabel(server));
  setLabelText(self->pingLabel,
               pingUnanswered(server) ? _unset : va("%d ms", server->ping));

  // Server discovery exposes aggregate occupancy but no pre-connect player rows.
  self->rosterTableView->control.view.hidden = true;
  self->rosterEmptyLabel->view.hidden = false;
  setLabelText(self->rosterEmptyLabel,
               "Player roster available after connecting");
  setLabelText(self->rosterCountLabel,
               va("%d / %d", server->clients, server->max_clients));

  setButtonEnabled(self->connectButton, true);

  $(self->rosterTableView, reloadData);
  sizeTableToRows(self->rosterTableView, 0, JOIN_ROSTER_MAX_HEIGHT);
  sizeBodyToContent(self->detailsBody);
}

/**
 * @brief Restores the selection after a sort or a refresh.
 * @details The selection is held by hostname, so a re-sort keeps the same
 * server rather than the same row. When the selected server has gone from the
 * list entirely, the first row takes over rather than the pane emptying.
 */
static void restoreSelection(JoinServerViewController *self) {

  TableView *tableView = self->serversTableView;

  ssize_t index = -1;
  const size_t count = self->servers ? self->servers->count : 0;
  for (size_t row = 0; row < count; row++) {
    const cl_server_info_t *server = $(self->servers, get, row);
    if (q_strcmp(server->hostname, self->selectedHostname) == 0) {
      index = (ssize_t) row;
      break;
    }
  }

  if (index < 0) {
    const cl_server_info_t *first = serverAtIndex(self->servers, 0);
    if (first) {
      q_strlcpy(self->selectedHostname, first->hostname, sizeof(self->selectedHostname));
      index = 0;
    } else {
      self->selectedHostname[0] = '\0';
    }
  }

  $(tableView, deselectAll);

  if (index >= 0) {
    $(tableView, selectRowAtIndex, (size_t) index);
  }

  refreshDetails(self);
}

/**
 * @brief Restates the counts and the ping legend the slider drives.
 */
static void refreshSummary(JoinServerViewController *self) {

  const PointerArray *known = cgi.Servers();
  const size_t listed = self->servers ? self->servers->count : 0;
  const size_t total = known ? known->count : listed;

  setLabelText(self->totalLabel,
               va("%d of %d listed", (int32_t) listed, (int32_t) total));
  setLabelText(self->legendLabel,
               va("Ping over %d ms is out of quick-join range", maxPing()));

  // The table and its empty state are mutually exclusive - one condition, one
  // signal - and the body closes up around whichever is showing.
  self->emptyLabel->view.hidden = listed > 0;
  self->serversTableView->control.view.hidden = listed == 0;

  sizeTableToRows(self->serversTableView, listed, JOIN_SERVERS_MAX_HEIGHT);
  sizeBodyToContent(self->listBody);
}

#pragma mark - Delegates

static void didToggleHideBots(Checkbox *checkbox) {

  cvarCheckboxDidToggle(checkbox);

  $((JoinServerViewController *) checkbox->delegate.self, reloadServers);
}

static void didToggleHideEmpty(Checkbox *checkbox) {

  cvarCheckboxDidToggle(checkbox);

  $((JoinServerViewController *) checkbox->delegate.self, reloadServers);
}

/**
 * @brief SliderDelegate for the max ping slider.
 * @details CvarSlider writes the cvar in its own `setValue`; the delegate slot
 * is free, and is what repaints the ping column against the new threshold.
 */
static void didSetMaxPing(Slider *slider, double value) {

  (void) value;

  JoinServerViewController *this = slider->delegate.self;

  refreshSummary(this);
  $(this->serversTableView, reloadData);
  restoreSelection(this);
}

/**
 * @brief ButtonDelegate for Quick Join.
 * @description Selects a server based on minumum ping and maximum players with
 * a bit of lovely random thrown in. Any server that matches the criteria will
 * be weighted by how much "better" they are by how much lower their ping is and
 * how many more players there are.
 */
static void didClickQuickJoin(Button *button) {

  JoinServerViewController *this = button->delegate.self;

  const int32_t max_ping = maxPing();
  const int32_t min_clients = Clampf(cg_quick_join_min_clients->integer, 0, MAX_CLIENTS);

  uint32_t total_weight = 0;

  const size_t count = this->servers ? this->servers->count : 0;

  for (size_t i = 0; i < count; i++) {
    const cl_server_info_t *server = $(this->servers, get, i);

    int32_t weight = 1;

    if (!(server->clients < min_clients || server->clients >= server->max_clients)) {
      // more weight for more populated servers
      weight += (server->clients - min_clients) * 5;

      // more weight for lower ping servers
      weight += (max_ping - server->ping) / 10;

      if (server->ping > max_ping) { // one third weight for high ping servers
        weight /= 3;
      }
    }

    total_weight += max(weight, 1);
  }

  if (total_weight == 0) {
    return;
  }

  const uint32_t random_weight = RandomRangeu(0, total_weight);
  uint32_t current_weight = 0;

  for (size_t i = 0; i < count; i++) {
    const cl_server_info_t *server = $(this->servers, get, i);

    int32_t weight = 1;

    if (server->ping > max_ping ||
      server->clients < min_clients ||
      server->clients >= server->max_clients) {

      weight = 0;
    } else {
      // more weight for more populated servers
      weight += server->clients - min_clients;

      // more weight for lower ping servers
      weight += (max_ping - server->ping) / 20;
    }

    current_weight += weight;

    if (current_weight > random_weight) {
      cgi.Connect(&server->addr);
      break;
    }
  }
}

/**
 * @brief ButtonDelegate for the Refresh button.
 */
static void didClickRefresh(Button *button) {
  cgi.GetServers();
}

/**
 * @brief ButtonDelegate for the Connect button.
 * @details Connect acts on the details pane's server, which is the selection
 * held by hostname - not on whatever row happens to be at the selected index.
 */
static void didClickConnect(Button *button) {

  JoinServerViewController *this = button->delegate.self;

  const cl_server_info_t *server = selectedServer(this);
  if (server) {
    cgi.Connect(&server->addr);
  }
}

#pragma mark - TableViewDataSource

static size_t numberOfRows(const TableView *tableView) {

  const JoinServerViewController *this = tableView->dataSource.self;

  return this->servers ? this->servers->count : 0;
}

static size_t numberOfRosterRows(const TableView *tableView) {
  (void) tableView;
  return 0;
}

#pragma mark - TableViewDelegate

static TableCellView *cellForColumnAndRow(const TableView *tableView, const TableColumn *column, size_t row) {

  const JoinServerViewController *this = tableView->dataSource.self;

  cl_server_info_t *server = (cl_server_info_t *) serverAtIndex(this->servers, row);
  assert(server);

  TableCellView *cell = $(alloc(TableCellView), initWithFrame, NULL);

  if (q_strlen(server->error)) {
    if (q_strcmp(column->identifier, _server) == 0) {
      $(cell->text, setText, server->error);
      $((View *) cell, addClassName, "error");
    } else {
      $(cell->text, setText, NULL);
    }
    return cell;
  }

  if (q_strcmp(column->identifier, _server) == 0) {
    $(cell->text, setText, server->hostname);
  } else if (q_strcmp(column->identifier, _map) == 0) {
    $(cell->text, setText, server->name);
  } else if (q_strcmp(column->identifier, _players) == 0) {
    $(cell->text, setText, va("%d / %d", server->clients, server->max_clients));
  } else if (q_strcmp(column->identifier, _ping) == 0) {

    // A server that never answered has no latency to state. Painting the
    // timeout sentinel red would read as "slow but joinable"; it is neither.
    if (pingUnanswered(server)) {
      $(cell->text, setText, _unset);
      $((View *) cell, addClassName, "pingUnanswered");
    } else {
      $(cell->text, setText, va("%d ms", server->ping));

      // The menu palette has no green: over threshold reads red, comfortably
      // under it reads blue, and everything between keeps the body colour.
      const int32_t threshold = maxPing();
      if (server->ping > threshold) {
        $((View *) cell, addClassName, "pingOver");
      } else if (server->ping <= threshold / 2) {
        $((View *) cell, addClassName, "pingFast");
      }
    }
  }

  return cell;
}

/**
 * @brief The roster reports what the status response actually carries: who is
 * on the server, their latency, and whether they are a bot.
 * @remarks The design's `Best` and `Status` columns are not on the wire -
 * `Sv_StatusString` sends `score`, which is the frag score and means nothing on
 * a race server. Surfacing a run time and a racing/spectating state here needs
 * the GAME module to contribute per-player keys to the status reply first.
 */
static TableCellView *rosterCellForColumnAndRow(const TableView *tableView, const TableColumn *column, size_t row) {
  (void) tableView;
  (void) column;
  (void) row;
  return $(alloc(TableCellView), initWithFrame, NULL);
}

static void didSetSortColumn(TableView *tableView) {

  JoinServerViewController *this = tableView->delegate.self;

  refreshSortColumnHeaders(this);
  refreshSortLabel(this);

  $(this, reloadServers);
}

static void didSelectRowsAtIndexes(TableView *tableView, const IndexSet *indexes) {

  JoinServerViewController *this = tableView->delegate.self;

  if (indexes->count == 0) {
    return;
  }

  const cl_server_info_t *server = serverAtIndex(this->servers, indexes->indexes[0]);
  if (server == NULL) {
    return;
  }

  q_strlcpy(this->selectedHostname, server->hostname, sizeof(this->selectedHostname));
  refreshDetails(this);

  const SDL_PropertiesID props = SDL_GetWindowProperties(((View *) tableView)->window);
  const SDL_Event *event = SDL_GetPointerProperty(props, "event", NULL);
  if (event && event->button.clicks == 2) {
    cgi.Connect(&server->addr);
  }
}

#pragma mark - Object

static void dealloc(Object *self) {

  JoinServerViewController *this = (JoinServerViewController *) self;

  release(this->servers);

  super(Object, self, dealloc);
}

#pragma mark - ViewController

/**
 * @see ViewController::loadView(ViewController *)
 */
static void loadView(ViewController *self) {

  super(ViewController, self, loadView);

  JoinServerViewController *this = (JoinServerViewController *) self;

  Checkbox *hideEmpty, *hideBots;
  Button *refresh;

  Outlet outlets[] = MakeOutlets(
    MakeOutlet("servers", &this->serversTableView),
    MakeOutlet("roster", &this->rosterTableView),
    MakeOutlet("servers_total", &this->totalLabel),
    MakeOutlet("servers_sort", &this->sortLabel),
    MakeOutlet("servers_body", &this->listBody),
    MakeOutlet("server_body", &this->detailsBody),
    MakeOutlet("servers_empty", &this->emptyLabel),
    MakeOutlet("servers_legend", &this->legendLabel),
    MakeOutlet("server_hostname", &this->hostnameLabel),
    MakeOutlet("server_address", &this->addressLabel),
    MakeOutlet("server_hint", &this->hintLabel),
    MakeOutlet("server_motd", &this->motdLabel),
    MakeOutlet("server_grid", &this->detailGrid),
    MakeOutlet("roster_rule", &this->rosterRule),
    MakeOutlet("roster_header", &this->rosterHeader),
    MakeOutlet("server_source", &this->sourceLabel),
    MakeOutlet("server_map", &this->mapLabel),
    MakeOutlet("server_gameplay", &this->gameplayLabel),
    MakeOutlet("server_players", &this->playersLabel),
    MakeOutlet("server_ping", &this->pingLabel),
    MakeOutlet("roster_count", &this->rosterCountLabel),
    MakeOutlet("roster_empty", &this->rosterEmptyLabel),
    MakeOutlet("connect", &this->connectButton),
    MakeOutlet("quickJoin", &this->quickJoinButton),
    MakeOutlet("refresh", &refresh),
    MakeOutlet("hideEmpty", &hideEmpty),
    MakeOutlet("hideBots", &hideBots),
    MakeOutlet("maxPing", &this->maxPingSlider)
  );

  $(self->view, awakeWithResourceName, "ui/play/JoinServerViewController.json");
  $(self->view, resolve, outlets);

  self->view->stylesheet = $$(Stylesheet, stylesheetWithResourceName, "ui/play/JoinServerViewController.css");
  assert(self->view->stylesheet);

  $(this->serversTableView, addColumnWithIdentifier, _server);
  $(this->serversTableView, addColumnWithIdentifier, _map);
  $(this->serversTableView, addColumnWithIdentifier, _players);
  $(this->serversTableView, addColumnWithIdentifier, _ping);

  this->serversTableView->dataSource.numberOfRows = numberOfRows;
  this->serversTableView->dataSource.self = this;

  this->serversTableView->delegate.cellForColumnAndRow = cellForColumnAndRow;
  this->serversTableView->delegate.didSetSortColumn = didSetSortColumn;
  this->serversTableView->delegate.didSelectRowsAtIndexes = didSelectRowsAtIndexes;
  this->serversTableView->delegate.self = this;

  $(this->rosterTableView, addColumnWithIdentifier, _runner);
  $(this->rosterTableView, addColumnWithIdentifier, _best);
  $(this->rosterTableView, addColumnWithIdentifier, _status);

  this->rosterTableView->dataSource.numberOfRows = numberOfRosterRows;
  this->rosterTableView->dataSource.self = this;

  this->rosterTableView->delegate.cellForColumnAndRow = rosterCellForColumnAndRow;
  this->rosterTableView->delegate.self = this;
  this->rosterTableView->control.selection = ControlSelectionNone;

  hideBots->delegate.didToggle = didToggleHideBots;
  hideBots->delegate.self = this;

  hideEmpty->delegate.didToggle = didToggleHideEmpty;
  hideEmpty->delegate.self = this;

  this->maxPingSlider->delegate.didSetValue = didSetMaxPing;
  this->maxPingSlider->delegate.self = this;
  $(this->maxPingSlider, setLabelFormat, "%g ms");

  refresh->delegate.didClick = didClickRefresh;
  refresh->delegate.self = this;

  this->connectButton->delegate.didClick = didClickConnect;
  this->connectButton->delegate.self = this;

  this->quickJoinButton->delegate.didClick = didClickQuickJoin;
  this->quickJoinButton->delegate.self = this;

  // Default sort is ping ascending, set at load rather than on a first click.
  // Setting it runs didSetSortColumn, which loads and sorts whatever servers
  // the client already knows about and settles the whole route with it - the
  // counts, the empty state, the header arrow and the details pane - so the
  // first frame is right whether or not viewWillAppear has asked for more.
  TableColumn *pingColumn = $(this->serversTableView, columnWithIdentifier, _ping);
  assert(pingColumn);
  $(this->serversTableView, setSortColumn, pingColumn);
}

/**
 * @see ViewController::respondToEvent(ViewController *, const SDL_Event *)
 */
static void respondToEvent(ViewController *self, const SDL_Event *event) {

  if (event->type == MVC_NOTIFICATION_EVENT && event->user.code == NOTIFICATION_SERVER_PARSED) {
    $((JoinServerViewController *) self, reloadServers);
  }

  super(ViewController, self, respondToEvent, event);
}

/**
 * @see ViewController::viewWillAppear(ViewController *)
 */
static void viewWillAppear(ViewController *self) {

  super(ViewController, self, viewWillAppear);

  JoinServerViewController *this = (JoinServerViewController *) self;

  // show what we already know at once, then ask the master again; querying only
  // when nothing was cached meant the first answer of a session was the only one,
  // so a list that was empty when the menu first opened stayed empty until the
  // Refresh button was pressed
  if (this->servers) {
    $(this, reloadServers);
  }

  cgi.GetServers();
}

#pragma mark - JoinServerViewController

/**
 * @brief True when two entries are the same server reached two ways.
 * @details The exact address is the identity: `Cl_ServerForNetaddr` already
 * merges entries that share one, so a same-address pair here would be a client
 * bug rather than a real duplicate, and merging it is free insurance. The
 * second test is the case the browser actually shows - one host answering both
 * a LAN broadcast and its configured internet address, which arrive under two
 * different addresses. Hostname alone is not identity (a cluster shares one),
 * so the map and both occupancy figures have to agree as well.
 */
static bool sameServer(const cl_server_info_t *a, const cl_server_info_t *b) {

  if (a->addr.addr == b->addr.addr && a->addr.port == b->addr.port) {
    return true;
  }

  return *a->hostname && q_strcmp(a->hostname, b->hostname) == 0 &&
         q_strcmp(a->name, b->name) == 0 &&
         a->max_clients == b->max_clients &&
         a->clients == b->clients;
}

/**
 * @brief True when `a` is the better of two copies of one server.
 */
static bool preferServer(const cl_server_info_t *a, const cl_server_info_t *b) {

  const bool aUnanswered = pingUnanswered(a);
  const bool bUnanswered = pingUnanswered(b);

  if (aUnanswered != bUnanswered) {
    return !aUnanswered;
  }

  return a->ping < b->ping;
}

/**
 * @brief Drops every entry that another copy of the same server supersedes.
 * @details A copy is superseded when another answered and it did not, when
 * another answered faster, or - for two equally good copies - when the other
 * came first. That last clause is what leaves exactly one of a tie standing.
 */
static void dedupeServers(PointerArray *servers) {

  for (size_t i = 0; i < servers->count; ) {
    const cl_server_info_t *server = $(servers, get, i);
    bool superseded = false, precedes = true;

    for (size_t j = 0; j < servers->count; j++) {

      if (j == i) {
        precedes = false;
        continue;
      }

      const cl_server_info_t *candidate = $(servers, get, j);
      if (!sameServer(server, candidate)) {
        continue;
      }

      if (preferServer(candidate, server) ||
          (precedes && !preferServer(server, candidate))) {
        superseded = true;
        break;
      }
    }

    if (superseded) {
      $(servers, removeAt, i);
    } else {
      i++;
    }
  }
}

/**
 * @brief Comparator for server sorting.
 */
static Order comparator(const ident a, const ident b) {

  JoinServerViewController *this = sortingJoinServerViewController;
  const TableColumn *sortColumn = this->serversTableView->sortColumn;

  // A server that never answered is neither fast nor slow, so it goes last
  // whichever way the ping column is pointing. This is decided before the
  // direction swap below, which is what makes it direction-independent.
  if (sortColumn && q_strcmp(sortColumn->identifier, _ping) == 0) {
    const bool leftUnanswered = pingUnanswered((const cl_server_info_t *) a);
    const bool rightUnanswered = pingUnanswered((const cl_server_info_t *) b);
    if (leftUnanswered != rightUnanswered) {
      return leftUnanswered ? OrderDescending : OrderAscending;
    }
  }

  if (this->serversTableView->sortColumn) {
    const cl_server_info_t *s0, *s1;

    switch (this->serversTableView->sortColumn->order) {
      case OrderAscending:
        s0 = a; s1 = b;
        break;
      case OrderDescending:
        s0 = b; s1 = a;
        break;
      default:
        return OrderSame;
    }

    int32_t cmp = 0;

    if (q_strcmp(this->serversTableView->sortColumn->identifier, _server) == 0) {
      cmp = q_strcmp(s0->hostname, s1->hostname);
    } else if (q_strcmp(this->serversTableView->sortColumn->identifier, _map) == 0) {
      cmp = q_strcmp(s0->name, s1->name);
    } else if (q_strcmp(this->serversTableView->sortColumn->identifier, _players) == 0) {
      cmp = s0->clients - s1->clients;
    } else if (q_strcmp(this->serversTableView->sortColumn->identifier, _ping) == 0) {
      cmp = s0->ping - s1->ping;
    } else {
      assert(false);
    }

    return cmp < 0 ? OrderAscending : cmp > 0 ? OrderDescending : OrderSame;
  }

  return OrderSame;
}

/**
 * @fn void JoinServerViewController::reloadServers(JoinServerViewController *self)
 * @memberof JoinServerViewController
 */
static void reloadServers(JoinServerViewController *self) {

  release(self->servers);

  self->servers = $(alloc(PointerArray), init);

  const PointerArray *servers = cgi.Servers();
  const size_t count = servers ? servers->count : 0;

  Cg_Debug("%d servers known to the client\n", (int32_t) count);

  uint32_t hidden = 0;

  for (size_t i = 0; i < count; i++) {
    cl_server_info_t *server = $(servers, get, i);

    const int32_t clients = cg_join_server_hide_bots->value ? server->clients - server->bots : server->clients;

    if (clients == 0 && (cg_join_server_hide_empty->value || cg_join_server_hide_bots->value)) {
      Cg_Debug("Hiding %s: %d clients, %d bots, hide_empty %d, hide_bots %d\n",
               server->hostname, server->clients, server->bots,
               cg_join_server_hide_empty->integer, cg_join_server_hide_bots->integer);

      hidden++;
      continue;
    }

    $(self->servers, add, server);
  }

  dedupeServers(self->servers);

  Cg_Debug("Showing %d servers, %u hidden by filters\n", (int32_t) self->servers->count, hidden);

  sortingJoinServerViewController = self;
  $(self->servers, sort, comparator);
  sortingJoinServerViewController = NULL;

  $(self->serversTableView, reloadData);

  refreshSummary(self);
  restoreSelection(self);
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ObjectInterface *) clazz->interface)->dealloc = dealloc;

  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
  ((ViewControllerInterface *) clazz->interface)->respondToEvent = respondToEvent;
  ((ViewControllerInterface *) clazz->interface)->viewWillAppear = viewWillAppear;

  ((JoinServerViewControllerInterface *) clazz->interface)->reloadServers = reloadServers;

  cg_join_server_hide_empty = cgi.AddCvar("cg_join_server_hide_empty", "0", CVAR_ARCHIVE, NULL);
  cg_join_server_hide_bots = cgi.AddCvar("cg_join_server_hide_bots", "0", CVAR_ARCHIVE, NULL);
}

/**
 * @fn Class *JoinServerViewController::_JoinServerViewController(void)
 * @memberof JoinServerViewController
 */
Class *_JoinServerViewController(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "JoinServerViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(JoinServerViewController),
      .interfaceOffset = offsetof(JoinServerViewController, interface),
      .interfaceSize = sizeof(JoinServerViewControllerInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
