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

#include "MainViewController.h"

#include "AdminViewController.h"
#include "ControlsViewController.h"
#include "CreditsViewController.h"
#include "HomeViewController.h"
#include "MapBrowserViewController.h"
#include "PlayViewController.h"
#include "SettingsViewController.h"
#include "VotingViewController.h"

#include "DialogViewController.h"

#define _Class _MainViewController

static MainViewController *activeMainViewController;

#pragma mark - Delegates

/**
 * @brief ButtonDelegate for menu navigation.
 */
static void didClickNavigateViewController(Button *button) {

  MainViewController *this = button->delegate.self;
  Class *clazz = button->delegate.data;

  if (clazz) {
    $(this, navigateToViewController, clazz);
  } else {
    Cg_Warn("Menu item does not provide a ViewController class\n");
  }
}

/**
 * @brief Returns input to the live game.
 */
static void didClickResume(Button *button) {
  cgi.SetKeyDest(KEY_GAME);
}

/**
 * @brief Toggles the quick-settings drawer.
 */
static void didClickQuickSettings(Button *button) {

  MainViewController *this = button->delegate.self;
  $(this, setQuickSettingsVisible, this->mainView->quickSettingsHost->hidden);
}

/**
 * @brief Sets or clears a Control state flag, notifying only on a change.
 */
static void setControlFlag(Control *control, unsigned int flag, bool set) {

  if (control == NULL) {
    return;
  }

  const unsigned int state = control->state;
  if (set) {
    control->state |= flag;
  } else {
    control->state &= ~flag;
  }

  if (state != control->state) {
    $(control, stateDidChange);
  }
}

/**
 * @brief Shows or hides the footer's commit pair and repaints its count.
 */
static void refreshCommitChrome(MainViewController *this,
                                const char *status, bool warn);

/**
 * @brief ButtonDelegate for the footer's Apply.
 */
static void didClickApplyChanges(Button *button) {

  MainViewController *this = button->delegate.self;

  if (this->commitDelegate.didApply) {
    this->commitDelegate.didApply(this->commitDelegate.self);
  }
}

/**
 * @brief ButtonDelegate for the footer's Revert.
 */
static void didClickRevertChanges(Button *button) {

  MainViewController *this = button->delegate.self;

  if (this->commitDelegate.didRevert) {
    this->commitDelegate.didRevert(this->commitDelegate.self);
  }
}

/**
 * @brief Opens the releases page.
 */
static void openReleasesPage(ident data) {
  SDL_OpenURL(QUETOO_RELEASES_URL);
}

/**
 * @brief Quit the game.
 */
static void quit(ident data) {
  cgi.Cbuf("quit\n");
}

/**
 * @brief Raises the Quit confirmation.
 * @details Reached from the footer button and from the tier-1 drawer's foot,
 * so it takes the controller rather than the Button that happened to send it.
 */
static void confirmQuit(MainViewController *this) {

  const Dialog dialog = {
    .message = "Are you sure you want to quit to the desktop?",
    .ok = "Quit",
    .cancel = "Cancel",
    .okFunction = quit
  };

  ViewController *viewController = (ViewController *) $(alloc(DialogViewController), initWithDialog, &dialog);
  $((ViewController *) this, addChildViewController, viewController);

  View *confirmButton = $(viewController->view, descendantWithIdentifier, "ok");
  if (confirmButton) {
    $(confirmButton, addClassName, "dangerButton");
  }

}

/**
 * @brief Disconnect from the current game.
 */
static void disconnect(ident data) {
  cgi.Cbuf("disconnect\n");
}

/**
 * @brief Raises the Disconnect confirmation.
 * @details Reached from the footer button and from the tier-1 drawer's foot.
 */
static void confirmDisconnect(MainViewController *this) {

  const Dialog dialog = {
    .message = "Disconnect from the current server?",
    .ok = "Disconnect",
    .cancel = "Cancel",
    .okFunction = disconnect
  };

  ViewController *viewController = (ViewController *) $(alloc(DialogViewController), initWithDialog, &dialog);
  $((ViewController *) this, addChildViewController, viewController);

  View *confirmButton = $(viewController->view, descendantWithIdentifier, "ok");
  if (confirmButton) {
    $(confirmButton, addClassName, "dangerButton");
  }

}

/**
 * @brief ButtonDelegate for Quit confirmation.
 */
static void didClickQuit(Button *button) {
  confirmQuit(button->delegate.self);
}

/**
 * @brief ButtonDelegate for Disconnect.
 */
static void didClickDisconnect(Button *button) {
  confirmDisconnect(button->delegate.self);
}

