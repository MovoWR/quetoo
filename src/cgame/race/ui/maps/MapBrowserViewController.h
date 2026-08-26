/*
 * Copyright(c) 2026 Quetoo Race Module
 */

#pragma once

#include "cg_race_map_browser.h"

#include <ObjectivelyMVC.h>
#include <ObjectivelyMVC/Button.h>
#include <ObjectivelyMVC/ImageView.h>
#include <ObjectivelyMVC/Label.h>
#include <ObjectivelyMVC/PageView.h>
#include <ObjectivelyMVC/TableView.h>
#include <ObjectivelyMVC/TextView.h>

typedef struct MapBrowserViewController MapBrowserViewController;
typedef struct MapBrowserViewControllerInterface MapBrowserViewControllerInterface;

struct MapBrowserViewController {
  ViewController viewController;
  MapBrowserViewControllerInterface *interface;
  TableView *mapsTableView;
  TableView *timesTableView;
  Label *emptyLabel;
  Label *countLabel;
  Label *footerLabel;
  TextView *filterTextView;
  Label *pageLabel;
  View *pagerView;
  Button *previousButton;
  Button *nextButton;
  Button *allScopeTab;
  Button *personalScopeTab;
  PageView *detailsPages;
  View *infoPage;
  View *timesPage;
  Button *infoTab;
  Button *timesTab;
  ImageView *mapShot;
  Label *mapShotPlaceholder;
  /**
   * @brief The preview's frame, and the prompt that stands in for the whole
   * detail pane before a map is chosen. With nothing selected the pane is one
   * heading and one line rather than a heading over "Unknown", two dashes and
   * a preview that reports itself unavailable - the same shape Join's details
   * pane settled on.
   */
  View *mapPreviewFrame;
  Label *mapDetailHint;
  Label *mapTitleLabel;
  Label *mapPathLabel;
  Label *mapAuthorLabel;
  Label *mapPersonalBestLabel;
  Label *mapRunsLabel;
  Label *mapWorldRecordLabel;
  Label *worldRecordChipLabel;
  Label *personalRecordChipLabel;
  Label *mapTimesLabel;
  Label *mapStatsLabel;
  Button *timesButton;
  Button *voteButton;

  /**
   * @brief "Watch world record".
   * @details Enabled only for the map that is actually loaded. `replay wr`
   * plays back a recording against the running BSP, so there is nothing to
   * watch for a map the server is not on - the design's button lives on the
   * selected map's pane, and this is the honest reading of it.
   */
  Button *watchRecordButton;

  Button *nominateButton;
  char selectedMap[32];
  int32_t page;
  char prefix[32];
  char scope[16];
};

struct MapBrowserViewControllerInterface {
  ViewControllerInterface viewControllerInterface;
};

CGAME_EXPORT void MapBrowserViewController_RefreshValues(void);
CGAME_EXPORT void MapBrowserViewController_RefreshDetails(void);
CGAME_EXPORT Class *_MapBrowserViewController(void);
