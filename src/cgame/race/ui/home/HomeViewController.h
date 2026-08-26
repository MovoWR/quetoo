/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "cg_score.h"
#include "cg_race_dashboard_layout.h"

#include <ObjectivelyMVC.h>

typedef struct HomeViewController HomeViewController;
typedef struct HomeViewControllerInterface HomeViewControllerInterface;

struct HomeViewController {
  ViewController viewController;
  HomeViewControllerInterface *interface;

  StackView *disconnectedPanel;
  StackView *dashboardRoot;
  StackView *sessionDashboard;
  View *sessionColumn;
  View *routeFilter;
  Label *routeHint;
  View *mapSummary;
  Label *mapSectionTitle;
  View *mapRule;
  StackView *mapHeadingRow;
  StackView *mapCopy;
  StackView *modePanel;
  View *overviewRow;
  View *recordsSummaryPanel;
  View *recordsSummaryHeader;
  View *recordsSummaryRule;
  Label *recordsSummaryTitle;
  Label *worldRecord;
  View *summaryRecordsHeaderBand;
  TableView *summaryRecordsTable;
  View *rosterPanel;
  View *rosterHeader;
  View *rosterRule;
  Label *rosterTitle;
  Label *rosterCount;
  View *rosterHeaderBand;
  TableView *rosterTable;
  Label *rosterEmpty;
  StackView *rosterFooter;
  Label *rosterPracticeSummary;
  Label *rosterSpectatorSummary;
  Label *mapTitle;
  Label *mapIdentifier;
  Label *modeStatus;
  Button *modeRaceButton;
  Button *modePracticeButton;
  Button *modeSpectatorButton;

  cg_leaderboard_snapshot_entry_t records[RACE_LEADERBOARD_TOP_MAX];
  size_t numRecords;
  cg_roster_entry_t players[MAX_CLIENTS];
  size_t numPlayers;

  cg_race_dashboard_layout_state_t dashboardLayout;
};

struct HomeViewControllerInterface {
  ViewControllerInterface viewControllerInterface;
};

CGAME_EXPORT Class *_HomeViewController(void);
void HomeViewController_Refresh(void);
void HomeViewController_RefreshPlayerActions(const player_state_t *ps);