/**
 * @brief QuickSettingsViewControllerDelegate for every tier-1 drawer item.
 * @details The drawer reports the chosen item and does none of them itself, so
 * each arm here is the shell action the tier-2 chrome already offers - which is
 * what keeps Disconnect and Quit behind the same confirmation dialogs as their
 * footer buttons rather than growing a second, quieter path to the same two
 * irreversible things.
 *
 * Every arm closes the drawer first. `Resume` hands input back to the game,
 * which the shell's own Escape handling would otherwise fight over; `Full menu`
 * closes it and stops, because the tier-2 menu is already behind the drawer.
 */
static void didSelectQuickSettingsItem(ident self, QuickSettingsItem item) {

  MainViewController *this = self;

  $(this, setQuickSettingsVisible, false);

  switch (item) {
    case QuickSettingsItemResume:
      cgi.SetKeyDest(KEY_GAME);
      break;
    case QuickSettingsItemRestartRun:
      cgi.Cbuf("kill\n");
      cgi.SetKeyDest(KEY_GAME);
      break;
    case QuickSettingsItemWatchBest:
      cgi.Cbuf("replay pb\n");
      cgi.SetKeyDest(KEY_GAME);
      break;
    case QuickSettingsItemSpectate:
      cgi.Cbuf("mode spectator\n");
      cgi.SetKeyDest(KEY_GAME);
      break;
    case QuickSettingsItemCallVote:
      $(this, navigateToViewController, _VotingViewController());
      break;
    case QuickSettingsItemFullMenu:
      break;
    case QuickSettingsItemDisconnect:
      confirmDisconnect(this);
      break;
    case QuickSettingsItemQuit:
      confirmQuit(this);
      break;
    default:
      break;
  }
}

#pragma mark - Object

/**
 * @see Object::dealloc(Object *)
 */
static void dealloc(Object *self) {

  MainViewController *this = (MainViewController *) self;

  if (activeMainViewController == this) {
    activeMainViewController = NULL;
  }

  release(this->mainView);
  release(this->navigationViewController);
  release(this->adminButton);
  release(this->activeVoteViewController);
  release(this->quickSettingsViewController);
  release(this->quickSettingsButton);
  release(this->resumeButton);
  release(this->homeButton);
  release(this->disconnectButton);
  release(this->applyButton);
  release(this->revertButton);

  super(Object, self, dealloc);
}

/**
 * @brief Returns true when a View and all of its ancestors are visible.
 */
static bool isVisibleControl(View *view) {

  for (View *ancestor = view; ancestor; ancestor = ancestor->superview) {
    if (ancestor->hidden) {
      return false;
    }
  }

  return $((Object *) view, isKindOfClass, _Control()) &&
         !$((Object *) view, isKindOfClass, _ScrollView()) &&
         !(((Control *) view)->state & ControlStateDisabled);
}

/**
 * @brief Returns the first visible enabled control in source order.
 */
static View *firstEnabledControl(View *view) {

  if (view->hidden) {
    return NULL;
  }
  if (isVisibleControl(view)) {
    return view;
  }

  const Array *subviews = (Array *) view->subviews;
  for (size_t i = 0; i < subviews->count; i++) {
    View *control = firstEnabledControl(subviews->elements[i]);
    if (control) {
      return control;
    }
  }

  return NULL;
}

/**
 * @brief Restores the previous focused control, or the route's first enabled control.
 */
static void restoreFocusedControl(MainViewController *this) {

  View *target = NULL;
  if (this->focusedIdentifier[0]) {
    target = $((View *) this->mainView, descendantWithIdentifier, this->focusedIdentifier);
    if (target && !isVisibleControl(target)) {
      target = NULL;
    }
  }

  if (target == NULL) {
    ViewController *top = $(this->navigationViewController, topViewController);
    target = top ? firstEnabledControl(top->view) : NULL;
  }
  if (target) {
    $(target, becomeKeyResponder);
    MainView_RevealView(target);
  }
}

/**
 * @brief Reflows the Race shell after ObjectivelyMVC has applied new raw-window geometry.
 * @details WindowController::setWindow sizes the root View in SDL window points before
 * this controller receives the event. Race only needs to invalidate its cached route
 * measurement, settle the responsive document, and recover the responder that setWindow
 * cleared; using the renderer's draw-scaled context here would mix coordinate systems.
 */
static void relayoutForViewportChange(MainViewController *this) {

  MainView_InvalidatePageSize(this->mainView);

  View *view = (View *) this->mainView;
  view->needsLayout = true;
  $(view, layoutIfNeeded);

  if (*cgi.state == CL_ACTIVE && cgi.GetKeyDest() == KEY_UI) {
    restoreFocusedControl(this);
  }
}

/**
 * @brief Resets only explicit page ScrollViews when entering a new route.
 */
