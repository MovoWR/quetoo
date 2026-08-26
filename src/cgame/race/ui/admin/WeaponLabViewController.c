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

#include <ctype.h>
#include <inttypes.h>
#include <math.h>

#include "race_admin_types.h"

#include "WeaponLabViewController.h"

#include "RaceSlider.h"

#define _Class _WeaponLabViewController

#pragma mark - The catalog

static WeaponLabViewController *activeWeaponLab;

static const cg_race_weapon_tuning_cache_t *authoritative(void) {
  return Cg_RaceWeaponTuning_Cache();
}

static const race_weapon_tuning_descriptor_t *rowDescriptor(const size_t row) {
  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  return cache->complete && row < WEAPON_LAB_ROW_COUNT
    ? cache->descriptors + row
    : NULL;
}

static const char *groupLabel(const size_t group) {
  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  if (!cache->complete) {
    return "Unavailable";
  }
  for (size_t row = 0; row < WEAPON_LAB_ROW_COUNT; row++) {
    if (cache->descriptors[row].group ==
        (race_weapon_tuning_group_t) group) {
      return cache->descriptors[row].group_label;
    }
  }
  return "Unavailable";
}

/**
 * @brief The separator the status and meta lines use between fragments.
 * @details U+00B7, which Coda carries; the design's arrow (U+2192) it does not,
 * so a pending drift reads `1800 -> 2100` rather than `1800 \xe2\x86\x92 2100`.
 * The same substitution the radius takes - see the stylesheet.
 */
#define WEAPON_LAB_DOT " \xc2\xb7 "

/**
 * @brief U+2013, the range separator, and U+2014, the unavailable cell.
 */
#define WEAPON_LAB_EN_DASH "\xe2\x80\x93"
#define WEAPON_LAB_EM_DASH "\xe2\x80\x94"

#pragma mark - Formatting

/**
 * @brief Prints one value the way its row prints it.
 */
static void formatValue(size_t row, double value, char *output, size_t size) {
  const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
  race_weapon_tuning_scalar_t scalar;
  if (!descriptor ||
      !Cg_RaceWeaponTuning_CanonicalValue(descriptor, value, &scalar)) {
    q_strlcpy(output, WEAPON_LAB_EM_DASH, size);
    return;
  }
  if (descriptor->type == RACE_WEAPON_TUNING_TYPE_INT32) {
    snprintf(output, size, "%d", scalar.integer);
  } else if (descriptor->type == RACE_WEAPON_TUNING_TYPE_UINT32) {
    snprintf(output, size, "%u", scalar.unsigned_integer);
  } else if (descriptor->step.real < 1.f) {
    snprintf(output, size, "%.2f", scalar.real);
    for (size_t length = strlen(output);
         length && output[length - 1u] == '0'; length--) {
      output[length - 1u] = '\0';
    }
    const size_t length = strlen(output);
    if (length && output[length - 1u] == '.') {
      output[length - 1u] = '\0';
    }
  } else {
    snprintf(output, size, "%.0f", scalar.real);
  }
}

/**
 * @brief Prints one value with its unit, for the baseline fragment.
 */
static void formatValueWithUnit(size_t row, double value, char *output, size_t size) {

  char number[32];
  formatValue(row, value, number, sizeof(number));

  const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
  if (descriptor && *descriptor->unit) {
    snprintf(output, size, "%s %s", number, descriptor->unit);
  } else {
    q_strlcpy(output, number, size);
  }
}

/**
 * @brief Prints a step, which is only ever a catalog literal.
 */
static void formatStep(size_t row, char *output, size_t size) {

  const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
  double step = 0.0;
  if (!descriptor || !Cg_RaceWeaponTuning_ScalarValue(
        descriptor, descriptor->step, &step)) {
    q_strlcpy(output, WEAPON_LAB_EM_DASH, size);
    return;
  }

  snprintf(output, size, "%.*f", step < 1.0 ? 2 : 0, step);
}

/**
 * @brief Rounds a raw control reading onto the row's step and into its range.
 * @details The design's rule, and the reason a drag cannot produce a value the
 * server would reject: "Values snap to the row's step and clamp to its range."
 */
static double quantize(size_t row, double value) {
  const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
  double minimum, maximum, step, compiledDefault;
  if (!descriptor || !isfinite(value) ||
      !Cg_RaceWeaponTuning_ScalarValue(
        descriptor, descriptor->minimum, &minimum) ||
      !Cg_RaceWeaponTuning_ScalarValue(
        descriptor, descriptor->maximum, &maximum) ||
      !Cg_RaceWeaponTuning_ScalarValue(
        descriptor, descriptor->step, &step) ||
      !Cg_RaceWeaponTuning_ScalarValue(
        descriptor, descriptor->compiled_default, &compiledDefault) ||
      step <= 0.0) {
    return value;
  }
  const double minimumGrid = minimum + round((value - minimum) / step) * step;
  const double defaultGrid = compiledDefault +
    round((value - compiledDefault) / step) * step;
  double snapped = fabs(value - defaultGrid) <= fabs(value - minimumGrid)
    ? defaultGrid
    : minimumGrid;
  snapped = fmin(fmax(snapped, minimum), maximum);
  race_weapon_tuning_scalar_t scalar;
  if (!Cg_RaceWeaponTuning_CanonicalValue(descriptor, snapped, &scalar)) {
    snapped = fmin(fmax(minimumGrid, minimum), maximum);
  }
  return snapped;
}

#pragma mark - View helpers

/**
 * @brief Case-insensitive substring test; an empty query matches everything.
 */
static bool containsIgnoringCase(const char *text, const char *query) {

  if (query == NULL || *query == '\0') {
    return true;
  }

  if (text == NULL) {
    return false;
  }

  const size_t length = strlen(query);

  for (const char *at = text; *at; at++) {

    size_t i = 0;
    while (i < length && at[i] &&
           tolower((unsigned char) at[i]) == tolower((unsigned char) query[i])) {
      i++;
    }

    if (i == length) {
      return true;
    }
  }

  return false;
}

/**
 * @brief Marks the layout dirty from this view up to the route root.
 */
