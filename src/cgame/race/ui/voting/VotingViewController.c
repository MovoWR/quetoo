/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 */

#include "cg_local.h"

#include <ObjectivelyMVC/Option.h>
#include <ObjectivelyMVC/Stylesheet.h>

#include "cg_race_physics.h"
#include "cg_race_vote.h"
#include "cg_score.h"
#include "race_map_state.h"
#include "race_physics.h"
#include "VotingViewController.h"

#define _Class _VotingViewController

static VotingViewController *activeVotingViewController;

static void refreshVotingState(VotingViewController *self,
                               const player_state_t *ps);

static void setLabelText(Label *label, const char *text) {
  if (label && label->text) {
    $(label->text, setText, text ? text : "");
  }
}

static void setButtonTitle(Button *button, const char *title) {
  if (button && button->title) {
    $(button->title, setText, title ? title : "");
  }
}

static void prepareControlForDisable(Control *control) {
  View *view = (View *) control;

  if ($(view, isKeyResponder)) {
    $(view, resignKeyResponder);
  }
  if ($(view, isTouchResponder)) {
    $(view, resignTouchResponder);
  }

  control->state &= ~(ControlStateFocused | ControlStateHighlighted);
}

static void setControlEnabled(Control *control, const bool enabled) {
  if (!control) {
    return;
  }

  const unsigned int oldState = control->state;
  if (enabled) {
    control->state &= ~ControlStateDisabled;
  } else {
    prepareControlForDisable(control);
    control->state |= ControlStateDisabled;
  }
  if (oldState != control->state) {
    $(control, stateDidChange);
  }
}

static void setViewHidden(View *view, const bool hidden) {
  if (view && view->hidden != hidden) {
    view->hidden = hidden;
    for (View *ancestor = view->superview; ancestor;
         ancestor = ancestor->superview) {
      ancestor->needsLayout = true;
    }
  }
}

static const char *textForTextView(const TextView *textView) {
  const String *text = textView ? (const String *) textView->attributedText
                                : NULL;
  return text ? text->chars : NULL;
}

static bool mapTargetValid(const char *input) {
  if (!input || !*input) {
    return false;
  }

  const size_t length = q_strlen(input);
  if (!length || length > RACE_MAP_IDENTITY_MAX || input[0] == '/' ||
      input[length - 1u] == '/') {
    return false;
  }

  size_t segmentStart = 0u;
  for (size_t i = 0u; i <= length; i++) {
    if (i < length) {
      const char c = input[i];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
            c == '/')) {
        return false;
      }
    }

    if (i == length || input[i] == '/') {
      const size_t segmentLength = i - segmentStart;
      if (!segmentLength ||
          (segmentLength == 1u && input[segmentStart] == '.') ||
          (segmentLength == 2u && input[segmentStart] == '.' &&
           input[segmentStart + 1u] == '.')) {
        return false;
      }
      segmentStart = i + 1u;
    }
  }
  return true;
}

static bool connected(void) {
  return *cgi.state == CL_ACTIVE && cgi.client != NULL;
}

static const char *rosterModeName(const cg_roster_group_t group) {
  switch (group) {
    case CG_ROSTER_PRACTICE_MODE:
      return "PRACTICE";
    case CG_ROSTER_SPECTATOR:
      return "SPECTATOR";
    default:
      return "RACE";
  }
}

static int compareRosterClient(const void *left, const void *right) {
  const cg_roster_entry_t *first = left;
  const cg_roster_entry_t *second = right;
  return (int) first->client - (int) second->client;
}

static uint64_t rosterSignature(const cg_roster_entry_t *entries,
                                const size_t count) {
  uint64_t hash = UINT64_C(1469598103934665603);
  for (size_t i = 0u; i < count; i++) {
    hash ^= entries[i].client;
    hash *= UINT64_C(1099511628211);
    hash ^= entries[i].group;
    hash *= UINT64_C(1099511628211);
    for (const unsigned char *c = (const unsigned char *) entries[i].name;
         *c; c++) {
      hash ^= *c;
      hash *= UINT64_C(1099511628211);
    }
  }
  hash ^= count;
  return hash;
}

