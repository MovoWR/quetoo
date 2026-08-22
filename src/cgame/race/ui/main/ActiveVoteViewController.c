/*
 * Copyright(c) 2026 Quetoo Race Module.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"
#include "cg_race_vote.h"

#include "ActiveVoteViewController.h"

#define _Class _ActiveVoteViewController

static void didClickVote(Button *button) {
  cgi.Cbuf(button->delegate.data);
}

static void setButtonEnabled(Button *button, const bool enabled) {
  Control *control = (Control *) button;
  const unsigned int oldState = control->state;
  if (enabled) {
    control->state &= ~ControlStateDisabled;
  } else {
    View *view = (View *) control;
    if ($(view, isKeyResponder)) {
      $(view, resignKeyResponder);
    }
    if ($(view, isTouchResponder)) {
      $(view, resignTouchResponder);
    }
    control->state |= ControlStateDisabled;
    control->state &= ~(ControlStateFocused | ControlStateHighlighted);
  }
  if (oldState != control->state) {
    $(control, stateDidChange);
  }
}

static void loadView(ViewController *self) {
  super(ViewController, self, loadView);
  ActiveVoteViewController *this = (ActiveVoteViewController *) self;
  View *view = $$(View, viewWithResourceName,
                 "ui/main/ActiveVoteViewController.json", NULL);
  assert(view);
  view->stylesheet = $$(Stylesheet, stylesheetWithResourceName,
                       "ui/main/ActiveVoteViewController.css");
  assert(view->stylesheet);
  Outlet outlets[] = MakeOutlets(
    MakeOutlet("voteTitle", &this->title),
    MakeOutlet("voteDetail", &this->detail),
    MakeOutlet("voteTally", &this->tally),
    MakeOutlet("voteRemaining", &this->remaining),
    MakeOutlet("voteYes", &this->yesButton),
    MakeOutlet("voteNo", &this->noButton)
  );
  $(view, resolve, outlets);
  this->detail->text->colorEscapes = true;
  this->yesButton->delegate = (ButtonDelegate) {
    .self = self,
    .data = "race vote yes\n",
    .didClick = didClickVote
  };
  this->noButton->delegate = (ButtonDelegate) {
    .self = self,
    .data = "race vote no\n",
    .didClick = didClickVote
  };
  $(self, setView, view);
  release(view);
}

static bool refresh(ActiveVoteViewController *self) {
  cg_race_vote_info_t info;
  if (!Cg_RaceVote_ParseInfo(cgi.ConfigString(CS_RACE_VOTE_INFO), &info)) {
    return false;
  }
  const cg_race_vote_client_state_t clientState = Cg_RaceVote_ClientState(
    cgi.client ? &cgi.client->frame.ps : NULL);
  const bool canCast = info.remaining > 0 &&
    (!clientState.authoritative || clientState.can_cast);
  char title[160];
  if (!q_strcmp(info.type, "kick")) {
    q_snprintf(title, sizeof(title), "%s proposes kicking %s",
               info.initiator, info.target);
  } else if (!q_strcmp(info.type, "map")) {
    q_snprintf(title, sizeof(title), "%s proposes changing map to %s",
               info.initiator, info.target);
  } else if (!q_strcmp(info.type, "physics")) {
    q_snprintf(title, sizeof(title), "%s proposes physics mode %s",
               info.initiator, info.target);
  } else {
    q_snprintf(title, sizeof(title), "%s called a %s vote: %s",
               info.initiator, info.type, info.target);
  }
  $(self->title->text, setText, canCast ? "ACTIVE VOTE" : "ACTIVE VOTE · VIEW ONLY");
  $(self->detail->text, setText, title);
  $(self->tally->text, setText,
    va("YES  %d     NO  %d     NEED  %d", info.yes_votes,
       info.no_votes, info.needed));
  $(self->remaining->text, setText, va("%ds", info.remaining));
  setButtonEnabled(self->yesButton, canCast);
  setButtonEnabled(self->noButton, canCast);
  self->viewController.view->needsLayout = true;
  return true;
}

static void initialize(Class *clazz) {
  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
  ((ActiveVoteViewControllerInterface *) clazz->interface)->refresh = refresh;
}

Class *_ActiveVoteViewController(void) {
  static Class *clazz;
  static Once once;
  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "ActiveVoteViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(ActiveVoteViewController),
      .interfaceOffset = offsetof(ActiveVoteViewController, interface),
      .interfaceSize = sizeof(ActiveVoteViewControllerInterface),
      .initialize = initialize,
    });
  });
  return clazz;
}

#undef _Class