static void invalidateLayoutChain(View *view) {

  while (view) {
    view->needsLayout = true;
    view = view->superview;
  }
}

static void setControlFlag(Control *control, ControlState flag, bool enabled) {

  const unsigned int previous = control->state;

  if (enabled) {
    control->state |= flag;
  } else {
    control->state &= ~flag;
  }

  if (control->state != previous) {
    $(control, stateDidChange);
  }
}

/**
 * @brief Writes a Text only when the string actually changed.
 * @details Every refresh runs on pointer motion, and setting a Text re-renders
 * it; setting a class name re-applies the stylesheet to the whole subtree.
 */
static void setTextIfChanged(Text *text, const char *string) {

  const char *current = text->text;

  if (current == NULL) {
    if (string == NULL || *string == '\0') {
      return;
    }
  } else if (string && !q_strcmp(current, string)) {
    return;
  }

  $(text, setText, string);
}

static void setClassIfChanged(View *view, const char *className, bool present) {

  if ($(view, hasClassName, className) == present) {
    return;
  }

  if (present) {
    $(view, addClassName, className);
  } else {
    $(view, removeClassName, className);
  }
}

static void setHiddenIfChanged(View *view, bool hidden) {

  if (view->hidden != hidden) {
    view->hidden = hidden;
    invalidateLayoutChain(view);
  }
}

#pragma mark - Model

/**
 * @brief The value a row currently shows: this client's draft over the accepted
 * snapshot.
 */
static double rowValue(const WeaponLabViewController *self, size_t row) {
  if (self->drafted[row]) {
    return self->draft[row];
  }
  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  double value = 0.0;
  if (cache->current_valid && Cg_RaceWeaponTuning_SnapshotValue(
        &cache->current, (race_weapon_tuning_id_t) row, &value)) {
    return value;
  }
  const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
  if (descriptor) {
    Cg_RaceWeaponTuning_ScalarValue(
      descriptor, descriptor->compiled_default, &value);
  }
  return value;
}

/**
 * @brief Whether this client holds an edit the server has not seen.
 * @details A draft entry equal to the accepted value is not pending: typing a
 * number back to where it started leaves nothing to apply.
 */
static bool isPending(const WeaponLabViewController *self, size_t row) {
  if (!self->drafted[row]) {
    return false;
  }
  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
  race_weapon_tuning_scalar_t draft;
  if (!cache->current_valid || !descriptor ||
      !Cg_RaceWeaponTuning_CanonicalValue(
        descriptor, self->draft[row], &draft)) {
    return true;
  }
  return memcmp(&draft, &cache->current.values[row], sizeof(draft)) != 0;
}

/**
 * @brief Whether the accepted value differs from the server baseline, which is
 * what the accent dot means.
 */
static bool isMoved(const WeaponLabViewController *self, size_t row) {
  (void) self;
  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  return cache->baseline_valid && cache->current_valid &&
         memcmp(&cache->baseline.values[row], &cache->current.values[row],
                sizeof(cache->current.values[row])) != 0;
}

static size_t pendingCount(const WeaponLabViewController *self) {

  size_t count = 0;

  for (size_t row = 0; row < WEAPON_LAB_ROW_COUNT; row++) {
    count += isPending(self, row) ? 1u : 0u;
  }

  return count;
}

/**
 * @brief True while a whole-snapshot operation must stay disabled.
 */
static bool isBusy(const WeaponLabViewController *self) {

  return pendingCount(self) > 0;
}

/**
 * @brief Whether the authoritative state currently permits this client to mutate.
 */
static bool isEditable(const WeaponLabViewController *self) {
  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  return (self->capabilities & RACE_ADMIN_CAP_SETTINGS_MUTATE) != 0 &&
         cache->complete && cache->synchronized && cache->baseline_valid &&
         cache->current_valid &&
         (cache->metadata.state == RACE_WEAPON_TUNING_STATE_INACTIVE ||
          cache->metadata.state == RACE_WEAPON_TUNING_STATE_ACTIVE) &&
         (cache->metadata.flags & RACE_WEAPON_TUNING_SYNC_CAN_MUTATE) != 0;
}

/**
 * @brief Whether this row is matched by the query the host handed down.
 * @details The design searches group name, row label and key, so a query for
 * "hyper" reaches the group and a query for "refire" reaches four rows across
 * four groups.
 */
static bool rowMatches(size_t row, const char *query) {
  const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
  return descriptor &&
    (containsIgnoringCase(descriptor->group_label, query) ||
     containsIgnoringCase(descriptor->label, query) ||
     containsIgnoringCase(descriptor->key, query));
}

static void clearDraft(WeaponLabViewController *self) {

  memset(self->drafted, 0, sizeof(self->drafted));
  memset(self->draft, 0, sizeof(self->draft));
  self->draftGeneration = 0u;
}

static void captureDraftBase(WeaponLabViewController *self) {
  if (pendingCount(self) == 0u) {
    const cg_race_weapon_tuning_cache_t *cache = authoritative();
    self->draftGeneration = cache->metadata.generation;
  }
}

static bool draftStale(const WeaponLabViewController *self) {
  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  return pendingCount(self) > 0u &&
    self->draftGeneration != cache->metadata.generation;
}

#pragma mark - Commands

static void refresh(WeaponLabViewController *self);

/**
 * @brief Sends one tuning command and records it in the Admin response block.
 * @details Sent requests remain pending until the correlated GAME result and,
 * for accepted mutations, its complete authoritative broadcast arrive. A
 * deliberately unsent request retains its specific local reason.
 * @param command The command, without its trailing newline.
 * @param note What the panel did with it locally.
 */
static void postRequest(WeaponLabViewController *self, const char *operation,
                        const uint32_t request, const char *note) {
  if (self->delegate.didPostCommand) {
    self->delegate.didPostCommand(
      self->delegate.self,
      request ? va("race tune %s req=%" PRIu32, operation, request)
              : va("race tune %s", operation),
      note && *note ? note
                    : "Request not sent: sign in with settings permission, wait "
                      "for current server values, or let the prior update finish.");
  }
}

#pragma mark - Delegates

