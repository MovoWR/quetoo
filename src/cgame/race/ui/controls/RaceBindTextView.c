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

#include "RaceBindTextView.h"

#define _Class _RaceBindTextView

#pragma mark - RaceBindTextView

SDL_Scancode RaceBindTextView_KeyForSlot(const char *bind, int slot) {

  if (bind == NULL || *bind == '\0' || slot < 0) {
    return SDL_SCANCODE_UNKNOWN;
  }

  SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
  for (int i = 0; i <= slot; i++) {
    key = cgi.KeyForBind(key, bind);
    if (key == SDL_SCANCODE_UNKNOWN) {
      break;
    }
  }

  return key;
}

#pragma mark - View

/**
 * @see View::awakeWithDictionary(View *, const Dictionary *)
 */
static void awakeWithDictionary(View *self, const Dictionary *dictionary) {

  super(View, self, awakeWithDictionary, dictionary);

  RaceBindTextView *this = (RaceBindTextView *) self;

  const Inlet inlets[] = MakeInlets(
    MakeInlet("slot", InletTypeInteger, &this->slot, NULL)
  );

  $(self, bind, inlets, dictionary);
}

/**
 * @see View::init(View *)
 */
static View *init(View *self) {
  return (View *) $((RaceBindTextView *) self, initWithBindAndSlot, NULL, 0);
}

/**
 * @see View::updateBindings(View *)
 */
static void updateBindings(View *self) {

  // BindTextView joins the whole key list; run it for the rest of the chain,
  // then narrow the field to this slot and label the empty case. The two slots
  // share one pill, and an empty alternate is hidden there entirely - see
  // MovementCombatViewController::refreshCapture - so its placeholder is only
  // ever seen while the player is aiming at it, and reads as an invitation to
  // add rather than as a missing binding.
  super(View, self, updateBindings);

  RaceBindTextView *this = (RaceBindTextView *) self;
  const BindTextView *bindTextView = (BindTextView *) self;

  const SDL_Scancode key = RaceBindTextView_KeyForSlot(bindTextView->bind, this->slot);
  if (key == SDL_SCANCODE_UNKNOWN) {
    $((TextView *) self, setDefaultText, this->slot > 0 ? "+" : "Unbound");
    $(self, addClassName, "unbound");
  } else {
    $((TextView *) self, setDefaultText, cgi.KeyName(key));
    $(self, removeClassName, "unbound");
  }
}

#pragma mark - Control

/**
 * @see Control::captureEvent(Control *, const SDL_Event *)
 */
static bool captureEvent(Control *self, const SDL_Event *event) {

  if (self->state & ControlStateFocused) {

    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {

      SDL_Scancode key;
      if (event->type == SDL_EVENT_KEY_DOWN) {
        key = event->key.scancode;
      } else {
        key = SDL_SCANCODE_MOUSE1 + (event->button.button - 1);
      }

      RaceBindTextView *this = (RaceBindTextView *) self;
      BindTextView *bindTextView = (BindTextView *) self;

      if (key != SDL_SCANCODE_ESCAPE) {

        // Capture replaces this slot rather than appending to the command's key
        // list, so the field the player aimed at is the only one that moves.
        const SDL_Scancode occupant =
          RaceBindTextView_KeyForSlot(bindTextView->bind, this->slot);
        if (occupant != SDL_SCANCODE_UNKNOWN) {
          cgi.BindKey(occupant, NULL);
        }

        if (key != SDL_SCANCODE_BACKSPACE) {
          cgi.BindKey(key, bindTextView->bind);
        }

        if (bindTextView->textView.delegate.didEndEditing) {
          bindTextView->textView.delegate.didEndEditing(&bindTextView->textView);
        }
      }

      self->state &= ~ControlStateFocused;
      return true;
    }
  }

  return super(Control, self, captureEvent, event);
}

#pragma mark - RaceBindTextView

/**
 * @fn RaceBindTextView *RaceBindTextView::initWithBindAndSlot(RaceBindTextView *self, const char *bind, int slot)
 *
 * @memberof RaceBindTextView
 */
static RaceBindTextView *initWithBindAndSlot(RaceBindTextView *self, const char *bind, int slot) {

  self = (RaceBindTextView *) $((BindTextView *) self, initWithBind, bind);
  if (self) {
    self->slot = slot;
  }

  return self;
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ViewInterface *) clazz->interface)->awakeWithDictionary = awakeWithDictionary;
  ((ViewInterface *) clazz->interface)->init = init;
  ((ViewInterface *) clazz->interface)->updateBindings = updateBindings;

  ((ControlInterface *) clazz->interface)->captureEvent = captureEvent;

  ((RaceBindTextViewInterface *) clazz->interface)->initWithBindAndSlot = initWithBindAndSlot;
}

/**
 * @fn Class *RaceBindTextView::_RaceBindTextView(void)
 * @memberof RaceBindTextView
 */
Class *_RaceBindTextView(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "RaceBindTextView",
      .superclass = _BindTextView(),
      .instanceSize = sizeof(RaceBindTextView),
      .interfaceOffset = offsetof(RaceBindTextView, interface),
      .interfaceSize = sizeof(RaceBindTextViewInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
