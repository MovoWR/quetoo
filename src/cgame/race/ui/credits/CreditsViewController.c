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

#include "CreditsViewController.h"

#define _Class _CreditsViewController

#pragma mark - Pages

typedef enum {
  CreditsPageRace,
  CreditsPageQuetoo,
  CreditsPageLicenses
} CreditsPage;

static const char *creditsPageNames[CREDITS_PAGE_COUNT] = {
  "Race",
  "Quetoo",
  "Licenses"
};

#pragma mark - Sections

typedef enum {
  CreditsSectionRace,
  CreditsSectionProgramming,
  CreditsSectionArtwork,
  CreditsSectionSound,
  CreditsSectionSpecialThanks,
  CreditsSectionEngine,
  CreditsSectionFonts
} CreditsSection;

/**
 * @brief How a section lays its entries out.
 */
typedef enum {
  /**
   * @brief The Race tab: three bordered cards rather than the route's usual
   * hairline sections. It is the one screen in this design system that frames
   * its content, because a people card, a tools card and a "built on" pointer
   * read as discrete blocks rather than as one flowing list.
   */
  CreditsLayoutCards,

  /**
   * @brief A handle, its real name, and what the person did.
   */
  CreditsLayoutCredits,

  /**
   * @brief Names only, flowed into as many columns as fit.
   */
  CreditsLayoutThanks,

  /**
   * @brief A name and a value, the grammar Settings uses for a readout.
   */
  CreditsLayoutValues
} CreditsLayout;

typedef struct {
  CreditsPage page;

  /**
   * @brief The eyebrow, or NULL for a section that carries no head.
   * @details The mock hides the Race tab's head with `sr-only` rather than
   * dropping it, because its shell tags sections by label for cross-tab search.
   * This route has no filter, so there is nothing left to tag and the label is
   * simply absent.
   */
  const char *label;

  CreditsLayout layout;

  /**
   * @brief Whether this section spans the route instead of taking a column.
   */
  bool wide;
} CreditsSectionDescriptor;

static const CreditsSectionDescriptor creditsSections[CREDITS_SECTION_COUNT] = {
  [CreditsSectionRace] = {
    .page = CreditsPageRace, .label = NULL,
    .layout = CreditsLayoutCards, .wide = true
  },
  [CreditsSectionProgramming] = {
    .page = CreditsPageQuetoo, .label = "Programming",
    .layout = CreditsLayoutCredits
  },
  [CreditsSectionArtwork] = {
    .page = CreditsPageQuetoo, .label = "Artwork",
    .layout = CreditsLayoutCredits
  },
  [CreditsSectionSound] = {
    .page = CreditsPageQuetoo, .label = "Sound",
    .layout = CreditsLayoutCredits
  },
  [CreditsSectionSpecialThanks] = {
    .page = CreditsPageQuetoo, .label = "Special thanks",
    .layout = CreditsLayoutThanks, .wide = true
  },
  [CreditsSectionEngine] = {
    .page = CreditsPageLicenses, .label = "Engine and framework",
    .layout = CreditsLayoutValues
  },
  [CreditsSectionFonts] = {
    .page = CreditsPageLicenses, .label = "Fonts",
    .layout = CreditsLayoutValues
  }
};

#pragma mark - Entries

/**
 * @brief One line of a section.
 * @details `handle` and `name` are two fields rather than one string because
 * that is the distinction upstream encodes with colour escapes: a name written
 * `^2jdolan^7 (Jay Dolan)` is a handle to be accented plus a real name to be
 * left alone, and a contributor credited only under their real name - Roland
 * Shaw, Anthony Webb - carries no escapes at all. Splitting the two here keeps
 * that difference legible instead of leaving it run-encoded in a string, and
 * the stylesheet colours each half.
 */
typedef struct {
  CreditsSection section;
  const char *handle;
  const char *name;
  const char *detail;
} CreditsEntryDescriptor;

/**
 * @brief The roster.
 * @details The Quetoo half is common's own
 * `ui/credits/CreditsViewController.json` transcribed - escapes resolved, order
 * preserved. It is the engine's attribution and Race does not get to edit it,
 * so anything that reads differently here is a bug, not a correction. The
 * Licenses half has no upstream screen at all; the identifiers come from each
 * project's own LICENSE file, and Coda's OFL ships in the ObjectivelyMVC repo
 * beside the font.
 */