/**
 * @brief SliderDelegate for a parameter row.
 * @details A drag writes the local draft and nothing else. The doc's reason is
 * hard: the server accepts only seven string commands per parsed packet, so a
 * control that sent per motion would lose most of what it sent and desynchronize
 * the panel from the snapshot.
 */
static void didSetRowValue(Slider *slider, double value) {

  WeaponLabViewController *self = slider->delegate.self;

  if (self->refreshing) {
    return;
  }

  // SliderDelegate carries no user data, so the row is the control's identity.
  for (size_t row = 0; row < WEAPON_LAB_ROW_COUNT; row++) {

    if (self->rowSliders[row] != slider) {
      continue;
    }

    if (!isEditable(self) || draftStale(self) ||
        Cg_RaceWeaponTuning_MutationPending()) {
      return;
    }
    captureDraftBase(self);
    self->draft[row] = quantize(row, value);
    self->drafted[row] = true;

    refresh(self);
    return;
  }
}

/**
 * @brief TextViewDelegate for a row's numeric field.
 * @details The field accepts free text while it is being typed and commits only
 * when what it holds parses inside the row's range, so backspacing through a
 * number does not snap the slider to the range minimum on the way.
 */
static void didEditRowField(TextView *textView) {

  WeaponLabViewController *self = textView->delegate.self;

  if (self->refreshing) {
    return;
  }

  const size_t row = (size_t) (intptr_t) textView->delegate.data;
  const String *text = textView->attributedText;
  const char *string = text && text->chars ? text->chars : "";

  char *end = NULL;
  const double value = strtod(string, &end);

  if (end == string || (end && *end != '\0') || !isfinite(value) ||
      !isEditable(self) || draftStale(self) ||
      Cg_RaceWeaponTuning_MutationPending()) {
    return;
  }

  const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
  race_weapon_tuning_scalar_t scalar;
  if (!descriptor ||
      !Cg_RaceWeaponTuning_CanonicalValue(descriptor, value, &scalar)) {
    return;
  }
  captureDraftBase(self);
  Cg_RaceWeaponTuning_ScalarValue(descriptor, scalar, self->draft + row);
  self->drafted[row] = true;

  refresh(self);
}

/**
 * @brief ButtonDelegate for a collapsed group card.
 */
static void didClickGroupToggle(Button *button) {

  WeaponLabViewController *self = button->delegate.self;

  self->openGroup = (size_t) (intptr_t) button->delegate.data;

  refresh(self);
}

/**
 * @brief ButtonDelegate for Apply.
 * @details One batch against the generation on screen. A stale generation is
 * the server's to reject - the panel never merges - and the pair count is
 * bounded inside the server's thirty-two-pair and 768-character limits.
 */
static void didClickApply(Button *button) {

  WeaponLabViewController *self = button->delegate.self;

  if (draftStale(self)) {
    postRequest(self, "apply", 0u,
                "Server values changed while you were editing. Reopen the "
                "panel or use Reset All before applying another draft.");
    return;
  }
  char pairs[768] = "";
  size_t length = 0u;

  for (size_t row = 0; row < WEAPON_LAB_ROW_COUNT; row++) {

    if (!isPending(self, row)) {
      continue;
    }

    char value[32];
    formatValue(row, self->draft[row], value, sizeof(value));

    const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
    if (!descriptor) {
      return;
    }
    const int written = snprintf(pairs + length, sizeof(pairs) - length,
                                 "%s%s=%s", length ? " " : "",
                                 descriptor->key, value);

    if (written < 0 || (size_t) written >= sizeof(pairs) - length) {
      cgi.Print("Weapon tuning batch is too long to send as one command; "
                "apply fewer values\n");
      return;
    }

    length += (size_t) written;
  }
  const uint32_t request = Cg_RaceWeaponTuning_RequestApply(
    self->draftGeneration, pairs);
  postRequest(self, "apply", request,
              "Apply sent; waiting for the server result and current values.");
  refresh(self);
}

/**
 * @brief Restores every weapon value to the runtime-captured server baseline.
 */
static void didClickResetAll(Button *button) {

  WeaponLabViewController *self = button->delegate.self;

  const uint32_t request = Cg_RaceWeaponTuning_RequestResetAll();
  if (request) {
    clearDraft(self);
  }
  postRequest(self, "reset", request,
              "Reset All sent; waiting for the server result and default values.");
  refresh(self);
}

#pragma mark - Construction

/**
 * @brief Builds one parameter row.
 * @details Three bands stacked: the label and its controls on the first, the
 * meta line under both, and the separator under that. The first band is a plain
 * View rather than a StackView so the label pins left and the control cluster
 * pins right however long the label runs, which is the design's `"lab ctl"`.
 */
