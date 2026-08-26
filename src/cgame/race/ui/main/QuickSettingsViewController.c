/*
 * Copyright(c) 2026 Quetoo Race Module
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#include "cg_local.h"

#include "QuickSettingsViewController.h"

#define _Class _QuickSettingsViewController

/**
 * @brief One drawer item: its outlets, and the bind its hint is resolved from.
 * @details The design prints a literal key beside three of the six list items
 * (Esc, R, F1, F3). Those are the designer's defaults, not Race's - every one
 * of these commands ships unbound - so the hint is resolved from the player's
 * own binding instead and an unbound command contributes no hint. Resume is
 * the one true literal: Escape resumes the game whether or not anything is
 * bound to it.
 */
static const struct {
  const char *item;
  const char *hint;
  const char *bind;
} itemOutlets[QuickSettingsItemCount] = {
  [QuickSettingsItemResume] = { "quickResume", "quickResumeHint", NULL },
  [QuickSettingsItemRestartRun] = { "quickRestart", "quickRestartHint", "kill" },
  [QuickSettingsItemWatchBest] = { "quickWatchBest", "quickWatchBestHint", "replay pb" },
  [QuickSettingsItemSpectate] = { "quickSpectate", "quickSpectateHint", "mode spectator" },
  [QuickSettingsItemCallVote] = { "quickCallVote", "quickCallVoteHint", "race vote" },
  [QuickSettingsItemFullMenu] = { "quickFullMenu", "quickFullMenuHint", NULL },
  [QuickSettingsItemDisconnect] = { "quickDisconnect", "quickDisconnectHint", NULL },
  [QuickSettingsItemQuit] = { "quickQuit", "quickQuitHint", NULL },
};

/**
 * @brief ButtonDelegate for every item in the drawer.
 */
static void didClickItem(Button *button) {

  QuickSettingsViewController *this = button->delegate.self;

  const QuickSettingsItem item = (QuickSettingsItem) (intptr_t) button->delegate.data;

  if (this->delegate.didSelectItem) {
    this->delegate.didSelectItem(this->delegate.self, item);
  }
}

static void loadView(ViewController *self) {

  super(ViewController, self, loadView);

  QuickSettingsViewController *this = (QuickSettingsViewController *) self;
  View *view = $$(View, viewWithResourceName, "ui/main/QuickSettingsViewController.json", NULL);
  assert(view);

  view->stylesheet = $$(Stylesheet, stylesheetWithResourceName, "ui/main/QuickSettingsViewController.css");
  assert(view->stylesheet);

  Outlet outlets[] = MakeOutlets(
    MakeOutlet("quickSubtitle", &this->subtitle),
    MakeOutlet("quickResume", &this->items[QuickSettingsItemResume]),
    MakeOutlet("quickResumeHint", &this->itemHints[QuickSettingsItemResume]),
    MakeOutlet("quickRestart", &this->items[QuickSettingsItemRestartRun]),
    MakeOutlet("quickRestartHint", &this->itemHints[QuickSettingsItemRestartRun]),
    MakeOutlet("quickWatchBest", &this->items[QuickSettingsItemWatchBest]),
    MakeOutlet("quickWatchBestHint", &this->itemHints[QuickSettingsItemWatchBest]),
    MakeOutlet("quickSpectate", &this->items[QuickSettingsItemSpectate]),
    MakeOutlet("quickSpectateHint", &this->itemHints[QuickSettingsItemSpectate]),
    MakeOutlet("quickCallVote", &this->items[QuickSettingsItemCallVote]),
    MakeOutlet("quickCallVoteHint", &this->itemHints[QuickSettingsItemCallVote]),
    MakeOutlet("quickFullMenu", &this->items[QuickSettingsItemFullMenu]),
    MakeOutlet("quickFullMenuHint", &this->itemHints[QuickSettingsItemFullMenu]),
    MakeOutlet("quickDisconnect", &this->items[QuickSettingsItemDisconnect]),
    MakeOutlet("quickDisconnectHint", &this->itemHints[QuickSettingsItemDisconnect]),
    MakeOutlet("quickQuit", &this->items[QuickSettingsItemQuit]),
    MakeOutlet("quickQuitHint", &this->itemHints[QuickSettingsItemQuit])
  );
  $(view, resolve, outlets);

  assert(this->subtitle);

  for (size_t i = 0; i < QuickSettingsItemCount; i++) {

    assert(this->items[i]);
    assert(this->itemHints[i]);

    this->items[i]->delegate = (ButtonDelegate) {
      .self = self,
      .data = (ident) (intptr_t) i,
      .didClick = didClickItem
    };
  }

  $(self, setView, view);
  release(view);
}

/**
 * @fn void QuickSettingsViewController::refresh(QuickSettingsViewController *self)
 * @memberof QuickSettingsViewController
 */
static void refresh(QuickSettingsViewController *self) {

  for (size_t i = 0; i < QuickSettingsItemCount; i++) {

    if (itemOutlets[i].bind == NULL) {
      continue;
    }

    const SDL_Scancode key = cgi.KeyForBind(SDL_SCANCODE_UNKNOWN, itemOutlets[i].bind);
    const char *name = key == SDL_SCANCODE_UNKNOWN ? NULL : cgi.KeyName(key);

    $(self->itemHints[i]->text, setText, name ? name : "");
  }

  $(self->viewController.view, updateBindings);
}

/**
 * @fn void QuickSettingsViewController::setRouteName(QuickSettingsViewController *self, const char *route)
 * @memberof QuickSettingsViewController
 */
static void setRouteName(QuickSettingsViewController *self, const char *route) {

  $(self->subtitle->text, setText,
    va("%s · Esc resumes", route && *route ? route : "Menu"));
}

static void initialize(Class *clazz) {
  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
  ((QuickSettingsViewControllerInterface *) clazz->interface)->refresh = refresh;
  ((QuickSettingsViewControllerInterface *) clazz->interface)->setRouteName = setRouteName;
}

Class *_QuickSettingsViewController(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "QuickSettingsViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(QuickSettingsViewController),
      .interfaceOffset = offsetof(QuickSettingsViewController, interface),
      .interfaceSize = sizeof(QuickSettingsViewControllerInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