static const CreditsEntryDescriptor creditsEntries[] = {

  { CreditsSectionProgramming, "jdolan", "Jay Dolan",
    "Engine, game module, tools, levels, artwork, sound" },
  { CreditsSectionProgramming, "Paril", "Jonathan Barkley",
    "Engine, game module, bots" },

  { CreditsSectionArtwork, "Panjoo", "DJ Bloot",
    "Logos, level design, models, textures, game balance" },
  { CreditsSectionArtwork, "Skies912", "Chris Glenn",
    "Level design, textures, positive vibes" },
  { CreditsSectionArtwork, "TRaK", "Georges Grondin",
    "Level design, textures, models" },
  { CreditsSectionArtwork, "Jester", "Steve Veihl",
    "Level design, brush magic" },

  { CreditsSectionSound, NULL, "Roland Shaw", "Sound design" },
  { CreditsSectionSound, NULL, "Anthony Webb", "Sound design" },
  { CreditsSectionSound, "floproast", "Jacob Zammit",
    "Quake II soundtrack covers" },

  { CreditsSectionSpecialThanks, "Alexandr Zhelanov", NULL, NULL },
  { CreditsSectionSpecialThanks, "BoBo the seal", "Brian Jones", NULL },
  { CreditsSectionSpecialThanks, "broar", NULL, NULL },
  { CreditsSectionSpecialThanks, "BuzzardBait", "Matthew Jarrett", NULL },
  { CreditsSectionSpecialThanks, "CardO", "Dan Shannon", NULL },
  { CreditsSectionSpecialThanks, "claire", "Joe Reid", NULL },
  { CreditsSectionSpecialThanks, "dfsp_spirit", "Tim Schäfer", NULL },
  { CreditsSectionSpecialThanks, "evil lair", "Yves Allaire", NULL },
  { CreditsSectionSpecialThanks, "GTDStudio", "Palrom", NULL },
  { CreditsSectionSpecialThanks, "Ingar", "Stijn Buys", NULL },
  { CreditsSectionSpecialThanks, "jhaa", "Juha Merilä", NULL },
  { CreditsSectionSpecialThanks, "KaBal", NULL, NULL },
  { CreditsSectionSpecialThanks, "KaadmY", NULL, NULL },
  { CreditsSectionSpecialThanks, "Karvajalka", "Antti Lahti", NULL },
  { CreditsSectionSpecialThanks, "keres", "Tom Havlik", NULL },
  { CreditsSectionSpecialThanks, "LadyHavoc", "Ashley Hale", NULL },
  { CreditsSectionSpecialThanks, "Lava_Croft", "Kai Holwerda", NULL },
  { CreditsSectionSpecialThanks, "Lunaran", "Matt Breit", NULL },
  { CreditsSectionSpecialThanks, "maci", "Marcel Wysocki", NULL },
  { CreditsSectionSpecialThanks, "Madsy", "Mads Elvheim", NULL },
  { CreditsSectionSpecialThanks, "mattn", "Martin Gerhardy", NULL },
  { CreditsSectionSpecialThanks, "mitomon", "Eduardo Pecina", NULL },
  { CreditsSectionSpecialThanks, "Nilium", "Noel Cower", NULL },
  { CreditsSectionSpecialThanks, "philipk", "Philip Klevstav", NULL },
  { CreditsSectionSpecialThanks, "Preacher", "Mattias Konradsson", NULL },
  { CreditsSectionSpecialThanks, "Quake Revitalization Project", NULL, NULL },
  { CreditsSectionSpecialThanks, "rorshach", "Kevin Johnstone", NULL },
  { CreditsSectionSpecialThanks, "Seargant Science", NULL, NULL },
  { CreditsSectionSpecialThanks, "sock", "Simon O'Callaghan", NULL },
  { CreditsSectionSpecialThanks, "Speedy", NULL, NULL },
  { CreditsSectionSpecialThanks, "spiney", "Pieter Verhoeven", NULL },
  { CreditsSectionSpecialThanks, "Stannum", "Jan van der Weg", NULL },
  { CreditsSectionSpecialThanks, "stereo84", "Stephan Reiter", NULL },
  { CreditsSectionSpecialThanks, "supa_user", "Dale Blount", NULL },
  { CreditsSectionSpecialThanks, "Tabun & Wirehead Studios", NULL, NULL },
  { CreditsSectionSpecialThanks, "tapir", "Coşku Baş", NULL },
  { CreditsSectionSpecialThanks, "The Bunny", "Chris Dillman", NULL },
  { CreditsSectionSpecialThanks, "Thorn", "Michael Rodenhurst", NULL },
  { CreditsSectionSpecialThanks, "Zander Noriega", NULL, NULL },

  { CreditsSectionEngine, NULL, "Quetoo", "GPL-2.0-or-later" },
  { CreditsSectionEngine, NULL, "ObjectivelyMVC", "zlib" },
  { CreditsSectionEngine, NULL, "Objectively", "zlib" },

  { CreditsSectionFonts, NULL, "Coda", "SIL Open Font License 1.1" }
};