static View *makeRow(WeaponLabViewController *self, size_t row) {

  const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
  assert(descriptor);

  double minimum, maximum, step;
  assert(Cg_RaceWeaponTuning_ScalarValue(
    descriptor, descriptor->minimum, &minimum));
  assert(Cg_RaceWeaponTuning_ScalarValue(
    descriptor, descriptor->maximum, &maximum));
  assert(Cg_RaceWeaponTuning_ScalarValue(
    descriptor, descriptor->step, &step));

  StackView *view = $(alloc(StackView), initWithFrame, NULL);
  $((View *) view, addClassName, "weaponLabRow");
  ((View *) view)->identifier = q_strdup(descriptor->key);

  View *band = $(alloc(View), initWithFrame, NULL);
  $(band, addClassName, "weaponLabRowBand");

  StackView *left = $(alloc(StackView), initWithFrame, NULL);
  $((View *) left, addClassName, "weaponLabRowLeft");
  left->axis = StackViewAxisHorizontal;

  // The accent dot, which means the *accepted* value differs from the server
  // baseline. Never the same statement as the gold pending fragment below.
  View *dot = $(alloc(View), initWithFrame, NULL);
  $(dot, addClassName, "weaponLabRowDot");
  dot->hidden = true;
  $((View *) left, addSubview, dot);
  self->rowDots[row] = dot;
  release(dot);

  Label *label = $(alloc(Label), initWithText, descriptor->label, NULL);
  $((View *) label, addClassName, "weaponLabRowLabel");
  $((View *) left, addSubview, (View *) label);
  self->rowLabels[row] = label;
  release(label);

  $(band, addSubview, (View *) left);
  release(left);

  StackView *control = $(alloc(StackView), initWithFrame, NULL);
  $((View *) control, addClassName, "weaponLabRowControl");
  control->axis = StackViewAxisHorizontal;

  // A RaceSlider with no cvar behind it: this row's authority is a GAME
  // snapshot, not a client variable. CvarSlider tolerates a NULL variable by
  // design - it neither reads nor writes one - and what the subclass adds is
  // the shell's filled track, which is the treatment the design asks for.
  RaceSlider *slider = (RaceSlider *) $((CvarSlider *) alloc(RaceSlider),
                                        initWithVariable, NULL, minimum,
                                        maximum, step);
  $((View *) slider, addClassName, "weaponLabSlider");

  // The readout is suppressed: the numeric field beside it is the value, and
  // two of them disagreeing mid-drag is worse than none. An authored format
  // survives CvarSlider::updateBindings, which would otherwise force "%g" back
  // on every pass - see RaceSlider::labelFormatAuthored.
  $((Slider *) slider, setLabelFormat, "");

  ((Slider *) slider)->delegate = (SliderDelegate) {
    .self = self,
    .didSetValue = didSetRowValue
  };

  $((View *) control, addSubview, (View *) slider);
  self->rowSliders[row] = (Slider *) slider;
  release(slider);

  TextView *field = $(alloc(TextView), initWithFrame, NULL);
  $((View *) field, addClassName, "weaponLabRowField");
  field->delegate = (TextViewDelegate) {
    .self = self,
    .data = (ident) (intptr_t) row,
    .didEdit = didEditRowField,
    .didEndEditing = didEditRowField
  };
  $((View *) control, addSubview, (View *) field);
  self->rowFields[row] = field;
  release(field);

  Label *unit = $(alloc(Label), initWithText, descriptor->unit, NULL);
  $((View *) unit, addClassName, "weaponLabRowUnit");
  $((View *) control, addSubview, (View *) unit);
  release(unit);

  $(band, addSubview, (View *) control);
  self->rowControls[row] = (View *) control;
  release(control);

  // An unavailable catalog draws one em dash here instead of a dead slider: there
  // is no value to show, and a disabled control would imply there was one.
  Label *unavailable = $(alloc(Label), initWithText, WEAPON_LAB_EM_DASH, NULL);
  $((View *) unavailable, addClassName, "weaponLabRowUnavailable");
  unavailable->view.hidden = true;
  $(band, addSubview, (View *) unavailable);
  self->rowUnavailable[row] = unavailable;
  release(unavailable);

  $((View *) view, addSubview, band);
  release(band);

  Label *meta = $(alloc(Label), initWithText, "", NULL);
  $((View *) meta, addClassName, "weaponLabRowMeta");
  $((View *) view, addSubview, (View *) meta);
  self->rowMeta[row] = meta;
  release(meta);

  // The dialect has no per-side borders, so the row rule is an explicit
  // hairline. Riding inside the row means the filter takes both together, and
  // the last visible row of a card drops it rather than ruling to nowhere.
  View *rule = $(alloc(View), initWithFrame, NULL);
  $(rule, addClassName, "weaponLabRowRule");
  $((View *) view, addSubview, rule);
  self->rowRules[row] = rule;
  release(rule);

  return (View *) view;
}

/**
 * @brief Builds one group card: a collapsed header and an expanded body, of
 * which exactly one is ever visible.
 * @details Both are built once and shown or hidden, rather than rebuilt on
 * every expansion. Section Phase 5 requires stable key-derived controls across
 * generation updates, and a control that is destroyed takes the focus, the
 * caret and the scroll position with it.
 */
static View *makeCard(WeaponLabViewController *self, size_t group) {

  const char *label = groupLabel(group);
  StackView *card = $(alloc(StackView), initWithFrame, NULL);
  $((View *) card, addClassName, "weaponLabCard");

  Button *toggle = $(alloc(Button), initWithTitle, label);
  $((View *) toggle, addClassName, "weaponLabCardToggle");
  toggle->delegate = (ButtonDelegate) {
    .self = self,
    .data = (ident) (intptr_t) group,
    .didClick = didClickGroupToggle
  };

  Label *toggleCount = $(alloc(Label), initWithText, "", NULL);
  $((View *) toggleCount, addClassName, "weaponLabCardToggleCount");
  $((View *) toggle, addSubview, (View *) toggleCount);
  self->groupToggleCounts[group] = toggleCount;
  release(toggleCount);

  $((View *) card, addSubview, (View *) toggle);
  self->groupToggles[group] = toggle;
  release(toggle);

  StackView *expanded = $(alloc(StackView), initWithFrame, NULL);
  $((View *) expanded, addClassName, "weaponLabCardBody");

  View *head = $(alloc(View), initWithFrame, NULL);
  $(head, addClassName, "weaponLabCardHead");

  Label *name = $(alloc(Label), initWithText, label, NULL);
  $((View *) name, addClassName, "weaponLabCardName");
  $(head, addSubview, (View *) name);
  release(name);

  Label *count = $(alloc(Label), initWithText, "", NULL);
  $((View *) count, addClassName, "weaponLabCardCount");
  $(head, addSubview, (View *) count);
  self->groupCounts[group] = count;
  release(count);

  $((View *) expanded, addSubview, head);
  release(head);

  View *headRule = $(alloc(View), initWithFrame, NULL);
  $(headRule, addClassName, "weaponLabCardHeadRule");
  $((View *) expanded, addSubview, headRule);
  release(headRule);

  for (size_t row = 0; row < WEAPON_LAB_ROW_COUNT; row++) {

    const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
    if (!descriptor || descriptor->group !=
        (race_weapon_tuning_group_t) group) {
      continue;
    }

    View *view = makeRow(self, row);
    $((View *) expanded, addSubview, view);
    self->rowViews[row] = view;
    release(view);
  }

  $((View *) card, addSubview, (View *) expanded);
  self->groupExpanded[group] = (View *) expanded;
  release(expanded);

  return (View *) card;
}