static size_t otherPlayers(cg_roster_entry_t entries[MAX_CLIENTS]) {
  cg_roster_entry_t snapshot[MAX_CLIENTS];
  const size_t snapshotCount = Cg_RosterSnapshot(snapshot, lengthof(snapshot));
  const int32_t localClient = cgi.client ? cgi.client->frame.ps.client : -1;
  size_t count = 0u;
  for (size_t i = 0u; i < snapshotCount; i++) {
    if (snapshot[i].client != localClient) {
      entries[count++] = snapshot[i];
    }
  }
  qsort(entries, count, sizeof(*entries), compareRosterClient);
  return count;
}

static bool selectedKickCurrent(const VotingViewController *self) {
  if (self->selectedKickSlot < 0 || !*self->selectedKickName) {
    return false;
  }

  cg_roster_entry_t entries[MAX_CLIENTS];
  const size_t count = otherPlayers(entries);
  for (size_t i = 0u; i < count; i++) {
    if (entries[i].client == self->selectedKickSlot &&
        !strcmp(entries[i].name, self->selectedKickName)) {
      return true;
    }
  }
  return false;
}

static size_t refreshKickRoster(VotingViewController *self) {
  cg_roster_entry_t entries[MAX_CLIENTS];
  const size_t count = connected() ? otherPlayers(entries) : 0u;
  const uint64_t signature = rosterSignature(entries, count);
  if (signature == self->rosterSignature) {
    return count;
  }

  const int32_t previousSlot = self->selectedKickSlot;
  char previousName[sizeof(self->selectedKickName)];
  q_strlcpy(previousName, self->selectedKickName, sizeof(previousName));

  $(self->kickTarget, removeAllOptions);
  $(self->kickTarget, addOption,
    count ? "Choose a connected player" : "No other players connected",
    NULL);

  bool preserveSelection = false;
  for (size_t i = 0u; i < count; i++) {
    char title[112];
    q_snprintf(title, sizeof(title), "[%u] %s  ·  %s",
               entries[i].client, entries[i].name,
               rosterModeName(entries[i].group));
    $(self->kickTarget, addOption, title,
      (ident) (intptr_t) (entries[i].client + 1u));
    if ((int32_t) entries[i].client == previousSlot &&
        !strcmp(entries[i].name, previousName)) {
      preserveSelection = true;
    }
  }

  self->rosterSignature = signature;
  if (preserveSelection) {
    $(self->kickTarget, selectOptionWithValue,
      (ident) (intptr_t) (previousSlot + 1));
  } else {
    $(self->kickTarget, selectOptionWithValue, NULL);
    self->selectedKickSlot = -1;
    self->selectedKickName[0] = '\0';
  }
  return count;
}

static void selectKickTarget(Select *select, Option *option) {
  VotingViewController *self = select->delegate.self;
  self->feedback[0] = '\0';
  self->feedbackUntil = 0u;
  self->selectedKickSlot = option && option->value
    ? (int32_t) (intptr_t) option->value - 1
    : -1;
  self->selectedKickName[0] = '\0';

  if (self->selectedKickSlot >= 0) {
    cg_roster_entry_t entries[MAX_CLIENTS];
    const size_t count = otherPlayers(entries);
    for (size_t i = 0u; i < count; i++) {
      if (entries[i].client == self->selectedKickSlot) {
        q_strlcpy(self->selectedKickName, entries[i].name,
                  sizeof(self->selectedKickName));
        break;
      }
    }
  }
  refreshVotingState(self, cgi.client ? &cgi.client->frame.ps : NULL);
}

static void didEditMapTarget(TextView *textView) {
  VotingViewController *self = textView->delegate.self;
  self->feedback[0] = '\0';
  self->feedbackUntil = 0u;
  refreshVotingState(self, cgi.client ? &cgi.client->frame.ps : NULL);
}

static void setFeedback(VotingViewController *self, const char *message) {
  q_strlcpy(self->feedback, message ? message : "", sizeof(self->feedback));
  self->feedbackUntil = cgi.client && *self->feedback
    ? cgi.client->unclamped_time + 5000u
    : 0u;
  setLabelText(self->proposalStatus, self->feedback);
}

