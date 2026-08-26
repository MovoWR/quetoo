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

#include "MainViewController.h"
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

  (void) root;

  // The design has one footer, and the commit pair and its count live in it.
  // Bindings are written the moment they are captured, so no binding change
  // needs a restart - the count is never the gold warning state.
  if (dirtyCount == 0) {
    MainViewController_SetCommitStatus(NULL, false);
  } else {
    char status[64];
    snprintf(status, sizeof(status), "%zu binding%s changed",
             dirtyCount, dirtyCount == 1 ? "" : "s");
    MainViewController_SetCommitStatus(status, false);
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
static void didRevertChanges(ident sender) {

  ControlsViewController *this = sender;

  MovementCombatViewController_RevertChanges(this->bindingsViewController);
}

/**
 * @brief ButtonDelegate for accepting the current values as the new baseline.
 */
static void didApplyChanges(ident sender) {

  ControlsViewController *this = sender;

  MovementCombatViewController_ApplyChanges(this->bindingsViewController);
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
    MakeOutlet("restoreDefaults", &this->restoreDefaults)
  );
  $(self->view, resolve, outlets);
  assert(this->restoreDefaults);

  this->restoreDefaults->delegate = (ButtonDelegate) {
    .self = this,
    .didClick = didClickRestoreDefaults
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

  MainViewController_SetCommitDelegate(&(const RaceCommitDelegate) {
    .self = self,
    .didApply = didApplyChanges,
    .didRevert = didRevertChanges
  });

  ControlsViewController_RefreshBindings(self->view);
}

/**
 * @see ViewController::viewWillDisappear(ViewController *)
 * @details The footer's commit pair is on loan for as long as this route is on
 * top. The shell drops it on every navigation as well.
 */
static void viewWillDisappear(ViewController *self) {

  super(ViewController, self, viewWillDisappear);

  MainViewController_SetCommitDelegate(NULL);
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
  ((ViewControllerInterface *) clazz->interface)->viewWillAppear = viewWillAppear;
  ((ViewControllerInterface *) clazz->interface)->viewWillDisappear = viewWillDisappear;
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