/**
 * @brief Drops generated catalog controls before accepting a different catalog.
 */
static void clearCatalogViews(WeaponLabViewController *self) {

  for (size_t group = 0; group < WEAPON_LAB_GROUP_COUNT; group++) {
    View *card = self->groupCards[group];
    if (card && card->superview) {
      $(card->superview, removeSubview, card);
    }
  }

  memset(self->groupCards, 0, sizeof(self->groupCards));
  memset(self->groupExpanded, 0, sizeof(self->groupExpanded));
  memset(self->groupToggles, 0, sizeof(self->groupToggles));
  memset(self->groupToggleCounts, 0, sizeof(self->groupToggleCounts));
  memset(self->groupCounts, 0, sizeof(self->groupCounts));
  memset(self->rowViews, 0, sizeof(self->rowViews));
  memset(self->rowDots, 0, sizeof(self->rowDots));
  memset(self->rowRules, 0, sizeof(self->rowRules));
  memset(self->rowControls, 0, sizeof(self->rowControls));
  memset(self->rowUnavailable, 0, sizeof(self->rowUnavailable));
  memset(self->rowLabels, 0, sizeof(self->rowLabels));
  memset(self->rowMeta, 0, sizeof(self->rowMeta));
  memset(self->rowSliders, 0, sizeof(self->rowSliders));
  memset(self->rowFields, 0, sizeof(self->rowFields));
  self->catalogBuilt = false;
  self->catalogHash = 0u;
}

/**
 * @brief Generates all five groups from the complete authoritative catalog.
 */
static bool buildCatalog(WeaponLabViewController *self) {

  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  if (!cache->complete) {
    return false;
  }
  if (self->catalogBuilt && self->catalogHash == cache->metadata.catalog_hash) {
    return true;
  }

  clearCatalogViews(self);

  View *grid = $(self->viewController.view, descendantWithIdentifier,
                 "weaponLabGroups");
  if (!grid) {
    return false;
  }

  for (size_t group = 0; group < WEAPON_LAB_GROUP_COUNT; group++) {
    View *card = makeCard(self, group);
    $(grid, addSubview, card);
    self->groupCards[group] = card;
    release(card);
  }

  self->catalogHash = cache->metadata.catalog_hash;
  self->catalogBuilt = true;
  return true;
}

#pragma mark - Refresh

static const char *stateLabel(race_weapon_tuning_state_t state) {
  switch (state) {
    case RACE_WEAPON_TUNING_STATE_INACTIVE:
      return "Default values";
    case RACE_WEAPON_TUNING_STATE_ACTIVE:
      return "Custom values";
    case RACE_WEAPON_TUNING_STATE_TRANSITION:
      return "Transition";
    case RACE_WEAPON_TUNING_STATE_RECOVERY:
      return "Recovery";
    case RACE_WEAPON_TUNING_STATE_ERROR:
      return "Error";
    default:
      return "Unknown";
  }
}

static const char *operationLabel(race_weapon_tuning_operation_t operation) {
  switch (operation) {
    case RACE_WEAPON_TUNING_OPERATION_SYNC:
      return "sync";
    case RACE_WEAPON_TUNING_OPERATION_APPLY:
      return "apply";
    case RACE_WEAPON_TUNING_OPERATION_RESET_ALL:
      return "reset";
    default:
      return "request";
  }
}

static const char *resultLabel(race_weapon_tuning_result_t result) {
  switch (result) {
    case RACE_WEAPON_TUNING_RESULT_OK:
      return "accepted";
    case RACE_WEAPON_TUNING_RESULT_NOOP:
      return "no change";
    case RACE_WEAPON_TUNING_RESULT_DENIED:
      return "denied";
    case RACE_WEAPON_TUNING_RESULT_INVALID:
      return "invalid";
    case RACE_WEAPON_TUNING_RESULT_STALE:
      return "stale";
    case RACE_WEAPON_TUNING_RESULT_INACTIVE:
      return "inactive";
    case RACE_WEAPON_TUNING_RESULT_ACTIVE:
      return "already active";
    case RACE_WEAPON_TUNING_RESULT_UNAVAILABLE:
      return "unavailable";
    case RACE_WEAPON_TUNING_RESULT_NOT_FOUND:
      return "not found";
    case RACE_WEAPON_TUNING_RESULT_INTERNAL:
      return "server error";
    default:
      return "unknown";
  }
}

/**
 * @brief Reconciles local draft bytes with a newly committed GAME transaction.
 */
static void reconcileAuthoritative(WeaponLabViewController *self) {

  const cg_race_weapon_tuning_cache_t *cache = authoritative();

  if (!cache->complete) {
    if (self->cacheRevision || self->catalogBuilt) {
      clearDraft(self);
      clearCatalogViews(self);
    }
    self->cacheRevision = cache->revision;
  } else if (self->cacheRevision != cache->revision) {
    if (self->catalogBuilt &&
        self->catalogHash != cache->metadata.catalog_hash) {
      clearDraft(self);
    } else if (cache->current_valid) {
      for (size_t row = 0; row < WEAPON_LAB_ROW_COUNT; row++) {
        if (!self->drafted[row]) {
          continue;
        }
        const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
        race_weapon_tuning_scalar_t scalar;
        if (descriptor && Cg_RaceWeaponTuning_CanonicalValue(
              descriptor, self->draft[row], &scalar) &&
            !memcmp(&scalar, cache->current.values + row, sizeof(scalar))) {
          self->drafted[row] = false;
          self->draft[row] = 0.0;
        }
      }
      if (!pendingCount(self)) {
        self->draftGeneration = 0u;
      }
    }
    self->cacheRevision = cache->revision;
  }

  if (self->resultRevision != cache->result_revision) {
    self->resultRevision = cache->result_revision;
    if (cache->result_valid && self->delegate.didPostCommand) {
      const race_weapon_tuning_result_message_t *result = &cache->result;
      self->delegate.didPostCommand(
        self->delegate.self,
        va("race tune %s req=%" PRIu32, operationLabel(result->operation),
           result->request_id),
        va("GAME %s at G%" PRIu64 ": %s", resultLabel(result->result),
           result->generation, result->text));
    }
  }
}