static void didClickBallot(Button *button) {
  VotingViewController *self = button->delegate.self;
  const char *command = button->delegate.data;
  if (command) {
    cgi.Cbuf(command);
    setFeedback(self, !q_strcmp(command, "race vote yes\n")
                        ? "YES ballot sent · awaiting authoritative tally"
                        : "NO ballot sent · awaiting authoritative tally");
  }
}

static void didClickPhysics(Button *button) {
  VotingViewController *self = button->delegate.self;
  const Option *option = $(self->physicsTarget, selectedOption);
  const char *physics = option ? option->value : NULL;
  if (physics) {
    cgi.Cbuf(va("race vote physics %s\n", physics));
    setFeedback(self, "Physics vote requested · check the live ballot or server response");
  }
}

static void didSelectPhysics(Select *select, Option *option) {
  VotingViewController *self = select->delegate.self;
  (void) option;
  refreshVotingState(self, cgi.client ? &cgi.client->frame.ps : NULL);
}

static void didClickMapVote(Button *button) {
  VotingViewController *self = button->delegate.self;
  const char *target = textForTextView(self->mapTarget);
  if (!mapTargetValid(target)) {
    setFeedback(self, "Map ID is invalid · use letters, numbers, dot, dash, underscore or slash");
    return;
  }
  cgi.Cbuf(va("race vote map %s\n", target));
  setFeedback(self, "Map vote requested · the server is validating catalog membership");
}

static void didClickKickVote(Button *button) {
  VotingViewController *self = button->delegate.self;
  if (!selectedKickCurrent(self)) {
    self->selectedKickSlot = -1;
    self->selectedKickName[0] = '\0';
    self->rosterSignature = 0u;
    refreshKickRoster(self);
    setFeedback(self, "Player selection changed · choose a connected player again");
    refreshVotingState(self, cgi.client ? &cgi.client->frame.ps : NULL);
    return;
  }
  cgi.Cbuf(va("race vote kick %d\n", self->selectedKickSlot));
  setFeedback(self, "Kick vote requested · exact client slot sent to the server");
}

static void didClickChoice(Button *button) {
  VotingViewController *self = button->delegate.self;
  const int32_t choice = (int32_t) (intptr_t) button->delegate.data;
  if (choice >= 1 && choice <= (int32_t) RACE_VOTE_MENU_MAX_CHOICES) {
    cgi.Cbuf(va("vote_menu %d\n", choice));
    setFeedback(self, "Next-map choice sent · tallies refresh from the server");
  }
}

static void didClickNominate(Button *button) {
  VotingViewController *self = button->delegate.self;
  const char *target = textForTextView(self->nominationTarget);
  if (!mapTargetValid(target)) {
    setFeedback(self, "Nomination is invalid · enter a canonical map ID from Maps");
    return;
  }
  cgi.Cbuf(va("nominate %s\n", target));
  setFeedback(self, "Nomination sent · the server will confirm catalog membership");
}

static const char *activeVoteHeadline(const cg_race_vote_info_t *info,
                                      char *output, const size_t outputSize) {
  if (!q_strcmp(info->type, "map")) {
    q_snprintf(output, outputSize, "Change map  ·  %s", info->target);
  } else if (!q_strcmp(info->type, "kick")) {
    q_snprintf(output, outputSize, "Remove player  ·  %s", info->target);
  } else if (!q_strcmp(info->type, "physics")) {
    q_snprintf(output, outputSize, "Change physics  ·  %s", info->target);
  } else {
    q_snprintf(output, outputSize, "%s  ·  %s", info->type, info->target);
  }
  return output;
}

