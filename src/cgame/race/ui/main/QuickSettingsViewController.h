/*
 * Copyright(c) 2026 Quetoo Race Module
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#pragma once

#include <ObjectivelyMVC.h>

typedef struct QuickSettingsViewController QuickSettingsViewController;
typedef struct QuickSettingsViewControllerInterface QuickSettingsViewControllerInterface;

typedef struct {
  ident self;
  void (*didClose)(ident self);
  void (*didShowControls)(ident self);
  void (*didShowSettings)(ident self);
} QuickSettingsViewControllerDelegate;

struct QuickSettingsViewController {
  ViewController viewController;
  QuickSettingsViewControllerInterface *interface;

  QuickSettingsViewControllerDelegate delegate;
  Control *sensitivity;
  Control *fov;
  Button *closeButton;
  Button *controlsButton;
  Button *settingsButton;

  /**
   * @brief The three HUD helper toggles, and the count they are summarized by.
   */
  Checkbox *helpers[3];
  Label *helperStatus;
};

struct QuickSettingsViewControllerInterface {
  ViewControllerInterface viewControllerInterface;

  void (*refresh)(QuickSettingsViewController *self);
};

CGAME_EXPORT Class *_QuickSettingsViewController(void);

