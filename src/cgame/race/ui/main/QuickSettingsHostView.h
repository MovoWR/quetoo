/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <ObjectivelyMVC/View.h>

typedef struct QuickSettingsHostView QuickSettingsHostView;
typedef struct QuickSettingsHostViewInterface QuickSettingsHostViewInterface;

/**
 * @brief Delegate notified when the full-window quick-settings scrim receives
 * a mouse-down outside its drawer.
 */
typedef struct {
  ident self;
  void (*didDismiss)(ident self);
} QuickSettingsHostViewDelegate;

/**
 * @brief The actual ObjectivelyMVC responder for the quick-settings scrim.
 * @extends View
 */
struct QuickSettingsHostView {
  View view;
  QuickSettingsHostViewInterface *interface;
  QuickSettingsHostViewDelegate delegate;
};

struct QuickSettingsHostViewInterface {
  ViewInterface viewInterface;
};

Class *_QuickSettingsHostView(void);