#pragma mark - Cards

typedef struct {
  const char *title;

  /**
   * @brief The bordered aside beside the title, or NULL.
   */
  const char *tag;

  /**
   * @brief A display line reading "<lead> <leadName>", the second half accented.
   */
  const char *lead;
  const char *leadName;

  /**
   * @brief Wrapped body copy above the card's rows, or NULL.
   */
  const char *copy;

  /**
   * @brief A button that changes page, or NULL. `link` is its title.
   */
  const char *link;
  CreditsPage linkPage;

  /**
   * @brief Small print under the button, or NULL.
   */
  const char *note;
} CreditsCardDescriptor;

static const CreditsCardDescriptor creditsCards[CREDITS_CARD_COUNT] = {
  {
    .title = "Development assistance",
    .copy = "Tools and AI systems that helped accelerate development and "
            "improve quality."
  },
  {
    .title = "Built on Quetoo",
    .copy = "Race is built on Quetoo by Jay Dolan and contributors.",
    .link = "View original Quetoo credits", .linkPage = CreditsPageQuetoo,
    .note = "Original engine, assets, and upstream contributors are "
            "acknowledged in the Quetoo and Licenses tabs."
  }
};

/**
 * @brief A card's rows. Same left-label, right-value shape as a license row.
 */
typedef struct {
  size_t card;
  const char *label;
  const char *value;

  /**
   * @brief Whether the value names a person, and so carries the accent.
   */
  bool credited;
} CreditsCardEntryDescriptor;

static const CreditsCardEntryDescriptor creditsCardEntries[] = {
  { 0, "AI coding assistants",
       "Code generation, design iteration, UI feedback", false }
};

#pragma mark - Construction

static Label *makeLabel(const char *text, const char *className) {

  Label *label = $(alloc(Label), initWithText, text, NULL);
  assert(label);

  $((View *) label, addClassName, className);
  return label;
}

/**
 * @brief A wrapped paragraph.
 * @details `lineWrap` is a Text inlet rather than a style attribute, so the
 * stylesheet cannot switch it on - it can only set the width the Text wraps to,
 * which is the Label's own frame.
 */
static Label *makeCopy(const char *text, const char *className) {

  Label *label = makeLabel(text, className);
  label->text->lineWrap = true;
  return label;
}

/**
 * @brief The hairline under a row or a section head.
 * @details The dialect has no per-side borders, so every rule in this route is
 * an explicit 1px View.
 */
static View *makeRule(const char *className) {

  View *rule = $(alloc(View), initWithFrame, NULL);
  assert(rule);

  $(rule, addClassName, className);
  return rule;
}

/**
 * @brief "handle (Real Name)", or just the name when there is no handle.
 * @details Two Labels rather than one: a Text carries a single colour, and the
 * accented handle beside the plain real name is the whole point of the pair.
 */
static View *makeName(const CreditsEntryDescriptor *entry) {

  StackView *view = $(alloc(StackView), initWithFrame, NULL);
  assert(view);

  $((View *) view, addClassName, "creditName");
  view->axis = StackViewAxisHorizontal;
  view->view.alignment = ViewAlignmentMiddleLeft;

  if (entry->handle) {

    Label *handle = makeLabel(entry->handle, "creditHandle");
    $((View *) view, addSubview, (View *) handle);
    release(handle);

    if (entry->name) {
      Label *name = makeLabel(va("(%s)", entry->name), "creditRealName");
      $((View *) view, addSubview, (View *) name);
      release(name);
    }

  } else {

    assert(entry->name);

    Label *name = makeLabel(entry->name, "creditPlainName");
    $((View *) view, addSubview, (View *) name);
    release(name);
  }

  return (View *) view;
}

