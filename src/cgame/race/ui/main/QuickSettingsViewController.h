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

/**
 * @brief The tier-1 drawer's items, in the order the design lists them.
 * @details The drawer is a menu of things the shell already knows how to do,
 * so it reports which item was chosen and does none of them itself - the same
 * separation the route buttons keep, and the reason Disconnect and Quit still
 * raise the shell's own confirmation dialogs rather than a second pair here.
 */
typedef enum {
  QuickSettingsItemResume,
  QuickSettingsItemRestartRun,
  QuickSettingsItemWatchBest,
  QuickSettingsItemSpectate,
  QuickSettingsItemCallVote,
  QuickSettingsItemFullMenu,
  QuickSettingsItemDisconnect,
  QuickSettingsItemQuit,

  QuickSettingsItemCount
} QuickSettingsItem;

typedef struct {
  ident self;

  /**
   * @brief Sent when any drawer item is chosen.
   */
  void (*didSelectItem)(ident self, QuickSettingsItem item);
} QuickSettingsViewControllerDelegate;

/**
 * @brief The tier-1 ESC drawer.
 * @details Ported from the Race menu design's `.qs-panel`: a full-height panel
 * flush to the right edge of the viewport carrying Run and Session groups over
 * a destructive foot. It holds no settings - the name is the design's, kept so
 * the shell's existing outlet and host names still read - and writes no cvar.
 * @extends ViewController
 */
struct QuickSettingsViewController {
  ViewController viewController;
  QuickSettingsViewControllerInterface *interface;

  QuickSettingsViewControllerDelegate delegate;

  /**
   * @brief The eight items, indexed by QuickSettingsItem.
   * @private
   */
  Button *items[QuickSettingsItemCount];

  /**
   * @brief Each item's right-aligned key hint.
   * @details Painted from the player's own bindings rather than from the
   * design's literals: an item whose command nobody has bound shows no hint at
   * all, because naming a key that would do nothing is worse than naming none.
   * Resume is the exception - Escape resumes the game unconditionally, so its
   * hint is not a binding and is written once.
   * @private
   */
  Label *itemHints[QuickSettingsItemCount];

  /**
   * @brief `<Route> · Esc resumes`.
   * @private
   */
  Label *subtitle;
};

struct QuickSettingsViewControllerInterface {
  ViewControllerInterface viewControllerInterface;

  /**
   * @fn void QuickSettingsViewController::refresh(QuickSettingsViewController *self)
   * @brief Repaints the key hints from the player's current bindings.
   * @memberof QuickSettingsViewController
   */
  void (*refresh)(QuickSettingsViewController *self);

  /**
   * @fn void QuickSettingsViewController::setRouteName(QuickSettingsViewController *self, const char *route)
   * @brief Names the route the drawer was opened over.
   * @memberof QuickSettingsViewController
   */
  void (*setRouteName)(QuickSettingsViewController *self, const char *route);
};

CGAME_EXPORT Class *_QuickSettingsViewController(void);
