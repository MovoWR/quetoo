/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "g_local.h"
#include "race_projectile_compat.h"

static g_projectile_observer_t g_projectile_observer;

g_projectile_observer_t G_SetProjectileObserver(
    const g_projectile_observer_t observer) {
  const g_projectile_observer_t previous = g_projectile_observer;
  g_projectile_observer = observer;
  return previous;
}

void G_ObserveProjectile(const g_entity_t *projectile,
                         const g_projectile_kind_t kind,
                         const g_projectile_operation_t operation,
                         const vec3_t normal) {
  if (g_projectile_observer && projectile) {
    g_projectile_observer(&(const g_projectile_observation_t) {
      .kind = kind,
      .operation = operation,
      .owner = projectile->owner,
      .projectile = projectile,
      .origin = projectile->s.origin,
      .velocity = projectile->velocity,
      .normal = normal
    });
  }
}
