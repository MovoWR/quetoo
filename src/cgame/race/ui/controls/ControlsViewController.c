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

#include <ObjectivelyMVC/Label.h>
#include <ObjectivelyMVC/TextView.h>

#include "BindTextView.h"
#include "ControlsViewController.h"
#include "DialogViewController.h"
#include "MovementCombatViewController.h"

#define _Class _ControlsViewController

#pragma mark - Route chrome

static View *controlsRoot(View *view) {

  assert(view);

  while (view->superview && !$(view, hasClassName, "controlsMenu")) {
    view = view->superview;
  }

  return view;
}

static void setControlFlag(Control *control, ControlState flag, bool enabled) {

  const unsigned int previous = control->state;
  if (enabled) {
    control->state |= flag;
  } else {
    control->state &= ~flag;
  }
  if (control->state != previous) {
    $(control, stateDidChange);
  }
}

void ControlsViewController_RefreshBindings(View *view) {

  View *root = controlsRoot(view);

  // Each field narrows the command's key list to its own slot, so a plain
  // updateBindings pass is the whole refresh - see RaceBindTextView.
  $(root, updateBindings);
}

void ControlsViewController_SetHint(View *view, const char *text) {

  View *root = controlsRoot(view);

  Label *hint = (Label *) $(root, descendantWithIdentifier, "controlsHint");
  if (hint) {
    $(hint->text, setText, text ? text : "");
  }
}

void ControlsViewController_UpdateStatus(View *view, size_t dirtyCount) {

  View *root = controlsRoot(view);

  Label *status = (Label *) $(root, descendantWithIdentifier, "controlsDirtyStatus");
  if (status) {
    if (dirtyCount == 0) {
      $(status->text, setText, "");
    } else {
      $(status->text, setTextWithFormat, "%zu binding%s changed",
        dirtyCount, dirtyCount == 1 ? "" : "s");
    }
  }

  // The commit pair is only reachable while there is something to commit, so a
  // stale count cannot survive a revert or a page change.
  static const char *commitIdentifiers[] = { "revertChanges", "applyChanges" };
  for (size_t i = 0; i < lengthof(commitIdentifiers); i++) {
    View *button = $(root, descendantWithIdentifier, commitIdentifiers[i]);
    if (button) {
      setControlFlag((Control *) button, ControlStateDisabled, dirtyCount == 0);
    }
  }
}

#pragma mark - Delegates

/**
 * @brief Restores the client-shipped bindings, once confirmed.
 */
static void restoreDefaults(ident data) {

  ControlsViewController *this = data;

  MovementCombatViewController_RestoreDefaults(this->bindingsViewController);
}

/**
 * @brief ButtonDelegate for the route-local factory-reset confirmation.
 * @details Reset is the widest action on the route and the only one that can
 * throw away work the player did in an earlier session: Race commands ship with
 * no key at all, so returning them to factory means clearing every replay,
 * ghost, marker and mode binding. It is confirmed for that reason, and it is
 * still undone by Revert changes - reset does not re-baseline the commit pair,
 * so the bindings it writes stay staged against the last Apply until the player
 * accepts them.
 */
static void didClickRestoreDefaults(Button *button) {

  ControlsViewController *this = button->delegate.self;

  const Dialog dialog = {
    .data = this,
    .message = "Reset every binding to the factory set? Race commands ship "
               "unbound, so this clears your replay, ghost and marker keys. "
               "Revert changes will still undo it until you Apply.",
    .ok = "Reset bindings",
    .cancel = "Cancel",
    .okFunction = restoreDefaults
  };

  ViewController *viewController =
    (ViewController *) $(alloc(DialogViewController), initWithDialog, &dialog);
  $((ViewController *) this, addChildViewController, viewController);

  View *confirmButton = $(viewController->view, descendantWithIdentifier, "ok");
  if (confirmButton) {
    $(confirmButton, addClassName, "dangerButton");
  }
}

/**
 * @brief ButtonDelegate for restoring the values held at route entry.
 */
