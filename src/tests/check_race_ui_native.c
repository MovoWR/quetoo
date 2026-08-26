/*
 * Copyright(c) 2006 Quetoo.
 *
 * Native ObjectivelyMVC dispatch fixture for Race-owned UI responders.
 */

#include <stdint.h>
#include <stdio.h>

#include "cg_race_dashboard_layout.h"
#include "ui/main/QuickSettingsHostView.h"

static uint32_t assertions;
static uint32_t failures;
static uint32_t dismissals;

#define UI_CHECK(condition, label) do { \
  assertions++; \
  if (!(condition)) { \
    fprintf(stderr, "FAIL:%s:%d:%s\n", __FILE__, __LINE__, label); \
    failures++; \
  } \
} while (0)

static void didDismiss(ident self) {
  uint32_t *count = self;
  (*count)++;
}

uint32_t Race_NativeTestUi(uint32_t *assertion_count) {
  assertions = 0u;
  failures = 0u;
  dismissals = 0u;

  const SDL_Rect hostFrame = MakeRect(0, 0, 200, 100);
  QuickSettingsHostView *host =
    (QuickSettingsHostView *) $((View *) alloc(QuickSettingsHostView),
      initWithFrame, &hostFrame);
  UI_CHECK(host != NULL, "quick settings host initializes");
  if (host == NULL) {
    *assertion_count = assertions;
    return failures;
  }

  host->delegate = (QuickSettingsHostViewDelegate) {
    .self = &dismissals,
    .didDismiss = didDismiss
  };

  View *drawer = $(alloc(View), initWithFrame, &MakeRect(100, 0, 100, 100));
  UI_CHECK(drawer != NULL, "quick settings drawer initializes");
  if (drawer) {
    $((View *) host, addSubview, drawer);
    release(drawer);
  }

  const SDL_Event inside = {
    .button = {
      .type = SDL_EVENT_MOUSE_BUTTON_DOWN,
      .x = 150.f,
      .y = 50.f
    }
  };
  $((View *) host, respondToEvent, &inside);
  UI_CHECK(dismissals == 0u, "drawer mouse-down stays open");

  const SDL_Event outside = {
    .button = {
      .type = SDL_EVENT_MOUSE_BUTTON_DOWN,
      .x = 50.f,
      .y = 50.f
    }
  };
  $((View *) host, respondToEvent, &outside);
  UI_CHECK(dismissals == 1u, "scrim mouse-down dismisses drawer");

  $((View *) host, respondToEvent, &outside);
  UI_CHECK(dismissals == 2u, "scrim remains a deterministic responder");

  release(host);

  View *dashboard = $(alloc(View), initWithFrame, &MakeRect(0, 0, 800, 600));
  UI_CHECK(dashboard != NULL, "dashboard view initializes");
  if (dashboard) {
    cg_race_dashboard_layout_state_t layout = { };
    uint32_t layoutCount = 0u;

    if (Cg_RaceDashboardLayout_ShouldRun(
          &layout, dashboard, MakeSize(800, 600), false)) {
      layoutCount++;
      $(dashboard, layoutIfNeeded);
    }
    UI_CHECK(layoutCount == 1u && !dashboard->needsLayout,
             "first dashboard frame lays out once");

    if (Cg_RaceDashboardLayout_ShouldRun(
          &layout, dashboard, MakeSize(800, 600), false)) {
      layoutCount++;
      $(dashboard, layoutIfNeeded);
    }
    UI_CHECK(layoutCount == 1u,
             "unchanged dashboard frame does not relayout");

    dashboard->needsLayout = true;
    UI_CHECK(Cg_RaceDashboardLayout_ShouldRun(
               &layout, dashboard, MakeSize(800, 600), false),
             "explicit dashboard invalidation relayouts");
    $(dashboard, layoutIfNeeded);

    UI_CHECK(Cg_RaceDashboardLayout_ShouldRun(
               &layout, dashboard, MakeSize(1024, 600), false),
             "dashboard resize relayouts");
    $(dashboard, layoutIfNeeded);

    UI_CHECK(Cg_RaceDashboardLayout_ShouldRun(
               &layout, dashboard, MakeSize(1024, 600), true),
             "dashboard title-wrap change relayouts");

    release(dashboard);
  }

  *assertion_count = assertions;
  return failures;
}

#undef UI_CHECK