/**
 * @brief One entry of a Credits or Values section: name left, detail right.
 */
static View *makeEntryRow(const CreditsEntryDescriptor *entry, bool last) {

  View *view = $(alloc(View), initWithFrame, NULL);
  assert(view);

  $(view, addClassName, "creditRow");

  View *name = makeName(entry);
  $(view, addSubview, name);
  release(name);

  if (entry->detail) {
    Label *detail = makeLabel(entry->detail, "creditDetail");
    $(view, addSubview, (View *) detail);
    release(detail);
  }

  // A separator under the last row of a section would draw a line to nowhere.
  if (!last) {
    View *rule = makeRule("creditRowRule");
    rule->alignment = ViewAlignmentBottomLeft;
    $(view, addSubview, rule);
    release(rule);
  }

  return view;
}

/**
 * @brief One card row: label left, value right.
 */
static View *makeCardRow(const CreditsCardEntryDescriptor *entry, bool last) {

  View *view = $(alloc(View), initWithFrame, NULL);
  assert(view);

  $(view, addClassName, "creditRow");

  Label *label = makeLabel(entry->label, "creditCardRowLabel");
  $(view, addSubview, (View *) label);
  release(label);

  Label *value = makeLabel(entry->value, entry->credited ? "creditCardRowName"
                                                         : "creditCardRowValue");
  $(view, addSubview, (View *) value);
  release(value);

  if (!last) {
    View *rule = makeRule("creditRowRule");
    rule->alignment = ViewAlignmentBottomLeft;
    $(view, addSubview, rule);
    release(rule);
  }

  return view;
}

/**
 * @brief ButtonDelegate for the page strip, and for the card that links into it.
 */
static void didClickPage(Button *button);

static View *makeCard(CreditsViewController *self, size_t card) {

  const CreditsCardDescriptor *descriptor = &creditsCards[card];

  StackView *view = $(alloc(StackView), initWithFrame, NULL);
  assert(view);

  $((View *) view, addClassName, "creditCard");

  StackView *head = $(alloc(StackView), initWithFrame, NULL);
  $((View *) head, addClassName, "creditCardHead");
  head->axis = StackViewAxisHorizontal;
  head->view.alignment = ViewAlignmentMiddleLeft;

  Label *title = makeLabel(descriptor->title, "creditCardTitle");
  $((View *) head, addSubview, (View *) title);
  release(title);

  if (descriptor->tag) {
    Label *tag = makeLabel(descriptor->tag, "creditCardTag");
    $((View *) head, addSubview, (View *) tag);
    release(tag);
  }

  $((View *) view, addSubview, (View *) head);
  release(head);

  if (descriptor->lead) {

    StackView *lead = $(alloc(StackView), initWithFrame, NULL);
    $((View *) lead, addClassName, "creditCardLead");
    lead->axis = StackViewAxisHorizontal;
    lead->view.alignment = ViewAlignmentMiddleLeft;

    Label *prefix = makeLabel(descriptor->lead, "creditCardLeadPrefix");
    $((View *) lead, addSubview, (View *) prefix);
    release(prefix);

    Label *name = makeLabel(descriptor->leadName, "creditCardLeadName");
    $((View *) lead, addSubview, (View *) name);
    release(name);

    $((View *) view, addSubview, (View *) lead);
    release(lead);
  }

  if (descriptor->copy) {
    Label *copy = makeCopy(descriptor->copy, "creditCardCopy");
    $((View *) view, addSubview, (View *) copy);
    release(copy);
  }

  size_t rows = 0;
  for (size_t i = 0; i < lengthof(creditsCardEntries); i++) {
    if (creditsCardEntries[i].card == card) {
      rows++;
    }
  }

  if (rows) {

    StackView *body = $(alloc(StackView), initWithFrame, NULL);
    $((View *) body, addClassName, "creditRows");

    size_t placed = 0;
    for (size_t i = 0; i < lengthof(creditsCardEntries); i++) {

      if (creditsCardEntries[i].card != card) {
        continue;
      }

      View *row = makeCardRow(&creditsCardEntries[i], ++placed == rows);
      $((View *) body, addSubview, row);
      release(row);
    }

    $((View *) view, addSubview, (View *) body);
    release(body);
  }

  if (descriptor->link) {

    Button *link = $(alloc(Button), initWithTitle, descriptor->link);
    $((View *) link, addClassName, "creditCardLink");
    link->delegate = (ButtonDelegate) {
      .self = self,
      .data = (ident) (intptr_t) descriptor->linkPage,
      .didClick = didClickPage
    };

    $((View *) view, addSubview, (View *) link);
    release(link);
  }

  if (descriptor->note) {
    Label *note = makeCopy(descriptor->note, "creditCardNote");
    $((View *) view, addSubview, (View *) note);
    release(note);
  }

  return (View *) view;
}