static void resetScrollViews(View *view) {

  if ($((Object *) view, isKindOfClass, _ScrollView()) &&
      $(view, hasClassName, "esc-page-scroll")) {
    $((ScrollView *) view, scrollToOffset, &MakePoint(0, 0));
  }
  if (view->superview && $((Object *) view->superview, isKindOfClass, _ScrollView()) &&
      $(view->superview, hasClassName, "esc-page-scroll")) {
    $((ScrollView *) view->superview, scrollToOffset, &MakePoint(0, 0));
  }

  const Array *subviews = (Array *) view->subviews;
  for (size_t i = 0; i < subviews->count; i++) {
    resetScrollViews(subviews->elements[i]);
  }
}

/**
 * @brief Discards page scroll wrappers left behind by a route that has been
 * popped.
 * @details MainView_WrapScrollContent does not nest a ScrollView under a route;
 * it `replaceSubview`s the route with one and takes the route inside it. The
 * navigation host's child is then the wrapper, and the route is the wrapper's.
 * NavigationViewController::popViewController removes the route's own view from
 * *its* superview - the wrapper - so the wrapper itself is never removed and
 * stays a child of the navigation host for the rest of the session.
 *
 * An orphaned wrapper is not inert. It is still sized to the viewport by
 * MainView::sizeNavigation and still holds the content height its old route
 * had, so ScrollView::layoutSubviews keeps deciding `contentSize.h > bounds.h`
 * and paints a ScrollBar down the right edge of whatever route is on screen
 * now - a bar with nothing behind it, which is the one players could see and
 * could not use.
 *
 * The worse half is MainView::navigationPage: it answers with the first
 * unhidden child of the navigation host, and an orphan is unhidden, so it is
 * taken for the live page. Everything keyed off that - `preparePageScrollViews`
 * above all - then runs against the orphan, and the route actually on screen
 * never gets a page ScrollView installed at all. That is why Settings drew a
 * 785-tall document inside a 717-tall pane with no way to reach the last 68
 * points of it.
 *
 * Emptiness is read off the wrapper rather than off a bookkeeping flag: a
 * ScrollView retains its contentView, so the pointer outlives the removal and
 * only the parent link tells the truth.
 */
static void removeStaleScrollWrappers(const MainViewController *self) {

  View *host = ((ViewController *) self->navigationViewController)->view;

  const Array *subviews = (Array *) host->subviews;
  for (ssize_t i = (ssize_t) subviews->count - 1; i >= 0; i--) {

    View *subview = subviews->elements[i];
    if (!$((Object *) subview, isKindOfClass, _ScrollView())) {
      continue;
    }

    const ScrollView *scrollView = (ScrollView *) subview;
    if (scrollView->contentView == NULL ||
        scrollView->contentView->superview != subview) {
      $(host, removeSubview, subview);
    }
  }
}

#pragma mark - ViewController

/**
 * @see ViewController::loadView(ViewController *)
 */
static void loadView(ViewController *self) {

  super(ViewController, self, loadView);

  MainViewController *this = (MainViewController *) self;

  this->mainView = $(alloc(MainView), initWithFrame, NULL);
  assert(this->mainView);

  $(self, setView, (View *) this->mainView);

  $(this, primaryButton, "Home", &(const ButtonDelegate) {
    .didClick = didClickNavigateViewController,
    .self = self,
    .data = _HomeViewController()
  });

  $(this, primaryButton, "Play", &(const ButtonDelegate) {
    .didClick = didClickNavigateViewController,
    .self = self,
    .data = _PlayViewController()
  });

  $(this, primaryButton, "Controls", &(const ButtonDelegate) {
    .didClick = didClickNavigateViewController,
    .self = self,
    .data = _ControlsViewController()
  });

  $(this, primaryButton, "Settings", &(const ButtonDelegate) {
    .didClick = didClickNavigateViewController,
    .self = self,
    .data = _SettingsViewController()
  });

  $(this, primaryButton, "Maps", &(const ButtonDelegate) {
    .didClick = didClickNavigateViewController,
    .self = self,
    .data = _MapBrowserViewController()
  });

  $(this, primaryButton, "Credits", &(const ButtonDelegate) {
    .didClick = didClickNavigateViewController,
    .self = self,
    .data = _CreditsViewController()
  });

  $(this, primaryButton, "Admin", &(const ButtonDelegate) {
    .didClick = didClickNavigateViewController,
    .self = self,
    .data = _AdminViewController()
  });

  $(this, secondaryButton, "Quick menu", &(const ButtonDelegate) {
    .didClick = didClickQuickSettings,
    .self = self
  });

  $(this, secondaryButton, "Resume", &(const ButtonDelegate) {
    .didClick = didClickResume,
    .self = self
  });

  $(this, secondaryButton, "Revert", &(const ButtonDelegate) {
    .didClick = didClickRevertChanges,
    .self = self
  });

  $(this, secondaryButton, "Apply", &(const ButtonDelegate) {
    .didClick = didClickApplyChanges,
    .self = self
  });

  $(this, secondaryButton, "Disconnect", &(const ButtonDelegate) {
    .didClick = didClickDisconnect,
    .self = self
  });

  $(this, secondaryButton, "Quit", &(const ButtonDelegate) {
    .didClick = didClickQuit,
    .self = self
  });

  $(self, addChildViewController, (ViewController *) this->navigationViewController);
  $(this->mainView->contentView, addSubview, this->navigationViewController->viewController.view);

  this->activeVoteViewController = (ActiveVoteViewController *)
    $((ViewController *) alloc(ActiveVoteViewController), init);
  assert(this->activeVoteViewController);
  $(self, addChildViewController, (ViewController *) this->activeVoteViewController);
  $(this->mainView->activeVoteHost,
    addSubview, this->activeVoteViewController->viewController.view);

  this->quickSettingsViewController = (QuickSettingsViewController *)
    $((ViewController *) alloc(QuickSettingsViewController), init);
  assert(this->quickSettingsViewController);
  this->quickSettingsViewController->delegate = (QuickSettingsViewControllerDelegate) {
    .self = self,
    .didSelectItem = didSelectQuickSettingsItem
  };
  $(self, addChildViewController, (ViewController *) this->quickSettingsViewController);
  $(this->mainView->quickSettingsHost,
    addSubview, this->quickSettingsViewController->viewController.view);

  $(this, navigateToViewController, _HomeViewController());

}

