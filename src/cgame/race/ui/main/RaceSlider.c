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

#include "RaceSlider.h"

#define _Class _RaceSlider

#pragma mark - Object

static void dealloc(Object *self) {

  RaceSlider *this = (RaceSlider *) self;

  release(this->fill);

  super(Object, self, dealloc);
}

#pragma mark - View

/**
 * @see View::init(View *)
 * @details Declared rather than inherited so a RaceSlider built from a view
 * resource is built the same way as one built in C. `CvarSlider::init`
 * dispatches through `$`, so the inherited implementation already reaches this
 * class's `initWithVariable` - but View+JSON warns about any class whose `init`
 * is its superclass's, and a slider whose fill is only created down one of two
 * construction paths is exactly the kind of thing that warning is for.
 */
static View *init(View *self) {
  return (View *) $((CvarSlider *) self, initWithVariable, NULL, 0.0, 0.0, 0.0);
}

/**
 * @see View::layoutSubviews(View *)
 */
static void layoutSubviews(View *self) {

  super(View, self, layoutSubviews);

  RaceSlider *this = (RaceSlider *) self;
  const Slider *slider = (const Slider *) self;

  // Slider has already narrowed the bar to leave room for the readout, so the
  // fill is measured against the bar's final bounds. The same fraction places
  // the handle, which is what keeps the two ends flush.
  const SDL_Rect bounds = $(slider->bar, bounds);
  const double fraction = slider->max > slider->min
    ? clamp((slider->value - slider->min) / (slider->max - slider->min), 0.0, 1.0)
    : 0.0;

  this->fill->frame = MakeRect(0, 0, (int32_t) (bounds.w * fraction), bounds.h);
}

/**
 * @see View::updateBindings(View *)
 * @details CvarSlider::updateBindings rewrites the readout format to `"%g"`
 * whenever the step is 1 or more, unconditionally and on every pass - so a
 * route that spells its own unit (`"%g ms"` on the quick-join threshold,
 * `"%.0f s"` and `"%.0f%%"` on the admin rules) had that unit stripped before
 * the control first drew, and no amount of setting it in `loadView` survived.
 * The `"%g"` is a sensible default for an integer slider and stays one; it just
 * has no business outranking a format the route asked for by name.
 */
static void updateBindings(View *self) {

  RaceSlider *this = (RaceSlider *) self;
  Slider *slider = (Slider *) self;

  char *authored = this->labelFormatAuthored && slider->labelFormat
    ? strdup(slider->labelFormat) : NULL;

  super(View, self, updateBindings);

  if (authored) {
    $(slider, setLabelFormat, authored);
    free(authored);
  }
}

/**
 * @see View::render(View *, Renderer *)
 */
static void render(View *self, Renderer *renderer) {

  // Deliberately not `super`: Slider::render draws a hardcoded white line the
  // full width of the bar, which the filled track replaces. `super` resolves to
  // the immediate superclass, so View's implementation is invoked directly to
  // skip that one step while still drawing this view normally.
  ((ViewInterface *) _View()->interface)->render(self, renderer);
}

#pragma mark - Slider

/**
 * @see Slider::setLabelFormat(Slider *, const char *)
 */
static void setLabelFormat(Slider *self, const char *labelFormat) {

  super(Slider, self, setLabelFormat, labelFormat);

  ((RaceSlider *) self)->labelFormatAuthored = true;
}

#pragma mark - CvarSlider

/**
 * @see CvarSlider::initWithVariable(CvarSlider *, cvar_t *, double, double, double)
 */
static CvarSlider *initWithVariable(CvarSlider *self, cvar_t *var, double min,
                                    double max, double step) {

  self = super(CvarSlider, self, initWithVariable, var, min, max, step);
  if (self) {

    RaceSlider *this = (RaceSlider *) self;
    Slider *slider = (Slider *) self;

    this->fill = $(alloc(View), initWithFrame, NULL);
    assert(this->fill);

    $(this->fill, addClassName, "fill");

    // Ahead of the handle, so the handle still draws over the filled portion.
    $(slider->bar, addSubviewRelativeTo, this->fill, (View *) slider->handle,
      ViewPositionBefore);

    // Slider::initWithFrame installs "%0.1f" through the setter this class
    // overrides, so construction would otherwise leave every slider claiming a
    // format it was never given. Cleared here, at the end of the one path every
    // RaceSlider is built through, so only a route's own call counts.
    this->labelFormatAuthored = false;
  }

  return self;
}

#pragma mark - Class lifecycle

/**
 * @see Class::initialize(Class *)
 */
static void initialize(Class *clazz) {

  ((ObjectInterface *) clazz->interface)->dealloc = dealloc;

  ((ViewInterface *) clazz->interface)->init = init;
  ((ViewInterface *) clazz->interface)->layoutSubviews = layoutSubviews;
  ((ViewInterface *) clazz->interface)->render = render;
  ((ViewInterface *) clazz->interface)->updateBindings = updateBindings;

  ((SliderInterface *) clazz->interface)->setLabelFormat = setLabelFormat;

  ((CvarSliderInterface *) clazz->interface)->initWithVariable = initWithVariable;
}

/**
 * @fn Class *RaceSlider::_RaceSlider(void)
 * @memberof RaceSlider
 */
Class *_RaceSlider(void) {
  static Class *clazz;
  static Once once;

  do_once(&once, {
    clazz = _initialize(&(const ClassDef) {
      .name = "RaceSlider",
      .superclass = _CvarSlider(),
      .instanceSize = sizeof(RaceSlider),
      .interfaceOffset = offsetof(RaceSlider, interface),
      .interfaceSize = sizeof(RaceSliderInterface),
      .initialize = initialize,
    });
  });

  return clazz;
}

#undef _Class
