#pragma once

/*
 * Race-local projectile observation seam used by QRPL recording. This is kept
 * out of the stock v1.0.79 GAME headers so the host API remains untouched.
 */
typedef enum {
  G_PROJECTILE_ROCKET,
  G_PROJECTILE_HYPERBLASTER
} g_projectile_kind_t;

typedef enum {
  G_PROJECTILE_SPAWN,
  G_PROJECTILE_IMPACT,
  G_PROJECTILE_SILENT_DESPAWN
} g_projectile_operation_t;

typedef struct {
  g_projectile_kind_t kind;
  g_projectile_operation_t operation;
  const g_entity_t *owner;
  const g_entity_t *projectile;
  vec3_t origin;
  vec3_t velocity;
  vec3_t normal;
} g_projectile_observation_t;

typedef void (*g_projectile_observer_t)(
  const g_projectile_observation_t *observation);

g_projectile_observer_t G_SetProjectileObserver(
  g_projectile_observer_t observer);

void G_ObserveProjectile(const g_entity_t *projectile,
                         g_projectile_kind_t kind,
                         g_projectile_operation_t operation,
                         vec3_t normal);

typedef struct {
  g_projectile_observer_t previous;
  bool installed;
} g_projectile_observer_lifecycle_t;

static inline bool G_InstallProjectileObserver(
    g_projectile_observer_lifecycle_t *lifecycle,
    g_projectile_observer_t observer) {
  if (!lifecycle || !observer) {
    return false;
  }
  if (!lifecycle->installed) {
    lifecycle->previous = G_SetProjectileObserver(observer);
    lifecycle->installed = true;
  }
  return true;
}

static inline void G_RestoreProjectileObserver(
    g_projectile_observer_lifecycle_t *lifecycle) {
  if (lifecycle && lifecycle->installed) {
    G_SetProjectileObserver(lifecycle->previous);
    lifecycle->previous = NULL;
    lifecycle->installed = false;
  }
}