/**
 * @brief Builds one section: its eyebrow, its rule, and its entries.
 */
static View *makeSection(CreditsViewController *self, size_t section) {

  const CreditsSectionDescriptor *descriptor = &creditsSections[section];

  StackView *view = $(alloc(StackView), initWithFrame, NULL);
  assert(view);

  $((View *) view, addClassName, "creditSection");

  if (descriptor->label) {

    Label *eyebrow = makeLabel(descriptor->label, "sectionEyebrow");
    $((View *) view, addSubview, (View *) eyebrow);
    release(eyebrow);

    View *rule = makeRule("sectionRule");
    $((View *) view, addSubview, rule);
    release(rule);
  }

  StackView *body = $(alloc(StackView), initWithFrame, NULL);

  // `columns` is what MainView promotes to a ColumnsView, which is this
  // dialect's `repeat(auto-fit, minmax(N, 1fr))`. Both grids in this route are
  // auto-fit in the mock, and neither can be reproduced by a StackView.
  switch (descriptor->layout) {
    case CreditsLayoutCards:
      $((View *) body, addClassName, "columns");
      $((View *) body, addClassName, "creditCards");
      break;
    case CreditsLayoutThanks:
      $((View *) body, addClassName, "columns");
      $((View *) body, addClassName, "creditThanks");
      break;
    default:
      $((View *) body, addClassName, "creditRows");
      break;
  }

  if (descriptor->layout == CreditsLayoutCards) {

    for (size_t card = 0; card < CREDITS_CARD_COUNT; card++) {
      View *cardView = makeCard(self, card);
      $((View *) body, addSubview, cardView);
      release(cardView);
    }

  } else {

    size_t count = 0;
    for (size_t i = 0; i < lengthof(creditsEntries); i++) {
      if ((size_t) creditsEntries[i].section == section) {
        count++;
      }
    }

    size_t placed = 0;
    for (size_t i = 0; i < lengthof(creditsEntries); i++) {

      if ((size_t) creditsEntries[i].section != section) {
        continue;
      }

      placed++;

      View *entry;
      if (descriptor->layout == CreditsLayoutThanks) {
        // A thanks cell is a ColumnsView slot rather than a line inside a row,
        // so it takes the slot's width instead of sizing to its own text.
        entry = makeName(&creditsEntries[i]);
        $(entry, addClassName, "creditThanksName");
      } else {
        entry = makeEntryRow(&creditsEntries[i], placed == count);
      }

      $((View *) body, addSubview, entry);
      release(entry);
    }
  }

  $((View *) view, addSubview, (View *) body);
  release(body);

  return (View *) view;
}

#pragma mark - Pages

/**
 * @brief Marks a view and every ancestor above it as needing layout.
 * @details View::layoutIfNeeded lays out only a view that carries needsLayout,
 * so marking the route root alone leaves everything beneath it untouched: a
 * section revealed by a page change would keep the geometry it had while it was
 * hidden, which is none, and every section would draw at the container's
 * origin. Marking upward from each section is what reaches the ColumnsView -
 * which then re-places its columns and re-marks each one's subtree, see
 * ColumnsView::placeColumn.
 */
static void invalidateLayoutChain(View *view) {

  while (view) {
    view->needsLayout = true;
    view = view->superview;
  }
}

