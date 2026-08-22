/*
 * Copyright(c) 1997-2001 id Software, Inc.
 * Copyright(c) 2002 The Quakeforge Project.
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"

#include "RosterSelect.h"

#define _Class _RosterSelect

/**
 * @brief Maximum number of roster rows rendered at once.
 */
#define ROSTER_SELECT_VISIBLE_OPTIONS 10u

/**
 * @brief Number of rows advanced by one mouse-wheel notch.
 */
#define ROSTER_SELECT_WHEEL_STEP 3

/**
 * @brief Returns the number of rows in the current bounded window.
 */
static size_t visibleOptionCount(const RosterSelect *self) {
  return min(self->select.options->count, ROSTER_SELECT_VISIBLE_OPTIONS);
}

/**
 * @brief Clamps the first visible row to the current Option count.
 */
static void clampFirstVisibleOption(RosterSelect *self) {

  const size_t visible = visibleOptionCount(self);
  const size_t maximum = self->select.options->count > visible
    ? self->select.options->count - visible
    : 0u;

  self->firstVisibleOption = min(self->firstVisibleOption, maximum);
}

/**
 * @brief Reveals the specified Option inside the bounded window.
 */
static void revealOption(RosterSelect *self, Option *option) {

  if (option == NULL) {
    return;
  }

  const ssize_t index = $(self->select.options, indexOfObject, option);
  if (index < 0) {
    return;
  }

  const size_t visible = visibleOptionCount(self);
  if ((size_t) index < self->firstVisibleOption) {
    self->firstVisibleOption = (size_t) index;
  } else if (visible && (size_t) index >= self->firstVisibleOption + visible) {
    self->firstVisibleOption = (size_t) index - visible + 1u;
  }

  clampFirstVisibleOption(self);
  self->select.control.view.needsLayout = true;
}

#pragma mark - View

/**
 * @see View::init(View *)
 */
static View *init(View *self) {
  return (View *) $((RosterSelect *) self, initWithFrame, NULL);
}

/**
 * @see View::layoutSubviews(View *)
 */
static void layoutSubviews(View *self) {

  RosterSelect *this = (RosterSelect *) self;
  Select *select = (Select *) self;
  Control *control = (Control *) self;
  View *menu = (View *) select->stackView;

  const SDL_Rect renderFrame = $(self, renderFrame);
  const int32_t menuWidth = max(1, renderFrame.w -
    self->padding.left - self->padding.right);
  menu->minSize.w = menu->maxSize.w = menuWidth;

  super(View, self, layoutSubviews);

  if (!$(control, isHighlighted)) {
    return;
  }

  clampFirstVisibleOption(this);
  const size_t visible = visibleOptionCount(this);
  const size_t end = this->firstVisibleOption + visible;

  for (size_t i = 0u; i < select->options->count; i++) {
    Option *option = $(select->options, objectAtIndex, i);
    option->view.hidden = i < this->firstVisibleOption || i >= end;
  }

  $(menu, sizeToFit);
  $(menu, layoutIfNeeded);

  if (menu->window == NULL) {
    return;
  }

  int32_t windowWidth, windowHeight;
  SDL_GetWindowSize(menu->window, &windowWidth, &windowHeight);

  menu->frame.x = renderFrame.x + self->padding.left;
  menu->frame.y = renderFrame.y + self->padding.top;
  menu->frame.x = clamp(menu->frame.x, 0, max(0, windowWidth - menu->frame.w));
  menu->frame.y = clamp(menu->frame.y, 0, max(0, windowHeight - menu->frame.h));
}

#pragma mark - Control

/**
 * @see Control::captureEvent(Control *, const SDL_Event *)
 */
static bool captureEvent(Control *self, const SDL_Event *event) {

  RosterSelect *this = (RosterSelect *) self;

  if ($(self, isHighlighted) && event->type == SDL_EVENT_MOUSE_WHEEL &&
      event->wheel.y != 0.f) {

    const size_t visible = visibleOptionCount(this);
    const int32_t maximum = this->select.options->count > visible
      ? (int32_t) (this->select.options->count - visible)
      : 0;
    const int32_t direction = event->wheel.y > 0.f ? -1 : 1;
    const int32_t first = clamp((int32_t) this->firstVisibleOption +
      direction * ROSTER_SELECT_WHEEL_STEP, 0, maximum);

    if ((size_t) first != this->firstVisibleOption) {
      this->firstVisibleOption = (size_t) first;
      self->view.needsLayout = true;
    }
    return true;
  }

  const bool wasHighlighted = $(self, isHighlighted);
  const bool handled = super(Control, self, captureEvent, event);

  // Select clears Highlighted before hit-testing its Options. If a pointer-up
  // lands in menu padding, it returns false despite changing state; consume
  // that transition so Control dispatches stateDidChange and closes the menu.
  if (wasHighlighted && event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
      !$(self, isHighlighted)) {
    return true;
  }

  return handled;
}

/**
 * @see Control::stateDidChange(Control *)
 */
static void stateDidChange(Control *self) {

  RosterSelect *this = (RosterSelect *) self;
  Select *select = (Select *) self;
  const bool highlighted = $(self, isHighlighted);

  if (highlighted && !this->menuOpen) {
    revealOption(this, $(select, selectedOption));
  }

  this->menuOpen = highlighted;
  super(Control, self, stateDidChange);

  View *menu = (View *) select->stackView;
  if (highlighted) {
    $(menu, addClassName, "rosterOptions");
  } else {
    $(menu, removeClassName, "rosterOptions");
  }
}

#pragma mark - Select

/**
 * @see Select::selectOption(Select *, Option *)
 */
static void selectOption(Select *self, Option *option) {

  super(Select, self, selectOption, option);
  revealOption((RosterSelect *) self, option);
}

#pragma mark - RosterSelect

/**
 * @brief Initializes this RosterSelect with the specified frame.
 */
static RosterSelect *initWithFrame(RosterSelect *self, const SDL_Rect *frame) {

  self = (RosterSelect *) super(Select, self, initWithFrame, frame);
  if (self) {
    self->firstVisibleOption = 0u;
    self->menuOpen = false;
  }

  return self;
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ViewInterface *) clazz->interface)->init = init;
  ((ViewInterface *) clazz->interface)->layoutSubviews = layoutSubviews;

  ((ControlInterface *) clazz->interface)->captureEvent = captureEvent;
  ((ControlInterface *) clazz->interface)->stateDidChange = stateDidChange;

  ((SelectInterface *) clazz->interface)->selectOption = selectOption;

  ((RosterSelectInterface *) clazz->interface)->initWithFrame = initWithFrame;
}

/**
 * @fn Class *RosterSelect::_RosterSelect(void)
 * @memberof RosterSelect
 */
Class *_RosterSelect(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "RosterSelect",
      .superclass = _Select(),
      .instanceSize = sizeof(RosterSelect),
      .interfaceOffset = offsetof(RosterSelect, interface),
      .interfaceSize = sizeof(RosterSelectInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
