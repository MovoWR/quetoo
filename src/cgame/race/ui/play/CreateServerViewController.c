/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 * Copyright(c) 2026 Quetoo Race Module.
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
#include "cg_team_mode.h"

#include "CreateServerViewController.h"
#include "MapListCollectionItemView.h"

#include "race_physics.h"

/**
 * @brief The cvar a hosted server's ruleset is chosen with.
 * @remarks Spelled here rather than shared: RACE_PHYSICS_SELECTOR_CVAR lives in
 * the GAME module's physics service translation unit, not in its header.
 */
static const char *_physicsSelector = "g_race_physics";

#define _Class _CreateServerViewController

/**
 * @brief The rotation the design describes: one map hosted on its own, or
 * several run in order.
 */
static const char *_rotationHint =
  "Click a map to host it on its own. Ctrl-click to add more and run them in rotation.";

#pragma mark - Selection state

/**
 * @brief Enables or disables a Button, notifying it only when the state moved.
 */
static void setButtonEnabled(Button *button, const bool enabled) {

  if (!button) {
    return;
  }

  Control *control = (Control *) button;
  const unsigned int old_state = control->state;

  if (enabled) {
    control->state &= ~ControlStateDisabled;
  } else {
    control->state |= ControlStateDisabled;
  }

  if (old_state != control->state) {
    $(control, stateDidChange);
  }
}

/**
 * @brief Restates the rotation's selection in the page, rather than in the
 * console the player cannot see from the menu.
 * @param this The controller.
 * @param selected How many maps are currently selected.
 * @details Three things read this one count so they cannot disagree: the
 * section head's status, the guidance line under the grid, and whether Start
 * race server is something the player can press at all.
 *
 * Zero selections are represented explicitly and keep Start race server
 * disabled. One or more selections use the same count in the heading while
 * the stable hint continues to explain single-map and rotation selection.
 */
static void updateMapSelection(CreateServerViewController *this, const size_t selected) {

  $(this->mapsSelection->text, setText,
    selected ? va("%zu map%s selected", selected, selected == 1 ? "" : "s")
             : "No maps selected");

  $(this->mapsHint->text, setText, _rotationHint);

  setButtonEnabled(this->create, selected > 0);
}

/**
 * @brief Restates the ruleset a server started from this page would be timed
 * under, beside the section that chooses it.
 */
static void updateRulesMeta(CreateServerViewController *this) {

  const Option *option = $(this->physics, selectedOption);
  const char *name = option && option->title ? option->title->text : NULL;

  $(this->rulesMeta->text, setText,
    name && *name ? va("Records kept under %s", name) : "");
}

#pragma mark - Delegates

/**
 * @brief CollectionViewDelegate callback for the map rotation.
 */
static void didModifyMapSelection(CollectionView *collectionView,
                                  const Array *selectionIndexPaths) {

  CreateServerViewController *this = collectionView->delegate.self;

  updateMapSelection(this, selectionIndexPaths ? selectionIndexPaths->count : 0);
}

/**
 * @brief Restates the physics select from `g_race_physics`, whatever spelling
 * that cvar happens to hold.
 * @details `g_race_physics` holds a selector, and a selector is not a preset
 * key: `q2` and `quake2` both name the `q2-v1` preset. CvarSelect matches an
 * option by string equality against the cvar and deselects everything when
 * nothing matches, so left to itself this select renders blank - the control
 * shows only its selected option, and there is no selected option. Resolving
 * the selector first is what puts a value back in the box.
 *
 * The latched string wins where there is one: `g_race_physics` is
 * `CVAR_LATCH | CVAR_SERVER_INFO` once the GAME module has registered it, so on
 * a listen server a pick made here is pending rather than live, and the pending
 * value is the one the server about to be started will run.
 *
 * Select::selectOption, not CvarSelect::selectOptionWithValue: this states what
 * the cvar already says and must not write it back. Writing on a page that was
 * merely opened would announce "will be changed for next game" for a change
 * nobody made.
 */
static void refreshPhysicsSelection(CreateServerViewController *this) {

  const cvar_t *selector = cgi.GetCvar(_physicsSelector);
  if (!selector) {
    return;
  }

  const char *spelling = selector->latched_string ? selector->latched_string : selector->string;

  race_physics_config_t configured;
  if (!Race_Physics_ConfigForSelector(spelling, &configured)) {
    return;
  }

  const race_physics_preset_descriptor_t *preset =
    Race_Physics_Preset(configured.preset);
  if (!preset) {
    return;
  }

  Option *option = $(this->physics, optionWithValue, (ident) preset->key);
  if (option) {
    $(this->physics, selectOption, option);
  }

  updateRulesMeta(this);
}

/**
 * @brief SelectDelegate callback for the physics preset.
 * @details The route needs the pick for its own caption, and CvarSelect needs
 * it to write the cvar - a Select has one delegate slot, so the one this route
 * installs calls the one it displaced rather than replacing it. Without the
 * chain, choosing a preset changes the sentence beside the section head and
 * nothing else, and the server starts on whatever the cvar still held.
 */
