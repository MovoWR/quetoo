/*
 * Copyright(c) 2026 Quetoo Race Module.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

typedef struct ActiveVoteViewController ActiveVoteViewController;
typedef struct ActiveVoteViewControllerInterface ActiveVoteViewControllerInterface;

struct ActiveVoteViewController {
  ViewController viewController;
  ActiveVoteViewControllerInterface *interface;
  Label *title;
  Label *detail;
  Label *tally;
  Label *remaining;
  Button *yesButton;
  Button *noButton;
};

struct ActiveVoteViewControllerInterface {
  ViewControllerInterface viewControllerInterface;
  bool (*refresh)(ActiveVoteViewController *self);
};

CGAME_EXPORT Class *_ActiveVoteViewController(void);
