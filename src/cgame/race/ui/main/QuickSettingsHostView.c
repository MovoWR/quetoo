/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "QuickSettingsHostView.h"

#define _Class _QuickSettingsHostView

/**
 * @see View::respondToEvent(View *, const SDL_Event *)
 */
static void respondToEvent(View *view, const SDL_Event *event) {

  QuickSettingsHostView *self = (QuickSettingsHostView *) view;

  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    const Array *subviews = (Array *) view->subviews;
    const View *drawer = subviews->count ? subviews->elements[0] : NULL;

    if (drawer && !$(drawer, didReceiveEvent, event)) {
      if (self->delegate.didDismiss) {
        self->delegate.didDismiss(self->delegate.self);
      }
      return;
    }
  }

  super(View, view, respondToEvent, event);
}

static void initialize(Class *clazz) {
  ((ViewInterface *) clazz->interface)->respondToEvent = respondToEvent;
}

Class *_QuickSettingsHostView(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "QuickSettingsHostView",
      .superclass = _View(),
      .instanceSize = sizeof(QuickSettingsHostView),
      .interfaceOffset = offsetof(QuickSettingsHostView, interface),
      .interfaceSize = sizeof(QuickSettingsHostViewInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
