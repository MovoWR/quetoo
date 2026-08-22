/*
 * Copyright(c) 2026 Quetoo Race Module
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#include "cg_local.h"

#include "QuickSettingsViewController.h"
#include "CvarCheckbox.h"

#define _Class _QuickSettingsViewController

/**
 * @brief The HUD helper cvars, in the order the drawer lists them.
 */
static const char *helperVars[] = {
  "cg_race_strafe_helper_draw",
  "cg_race_strafe_helper_ups",
  "cg_input_viewer"
};

/**
 * @brief Repaints the "N of 3 on" count in the HUD helpers eyebrow.
 * @details Read from the cvars rather than from the Checkbox states, for the
 * same reason the Settings route paints its preset strips from them: a helper
 * can also be toggled from the console or from the full Settings route while
 * the drawer is open.
 */
static void refreshHelperStatus(QuickSettingsViewController *self) {

  if (self->helperStatus == NULL) {
    return;
  }

  size_t on = 0;
  for (size_t i = 0; i < lengthof(helperVars); i++) {
    if (cgi.GetCvarInteger(helperVars[i])) {
      on++;
    }
  }

  $(self->helperStatus->text, setText,
    va("%zu of %zu on", on, lengthof(helperVars)));
}

/**
 * @brief CheckboxDelegate for every helper row.
 */
static void didToggleHelper(Checkbox *checkbox) {

  cvarCheckboxDidToggle(checkbox);

  refreshHelperStatus(checkbox->delegate.self);
}

static void didClickButton(Button *button) {

  QuickSettingsViewController *this = button->delegate.self;

  if (button == this->closeButton) {
    if (this->delegate.didClose) {
      this->delegate.didClose(this->delegate.self);
    }
  } else if (button == this->controlsButton) {
    if (this->delegate.didShowControls) {
      this->delegate.didShowControls(this->delegate.self);
    }
  } else if (button == this->settingsButton) {
    if (this->delegate.didShowSettings) {
      this->delegate.didShowSettings(this->delegate.self);
    }
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
    MakeOutlet("quickSensitivity", &this->sensitivity),
    MakeOutlet("quickFov", &this->fov),
    MakeOutlet("quickClose", &this->closeButton),
    MakeOutlet("quickControls", &this->controlsButton),
    MakeOutlet("quickSettings", &this->settingsButton),
    MakeOutlet("quickStrafeHelper", &this->helpers[0]),
    MakeOutlet("quickUps", &this->helpers[1]),
    MakeOutlet("quickInputViewer", &this->helpers[2]),
    MakeOutlet("quickHelperStatus", &this->helperStatus)
  );
  $(view, resolve, outlets);

  assert(this->sensitivity);
  assert(this->fov);
  assert(this->helperStatus);

  // Slider draws its own readout from a single labelFormat, and the stock
  // "%0.1f" cannot say 1.500 on a 0.125 step - the same per-descriptor fix the
  // Settings route makes. CvarSlider::updateBindings reasserts "%g" on every
  // refresh for steps of 1 or more, so the field of view keeps that instead;
  // over 90 - 160 the two print identically.
  $((Slider *) this->sensitivity, setLabelFormat, "%0.3f");
  $((Slider *) this->fov, setLabelFormat, "%0.0f");

  for (size_t i = 0; i < lengthof(this->helpers); i++) {
    this->helpers[i]->delegate = (CheckboxDelegate) {
      .self = self,
      .didToggle = didToggleHelper
    };
  }

  Button *buttons[] = { this->closeButton, this->controlsButton, this->settingsButton };
  for (size_t i = 0; i < lengthof(buttons); i++) {
    buttons[i]->delegate = (ButtonDelegate) {
      .self = self,
      .didClick = didClickButton
    };
  }

  $(self, setView, view);
  release(view);
}

static void refresh(QuickSettingsViewController *self) {
  $(self->viewController.view, updateBindings);
  refreshHelperStatus(self);
}

static void initialize(Class *clazz) {
  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
  ((QuickSettingsViewControllerInterface *) clazz->interface)->refresh = refresh;
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