/**
 * @brief Repaints the status strip: what is running, and which snapshot.
 */
static void refreshStatus(WeaponLabViewController *self) {

  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  const race_weapon_tuning_state_t state = cache->status_valid
    ? cache->status.state
    : cache->complete ? cache->metadata.state : RACE_WEAPON_TUNING_STATE_TOTAL;
  const bool custom = state == RACE_WEAPON_TUNING_STATE_ACTIVE;
  const bool defaults = state == RACE_WEAPON_TUNING_STATE_INACTIVE;

  setClassIfChanged(self->statusStrip, "inactive", !custom);
  setHiddenIfChanged(self->unrankedChip, !custom);
  setTextIfChanged(self->stateChip->text, stateLabel(state));

  if (!cache->status_valid || !cache->complete || !cache->synchronized) {
    setTextIfChanged(self->statusDetail->text,
                     "UNSYNCHRONIZED" WEAPON_LAB_DOT
                     "awaiting one complete authoritative GAME transaction"
                     WEAPON_LAB_DOT "runs are UNRANKED");
  } else if (custom) {
    setTextIfChanged(self->statusDetail->text,
                      va("%s" WEAPON_LAB_DOT "GAME authoritative"
                         WEAPON_LAB_DOT "custom values are UNRANKED",
                         cache->metadata.identity));
  } else if (defaults) {
    setTextIfChanged(self->statusDetail->text,
                     "GAME authoritative" WEAPON_LAB_DOT
                     "server default values are active");
  } else {
    setTextIfChanged(self->statusDetail->text,
                     va("%s" WEAPON_LAB_DOT
                        "mutation controls locked until GAME recovers",
                        cache->metadata.identity));
  }

  const bool canAdminister =
    (self->capabilities & RACE_ADMIN_CAP_SETTINGS_MUTATE) != 0;
  setTextIfChanged(self->roleChip->text,
                   canAdminister ? "Editor" : "Viewer");
}

/**
 * @brief Repaints one parameter row.
 */
static void refreshRow(WeaponLabViewController *self, size_t row) {

  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
  if (!descriptor || !self->rowViews[row]) {
    return;
  }
  const bool available = cache->complete && cache->synchronized &&
    cache->baseline_valid && cache->current_valid &&
    (cache->metadata.state == RACE_WEAPON_TUNING_STATE_INACTIVE ||
     cache->metadata.state == RACE_WEAPON_TUNING_STATE_ACTIVE);
  const bool pending = isPending(self, row);
  const bool editable = isEditable(self);
  const bool locked = !editable || Cg_RaceWeaponTuning_MutationPending() ||
    draftStale(self);

  setHiddenIfChanged(self->rowDots[row], !available || !isMoved(self, row));

  setClassIfChanged(self->rowViews[row], "pending", pending);
  setClassIfChanged(self->rowViews[row], "moved",
                    available && isMoved(self, row));

  setHiddenIfChanged(self->rowControls[row], !available);
  setHiddenIfChanged((View *) self->rowUnavailable[row], available);

  if (available) {

    const double value = rowValue(self, row);

    $(self->rowSliders[row], setValue, value);

    char text[32];
    formatValue(row, value, text, sizeof(text));

    // The field is left alone while it has focus. It accepts free text as it is
    // typed and commits only what parses inside range, so rewriting it from the
    // model mid-entry would take the caret and the half-typed number with it.
    if (!$((Control *) self->rowFields[row], isFocused)) {
      $(self->rowFields[row], setAttributedText, text);
    }

    setControlFlag((Control *) self->rowSliders[row], ControlStateDisabled,
                   locked);
    setControlFlag((Control *) self->rowFields[row], ControlStateDisabled,
                   locked);
  }

  char baseline[64], range[64], step[16];
  double baselineValue = 0.0;
  if (available) {
    Cg_RaceWeaponTuning_SnapshotValue(
      &cache->baseline, (race_weapon_tuning_id_t) row, &baselineValue);
  } else {
    Cg_RaceWeaponTuning_ScalarValue(
      descriptor, descriptor->compiled_default, &baselineValue);
  }
  formatValueWithUnit(row, baselineValue, baseline, sizeof(baseline));
  formatStep(row, step, sizeof(step));

  double minimum, maximum;
  Cg_RaceWeaponTuning_ScalarValue(descriptor, descriptor->minimum, &minimum);
  Cg_RaceWeaponTuning_ScalarValue(descriptor, descriptor->maximum, &maximum);
  char low[32], high[32];
  formatValue(row, minimum, low, sizeof(low));
  formatValue(row, maximum, high, sizeof(high));
  snprintf(range, sizeof(range), "%s" WEAPON_LAB_EN_DASH "%s" WEAPON_LAB_DOT
           "step %s", low, high, step);

  if (pending) {

    char from[32], to[32];
    double accepted = 0.0;
    Cg_RaceWeaponTuning_SnapshotValue(
      &cache->current, (race_weapon_tuning_id_t) row, &accepted);
    formatValue(row, accepted, from, sizeof(from));
    formatValue(row, self->draft[row], to, sizeof(to));

    setTextIfChanged(self->rowMeta[row]->text,
                     va("baseline %s" WEAPON_LAB_DOT "%s" WEAPON_LAB_DOT
                        "pending %s -> %s", baseline, range, from, to));
  } else {
    setTextIfChanged(self->rowMeta[row]->text,
                     va("baseline %s" WEAPON_LAB_DOT "%s", baseline, range));
  }
}

/**
 * @brief Repaints the group grid: which card is open, which rows the query
 * left standing, and where each card's last rule falls.
 * @details One group is expanded at a time, which is the doc's mock and what
 * keeps the catalog from reading as a wall. A query overrides that: every
 * group holding a hit opens, because a match the reader cannot see is not a
 * match they can act on.
 */
