/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 */

#pragma once

#include <ObjectivelyMVC/Button.h>
#include <ObjectivelyMVC/Label.h>
#include <ObjectivelyMVC/Select.h>
#include <ObjectivelyMVC/TextView.h>
#include <ObjectivelyMVC/ViewController.h>

#include "race_physics.h"

typedef struct VotingViewController VotingViewController;
typedef struct VotingViewControllerInterface VotingViewControllerInterface;

struct VotingViewController {
  ViewController viewController;
  VotingViewControllerInterface *interface;

  Label *liveStatus;
  Label *liveHeadline;
  Label *liveDetail;
  Label *liveHint;
  Label *liveYes;
  Label *liveNo;
  Label *liveNeeded;
  Label *liveRemaining;
  Button *yesButton;
  Button *noButton;
  Label *actionCaption;

  Label *currentPhysics;
  Select *physicsTarget;
  Button *physicsButton;
  TextView *mapTarget;
  Button *mapButton;
  Select *kickTarget;
  Button *kickButton;
  Label *proposalStatus;

  Label *nextMapStatus;
  Label *nextMapDetail;
  Label *nextMapRemaining;
  View *nextMapEmpty;
  View *nextMapChoices;
  Button *choiceButtons[8];
  TextView *nominationTarget;
  Button *nominateButton;
  Label *nominationHint;

  uint64_t rosterSignature;
  race_physics_preset_id_t displayedPhysics;
  int32_t selectedKickSlot;
  char selectedKickName[32];
  char feedback[128];
  uint32_t feedbackUntil;
  bool hadVote;
  bool hadMenu;
  bool requestsScores;
};

struct VotingViewControllerInterface {
  ViewControllerInterface viewControllerInterface;
};

CGAME_EXPORT Class *_VotingViewController(void);

void VotingViewController_Refresh(const player_state_t *ps);