static void refreshLiveVote(VotingViewController *self,
                            const bool isConnected,
                            const cg_race_vote_info_t *info,
                            const bool hasVote,
                            const bool clientCanCast) {
  if (!isConnected) {
    setLabelText(self->liveStatus, "OFFLINE");
    setLabelText(self->liveHeadline, "Connect to a Race server");
    setLabelText(self->liveDetail,
                 "Live ballots and proposal controls require an active session.");
    setLabelText(self->liveHint,
                 "The page will synchronize automatically after connecting.");
    setLabelText(self->liveYes, "-");
    setLabelText(self->liveNo, "-");
    setLabelText(self->liveNeeded, "-");
    setLabelText(self->liveRemaining, "-");
    setLabelText(self->actionCaption,
                 "Connect to a Race server to cast a ballot.");
    setControlEnabled((Control *) self->yesButton, false);
    setControlEnabled((Control *) self->noButton, false);
    return;
  }

  if (!hasVote) {
    setLabelText(self->liveStatus, "BALLOT READY");
    setLabelText(self->liveHeadline, "No vote is active");
    setLabelText(self->liveDetail,
                 "Call a proposal below or wait for another runner.");
    setLabelText(self->liveHint,
                 "Eligibility is frozen when voting starts and enforced by the server.");
    setLabelText(self->liveYes, "0");
    setLabelText(self->liveNo, "0");
    setLabelText(self->liveNeeded, "-");
    setLabelText(self->liveRemaining, "-");
    setLabelText(self->actionCaption,
                 "Ballot controls unlock when an eligible vote is live.");
    setControlEnabled((Control *) self->yesButton, false);
    setControlEnabled((Control *) self->noButton, false);
    return;
  }

  const bool accepting = info->remaining > 0;
  const bool canCast = accepting && clientCanCast;
  char headline[160];
  setLabelText(self->liveStatus, !accepting
               ? "BALLOT · APPLYING"
               : canCast ? "BALLOT · LIVE" : "BALLOT · VIEW ONLY");
  setLabelText(self->liveHeadline,
               activeVoteHeadline(info, headline, sizeof(headline)));
  setLabelText(self->liveDetail,
               va("Called by %s  ·  %d yes vote%s required",
                  info->initiator, info->needed, info->needed == 1 ? "" : "s"));
  setLabelText(self->liveHint, !accepting
               ? "The result is final and the validated action is being applied."
               : canCast
                 ? "Cast YES or NO. You may change your ballot until the result is final."
                 : "This connection is not part of the server's frozen electorate.");
  setLabelText(self->liveYes, va("%d", info->yes_votes));
  setLabelText(self->liveNo, va("%d", info->no_votes));
  setLabelText(self->liveNeeded, va("%d", info->needed));
  setLabelText(self->liveRemaining,
               info->remaining > 0 ? va("%ds", info->remaining) : "NOW");
  setLabelText(self->actionCaption, !accepting
               ? "The result is final; ballot controls are locked."
               : canCast
                 ? "You may change your ballot while voting is live."
                 : "Voting controls are disabled for this connection.");
  setControlEnabled((Control *) self->yesButton, canCast);
  setControlEnabled((Control *) self->noButton, canCast);
}

static void refreshPhysics(VotingViewController *self,
                           const bool canPropose) {
  const race_physics_config_t *config = Race_Physics_Current();
  const race_physics_preset_descriptor_t *current = config
    ? Race_Physics_Preset(config->preset)
    : NULL;
  setLabelText(self->currentPhysics,
               !Cg_RacePhysics_Synchronized()
                 ? "CURRENT · SYNCING"
                 : current
                   ? va("CURRENT · %s", current->short_name)
                   : "CURRENT · UNKNOWN");

  if (current && self->displayedPhysics != current->id) {
    $(self->physicsTarget, selectOptionWithValue, (ident) current->key);
    self->displayedPhysics = current->id;
  }

  const Option *option = $(self->physicsTarget, selectedOption);
  const race_physics_preset_descriptor_t *selected = option
    ? Race_Physics_PresetForKey(option->value)
    : NULL;
  const bool alreadyCurrent = config && selected &&
    config->preset == selected->id;
  setControlEnabled((Control *) self->physicsTarget, canPropose);
  setControlEnabled((Control *) self->physicsButton,
                    canPropose && selected && !alreadyCurrent);
}