/**
 * @brief Updates the Admin button visibility and dismisses the admin panel if the player is no longer an admin.
 */
static void refreshAdminCapabilities(MainViewController *this,
                                     uint32_t capabilities) {

  if (this->adminButton) {

    // The tab is offered only to a session that holds something. A session that
    // *loses* everything while the route is open is not evicted, though: the
    // design answers that case with its own dismissal - "Admin route hidden",
    // naming the 0x00 mask and where `radmin` lives - and being thrown back to
    // Home explains nothing about why the route went away.
    this->adminButton->control.view.hidden = capabilities == 0;
  }
}

static void refreshAdminButton(MainViewController *this) {
  const uint32_t capabilities = cgi.client
    ? (uint16_t) cgi.client->frame.ps.stats[STAT_RACE_ADMIN_CAPABILITIES]
    : 0;
  refreshAdminCapabilities(this, capabilities);
}

/**
 * @brief Removes modal dialogs from a ViewController subtree.
 */
static void dismissDialogs(ViewController *parent) {

  Array *children = parent->childViewControllers;
  for (size_t i = children->count; i > 0; i--) {
    ViewController *child = children->elements[i - 1];
    if ($((Object *) child, isKindOfClass, _DialogViewController())) {
      $(child, removeFromParentViewController);
    } else {
      dismissDialogs(child);
    }
  }
}

/**
 * @brief Keeps player-dependent actions synchronized without rebuilding the roster.
 */
static void refreshEscState(MainViewController *this) {

  const bool menuActive = *cgi.state == CL_ACTIVE && cgi.GetKeyDest() == KEY_UI;
  if (!menuActive) {
    this->menuActive = false;
    dismissDialogs((ViewController *) this);
    $(this, setQuickSettingsVisible, false);
    return;
  }

  // MainView invokes this during draw on the stock host. Rebuilding table rows
  // here would happen after ObjectivelyMVC's theme and layout passes, leaving
  // those fresh rows unstyled for the frame and repeating the problem forever.
  // Roster messages already refresh Home directly; only the closed -> open
  // transition needs the shell-wide refresh and its deterministic Home route.
  if (!this->menuActive) {
    $(this, refreshRoster);
  } else {
    HomeViewController_RefreshPlayerActions(
      cgi.client ? &cgi.client->frame.ps : NULL);
  }
}

/**
 * @brief Keeps the active ESC shell and Home roster synchronized with live state.
 */
