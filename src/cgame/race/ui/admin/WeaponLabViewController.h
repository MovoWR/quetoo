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

#pragma once

#include "cg_types.h"
#include "cg_race_weapon_tuning.h"

#include <ObjectivelyMVC.h>

/**
 * @file
 * @brief Admin > Weapons authoritative weapon-tuning controller.
 */

typedef struct WeaponLabViewController WeaponLabViewController;
typedef struct WeaponLabViewControllerInterface WeaponLabViewControllerInterface;

#define WEAPON_LAB_GROUP_COUNT RACE_WEAPON_TUNING_GROUP_TOTAL
#define WEAPON_LAB_ROW_COUNT RACE_WEAPON_TUNING_VALUE_COUNT

/**
 * @brief Where this route writes the command it just sent.
 * @details The lab has no response block of its own: the design prints every
 * accepted action into the Admin route's existing one, so the host lends it.
 */
typedef struct {
  ident self;
  void (*didPostCommand)(ident self, const char *command, const char *reply);
} WeaponLabDelegate;

/**
 * @brief Admin > Weapons, the weapon-physics tuning lab.
 * @details Ported from the "Weapons - weapon-tuning lab" handoff, which
 * provides a temporary, volatile balancing surface over one generated catalog.
 *
 * This is a tab *kind*, not another Admin section group: its rows are one
 * authoritative GAME snapshot rather than settings rows, so nothing here binds
 * through adminRows or gset. Every control is a local draft until Apply, which
 * sends one batch against the generation on screen.
 *
 * @remarks The catalog, baseline and current values
 * are rendered only from a complete CGAME cache transaction authored by GAME.
 * Every mutation remains pending until a correlated result and the following
 * complete authoritative broadcast arrive. A local draft never promotes
 * accepted state, and a generation change makes that draft visibly stale.
 * @extends ViewController
 */
struct WeaponLabViewController {

  /**
   * @brief The superclass.
   * @private
   */
  ViewController viewController;

  /**
   * @brief The interface.
   * @private
   */
  WeaponLabViewControllerInterface *interface;

  /**
   * @brief The Admin route's response block, on loan.
   * @private
   */
  WeaponLabDelegate delegate;

  /**
   * @brief The status strip and its two clusters.
   * @private
   */
  View *statusStrip;
  View *unrankedChip;
  Label *stateChip;
  Label *statusDetail;
  Label *roleChip;

  /**
   * @brief The group grid: one card per catalog group, one of them expanded.
   * @details The grid container itself is deliberately not held. It is authored
   * as a plain StackView and the shell replaces it with a ColumnsView when the
   * page is prepared, moving these cards across; a pointer to the container
   * would be left addressing the discarded view.
   * @private
   */
  View *groupCards[WEAPON_LAB_GROUP_COUNT];
  View *groupExpanded[WEAPON_LAB_GROUP_COUNT];
  Button *groupToggles[WEAPON_LAB_GROUP_COUNT];
  Label *groupToggleCounts[WEAPON_LAB_GROUP_COUNT];
  Label *groupCounts[WEAPON_LAB_GROUP_COUNT];

  /**
   * @brief Rows, and the parts of a row a refresh drives.
   * @private
   */
  View *rowViews[WEAPON_LAB_ROW_COUNT];
  View *rowDots[WEAPON_LAB_ROW_COUNT];
  View *rowRules[WEAPON_LAB_ROW_COUNT];
  View *rowControls[WEAPON_LAB_ROW_COUNT];
  Label *rowUnavailable[WEAPON_LAB_ROW_COUNT];
  Label *rowLabels[WEAPON_LAB_ROW_COUNT];
  Label *rowMeta[WEAPON_LAB_ROW_COUNT];
  Slider *rowSliders[WEAPON_LAB_ROW_COUNT];
  TextView *rowFields[WEAPON_LAB_ROW_COUNT];

  /**
   * @brief The commit footer.
   * @private
   */
  Label *pendingCount;
  Label *valueCount;
  Button *resetButton;
  Button *applyButton;

  /**
   * @brief This client's sparse draft over the accepted GAME snapshot.
   * @details Accepted and baseline values are never copied into this controller;
   * they remain in the fail-closed CGAME cache. `draftGeneration` is captured
   * on the first edit so a later broadcast makes the draft visibly stale rather
   * than silently rebasing it.
   * @private
   */
  double draft[WEAPON_LAB_ROW_COUNT];
  bool drafted[WEAPON_LAB_ROW_COUNT];
  uint64_t draftGeneration;

  /**
   * @brief Last authoritative cache identities painted by this controller.
   */
  uint64_t catalogHash;
  uint32_t cacheRevision;
  uint32_t resultRevision;
  race_weapon_tuning_state_t statusState;
  bool synchronized;
  bool mutationPending;
  bool catalogBuilt;

  /**
   * @brief Which group is expanded, and the query the host last handed down.
   * @private
   */
  size_t openGroup;
  char query[64];

  /**
   * @brief The capability mask this client last published.
   * @private
   */
  uint16_t capabilities;

  /**
   * @brief Callback synchronization guard.
   * @details Slider::setValue notifies its delegate, so writing the model back
   * into the controls would otherwise re-enter as an edit.
   * @private
   */
  bool refreshing;
};

/**
 * @brief The WeaponLabViewController interface.
 */
struct WeaponLabViewControllerInterface {

  /**
   * @brief The superclass interface.
   */
  ViewControllerInterface viewControllerInterface;
};

/**
 * @brief Lends the lab the Admin route's response block.
 */
void WeaponLabViewController_SetDelegate(ViewController *self,
                                         const WeaponLabDelegate *delegate);

/**
 * @brief Hands down the capability mask; Editor requires SETTINGS_MUTATE.
 */
void WeaponLabViewController_SetCapabilities(ViewController *self,
                                             uint16_t capabilities);

/**
 * @brief Hands down the Admin route's filter query and repaints.
 */
void WeaponLabViewController_SetQuery(ViewController *self, const char *query);

/**
 * @brief How many catalog rows the given query matches, for the tab strip.
 */
size_t WeaponLabViewController_Hits(const char *query);

/**
 * @brief Whether this client holds an edit it has not applied.
 * @details The host asks before it lets the tab or the route go: a draft is
 * lost work, and Phase 5 requires the confirmation.
 */
bool WeaponLabViewController_HasPendingDraft(const ViewController *self);

/**
 * @brief Throws away the pending draft, mutating nothing on the server.
 */
void WeaponLabViewController_DiscardDraft(ViewController *self);

/**
 * @brief Reconciles an open panel with the latest complete authoritative cache.
 */
void WeaponLabViewController_RefreshAuthoritativeState(void);

/**
 * @fn Class *WeaponLabViewController::_WeaponLabViewController(void)
 * @brief The WeaponLabViewController archetype.
 * @return The WeaponLabViewController Class.
 * @memberof WeaponLabViewController
 */
CGAME_EXPORT Class *_WeaponLabViewController(void);