static void refreshNextMap(VotingViewController *self,
                           const bool isConnected,
                           const cg_race_vote_menu_t *menu,
                           const bool hasMenu,
                           const bool clientCanCast,
                           const bool clientCanNominate) {
  setViewHidden(self->nextMapEmpty, hasMenu);
  setViewHidden(self->nextMapChoices, !hasMenu);

  if (hasMenu) {
    setLabelText(self->nextMapStatus, clientCanCast
                 ? "NEXT MAP · LIVE" : "NEXT MAP · VIEW ONLY");
    setLabelText(self->nextMapDetail,
                 clientCanCast
                   ? "Choose one map. Names, totals and countdown are authoritative."
                   : "This connection can follow the ballot but server policy blocks voting.");
    setLabelText(self->nextMapRemaining, va("%ds", menu->remaining));
  } else if (isConnected) {
    setLabelText(self->nextMapStatus, "NEXT MAP · NOMINATIONS");
    setLabelText(self->nextMapDetail,
                 "The ballot opens automatically when intermission begins.");
    setLabelText(self->nextMapRemaining, "WAITING");
  } else {
    setLabelText(self->nextMapStatus, "NEXT MAP · OFFLINE");
    setLabelText(self->nextMapDetail,
                 "Connect to nominate maps and receive the intermission ballot.");
    setLabelText(self->nextMapRemaining, "OFFLINE");
  }

  for (size_t i = 0u; i < lengthof(self->choiceButtons); i++) {
    const bool visible = hasMenu && i < menu->num_choices;
    setViewHidden((View *) self->choiceButtons[i], !visible);
    setControlEnabled((Control *) self->choiceButtons[i],
                      visible && isConnected && clientCanCast &&
                      menu->remaining > 0);
    if (visible) {
      const race_vote_menu_choice_t *choice = menu->choices + i;
      setButtonTitle(self->choiceButtons[i],
                     va("%zu  %s    ·    %u vote%s", i + 1u, choice->name,
                        choice->votes, choice->votes == 1u ? "" : "s"));
    }
  }

  const bool canNominate = isConnected && !hasMenu && clientCanNominate;
  setControlEnabled((Control *) self->nominationTarget, canNominate);
  setControlEnabled((Control *) self->nominateButton,
                    canNominate &&
                    mapTargetValid(textForTextView(self->nominationTarget)));
  setLabelText(self->nominationHint,
               hasMenu
                 ? "Nominations are closed while the intermission ballot is live."
                 : isConnected && !clientCanNominate
                   ? "Server policy does not allow this connection to nominate maps."
                 : "Nominations are considered first when the server builds the ballot.");
}

static void refreshVotingState(VotingViewController *self,
                               const player_state_t *ps) {
  if (!self || !self->viewController.view) {
    return;
  }

  const bool isConnected = connected();
  cg_race_vote_info_t vote = { 0 };
  const bool hasVote = isConnected && Cg_RaceVote_ParseInfo(
    cgi.ConfigString(CS_RACE_VOTE_INFO), &vote);
  cg_race_vote_menu_t menu = { 0 };
  const bool hasMenu = isConnected && Cg_RaceVote_ParseMenu(
    cgi.ConfigString(CS_RACE_VOTE_MENU), &menu);
  const cg_race_vote_client_state_t clientState =
    Cg_RaceVote_ClientState(ps);
  const bool clientCanCast = !clientState.authoritative ||
    clientState.can_cast;
  const bool clientCanCastMenu = !clientState.authoritative ||
    clientState.can_cast_menu;
  const bool clientCanNominate = !clientState.authoritative ||
    clientState.can_nominate;

  if (hasVote != self->hadVote || hasMenu != self->hadMenu) {
    self->feedback[0] = '\0';
    self->feedbackUntil = 0u;
  } else if (*self->feedback && cgi.client &&
             (int32_t) (cgi.client->unclamped_time - self->feedbackUntil) >= 0) {
    self->feedback[0] = '\0';
    self->feedbackUntil = 0u;
  }
  self->hadVote = hasVote;
  self->hadMenu = hasMenu;

  const bool spectator = isConnected && ps &&
    (ps->stats[STAT_SPECTATOR] ||
     ps->stats[STAT_RACE_MODE] == RACE_MODE_SPECTATOR);
  const bool canPropose = isConnected && !spectator && !hasVote && !hasMenu;

  const size_t rosterCount = refreshKickRoster(self);
  if (!selectedKickCurrent(self) && self->selectedKickSlot >= 0) {
    self->rosterSignature = 0u;
    refreshKickRoster(self);
  }

  refreshLiveVote(self, isConnected, &vote, hasVote, clientCanCast);
  refreshPhysics(self, canPropose);
  refreshNextMap(self, isConnected, &menu, hasMenu, clientCanCastMenu,
                 clientCanNominate);

  setControlEnabled((Control *) self->mapTarget, canPropose);
  setControlEnabled((Control *) self->mapButton,
                    canPropose && mapTargetValid(textForTextView(self->mapTarget)));
  setControlEnabled((Control *) self->kickTarget,
                    canPropose && rosterCount > 0u);
  setControlEnabled((Control *) self->kickButton,
                    canPropose && selectedKickCurrent(self));

  if (!isConnected) {
    setLabelText(self->proposalStatus,
                 "Offline · connect to a Race server to call or cast votes");
  } else if (hasMenu) {
    setLabelText(self->proposalStatus,
                 "Proposals locked · the intermission next-map ballot is live");
  } else if (hasVote) {
    setLabelText(self->proposalStatus,
                 "Proposals locked · resolve the active ballot first");
  } else if (spectator) {
    setLabelText(self->proposalStatus,
                 "Spectators cannot call proposals · ballot access follows server policy");
  } else if (*self->feedback) {
    setLabelText(self->proposalStatus, self->feedback);
  } else {
    setLabelText(self->proposalStatus,
                 "Ready · server cooldown, activity and per-map call limits apply");
  }
}