static void refreshRoster(MainViewController *this) {

  const bool isActive = *cgi.state == CL_ACTIVE;
  const bool menuActive = isActive && cgi.GetKeyDest() == KEY_UI;

  if (this->resumeButton) {
    this->resumeButton->control.view.hidden = !isActive;
  }
  if (this->quickSettingsButton) {
    this->quickSettingsButton->control.view.hidden = !isActive;
  }
  if (this->disconnectButton) {
    this->disconnectButton->control.view.hidden = !isActive;
  }

  View *primaryMenu = (View *) this->mainView->primaryMenu;
  View *topActions = (View *) this->mainView->topActions;
  View *secondaryMenu = (View *) this->mainView->secondaryMenu;
  primaryMenu->needsLayout = true;
  topActions->needsLayout = true;
  secondaryMenu->needsLayout = true;
  $(primaryMenu, layoutIfNeeded);
  $(topActions, layoutIfNeeded);
  $(secondaryMenu, layoutIfNeeded);

  $((View *) this->mainView, updateBindings);

  if (menuActive && !this->menuActive) {

    $(this, navigateToViewController, _HomeViewController());

    // The design opens the tier-1 drawer here, on the KEY_GAME -> KEY_UI
    // transition Escape causes - the engine consumes the key itself, so the
    // transition is the only hook available. That is not done: a drawer over
    // every entry to the menu reads as the menu being blocked, because the
    // scrim covers the tier-2 routes until it is dismissed. The drawer is
    // opened deliberately, from Quick menu in the header, instead.
    if (this->resumeButton) {
      $((View *) this->resumeButton, becomeKeyResponder);
    }
  }

  this->menuActive = menuActive;
  HomeViewController_Refresh();
  $(this, refreshVote);
}

/**
 * @brief Refreshes the global active-vote surface from its existing configstring.
 */
static void refreshVote(MainViewController *this) {

  ViewController *topViewController =
    $(this->navigationViewController, topViewController);
  const bool votingRoute = topViewController &&
    $((Object *) topViewController, isKindOfClass, _VotingViewController());
  const bool hasVote = *cgi.state == CL_ACTIVE &&
    $(this->activeVoteViewController, refresh);
  const bool visible = hasVote && !votingRoute;
  const bool changed = this->mainView->activeVoteHost->hidden == visible;

  this->mainView->activeVoteHost->hidden = !visible;
  if (changed) {
    View *view = (View *) this->mainView;
    view->needsLayout = true;
    $(view, layoutIfNeeded);
  }
}

/**
 * @brief The selected route's name, for the drawer's `<Route> - Esc resumes`.
 * @details Read back from the route strip rather than tracked separately, so
 * the drawer cannot name a route the strip is not showing. Falls back to the
 * design's own word for the drawer when nothing is selected yet.
 */
static const char *currentRouteName(const MainViewController *this) {

  for (size_t i = 0; i < this->numRouteButtons; i++) {

    const Control *control = (const Control *) this->routeButtons[i];
    if (control->state & ControlStateSelected) {
      const char *title = this->routeButtons[i]->title->text;
      if (title && *title) {
        return title;
      }
    }
  }

  return "Menu";
}

/**
 * @brief Opens or closes the shell-owned quick-settings drawer.
 */
static void setQuickSettingsVisible(MainViewController *this, bool visible) {

  visible = visible && *cgi.state == CL_ACTIVE && cgi.GetKeyDest() == KEY_UI;
  this->mainView->quickSettingsHost->hidden = !visible;

  if (this->quickSettingsButton) {
    Control *control = (Control *) this->quickSettingsButton;
    const unsigned int oldState = control->state;
    if (visible) {
      control->state |= ControlStateSelected;
    } else {
      control->state &= ~ControlStateSelected;
    }
    if (oldState != control->state) {
      $(control, stateDidChange);
    }
  }

  if (visible) {
    // The drawer names the route it was opened over, and resolves its key hints
    // from the player's live bindings - both can change while it is closed.
    $(this->quickSettingsViewController, setRouteName, currentRouteName(this));
    $(this->quickSettingsViewController, refresh);
    $((View *) this->quickSettingsViewController->items[QuickSettingsItemResume],
      becomeKeyResponder);
  }

  View *view = (View *) this->mainView;
  view->needsLayout = true;
  $(view, layoutIfNeeded);
}

/**
 * @see ViewController::viewWillAppear(ViewController *)
 */
static void viewWillAppear(ViewController *self) {

  super(ViewController, self, viewWillAppear);

  refreshAdminButton((MainViewController *) self);
  refreshRoster((MainViewController *) self);

  MainViewController *this = (MainViewController *) self;
  if (this->updateAvailable) {
    this->updateAvailable = false;

    const Dialog dialog = {
      .message = "A new version of Quetoo is available. Download now?",
      .ok = "Yes",
      .cancel = "No",
      .okFunction = openReleasesPage
    };

    ViewController *viewController = (ViewController *)
      $(alloc(DialogViewController), initWithDialog, &dialog);
    $(self, addChildViewController, viewController);
  }
}

/**
 * @see ViewController::viewWillDisappear(ViewController *)
 */
static void viewWillDisappear(ViewController *self) {

  MainViewController *this = (MainViewController *) self;
  this->menuActive = false;
  dismissDialogs(self);
  $(this, setQuickSettingsVisible, false);

  super(ViewController, self, viewWillDisappear);
}

/**
 * @see ViewController::respondToEvent(ViewController *, const SDL_Event *)
 */