static void refreshGroups(WeaponLabViewController *self) {

  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  if (!self->catalogBuilt || !cache->complete) {
    return;
  }
  const bool available = cache->synchronized && cache->baseline_valid &&
    cache->current_valid &&
    (cache->metadata.state == RACE_WEAPON_TUNING_STATE_INACTIVE ||
     cache->metadata.state == RACE_WEAPON_TUNING_STATE_ACTIVE);
  const bool filtering = *self->query != '\0';

  size_t groupHits[WEAPON_LAB_GROUP_COUNT] = { 0 };
  size_t groupRows[WEAPON_LAB_GROUP_COUNT] = { 0 };
  size_t groupChanged[WEAPON_LAB_GROUP_COUNT] = { 0 };
  size_t lastVisible[WEAPON_LAB_GROUP_COUNT];

  for (size_t group = 0; group < WEAPON_LAB_GROUP_COUNT; group++) {
    lastVisible[group] = WEAPON_LAB_ROW_COUNT;
  }

  for (size_t row = 0; row < WEAPON_LAB_ROW_COUNT; row++) {

    const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
    if (!descriptor || descriptor->group >= WEAPON_LAB_GROUP_COUNT) {
      continue;
    }
    const size_t group = descriptor->group;
    const bool visible = rowMatches(row, self->query);

    groupRows[group]++;

    if (available && (isMoved(self, row) || isPending(self, row))) {
      groupChanged[group]++;
    }

    setHiddenIfChanged(self->rowViews[row], !visible);

    if (visible) {
      groupHits[group]++;
      lastVisible[group] = row;
      refreshRow(self, row);
    }
  }

  for (size_t row = 0; row < WEAPON_LAB_ROW_COUNT; row++) {
    const race_weapon_tuning_descriptor_t *descriptor = rowDescriptor(row);
    if (descriptor && self->rowRules[row]) {
      setHiddenIfChanged(self->rowRules[row],
                         lastVisible[descriptor->group] == row);
    }
  }

  // A query that empties the open group would otherwise leave the grid showing
  // nothing but headers.
  if (!filtering && groupHits[self->openGroup] == 0) {
    for (size_t group = 0; group < WEAPON_LAB_GROUP_COUNT; group++) {
      if (groupHits[group]) {
        self->openGroup = group;
        break;
      }
    }
  }

  for (size_t group = 0; group < WEAPON_LAB_GROUP_COUNT; group++) {

    const bool open = filtering || group == self->openGroup;

    setHiddenIfChanged(self->groupCards[group], groupHits[group] == 0);
    setHiddenIfChanged(self->groupExpanded[group], !open);
    setHiddenIfChanged((View *) self->groupToggles[group], open);
    setClassIfChanged(self->groupCards[group], "collapsed", !open);

    setTextIfChanged(self->groupCounts[group]->text,
                     va("%zu / %zu", groupHits[group], groupRows[group]));

    if (groupChanged[group]) {
      setTextIfChanged(self->groupToggleCounts[group]->text,
                       va("%zu values" WEAPON_LAB_DOT "%zu changed",
                          groupRows[group], groupChanged[group]));
    } else {
      setTextIfChanged(self->groupToggleCounts[group]->text,
                       va("%zu values", groupRows[group]));
    }

  }
}

/**
 * @brief Repaints the commit footer.
 */
static void refreshFooter(WeaponLabViewController *self) {

  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  const bool available = cache->complete && cache->synchronized &&
    cache->baseline_valid && cache->current_valid &&
    (cache->metadata.state == RACE_WEAPON_TUNING_STATE_INACTIVE ||
     cache->metadata.state == RACE_WEAPON_TUNING_STATE_ACTIVE);
  const size_t pending = pendingCount(self);
  const bool editable = isEditable(self);
  const bool draft = pending > 0;
  const bool mutation = Cg_RaceWeaponTuning_MutationPending();
  const bool stale = draftStale(self);

  size_t hits = 0;
  for (size_t row = 0; row < WEAPON_LAB_ROW_COUNT; row++) {
    hits += rowMatches(row, self->query) ? 1u : 0u;
  }

  setTextIfChanged(self->pendingCount->text,
                   available ? stale ? "Server values changed while editing"
                                     : va("Pending %zu", pending)
                             : "");
  setClassIfChanged((View *) self->pendingCount, "pending", draft);
  setHiddenIfChanged((View *) self->pendingCount, !available);

  setTextIfChanged(self->valueCount->text,
                    va("%zu of %zu values", hits,
                       (size_t) WEAPON_LAB_ROW_COUNT));

  setControlFlag((Control *) self->resetButton, ControlStateDisabled,
                 !available || !editable || mutation);

  setTextIfChanged(self->applyButton->title, "Apply");
  setControlFlag((Control *) self->applyButton, ControlStateDisabled,
                 !available || !editable || !draft || mutation || stale);
}

static void refresh(WeaponLabViewController *self) {

  const cg_race_weapon_tuning_cache_t *cache = authoritative();

  self->refreshing = true;

  reconcileAuthoritative(self);
  buildCatalog(self);

  refreshStatus(self);
  refreshGroups(self);
  refreshFooter(self);

  self->refreshing = false;

  invalidateLayoutChain(self->viewController.view);

  self->statusState = cache->status_valid
    ? cache->status.state
    : RACE_WEAPON_TUNING_STATE_TOTAL;
  self->synchronized = cache->synchronized;
  self->mutationPending = Cg_RaceWeaponTuning_MutationPending();
}

#pragma mark - Host interface

void WeaponLabViewController_SetDelegate(ViewController *self,
                                         const WeaponLabDelegate *delegate) {

  assert(self);

  ((WeaponLabViewController *) self)->delegate = delegate
    ? *delegate
    : (WeaponLabDelegate) { NULL, NULL };
}

void WeaponLabViewController_SetCapabilities(ViewController *self,
                                             uint16_t capabilities) {

  assert(self);

  WeaponLabViewController *this = (WeaponLabViewController *) self;

  if (this->capabilities == capabilities) {
    return;
  }

  this->capabilities = capabilities;

  // Authority can be revoked while the route is open, and a draft staged as an
  // editor must not survive the demotion: the values on screen would keep
  // claiming an edit this client can no longer make.
  if (!isEditable(this)) {
    clearDraft(this);
  }

  refresh(this);
}