static void didClickRevertChanges(Button *button) {

  ControlsViewController *this = button->delegate.self;

  MovementCombatViewController_RevertChanges(this->bindingsViewController);
}

/**
 * @brief ButtonDelegate for accepting the current values as the new baseline.
 */
static void didClickApply(Button *button) {

  ControlsViewController *this = button->delegate.self;

  MovementCombatViewController_ApplyChanges(this->bindingsViewController);
}

/**
 * @brief Quits the game.
 */
static void quit(ident data) {
  cgi.Cbuf("quit\n");
}

/**
 * @brief ButtonDelegate for the route-local Quit confirmation.
 */
static void didClickQuit(Button *button) {

  ControlsViewController *this = button->delegate.self;
  const Dialog dialog = {
    .message = "Are you sure you want to quit to the desktop?",
    .ok = "Quit",
    .cancel = "Cancel",
    .okFunction = quit
  };

  ViewController *viewController =
    (ViewController *) $(alloc(DialogViewController), initWithDialog, &dialog);
  $((ViewController *) this, addChildViewController, viewController);

  View *confirmButton = $(viewController->view, descendantWithIdentifier, "ok");
  if (confirmButton) {
    $(confirmButton, addClassName, "dangerButton");
  }
}

#pragma mark - ViewController

/**
 * @see ViewController::loadView(ViewController *)
 */
static void loadView(ViewController *self) {

  super(ViewController, self, loadView);

  ControlsViewController *this = (ControlsViewController *) self;

  View *view = $$(View, viewWithResourceName, "ui/controls/ControlsViewController.json", NULL);
  assert(view);

  view->stylesheet = $$(Stylesheet, stylesheetWithResourceName,
                        "ui/controls/MovementCombatViewController.css");
  assert(view->stylesheet);

  $(self, setView, view);
  release(view);

  Outlet outlets[] = MakeOutlets(
    MakeOutlet("controlsDirtyStatus", &this->dirtyStatus),
    MakeOutlet("revertChanges", &this->revertChanges),
    MakeOutlet("applyChanges", &this->apply),
    MakeOutlet("restoreDefaults", &this->restoreDefaults),
    MakeOutlet("quit", &this->quit)
  );
  $(self->view, resolve, outlets);
  assert(this->dirtyStatus);
  assert(this->revertChanges);
  assert(this->apply);
  assert(this->restoreDefaults);
  assert(this->quit);

  this->revertChanges->delegate = (ButtonDelegate) {
    .self = this,
    .didClick = didClickRevertChanges
  };
  this->apply->delegate = (ButtonDelegate) {
    .self = this,
    .didClick = didClickApply
  };
  this->restoreDefaults->delegate = (ButtonDelegate) {
    .self = this,
    .didClick = didClickRestoreDefaults
  };
  this->quit->delegate = (ButtonDelegate) {
    .self = this,
    .didClick = didClickQuit
  };

  // Every binding lives on one flowed page behind a page strip, so the route
  // hosts it directly; the design has no nested tab view controller here.
  this->bindingsViewController =
    $((ViewController *) alloc(MovementCombatViewController), init);
  assert(this->bindingsViewController);

  $(self, addChildViewController, this->bindingsViewController);
  $((View *) ((Panel *) view)->contentView, addSubview,
    this->bindingsViewController->view);
  release(this->bindingsViewController);
}

/**
 * @see ViewController::viewWillAppear(ViewController *)
 */
static void viewWillAppear(ViewController *self) {

  super(ViewController, self, viewWillAppear);
  ControlsViewController_RefreshBindings(self->view);
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
  ((ViewControllerInterface *) clazz->interface)->viewWillAppear = viewWillAppear;
}

/**
 * @fn Class *ControlsViewController::_ControlsViewController(void)
 * @memberof ControlsViewController
 */
Class *_ControlsViewController(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "ControlsViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(ControlsViewController),
      .interfaceOffset = offsetof(ControlsViewController, interface),
      .interfaceSize = sizeof(ControlsViewControllerInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