static void respondToEvent(ViewController *self, const SDL_Event *event) {

  super(ViewController, self, respondToEvent, event);

  MainViewController *this = (MainViewController *) self;

  if (event->type == MVC_VIEW_EVENT && event->user.code == ViewEventFocus) {
    View *focused = event->user.data1;
    if (focused && focused->identifier) {
      q_strlcpy(this->focusedIdentifier, focused->identifier,
                sizeof(this->focusedIdentifier));
    }
    MainView_RevealView(focused);
  } else if (event->type == SDL_EVENT_WINDOW_EXPOSED ||
             event->type == SDL_EVENT_WINDOW_MOVED ||
             event->type == SDL_EVENT_WINDOW_RESIZED ||
             event->type == SDL_EVENT_WINDOW_MAXIMIZED ||
             event->type == SDL_EVENT_WINDOW_RESTORED ||
             event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
             event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
             event->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
             event->type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED) {
    relayoutForViewportChange(this);
  }
}

#pragma mark - MainViewController

/**
 * @fn MainViewController *MainViewController::init(MainViewController *self)
 * @memberof MainViewController
 */
static MainViewController *init(MainViewController *self) {

  self = (MainViewController *) super(ViewController, self, init);
  if (self) {
    self->navigationViewController = $(alloc(NavigationViewController), init);
    assert(self->navigationViewController);
    activeMainViewController = self;
  }
  return self;
}

/**
 * @brief Synchronizes the selected route control and centered-window title.
 */
static void selectRoute(MainViewController *self, Class *clazz) {

  bool onStrip = false;

  for (size_t i = 0; i < self->numRouteButtons; i++) {
    Control *control = (Control *) self->routeButtons[i];
    View *view = (View *) control;
    const unsigned int oldState = control->state;
    if (self->routeClasses[i] == clazz) {
      control->state |= ControlStateSelected;
      $(view, addClassName, "activeRoute");
      $(self->mainView->windowTitle->text, setText,
        self->routeButtons[i]->title->text);
      onStrip = true;

      // The design gives Home a lockup where every other route takes the plain
      // route title, and the two are alternatives rather than a stack.
      const bool lockup = clazz == _HomeViewController();
      if (self->mainView->windowLockup) {
        if (lockup && self->mainView->windowLockup->image == NULL) {
          $(self->mainView->windowLockup, setImageWithResourceName,
            "ui/main/menu_lockup.png");
        }
        self->mainView->windowLockup->view.hidden = !lockup;
      }
      self->mainView->windowTitle->view.hidden = lockup;
    } else {
      control->state &= ~ControlStateSelected;
      $(view, removeClassName, "activeRoute");
    }
    if (oldState != control->state) {
      $(control, stateDidChange);
    }
  }

  // A route that is not on the strip still has to name itself. The design ships
  // seven tabs and reaches voting from the tier-1 drawer and from Maps instead,
  // so without this the header would keep whatever route the player came from -
  // "Credits" over a ballot.
  if (!onStrip) {

    self->mainView->windowTitle->view.hidden = false;
    if (self->mainView->windowLockup) {
      self->mainView->windowLockup->view.hidden = true;
    }

    $(self->mainView->windowTitle->text, setText,
      clazz == _VotingViewController() ? "Voting" : "Race");
  }
}

/**
 * @brief Names the Play route's tabs the way the design names its pages.
 * @details Play is the one tabbed route, and TabViewItem takes its label from
 * the identifier of the view it wraps - which is also the `#Join` / `#Create` /
 * `#Player` selector each of those pages is styled by. The design's strip reads
 * "Join server / Create server / Runner", so the label is set here and the
 * identifiers are left alone: renaming them would rewrite three stylesheets to
 * change three words, and an identifier with a space in it is not addressable
 * as a CSS id at all.
 * @remarks Done from the shell rather than from PlayViewController because that
 * one is common's, shared with default, ctf and lithium - all of which keep the
 * framework's own wording.
 */
static void retitlePlayTabs(PlayViewController *playViewController) {

  static const struct {
    const char *identifier;
    const char *title;
  } titles[] = {
    { "Join", "Join server" },
    { "Create", "Create server" },
    { "Player", "Runner" },
  };

  // Both halves are built in PlayViewController::loadView, which the caller has
  // already forced by touching the route's view - the guard is for the order
  // changing underneath this, not for the order today.
  if (playViewController->tabViewController == NULL ||
      playViewController->tabViewController->tabView == NULL) {
    return;
  }

  TabView *tabView = playViewController->tabViewController->tabView;

  for (size_t i = 0; i < lengthof(titles); i++) {

    TabViewItem *tab = $(tabView, tabWithIdentifier, titles[i].identifier);
    if (tab) {
      $(tab->label->text, setText, titles[i].title);
    }
  }
}