static void loadView(ViewController *viewController) {
  super(ViewController, viewController, loadView);
  VotingViewController *self = (VotingViewController *) viewController;

  Outlet outlets[] = MakeOutlets(
    MakeOutlet("liveStatus", &self->liveStatus),
    MakeOutlet("liveHeadline", &self->liveHeadline),
    MakeOutlet("liveDetail", &self->liveDetail),
    MakeOutlet("liveHint", &self->liveHint),
    MakeOutlet("liveYes", &self->liveYes),
    MakeOutlet("liveNo", &self->liveNo),
    MakeOutlet("liveNeeded", &self->liveNeeded),
    MakeOutlet("liveRemaining", &self->liveRemaining),
    MakeOutlet("vote_yes", &self->yesButton),
    MakeOutlet("vote_no", &self->noButton),
    MakeOutlet("ballotActionCaption", &self->actionCaption),
    MakeOutlet("currentPhysics", &self->currentPhysics),
    MakeOutlet("vote_physics_target", &self->physicsTarget),
    MakeOutlet("vote_physics_start", &self->physicsButton),
    MakeOutlet("vote_map_target", &self->mapTarget),
    MakeOutlet("vote_map_start", &self->mapButton),
    MakeOutlet("vote_kick_target", &self->kickTarget),
    MakeOutlet("vote_kick_start", &self->kickButton),
    MakeOutlet("proposalStatus", &self->proposalStatus),
    MakeOutlet("nextMapStatus", &self->nextMapStatus),
    MakeOutlet("nextMapDetail", &self->nextMapDetail),
    MakeOutlet("nextMapRemaining", &self->nextMapRemaining),
    MakeOutlet("nextMapEmpty", &self->nextMapEmpty),
    MakeOutlet("nextMapChoices", &self->nextMapChoices),
    MakeOutlet("vote_choice_1", &self->choiceButtons[0]),
    MakeOutlet("vote_choice_2", &self->choiceButtons[1]),
    MakeOutlet("vote_choice_3", &self->choiceButtons[2]),
    MakeOutlet("vote_choice_4", &self->choiceButtons[3]),
    MakeOutlet("vote_choice_5", &self->choiceButtons[4]),
    MakeOutlet("vote_choice_6", &self->choiceButtons[5]),
    MakeOutlet("vote_choice_7", &self->choiceButtons[6]),
    MakeOutlet("vote_choice_8", &self->choiceButtons[7]),
    MakeOutlet("nomination_target", &self->nominationTarget),
    MakeOutlet("nominate_map", &self->nominateButton),
    MakeOutlet("nominationHint", &self->nominationHint)
  );

  View *view = $$(View, viewWithResourceName,
                  "ui/voting/VotingViewController.json", outlets);
  assert(view);
  view->stylesheet = $$(Stylesheet, stylesheetWithResourceName,
                        "ui/voting/VotingViewController.css");
  assert(view->stylesheet);
  $(viewController, setView, view);
  release(view);

  self->selectedKickSlot = -1;

  self->yesButton->delegate = (ButtonDelegate) {
    .self = self,
    .data = "race vote yes\n",
    .didClick = didClickBallot
  };
  self->noButton->delegate = (ButtonDelegate) {
    .self = self,
    .data = "race vote no\n",
    .didClick = didClickBallot
  };
  size_t numPresets;
  const race_physics_preset_descriptor_t *presets =
    Race_Physics_Presets(&numPresets);
  for (size_t i = 0u; i < numPresets; i++) {
    if (presets[i].available) {
      $(self->physicsTarget, addOption, presets[i].name,
        (ident) presets[i].key);
    }
  }
  self->physicsTarget->delegate = (SelectDelegate) {
    .self = self,
    .didSelectOption = didSelectPhysics
  };
  self->physicsButton->delegate = (ButtonDelegate) {
    .self = self,
    .didClick = didClickPhysics
  };
  self->mapButton->delegate = (ButtonDelegate) {
    .self = self,
    .didClick = didClickMapVote
  };
  self->kickButton->delegate = (ButtonDelegate) {
    .self = self,
    .didClick = didClickKickVote
  };
  for (size_t i = 0u; i < lengthof(self->choiceButtons); i++) {
    self->choiceButtons[i]->delegate = (ButtonDelegate) {
      .self = self,
      .data = (ident) (intptr_t) (i + 1u),
      .didClick = didClickChoice
    };
  }
  self->nominateButton->delegate = (ButtonDelegate) {
    .self = self,
    .didClick = didClickNominate
  };

  self->mapTarget->delegate = (TextViewDelegate) {
    .self = self,
    .didEdit = didEditMapTarget,
    .didEndEditing = didEditMapTarget
  };
  self->nominationTarget->delegate = (TextViewDelegate) {
    .self = self,
    .didEdit = didEditMapTarget,
    .didEndEditing = didEditMapTarget
  };
  self->kickTarget->delegate = (SelectDelegate) {
    .self = self,
    .didSelectOption = selectKickTarget
  };

  $(self->mapTarget, setDefaultText, "e.g. mzc_dj");
  $(self->nominationTarget, setDefaultText, "e.g. potato");
  refreshVotingState(self, cgi.client ? &cgi.client->frame.ps : NULL);
}