static void selectPhysics(Select *select, Option *option) {

  CreateServerViewController *this = select->delegate.self;

  if (this->physicsCvarDelegate.didSelectOption) {
    this->physicsCvarDelegate.didSelectOption(select, option);
  }

  updateRulesMeta(this);
}

/**
 * @brief Select teams mode.
 */
static void selectTeams(Select *select, Option *option) {

  const cg_team_mode_t *mode = option->value;

  for (const cg_team_mode_cvar_t *cvar = mode->cvars; cvar->var; cvar++) {
    cgi.SetCvarString(cvar->var, cvar->value);
  }
}

/**
 * @brief ButtonDelegate for the Create button.
 */
static void createServer(Button *button) {

  CreateServerViewController *this = button->delegate.self;

  PointerArray *selectedMaps = $(this->mapList, selectedMaps);
  if (selectedMaps->count) {

    file_t *file = cgi.OpenFileWrite(MAP_LIST_UI);
    if (file) {

      String *string = str("");
      assert(string);

      for (size_t i = 0; i < selectedMaps->count; i++) {
        const MapListItemInfo *info = (MapListItemInfo *) $(selectedMaps, get, i);

        char name[MAX_QPATH];
        StripExtension(Basename(info->mapname), name);

        $(string, appendFormat, "{\n\tname %s\n}\n", name);
      }

      const int64_t len = cgi.WriteFile(file, string->chars, string->length, 1);

      if (len == -1) {
        Cg_Warn("Failed to write %s\n", MAP_LIST_UI);
      } else {
        Cg_Debug("Wrote %s %"PRId64" bytes\n", MAP_LIST_UI, len);
      }

      release(string);

      cgi.CloseFile(file);

      cgi.SetCvarString("sv_map_list", MAP_LIST_UI);
      cgi.Cbuf("next_map");
    } else {
      Cg_Warn("Failed to create %s\n", MAP_LIST_UI);
    }
  } else {
    // Unreachable while the button is disabled at an empty rotation, but the
    // answer belongs on the page either way: the console this used to print to
    // is not somewhere a player in the menu is looking.
    $(this->mapsHint->text, setText, "Select at least one map to start a server.");
  }

  release(selectedMaps);
}

/**
 * @brief Adds a roster of integer options, plus the cvar's current value when
 * the roster does not name it.
 * @param labelFormat How every option on this roster spells its number, unit
 * and all - a bare `20` on a row called Time limit is not a duration.
 * @param zeroLabel The label zero takes instead of `labelFormat`, or `NULL`.
 * @return True when the cvar's current value is not on the roster, so the row
 * can say where that value came from.
 * @details The design states both of these as fixed rosters, but neither cvar
 * is constrained to one: `sv_max_clients` ships at MAX_CLIENTS and either can
 * be set from the console or a config. Keeping the odd value as an option is
 * what stops the select reading as empty - and stops it quietly rewriting a
 * setting the player did not touch. The odd value is spelled like every other
 * option on the roster: it is a value, not a value with an explanation stapled
 * to it, and the explanation belongs beside the setting's name.
 */
static bool addIntegerOptions(Select *select, const char *var, const char *labelFormat,
                              const int32_t *values, size_t count, const char *zeroLabel) {

  const int32_t current = cgi.GetCvarInteger(var);

  bool named = false;
  for (size_t i = 0; i < count; i++) {
    named = named || values[i] == current;
  }

  if (!named) {
    $(select, addOption, current == 0 && zeroLabel ? zeroLabel : va(labelFormat, current),
      (ident) (intptr_t) current);
  }

  for (size_t i = 0; i < count; i++) {
    $(select, addOption, values[i] == 0 && zeroLabel ? zeroLabel : va(labelFormat, values[i]),
      (ident) (intptr_t) values[i]);
  }

  return !named;
}

/**
 * @brief Shows or hides a row's caption, which is only ever the one sentence.
 */
static void setRowCaption(Label *caption, const bool shown, const char *text) {

  caption->view.hidden = !shown;
  $(caption->text, setText, shown ? text : "");
}

#pragma mark - ViewController

/**
 * @see ViewController::loadView(ViewController *)
 */