/**
 * @fn void MainViewController::navigateToViewController(MainViewController *self, Class *clazz)
 * @memberof MainViewController
 */
static void navigateToViewController(MainViewController *self, Class *clazz) {

  assert(clazz);

  $(self, setQuickSettingsVisible, false);

  // The outgoing route's viewWillDisappear also clears this; doing it here as
  // well is what makes a stale delegate impossible rather than merely unlikely.
  self->commitDelegate = (RaceCommitDelegate) { 0 };
  refreshCommitChrome(self, NULL, false);

  selectRoute(self, clazz);

  ViewController *topViewController = $(self->navigationViewController, topViewController);
  if (topViewController && $((Object *) topViewController, isKindOfClass, clazz)) {
    return;
  }

  self->focusedIdentifier[0] = '\0';

  $(self->navigationViewController, popToRootViewController);
  $(self->navigationViewController, popViewController);

  // Before the new route is pushed, so that MainView::navigationPage cannot be
  // answered by the wrapper the popped one left behind.
  removeStaleScrollWrappers(self);

  ViewController *viewController = $((ViewController *) _alloc(clazz), init);
  $(self->navigationViewController, pushViewController, viewController);

  if (clazz == _PlayViewController()) {
    retitlePlayTabs((PlayViewController *) viewController);
  }

  // Column containers are authored as plain StackViews and only become a
  // wrapping ColumnsView once promoted; an unpromoted one keeps StackView's
  // default vertical axis and silently stacks a route's sections instead of
  // flowing them. Promote here, at the one boundary every route passes through,
  // rather than leaving it to the size cache in MainView::pageSize - that gate
  // can be answered by a stale page and skip the route just pushed.
  MainView_PrepareColumnsViews(viewController->view);

  MainView_InvalidatePageSize(self->mainView);
  resetScrollViews(viewController->view);

  View *mainView = (View *) self->mainView;
  mainView->needsLayout = true;
  $(mainView, layoutIfNeeded);

  release(viewController);
}

/**
 * @fn void MainViewController::primaryButton(MainViewController *self, const char *title, const ButtonDelegate *delegate)
 * @memberof MainViewController
 */
static void primaryButton(MainViewController *self, const char *title, const ButtonDelegate *delegate) {

  Button *button = $(alloc(Button), initWithTitle, title);
  assert(button);

  button->control.view.identifier = q_strdup(va("route_%s", title));
  assert(button->control.view.identifier);
  button->delegate = *delegate;

  assert(self->numRouteButtons < MAIN_VIEW_CONTROLLER_MAX_ROUTES);
  self->routeButtons[self->numRouteButtons] = button;
  self->routeClasses[self->numRouteButtons] = delegate->data;
  self->numRouteButtons++;

  if (strcmp(title, "Home") == 0) {
    self->homeButton = (Button *) retain(button);
  } else if (strcmp(title, "Admin") == 0) {
    self->adminButton = (Button *) retain(button);
    self->adminButton->control.view.hidden = true;
  }

  $((View *) self->mainView->primaryMenu, addSubview, (View *) button);
  release(button);
}

/**
 * @fn void MainViewController::secondaryButton(MainViewController *self, const char *title, const ButtonDelegate *delegate)
 * @memberof MainViewController
 */
static void secondaryButton(MainViewController *self, const char *title, const ButtonDelegate *delegate) {

  Button *button = $(alloc(Button), initWithTitle, title);
  assert(button);

  button->control.view.identifier = q_strdup(title);
  assert(button->control.view.identifier);
  button->delegate = *delegate;
  // Resume is the one filled action in the bars. Quick menu and Disconnect
  // are ghosts, and danger is reserved for Quit alone.
  if (strcmp(title, "Quick menu") == 0) {
    self->quickSettingsButton = (Button *) retain(button);
    $((View *) button, addClassName, "ghostAction");
    $((View *) self->mainView->topActions, addSubview, (View *) button);
  } else if (strcmp(title, "Resume") == 0) {
    self->resumeButton = (Button *) retain(button);
    $((View *) button, addClassName, "primaryAction");
    $((View *) self->mainView->topActions, addSubview, (View *) button);
  } else if (strcmp(title, "Revert") == 0) {
    self->revertButton = (Button *) retain(button);
    $((View *) button, addClassName, "ghostAction");
    $((View *) button, addClassName, "commitAction");
    $((View *) self->mainView->secondaryMenu, addSubview, (View *) button);
  } else if (strcmp(title, "Apply") == 0) {
    self->applyButton = (Button *) retain(button);
    $((View *) button, addClassName, "primaryAction");
    $((View *) button, addClassName, "commitAction");
    $((View *) self->mainView->secondaryMenu, addSubview, (View *) button);
  } else {
    if (strcmp(title, "Disconnect") == 0) {
      self->disconnectButton = (Button *) retain(button);
      $((View *) button, addClassName, "ghostAction");
    } else {
      $((View *) button, addClassName, "dangerAction");
    }
    $((View *) self->mainView->secondaryMenu, addSubview, (View *) button);
  }

  release(button);
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
  ((ViewControllerInterface *) clazz->interface)->viewWillDisappear =
    viewWillDisappear;

  ((MainViewControllerInterface *) clazz->interface)->init = init;
  ((MainViewControllerInterface *) clazz->interface)->navigateToViewController = navigateToViewController;
  ((MainViewControllerInterface *) clazz->interface)->primaryButton = primaryButton;
  ((MainViewControllerInterface *) clazz->interface)->secondaryButton = secondaryButton;
  ((MainViewControllerInterface *) clazz->interface)->refreshAdminButton = refreshAdminButton;
  ((MainViewControllerInterface *) clazz->interface)->refreshEscState = refreshEscState;
  ((MainViewControllerInterface *) clazz->interface)->refreshRoster = refreshRoster;
  ((MainViewControllerInterface *) clazz->interface)->refreshVote = refreshVote;
  ((MainViewControllerInterface *) clazz->interface)->setQuickSettingsVisible = setQuickSettingsVisible;
}