static void viewWillAppear(ViewController *viewController) {
  super(ViewController, viewController, viewWillAppear);
  VotingViewController *self = (VotingViewController *) viewController;
  activeVotingViewController = self;
  if (!self->requestsScores) {
    cgi.Cbuf("+score\n");
    self->requestsScores = true;
  }
  refreshVotingState(activeVotingViewController,
                     cgi.client ? &cgi.client->frame.ps : NULL);
}

static void viewWillDisappear(ViewController *viewController) {
  VotingViewController *self = (VotingViewController *) viewController;
  if (self->requestsScores) {
    cgi.Cbuf("-score\n");
    self->requestsScores = false;
  }
  if (activeVotingViewController == self) {
    activeVotingViewController = NULL;
  }
  super(ViewController, viewController, viewWillDisappear);
}

static void dealloc(Object *object) {
  VotingViewController *self = (VotingViewController *) object;
  if (self->requestsScores) {
    cgi.Cbuf("-score\n");
    self->requestsScores = false;
  }
  if (activeVotingViewController == self) {
    activeVotingViewController = NULL;
  }
  super(Object, object, dealloc);
}

static void initialize(Class *clazz) {
  ((ObjectInterface *) clazz->interface)->dealloc = dealloc;
  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
  ((ViewControllerInterface *) clazz->interface)->viewWillAppear = viewWillAppear;
  ((ViewControllerInterface *) clazz->interface)->viewWillDisappear =
    viewWillDisappear;
}

Class *_VotingViewController(void) {
  static Class *clazz;
  static Once once;
  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "VotingViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(VotingViewController),
      .interfaceOffset = offsetof(VotingViewController, interface),
      .interfaceSize = sizeof(VotingViewControllerInterface),
      .initialize = initialize
    });
  });
  return clazz;
}

void VotingViewController_Refresh(const player_state_t *ps) {
  if (activeVotingViewController) {
    refreshVotingState(activeVotingViewController, ps);
  }
}

#undef _Class