static void loadView(ViewController *self) {

  super(ViewController, self, loadView);

  CreateServerViewController *this = (CreateServerViewController *) self;

  Outlet outlets[] = MakeOutlets(
    MakeOutlet("maxClients", &this->maxClients),
    MakeOutlet("maxClientsCaption", &this->maxClientsCaption),
    MakeOutlet("timeLimit", &this->timeLimit),
    MakeOutlet("timeLimitCaption", &this->timeLimitCaption),
    MakeOutlet("gameplay", &this->gameplay),
    MakeOutlet("physics", &this->physics),
    MakeOutlet("teams", &this->teams),
    MakeOutlet("mapList", &this->mapList),
    MakeOutlet("create", &this->create),
    MakeOutlet("rulesMeta", &this->rulesMeta),
    MakeOutlet("mapsSelection", &this->mapsSelection),
    MakeOutlet("mapsHint", &this->mapsHint)
  );

  $(self->view, awakeWithResourceName, "ui/play/CreateServerViewController.json");
  $(self->view, resolve, outlets);

  self->view->stylesheet = $$(Stylesheet, stylesheetWithResourceName, "ui/play/CreateServerViewController.css");
  assert(self->view->stylesheet);

  static const int32_t slots[] = { 4, 8, 16, 24, 32 };
  setRowCaption(this->maxClientsCaption,
                addIntegerOptions(this->maxClients, "sv_max_clients", "%d",
                                  slots, lengthof(slots), NULL),
                "from your config");

  // Zero is how the engine spells "no limit": the level's timer is only armed
  // while `g_level.time_limit` is non-zero, so None is a value on this roster
  // rather than a switch beside it. Every other option carries its unit, because
  // the row says what is being limited and the value has to say in what.
  static const int32_t minutes[] = { 10, 20, 30, 0 };
  setRowCaption(this->timeLimitCaption,
                addIntegerOptions(this->timeLimit, "g_time_limit", "%d min",
                                  minutes, lengthof(minutes), "None"),
                "from your config");

  $(this->gameplay, addOption, "Default", "default");
  $(this->gameplay, addOption, "Deathmatch", "deathmatch");
  $(this->gameplay, addOption, "Instagib", "instagib");
  $(this->gameplay, addOption, "Arena", "arena");

  // The physics options are the preset table's own, so a preset that this build
  // retires disappears from the menu rather than lingering as a dead label.
  size_t num_presets;
  const race_physics_preset_descriptor_t *presets = Race_Physics_Presets(&num_presets);
  for (size_t i = 0; i < num_presets; i++) {
    if (presets[i].available) {
      $(this->physics, addOption, presets[i].name, (ident) presets[i].key);
    }
  }

  size_t num_team_modes;
  const cg_team_mode_t *team_modes = Cg_TeamModes(&num_team_modes);
  if (num_team_modes) {
    for (size_t i = 0; i < num_team_modes; i++) {
      $(this->teams, addOption, team_modes[i].name, (ident) &team_modes[i]);
    }
    $(this->teams, selectOptionWithValue, (ident) &team_modes[0]);
  }
  this->teams->delegate.didSelectOption = selectTeams;

  // The physics select owns two things now: the cvar CvarSelect writes, and the
  // sentence beside the section head that says what that cvar means for the
  // records of the server about to be started. CvarSelect's own delegate is kept
  // rather than overwritten, so the first of those still happens.
  this->physicsCvarDelegate = this->physics->delegate;
  this->physics->delegate.self = this;
  this->physics->delegate.didSelectOption = selectPhysics;
  refreshPhysicsSelection(this);

  // MapListCollectionView already claims `itemForObjectAtIndexPath`; the
  // selection callback beside it is unused, so the route can take it without
  // shadowing the collection view itself.
  this->mapList->collectionView.delegate.self = this;
  this->mapList->collectionView.delegate.didModifySelection = didModifyMapSelection;

  // Maps load on a worker thread, so the page opens on an empty rotation
  // whatever the filesystem holds - which is the state this states.
  updateMapSelection(this, 0);

  this->create->delegate.didClick = createServer;
  this->create->delegate.self = this;
}

/**
 * @see ViewController::viewDidAppear(ViewController *)
 * @details ViewController::addChildViewController runs `updateBindings` over
 * the whole subtree and only then calls this, and CvarSelect::updateBindings
 * deselects every option whose value is not the cvar's string verbatim. The
 * physics select's values are preset keys and the cvar holds a selector, so
 * that pass is what empties the box - restating the selection here is what
 * survives it, on every navigation to Play rather than only at load.
 */
static void viewDidAppear(ViewController *self) {

  super(ViewController, self, viewDidAppear);

  refreshPhysicsSelection((CreateServerViewController *) self);
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
  ((ViewControllerInterface *) clazz->interface)->viewDidAppear = viewDidAppear;

  // g_race_physics belongs to the GAME module, which is not loaded while the
  // player is only browsing the menu - so the client registers it here to have
  // something for the select to bind to. Cvar_Add merges flags rather than
  // replacing them, so the server's CVAR_LATCH | CVAR_SERVER_INFO still lands
  // when the GAME module registers the same name.
  cgi.AddCvar(_physicsSelector, RACE_PHYSICS_SELECTOR_Q2_KEY, CVAR_ARCHIVE,
              "Race physics preset a hosted server runs");
}

/**
 * @fn Class *CreateServerViewController::_CreateServerViewController(void)
 * @memberof CreateServerViewController
 */
Class *_CreateServerViewController(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "CreateServerViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(CreateServerViewController),
      .interfaceOffset = offsetof(CreateServerViewController, interface),
      .interfaceSize = sizeof(CreateServerViewControllerInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