/**
 * @fn Class *MainViewController::_MainViewController(void)
 * @memberof MainViewController
 */
Class *_MainViewController(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "MainViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(MainViewController),
      .interfaceOffset = offsetof(MainViewController, interface),
      .interfaceSize = sizeof(MainViewControllerInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

void MainViewController_RefreshAdmin(const player_state_t *ps) {
  if (activeMainViewController) {
    const uint32_t capabilities = ps
      ? (uint16_t) ps->stats[STAT_RACE_ADMIN_CAPABILITIES]
      : 0;
    refreshAdminCapabilities(activeMainViewController, capabilities);
  }
}

void MainViewController_ClearState(void) {
  if (activeMainViewController) {
    $(activeMainViewController, navigateToViewController,
      _HomeViewController());
  }
}

void MainViewController_RefreshEscState(void) {
  if (activeMainViewController) {
    $(activeMainViewController, refreshEscState);
  }
}

void MainViewController_CloseQuickSettings(void) {
  if (activeMainViewController) {
    $(activeMainViewController, setQuickSettingsVisible, false);
  }
}

void MainViewController_RefreshVote(void) {
  if (activeMainViewController) {
    $(activeMainViewController, refreshVote);
  }
}

static void refreshCommitChrome(MainViewController *this,
                                const char *status, bool warn) {

  const bool staged = this->commitDelegate.didApply != NULL;
  const bool dirty = staged && status && *status;

  if (this->applyButton) {
    this->applyButton->control.view.hidden = !staged;
    setControlFlag((Control *) this->applyButton, ControlStateDisabled, !dirty);
  }
  if (this->revertButton) {
    this->revertButton->control.view.hidden = !staged;
    setControlFlag((Control *) this->revertButton, ControlStateDisabled, !dirty);
  }

  Label *commitStatus = this->mainView ? this->mainView->commitStatus : NULL;
  if (commitStatus) {

    $(commitStatus->text, setText, dirty ? status : "");

    View *view = (View *) commitStatus;
    const bool wasWarn = $(view, hasClassName, "warn");
    if ((dirty && warn) != wasWarn) {
      if (dirty && warn) {
        $(view, addClassName, "warn");
      } else {
        $(view, removeClassName, "warn");
      }
    }
  }

  View *view = (View *) this->mainView;
  view->needsLayout = true;
  $(view, layoutIfNeeded);
}

void MainViewController_SetCommitDelegate(const RaceCommitDelegate *delegate) {

  if (activeMainViewController == NULL) {
    return;
  }

  activeMainViewController->commitDelegate = delegate
    ? *delegate
    : (RaceCommitDelegate) { 0 };

  refreshCommitChrome(activeMainViewController, NULL, false);
}

void MainViewController_SetCommitStatus(const char *status, bool warn) {

  if (activeMainViewController) {
    refreshCommitChrome(activeMainViewController, status, warn);
  }
}

void MainViewController_SetRouteEyebrow(const char *text, bool offline) {

  if (activeMainViewController == NULL ||
      activeMainViewController->mainView == NULL) {
    return;
  }

  MainView *mainView = activeMainViewController->mainView;

  q_strlcpy(mainView->eyebrowOverride, text ? text : "",
            sizeof(mainView->eyebrowOverride));
  mainView->eyebrowOffline = offline;

  View *view = (View *) mainView;
  view->needsLayout = true;
  $(view, layoutIfNeeded);
}

#undef _Class