static void selectPage(CreditsViewController *self, size_t page) {

  if (page >= CREDITS_PAGE_COUNT) {
    return;
  }

  self->selectedPage = page;

  for (size_t i = 0; i < CREDITS_PAGE_COUNT; i++) {

    Control *control = (Control *) self->pageButtons[i];
    const unsigned int state = control->state;

    if (i == page) {
      control->state |= ControlStateSelected;
    } else {
      control->state &= ~ControlStateSelected;
    }

    if (control->state != state) {
      $(control, stateDidChange);
    }
  }

  bool columns = false, wide = false;

  for (size_t section = 0; section < CREDITS_SECTION_COUNT; section++) {

    const bool visible = (size_t) creditsSections[section].page == page;

    self->sectionViews[section]->hidden = !visible;
    if (visible) {
      if (creditsSections[section].wide) {
        wide = true;
      } else {
        columns = true;
      }
    }
  }

  // An empty container still costs its parent a spacing gap, so a container
  // with nothing on this page is hidden rather than left standing. Both are
  // looked up by identifier rather than held on the controller: promotion to a
  // ColumnsView replaces the view, but carries its identifier across.
  View *view = self->viewController.view;

  View *columnsView = $(view, descendantWithIdentifier, "creditsColumns");
  if (columnsView) {
    columnsView->hidden = !columns;
  }

  View *wideView = $(view, descendantWithIdentifier, "creditsWideColumn");
  if (wideView) {
    wideView->hidden = !wide;
  }

  for (size_t section = 0; section < CREDITS_SECTION_COUNT; section++) {
    invalidateLayoutChain(self->sectionViews[section]);
  }
}

#pragma mark - Delegates

static void didClickPage(Button *button) {

  CreditsViewController *this = button->delegate.self;

  selectPage(this, (size_t) (intptr_t) button->delegate.data);
}

#pragma mark - ViewController

/**
 * @see ViewController::loadView(ViewController *)
 */
static void loadView(ViewController *viewController) {

  super(ViewController, viewController, loadView);

  CreditsViewController *self = (CreditsViewController *) viewController;

  View *view = $$(View, viewWithResourceName,
                  "ui/credits/CreditsViewController.json", NULL);
  assert(view);
  assert(view->identifier && !q_strcmp(view->identifier, "raceCreditsRoot"));

  view->stylesheet = $$(Stylesheet, stylesheetWithResourceName,
                        "ui/credits/CreditsViewController.css");
  assert(view->stylesheet);

  $(viewController, setView, view);
  release(view);

  View *tabs = $(viewController->view, descendantWithIdentifier, "creditsPageTabs");
  assert(tabs);

  for (size_t page = 0; page < CREDITS_PAGE_COUNT; page++) {

    Button *button = $(alloc(Button), initWithTitle, creditsPageNames[page]);
    assert(button);

    $((View *) button, addClassName, "creditsPageTab");
    button->delegate = (ButtonDelegate) {
      .self = self,
      .data = (ident) (intptr_t) page,
      .didClick = didClickPage
    };

    $(tabs, addSubview, (View *) button);
    self->pageButtons[page] = button;
    release(button);
  }

  View *columns = $(viewController->view, descendantWithIdentifier, "creditsColumns");
  assert(columns);

  View *wide = $(viewController->view, descendantWithIdentifier, "creditsWideColumn");
  assert(wide);

  // Built here rather than authored in the JSON because every section is the
  // same shape over the same roster table - a name that is in the table is then
  // necessarily on a page. Wide sections go to their own full-width container:
  // a ColumnsView column cannot span its siblings, which is what the mock's
  // `grid-column: 1 / -1` does.
  for (size_t section = 0; section < CREDITS_SECTION_COUNT; section++) {

    View *sectionView = makeSection(self, section);
    $(creditsSections[section].wide ? wide : columns, addSubview, sectionView);
    self->sectionViews[section] = sectionView;
    release(sectionView);
  }

  selectPage(self, CreditsPageRace);
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {
  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
}

/**
 * @fn Class *CreditsViewController::_CreditsViewController(void)
 * @memberof CreditsViewController
 */
Class *_CreditsViewController(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "CreditsViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(CreditsViewController),
      .interfaceOffset = offsetof(CreditsViewController, interface),
      .interfaceSize = sizeof(CreditsViewControllerInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