void WeaponLabViewController_SetQuery(ViewController *self, const char *query) {

  assert(self);

  WeaponLabViewController *this = (WeaponLabViewController *) self;

  if (!q_strcmp(this->query, query ? query : "")) {
    return;
  }

  q_strlcpy(this->query, query ? query : "", sizeof(this->query));

  refresh(this);
}

size_t WeaponLabViewController_Hits(const char *query) {

  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  if (!cache->complete) {
    return query == NULL || *query == '\0' ? 1u : 0u;
  }

  size_t hits = 0;

  for (size_t row = 0; row < WEAPON_LAB_ROW_COUNT; row++) {
    hits += rowMatches(row, query) ? 1u : 0u;
  }

  return hits;
}

void WeaponLabViewController_RefreshAuthoritativeState(void) {

  if (!activeWeaponLab || !activeWeaponLab->viewController.view) {
    return;
  }

  const cg_race_weapon_tuning_cache_t *cache = authoritative();
  const race_weapon_tuning_state_t statusState = cache->status_valid
    ? cache->status.state
    : RACE_WEAPON_TUNING_STATE_TOTAL;
  const bool mutationPending = Cg_RaceWeaponTuning_MutationPending();

  if (cache->revision == activeWeaponLab->cacheRevision &&
      cache->result_revision == activeWeaponLab->resultRevision &&
      statusState == activeWeaponLab->statusState &&
      cache->synchronized == activeWeaponLab->synchronized &&
      mutationPending == activeWeaponLab->mutationPending) {
    return;
  }

  refresh(activeWeaponLab);
}

bool WeaponLabViewController_HasPendingDraft(const ViewController *self) {

  return self ? isBusy((const WeaponLabViewController *) self) : false;
}

void WeaponLabViewController_DiscardDraft(ViewController *self) {

  assert(self);

  WeaponLabViewController *this = (WeaponLabViewController *) self;

  clearDraft(this);
  refresh(this);
}

#pragma mark - ViewController

static void resolveOutlets(WeaponLabViewController *self) {

  Outlet outlets[] = MakeOutlets(
    MakeOutlet("weaponLabStatus", &self->statusStrip),
    MakeOutlet("weaponLabUnranked", &self->unrankedChip),
    MakeOutlet("weaponLabState", &self->stateChip),
    MakeOutlet("weaponLabStatusDetail", &self->statusDetail),
    MakeOutlet("weaponLabRole", &self->roleChip),
    MakeOutlet("weaponLabPending", &self->pendingCount),
    MakeOutlet("weaponLabValueCount", &self->valueCount),
    MakeOutlet("weaponLabReset", &self->resetButton),
    MakeOutlet("weaponLabApply", &self->applyButton)
  );

  $(self->viewController.view, resolve, outlets);

  assert(self->statusStrip);
  assert(self->unrankedChip);
  assert(self->stateChip);
  assert(self->statusDetail);
  assert(self->roleChip);
  assert(self->pendingCount);
  assert(self->valueCount);
  assert(self->resetButton);
  assert(self->applyButton);
}

/**
 * @brief Wires the chrome the JSON declares.
 */
static void bindControls(WeaponLabViewController *self) {

  self->resetButton->delegate = (ButtonDelegate) {
    .self = self,
    .didClick = didClickResetAll
  };

  self->applyButton->delegate = (ButtonDelegate) {
    .self = self,
    .didClick = didClickApply
  };
}

/**
 * @see ViewController::loadView(ViewController *)
 */
static void loadView(ViewController *viewController) {

  super(ViewController, viewController, loadView);

  WeaponLabViewController *self = (WeaponLabViewController *) viewController;

  View *view = $$(View, viewWithResourceName,
                  "ui/admin/WeaponLabViewController.json", NULL);
  assert(view);
  assert(view->identifier && !q_strcmp(view->identifier, "raceWeaponLab"));

  view->stylesheet = $$(Stylesheet, stylesheetWithResourceName,
                        "ui/admin/WeaponLabViewController.css");
  assert(view->stylesheet);

  $(viewController, setView, view);
  release(view);

  resolveOutlets(self);
  bindControls(self);

  // Cards are generated only after a complete catalog transaction commits.
  // Until then, the authored status chrome is a fail-closed shell.
  self->openGroup = RACE_WEAPON_TUNING_GROUP_HYPER;

  refresh(self);
}

/**
 * @see ViewController::viewWillAppear(ViewController *)
 */
static void viewWillAppear(ViewController *viewController) {

  super(ViewController, viewController, viewWillAppear);

  WeaponLabViewController *self = (WeaponLabViewController *) viewController;
  activeWeaponLab = self;
  Cg_RaceWeaponTuning_RequestSync();
  refresh(self);
}

static void viewWillDisappear(ViewController *viewController) {

  WeaponLabViewController *self = (WeaponLabViewController *) viewController;
  if (activeWeaponLab == self) {
    activeWeaponLab = NULL;
  }
  super(ViewController, viewController, viewWillDisappear);
}

static void dealloc(Object *object) {

  WeaponLabViewController *self = (WeaponLabViewController *) object;
  if (activeWeaponLab == self) {
    activeWeaponLab = NULL;
  }
  super(Object, object, dealloc);
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ObjectInterface *) clazz->interface)->dealloc = dealloc;
  ((ViewControllerInterface *) clazz->interface)->loadView = loadView;
  ((ViewControllerInterface *) clazz->interface)->viewWillAppear = viewWillAppear;
  ((ViewControllerInterface *) clazz->interface)->viewWillDisappear =
    viewWillDisappear;
}

/**
 * @fn Class *WeaponLabViewController::_WeaponLabViewController(void)
 * @memberof WeaponLabViewController
 */
Class *_WeaponLabViewController(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "WeaponLabViewController",
      .superclass = _ViewController(),
      .instanceSize = sizeof(WeaponLabViewController),
      .interfaceOffset = offsetof(WeaponLabViewController, interface),
      .interfaceSize = sizeof(WeaponLabViewControllerInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
