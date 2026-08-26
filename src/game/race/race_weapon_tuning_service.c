/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "g_local.h"

#include "race.h"
#include "race_admin_service.h"
#include "race_physics.h"
#include "race_replay_playback_service.h"
#include "race_weapon_tuning_service.h"
#include "race_weapon_tuning_wire.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define RACE_WEAPON_TUNING_SYNC_INTERVAL 250u

typedef struct {
  race_weapon_tuning_state_t state;
  uint64_t generation;
  race_physics_preset_id_t preset;
  race_weapon_tuning_snapshot_t baseline;
  race_weapon_tuning_snapshot_t current;
  bool baseline_valid;
  bool current_valid;
  char identity[RACE_WEAPON_TUNING_IDENTITY_SIZE];
  uint32_t sync_times[MAX_CLIENTS];
} race_weapon_tuning_service_t;

static race_weapon_tuning_service_t race_weapon_tuning;

static const race_physics_preset_descriptor_t *
Race_WeaponTuningService_ActivePreset(void) {
  const race_physics_config_t *config = Race_Physics_Current();
  const race_physics_preset_descriptor_t *preset = config
    ? Race_Physics_Preset(config->preset) : NULL;
  if (preset) {
    return preset;
  }
  const race_physics_config_t *fallback = Race_Physics_Default();
  return fallback ? Race_Physics_Preset(fallback->preset) : NULL;
}

static void Race_WeaponTuningService_Command(g_client_t *cl,
                                              size_t subcommand_index);
static bool Race_WeaponTuningService_Ensure(char *error, size_t error_size);

static void Race_WeaponTuningService_Print(g_client_t *cl,
                                            const char *format, ...) {
  char output[MAX_STRING_CHARS];
  va_list args;
  va_start(args, format);
  vsnprintf(output, sizeof(output), format, args);
  va_end(args);
  output[sizeof(output) - 1u] = '\0';
  if (cl) {
    gi.ClientPrint(cl, PRINT_HIGH, "%s", output);
  } else {
    gi.Print("%s", output);
  }
}

static bool Race_WeaponTuningService_Parse64(const char *text,
                                              uint64_t *value) {
  if (!text || !*text || !value || (text[0] == '0' && text[1])) {
    return false;
  }
  uint64_t parsed = 0u;
  for (const unsigned char *c = (const unsigned char *) text; *c; c++) {
    if (*c < '0' || *c > '9') {
      return false;
    }
    const uint64_t digit = (uint64_t) (*c - '0');
    if (parsed > (UINT64_MAX - digit) / 10u) {
      return false;
    }
    parsed = parsed * 10u + digit;
  }
  *value = parsed;
  return true;
}

static bool Race_WeaponTuningService_RequestId(const char *text,
                                                uint32_t *request_id) {
  static const char prefix[] = "req=";
  if (!text || strncmp(text, prefix, sizeof(prefix) - 1u)) {
    return false;
  }
  uint64_t parsed;
  if (!Race_WeaponTuningService_Parse64(text + sizeof(prefix) - 1u,
                                        &parsed) || !parsed ||
      parsed > UINT32_MAX) {
    return false;
  }
  *request_id = (uint32_t) parsed;
  return true;
}

static bool Race_WeaponTuningService_ClientCanMutate(const g_client_t *cl) {
  return cl && (Race_AdminService_ClientCapabilities(cl) &
                RACE_ADMIN_CAP_SETTINGS_MUTATE) != 0u;
}

static uint64_t Race_WeaponTuningService_Hash(
    const race_weapon_tuning_snapshot_t *snapshot, const bool valid) {
  return valid ? Race_WeaponTuning_SnapshotHash(snapshot) : 0u;
}

static void Race_WeaponTuningService_Status(void) {
  const race_physics_preset_descriptor_t *preset =
    Race_WeaponTuningService_ActivePreset();
  race_weapon_tuning_status_t status = {
    .state = race_weapon_tuning.state,
    .generation = race_weapon_tuning.generation,
    .hash = Race_WeaponTuningService_Hash(
      &race_weapon_tuning.current, race_weapon_tuning.current_valid),
    .hyper_climb_range = race_weapon_tuning.current_valid
      ? Race_WeaponTuning_Float(&race_weapon_tuning.current,
                                RACE_WEAPON_TUNING_HYPER_CLIMB_RANGE)
      : 0.f
  };
  q_strlcpy(status.preset_key, preset ? preset->key : "unknown",
            sizeof(status.preset_key));
  q_strlcpy(status.identity,
            race_weapon_tuning.current_valid
              ? race_weapon_tuning.identity : "none",
            sizeof(status.identity));
  char wire[RACE_WEAPON_TUNING_STATUS_SIZE];
  if (!Race_WeaponTuning_StatusEncode(&status, wire, sizeof(wire))) {
    q_strlcpy(wire,
              "v2\\error\\0\\0000000000000000\\unknown\\none\\0",
              sizeof(wire));
  }
  gi.SetConfigString(CS_RACE_WEAPON_TUNING_STATUS, wire);
}

static bool Race_WeaponTuningService_Send(g_client_t *cl,
                                           const void *payload,
                                           const size_t length) {
  if (!cl || !cl->in_use || !payload || !length ||
      length > RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD) {
    return false;
  }
  gi.WriteByte(SV_CMD_RACE_WEAPON_TUNING);
  gi.WriteByte((int32_t) length);
  gi.WriteData(payload, length);
  gi.Unicast(cl, true);
  return true;
}

static void Race_WeaponTuningService_SendSnapshot(
    g_client_t *cl, const uint32_t request_id,
    const race_weapon_tuning_snapshot_kind_t kind,
    const race_weapon_tuning_snapshot_t *snapshot) {
  race_weapon_tuning_snapshot_message_t message = {
    .request_id = request_id,
    .generation = race_weapon_tuning.generation,
    .hash = Race_WeaponTuning_SnapshotHash(snapshot),
    .kind = kind,
    .snapshot = *snapshot
  };
  uint8_t payload[RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD];
  const size_t length = Race_WeaponTuningWire_EncodeSnapshot(
    &message, payload, sizeof(payload));
  Race_WeaponTuningService_Send(cl, payload, length);
}

static void Race_WeaponTuningService_SendFull(g_client_t *cl,
                                               const uint32_t request_id) {
  if (!cl || !cl->in_use) {
    return;
  }
  char error[RACE_WEAPON_TUNING_ERROR_SIZE];
  Race_WeaponTuningService_Ensure(error, sizeof(error));
  const race_physics_preset_descriptor_t *preset =
    Race_WeaponTuningService_ActivePreset();
  uint16_t flags = 0u;
  if (race_weapon_tuning.baseline_valid) {
    flags |= RACE_WEAPON_TUNING_SYNC_HAS_BASELINE;
  }
  if (race_weapon_tuning.current_valid) {
    flags |= RACE_WEAPON_TUNING_SYNC_HAS_CURRENT;
  }
  if (Race_WeaponTuningService_ClientCanMutate(cl)) {
    flags |= RACE_WEAPON_TUNING_SYNC_CAN_MUTATE;
  }

  race_weapon_tuning_sync_begin_t begin = {
    .request_id = request_id,
    .generation = race_weapon_tuning.generation,
    .session_generation = 0u,
    .catalog_hash = Race_WeaponTuning_CatalogHash(),
    .baseline_hash = Race_WeaponTuningService_Hash(
      &race_weapon_tuning.baseline, race_weapon_tuning.baseline_valid),
    .current_hash = Race_WeaponTuningService_Hash(
      &race_weapon_tuning.current, race_weapon_tuning.current_valid),
    .previous_hash = 0u,
    .slot_a_hash = 0u,
    .slot_b_hash = 0u,
    .flags = flags,
    .state = race_weapon_tuning.state
  };
  q_strlcpy(begin.preset_key, preset ? preset->key : "unknown",
            sizeof(begin.preset_key));
  q_strlcpy(begin.identity, race_weapon_tuning.current_valid
              ? race_weapon_tuning.identity : "none",
            sizeof(begin.identity));

  uint8_t payload[RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD];
  size_t length = Race_WeaponTuningWire_EncodeSyncBegin(
    &begin, payload, sizeof(payload));
  if (!Race_WeaponTuningService_Send(cl, payload, length)) {
    return;
  }

  size_t count;
  const race_weapon_tuning_descriptor_t *catalog =
    Race_WeaponTuning_Catalog(&count);
  for (size_t i = 0u; i < count; i++) {
    race_weapon_tuning_catalog_entry_t entry = {
      .request_id = request_id,
      .generation = race_weapon_tuning.generation,
      .index = (uint16_t) i,
      .descriptor = catalog[i]
    };
    length = Race_WeaponTuningWire_EncodeCatalogEntry(
      &entry, payload, sizeof(payload));
    if (!Race_WeaponTuningService_Send(cl, payload, length)) {
      return;
    }
  }

  if (flags & RACE_WEAPON_TUNING_SYNC_HAS_BASELINE) {
    Race_WeaponTuningService_SendSnapshot(
      cl, request_id, RACE_WEAPON_TUNING_SNAPSHOT_BASELINE,
      &race_weapon_tuning.baseline);
  }
  if (flags & RACE_WEAPON_TUNING_SYNC_HAS_CURRENT) {
    Race_WeaponTuningService_SendSnapshot(
      cl, request_id, RACE_WEAPON_TUNING_SNAPSHOT_CURRENT,
      &race_weapon_tuning.current);
  }

  race_weapon_tuning_sync_end_t end = {
    .request_id = request_id,
    .generation = race_weapon_tuning.generation,
    .catalog_hash = Race_WeaponTuning_CatalogHash(),
    .descriptor_count = (uint16_t) count
  };
  length = Race_WeaponTuningWire_EncodeSyncEnd(
    &end, payload, sizeof(payload));
  Race_WeaponTuningService_Send(cl, payload, length);
}

static void Race_WeaponTuningService_Broadcast(void) {
  G_ForEachClient(cl, {
    Race_WeaponTuningService_SendFull(cl, 0u);
  });
}

static void Race_WeaponTuningService_Result(
    g_client_t *cl, const uint32_t request_id,
    const race_weapon_tuning_operation_t operation,
    const race_weapon_tuning_result_t result, const char *text) {
  if (cl && request_id) {
    const race_physics_preset_descriptor_t *preset =
      Race_WeaponTuningService_ActivePreset();
    race_weapon_tuning_result_message_t message = {
      .request_id = request_id,
      .generation = race_weapon_tuning.generation,
      .hash = Race_WeaponTuningService_Hash(
        &race_weapon_tuning.current, race_weapon_tuning.current_valid),
      .operation = operation,
      .result = result
    };
    q_strlcpy(message.preset_key, preset ? preset->key : "unknown",
              sizeof(message.preset_key));
    q_strlcpy(message.text, text, sizeof(message.text));
    uint8_t payload[RACE_WEAPON_TUNING_WIRE_MAX_PAYLOAD];
    const size_t length = Race_WeaponTuningWire_EncodeResult(
      &message, payload, sizeof(payload));
    Race_WeaponTuningService_Send(cl, payload, length);
  }
  Race_WeaponTuningService_Print(cl, "Weapon tuning: %s\n", text);
}

static bool Race_WeaponTuningService_Advance(
    g_client_t *cl, const uint32_t request_id,
    const race_weapon_tuning_operation_t operation) {
  if (race_weapon_tuning.generation != UINT64_MAX) {
    race_weapon_tuning.generation++;
    return true;
  }
  race_weapon_tuning.state = RACE_WEAPON_TUNING_STATE_ERROR;
  Race_WeaponTuningService_Status();
  Race_WeaponTuningService_Result(cl, request_id, operation,
                                  RACE_WEAPON_TUNING_RESULT_INTERNAL,
                                  "generation exhausted; service blocked");
  return false;
}

static bool Race_WeaponTuningService_Milliseconds(
    const cvar_t *cvar, uint32_t *milliseconds) {
  if (!cvar || !milliseconds || !isfinite(cvar->value) ||
      cvar->value < 0.f) {
    return false;
  }
  const float scaled = cvar->value * 1000.f;
  if (!isfinite(scaled) || (double) scaled > (double) UINT32_MAX) {
    return false;
  }
  // Match common's SECONDS_TO_MILLIS float expression followed by the
  // uint32_t G_WeaponFired / projectile-timer argument conversion.
  *milliseconds = (uint32_t) scaled;
  return true;
}

static bool Race_WeaponTuningService_Capture(
    race_weapon_tuning_snapshot_t *snapshot,
    const race_physics_preset_descriptor_t *preset,
    char *error, const size_t error_size) {
  if (!snapshot || !preset) {
    return false;
  }
  Race_WeaponTuning_DefaultSnapshot(snapshot);
#define SET_INT(id_, cvar_) \
  snapshot->values[id_].integer = (cvar_)->integer
#define SET_UINT_MS(id_, cvar_) \
  if (!Race_WeaponTuningService_Milliseconds( \
        cvar_, &snapshot->values[id_].unsigned_integer)) { \
    q_snprintf(error, error_size, "invalid backing cvar %s", (cvar_)->name); \
    return false; \
  }
  SET_INT(RACE_WEAPON_TUNING_HYPER_KNOCKBACK,
          g_balance_hyperblaster_knockback);
  SET_INT(RACE_WEAPON_TUNING_HYPER_SPEED,
          g_balance_hyperblaster_speed);
  SET_UINT_MS(RACE_WEAPON_TUNING_HYPER_REFIRE_MS,
              g_balance_hyperblaster_refire);
  snapshot->values[RACE_WEAPON_TUNING_HYPER_CLIMB_IMPULSE_Z].real =
    g_balance_hyperblaster_climb_knockback->value;
  snapshot->values[RACE_WEAPON_TUNING_HYPER_CLIMB_IN].real = 0.f;
  snapshot->values[RACE_WEAPON_TUNING_HYPER_CLIMB_VELOCITY_BOOST].real = 0.f;
  snapshot->values[RACE_WEAPON_TUNING_HYPER_CLIMB_3D].real = 0.f;
  snapshot->values[RACE_WEAPON_TUNING_HYPER_CLIMB_3D_UP].real = 1.f;
  snapshot->values[RACE_WEAPON_TUNING_HYPER_CLIMB_3D_SIDE].real = 1.f;
  snapshot->values[RACE_WEAPON_TUNING_HYPER_CLIMB_3D_IN].real = 0.f;
  snapshot->values[RACE_WEAPON_TUNING_HYPER_CLIMB_RANGE].real =
    preset->hyperblaster_climb_range;
  SET_INT(RACE_WEAPON_TUNING_STANDARD_ROCKET_SPEED,
          g_balance_rocketlauncher_speed);
  SET_INT(RACE_WEAPON_TUNING_STANDARD_ROCKET_KNOCKBACK,
          g_balance_rocketlauncher_knockback);
  SET_UINT_MS(RACE_WEAPON_TUNING_STANDARD_ROCKET_REFIRE_MS,
              g_balance_rocketlauncher_refire);
  SET_INT(RACE_WEAPON_TUNING_STANDARD_GRENADE_SPEED,
          g_balance_grenadelauncher_speed);
  SET_INT(RACE_WEAPON_TUNING_STANDARD_GRENADE_KNOCKBACK,
          g_balance_grenadelauncher_knockback);
  SET_UINT_MS(RACE_WEAPON_TUNING_STANDARD_GRENADE_FUSE_MS,
              g_balance_grenadelauncher_timer);
  SET_UINT_MS(RACE_WEAPON_TUNING_STANDARD_GRENADE_REFIRE_MS,
              g_balance_grenadelauncher_refire);
  snapshot->values[RACE_WEAPON_TUNING_HAND_GRENADE_KNOCKBACK].integer = 120;
  SET_UINT_MS(RACE_WEAPON_TUNING_HAND_GRENADE_REFIRE_MS,
              g_balance_handgrenade_refire);
  snapshot->values[RACE_WEAPON_TUNING_GLOBAL_SELF_KNOCKBACK].real =
    g_self_knockback->value;
  snapshot->values[RACE_WEAPON_TUNING_GRENADE_INHERIT_FRACTION].real =
    .33f * g_player_projectile->value;
#undef SET_UINT_MS
#undef SET_INT
  return Race_WeaponTuning_SnapshotValid(snapshot, error, error_size);
}

static bool Race_WeaponTuningService_Ensure(char *error,
                                             const size_t error_size) {
  const race_physics_preset_descriptor_t *preset =
    Race_WeaponTuningService_ActivePreset();
  if (!preset) {
    if (error && error_size) {
      q_snprintf(error, error_size, "active physics preset unavailable");
    }
    race_weapon_tuning.state = RACE_WEAPON_TUNING_STATE_ERROR;
    Race_WeaponTuningService_Status();
    return false;
  }

  if (race_weapon_tuning.state == RACE_WEAPON_TUNING_STATE_ACTIVE) {
    const bool ready = race_weapon_tuning.baseline_valid &&
                       race_weapon_tuning.current_valid;
    if (!ready && error && error_size) {
      q_snprintf(error, error_size, "weapon tuning baseline unavailable");
    }
    return ready;
  }
  if (race_weapon_tuning.state != RACE_WEAPON_TUNING_STATE_INACTIVE) {
    if (error && error_size) {
      q_snprintf(error, error_size, "weapon tuning service unavailable");
    }
    return false;
  }

  if (race_weapon_tuning.baseline_valid &&
      race_weapon_tuning.current_valid &&
      race_weapon_tuning.preset == preset->id) {
    if (error && error_size) {
      *error = '\0';
    }
    return true;
  }

  char catalog_error[RACE_WEAPON_TUNING_ERROR_SIZE];
  race_weapon_tuning_snapshot_t captured;
  if (!Race_WeaponTuning_CatalogValid(catalog_error,
                                      sizeof(catalog_error)) ||
      !Race_WeaponTuningService_Capture(
        &captured, preset, catalog_error, sizeof(catalog_error))) {
    if (error && error_size) {
      q_snprintf(error, error_size, "%s", *catalog_error
        ? catalog_error : "invalid weapon tuning baseline");
    }
    race_weapon_tuning.state = RACE_WEAPON_TUNING_STATE_ERROR;
    Race_WeaponTuningService_Status();
    return false;
  }

  const bool refresh = race_weapon_tuning.baseline_valid ||
                       race_weapon_tuning.current_valid;
  if (refresh && race_weapon_tuning.generation == UINT64_MAX) {
    if (error && error_size) {
      q_snprintf(error, error_size, "generation exhausted; service blocked");
    }
    race_weapon_tuning.state = RACE_WEAPON_TUNING_STATE_ERROR;
    Race_WeaponTuningService_Status();
    return false;
  }

  race_weapon_tuning.baseline = captured;
  race_weapon_tuning.current = captured;
  race_weapon_tuning.baseline_valid = true;
  race_weapon_tuning.current_valid = true;
  race_weapon_tuning.preset = preset->id;
  q_strlcpy(race_weapon_tuning.identity, "baseline",
            sizeof(race_weapon_tuning.identity));
  if (refresh) {
    race_weapon_tuning.generation++;
  }
  if (error && error_size) {
    *error = '\0';
  }
  Race_WeaponTuningService_Status();
  return true;
}

static bool Race_WeaponTuningService_CoveredEntity(const g_entity_t *ent) {
  if (!ent) {
    return false;
  }
  if (ent->race_weapon_tuning_generation) {
    return true;
  }
  if (!ent->owner || !ent->owner->client || !ent->classname) {
    return false;
  }
  static const char *const classnames[] = {
    "G_GrenadeProjectile",
    "G_RocketProjectile",
    "G_HyperblasterProjectile",
    "G_PullGrenadePin",
    "G_FireBfg",
    "G_BfgProjectile"
  };
  for (size_t i = 0u; i < lengthof(classnames); i++) {
    if (!strcmp(ent->classname, classnames[i])) {
      return true;
    }
  }
  return false;
}

static void Race_WeaponTuningService_CleanBoundary(void) {
  G_ForEachClient(cl, {
    Race_ReplayPlaybackService_ClientRunStarted(cl);
    Race_Reset(cl);
    cl->inventory[POWERUP_QUAD] = 0;
    cl->quad_damage_time = 0u;
    cl->quad_countdown_time = 0u;
    cl->quad_attack_time = 0u;
    if (cl->entity) {
      cl->entity->s.effects &= ~EF_QUAD;
    }
    if (cl->held_grenade) {
      if (cl->held_grenade->in_use) {
        G_FreeEntity(cl->held_grenade);
      }
      cl->held_grenade = NULL;
    }
    cl->grenade_hold_time = 0u;
    cl->grenade_hold_frame = 0u;
  });
  G_ForEachEntity(ent, {
    if (Race_WeaponTuningService_CoveredEntity(ent)) {
      G_FreeEntity(ent);
    }
  });
  G_ForEachClient(cl, {
    if (!cl->persistent.spectator) {
      cl->persistent.race_mode = RACE_MODE_PRACTICE;
      cl->ps.stats[STAT_RACE_MODE] = RACE_MODE_PRACTICE;
      if (cl->entity) {
        G_ClientRespawn(cl, false);
      }
    }
  });
}

static bool Race_WeaponTuningService_Authorize(g_client_t *cl,
                                                const uint32_t request_id,
                                                const race_weapon_tuning_operation_t op) {
  if (!cl || Race_AdminService_AuthorizeClientAction(
               cl, RACE_ADMIN_ACTION_SETTINGS_MUTATE)) {
    return true;
  }
  Race_WeaponTuningService_Result(cl, request_id, op,
                                  RACE_WEAPON_TUNING_RESULT_DENIED,
                                  "settings mutation denied");
  return false;
}

static bool Race_WeaponTuningService_Cas(g_client_t *cl,
                                         const uint32_t request_id,
                                         const race_weapon_tuning_operation_t op,
                                         const char *text) {
  uint64_t expected;
  if (!Race_WeaponTuningService_Parse64(text, &expected)) {
    Race_WeaponTuningService_Result(cl, request_id, op,
                                    RACE_WEAPON_TUNING_RESULT_INVALID,
                                    "invalid expected generation");
    return false;
  }
  if (expected != race_weapon_tuning.generation) {
    Race_WeaponTuningService_Result(cl, request_id, op,
                                    RACE_WEAPON_TUNING_RESULT_STALE,
                                    "stale generation");
    return false;
  }
  return true;
}

static void Race_WeaponTuningService_CommitEffective(
    g_client_t *cl, const uint32_t request_id,
    const race_weapon_tuning_operation_t op,
    const race_weapon_tuning_snapshot_t *candidate,
    const char *identity, const char *action) {
  const race_weapon_tuning_snapshot_t accepted = *candidate;
  if (Race_WeaponTuning_SnapshotEqual(
        &accepted, &race_weapon_tuning.current)) {
    Race_WeaponTuningService_Result(cl, request_id, op,
                                    RACE_WEAPON_TUNING_RESULT_NOOP,
                                    "no effective change");
    return;
  }
  if (!Race_WeaponTuningService_Advance(cl, request_id, op)) {
    return;
  }
  race_weapon_tuning.state = RACE_WEAPON_TUNING_STATE_TRANSITION;
  Race_WeaponTuningService_Status();
  race_weapon_tuning.current = accepted;
  race_weapon_tuning.current_valid = true;
  Race_WeaponTuningService_CleanBoundary();
  if (Race_WeaponTuning_SnapshotEqual(
        &race_weapon_tuning.current, &race_weapon_tuning.baseline)) {
    race_weapon_tuning.state = RACE_WEAPON_TUNING_STATE_INACTIVE;
    q_strlcpy(race_weapon_tuning.identity, "baseline",
              sizeof(race_weapon_tuning.identity));
  } else {
    race_weapon_tuning.state = RACE_WEAPON_TUNING_STATE_ACTIVE;
    q_strlcpy(race_weapon_tuning.identity, identity,
              sizeof(race_weapon_tuning.identity));
  }
  Race_WeaponTuningService_Status();
  Race_AdminService_AuditClientAction(cl, RACE_ADMIN_ACTION_SETTINGS_MUTATE,
                                      action, "accepted");
  Race_WeaponTuningService_Result(cl, request_id, op,
                                  RACE_WEAPON_TUNING_RESULT_OK, action);
  Race_WeaponTuningService_Broadcast();
}

static void Race_WeaponTuningService_Apply(
    g_client_t *cl, const uint32_t request_id, const char *generation_text,
    const size_t first_pair) {
  const race_weapon_tuning_operation_t op = RACE_WEAPON_TUNING_OPERATION_APPLY;
  char error[RACE_WEAPON_TUNING_ERROR_SIZE];
  if (!Race_WeaponTuningService_Authorize(cl, request_id, op)) {
    return;
  }
  if (!Race_WeaponTuningService_Ensure(error, sizeof(error))) {
    Race_WeaponTuningService_Result(cl, request_id, op,
                                    RACE_WEAPON_TUNING_RESULT_INTERNAL,
                                    *error ? error : "baseline unavailable");
    return;
  }
  if (!Race_WeaponTuningService_Cas(cl, request_id, op, generation_text)) {
    return;
  }
  if (first_pair >= (size_t) gi.Argc() ||
      (size_t) gi.Argc() - first_pair > 32u) {
    if (first_pair >= (size_t) gi.Argc()) {
      Race_WeaponTuningService_Result(cl, request_id, op,
                                      RACE_WEAPON_TUNING_RESULT_INVALID,
                                      "apply requires key=value pairs");
    } else if ((size_t) gi.Argc() - first_pair > 32u) {
      Race_WeaponTuningService_Result(cl, request_id, op,
                                      RACE_WEAPON_TUNING_RESULT_INVALID,
                                      "apply exceeds 32 pairs");
    }
    return;
  }
  race_weapon_tuning_snapshot_t candidate = race_weapon_tuning.current;
  bool assigned[RACE_WEAPON_TUNING_VALUE_COUNT] = { false };
  for (size_t i = first_pair; i < (size_t) gi.Argc(); i++) {
    char pair[RACE_WEAPON_TUNING_KEY_SIZE +
              RACE_WEAPON_TUNING_VALUE_TEXT_SIZE + 1u];
    if (strlen(gi.Argv((int32_t) i)) >= sizeof(pair)) {
      Race_WeaponTuningService_Result(cl, request_id, op,
                                      RACE_WEAPON_TUNING_RESULT_INVALID,
                                      "oversized assignment");
      return;
    }
    q_strlcpy(pair, gi.Argv((int32_t) i), sizeof(pair));
    char *equals = strchr(pair, '=');
    if (!equals || equals == pair || !equals[1] || strchr(equals + 1, '=')) {
      Race_WeaponTuningService_Result(cl, request_id, op,
                                      RACE_WEAPON_TUNING_RESULT_INVALID,
                                      "malformed assignment");
      return;
    }
    *equals = '\0';
    const race_weapon_tuning_descriptor_t *descriptor =
      Race_WeaponTuning_DescriptorForKey(pair);
    race_weapon_tuning_scalar_t value;
    if (!descriptor) {
      Race_WeaponTuningService_Result(cl, request_id, op,
                                      RACE_WEAPON_TUNING_RESULT_NOT_FOUND,
                                      "unknown assignment key");
      return;
    }
    if (assigned[descriptor->id]) {
      Race_WeaponTuningService_Result(cl, request_id, op,
                                      RACE_WEAPON_TUNING_RESULT_INVALID,
                                      "duplicate assignment key");
      return;
    }
    if (!Race_WeaponTuning_ParseValue(descriptor, equals + 1, &value,
                                      error, sizeof(error))) {
      Race_WeaponTuningService_Result(cl, request_id, op,
                                      RACE_WEAPON_TUNING_RESULT_INVALID,
                                      error);
      return;
    }
    candidate.values[descriptor->id] = value;
    assigned[descriptor->id] = true;
  }
  if (!Race_WeaponTuning_SnapshotValid(&candidate, error, sizeof(error))) {
    Race_WeaponTuningService_Result(cl, request_id, op,
                                    RACE_WEAPON_TUNING_RESULT_INVALID,
                                    error);
    return;
  }
  Race_WeaponTuningService_CommitEffective(
    cl, request_id, op, &candidate, "custom",
    "batch applied; runs reset");
}

static void Race_WeaponTuningService_Reset(
    g_client_t *cl, const uint32_t request_id, const char *generation_text,
    const char *selector) {
  const race_weapon_tuning_operation_t op =
    RACE_WEAPON_TUNING_OPERATION_RESET_ALL;
  if (strcmp(selector, "all")) {
    Race_WeaponTuningService_Result(cl, request_id, op,
                                    RACE_WEAPON_TUNING_RESULT_INVALID,
                                    "reset requires all");
    return;
  }
  char error[RACE_WEAPON_TUNING_ERROR_SIZE];
  if (!Race_WeaponTuningService_Authorize(cl, request_id, op)) {
    return;
  }
  if (!Race_WeaponTuningService_Ensure(error, sizeof(error))) {
    Race_WeaponTuningService_Result(cl, request_id, op,
                                    RACE_WEAPON_TUNING_RESULT_INTERNAL,
                                    *error ? error : "baseline unavailable");
    return;
  }
  if (!Race_WeaponTuningService_Cas(cl, request_id, op, generation_text)) {
    return;
  }
  Race_WeaponTuningService_CommitEffective(
    cl, request_id, op, &race_weapon_tuning.baseline, "baseline",
    "baseline values restored");
}

static void Race_WeaponTuningService_PrintStatus(g_client_t *cl) {
  char error[RACE_WEAPON_TUNING_ERROR_SIZE];
  if (!Race_WeaponTuningService_Ensure(error, sizeof(error))) {
    Race_WeaponTuningService_Print(cl, "WEAPON TUNING unavailable: %s\n",
                                   *error ? error : "baseline unavailable");
    return;
  }
  const race_physics_preset_descriptor_t *preset =
    Race_WeaponTuningService_ActivePreset();
  Race_WeaponTuningService_Print(
    cl, "WEAPON TUNING %s generation=%" PRIu64
        " preset=%s hash=%016" PRIx64 " identity=%s rankable=%d\n",
    Race_WeaponTuning_StateToken(race_weapon_tuning.state),
    race_weapon_tuning.generation,
    preset ? preset->key : "unknown",
    Race_WeaponTuningService_Hash(&race_weapon_tuning.current,
                                  race_weapon_tuning.current_valid),
    race_weapon_tuning.identity,
    Race_WeaponTuningService_Rankable());
}

static bool Race_WeaponTuningService_FormatRawValue(
    const race_weapon_tuning_descriptor_t *descriptor,
    const race_weapon_tuning_scalar_t value, char *output,
    const size_t capacity) {
  if (!descriptor || !output || !capacity) {
    return false;
  }

  int32_t written;
  if (descriptor->type == RACE_WEAPON_TUNING_TYPE_INT32) {
    written = snprintf(output, capacity, "%d", value.integer);
  } else if (descriptor->type == RACE_WEAPON_TUNING_TYPE_UINT32) {
    written = snprintf(output, capacity, "%u", value.unsigned_integer);
  } else if (descriptor->type == RACE_WEAPON_TUNING_TYPE_FLOAT &&
             isfinite(value.real)) {
    written = snprintf(output, capacity, "%.6g", (double) value.real);
  } else {
    return false;
  }
  return written > 0 && (size_t) written < capacity;
}

static void Race_WeaponTuningService_List(g_client_t *cl,
                                           const char *group_filter) {
  char error[RACE_WEAPON_TUNING_ERROR_SIZE];
  if (!Race_WeaponTuningService_Ensure(error, sizeof(error))) {
    Race_WeaponTuningService_Print(cl, "Weapon tuning unavailable: %s\n",
                                   *error ? error : "baseline unavailable");
    return;
  }
  size_t count;
  const race_weapon_tuning_descriptor_t *catalog =
    Race_WeaponTuning_Catalog(&count);
  for (size_t i = 0u; i < count; i++) {
    const race_weapon_tuning_descriptor_t *descriptor = catalog + i;
    if (group_filter && *group_filter &&
        strcmp(group_filter, descriptor->group_key)) {
      continue;
    }
    char current[RACE_WEAPON_TUNING_VALUE_TEXT_SIZE] = "-";
    char baseline[RACE_WEAPON_TUNING_VALUE_TEXT_SIZE] = "-";
    char minimum[RACE_WEAPON_TUNING_VALUE_TEXT_SIZE];
    char maximum[RACE_WEAPON_TUNING_VALUE_TEXT_SIZE];
    char step[RACE_WEAPON_TUNING_VALUE_TEXT_SIZE] = "-";
    if (race_weapon_tuning.current_valid) {
      Race_WeaponTuning_FormatValue(descriptor,
        race_weapon_tuning.current.values[i], current, sizeof(current));
    }
    if (race_weapon_tuning.baseline_valid) {
      Race_WeaponTuning_FormatValue(descriptor,
        race_weapon_tuning.baseline.values[i], baseline, sizeof(baseline));
    }
    Race_WeaponTuning_FormatValue(descriptor, descriptor->minimum,
                                  minimum, sizeof(minimum));
    Race_WeaponTuning_FormatValue(descriptor, descriptor->maximum,
                                  maximum, sizeof(maximum));
    Race_WeaponTuningService_FormatRawValue(descriptor, descriptor->step,
                                            step, sizeof(step));
    Race_WeaponTuningService_Print(
      cl, "%s current=%s baseline=%s range=%s..%s step=%s unit=%s\n",
      descriptor->key, current, baseline, minimum, maximum, step,
      descriptor->unit);
  }
}

static void Race_WeaponTuningService_Get(g_client_t *cl, const char *key) {
  char error[RACE_WEAPON_TUNING_ERROR_SIZE];
  if (!Race_WeaponTuningService_Ensure(error, sizeof(error))) {
    Race_WeaponTuningService_Print(cl, "Weapon tuning unavailable: %s\n",
                                   *error ? error : "baseline unavailable");
    return;
  }
  const race_weapon_tuning_descriptor_t *descriptor =
    Race_WeaponTuning_DescriptorForKey(key);
  if (!descriptor) {
    Race_WeaponTuningService_Print(cl, "Unknown weapon tuning key: %s\n", key);
    return;
  }
  char current[RACE_WEAPON_TUNING_VALUE_TEXT_SIZE] = "-";
  char baseline[RACE_WEAPON_TUNING_VALUE_TEXT_SIZE] = "-";
  if (race_weapon_tuning.current_valid) {
    Race_WeaponTuning_FormatValue(descriptor,
      race_weapon_tuning.current.values[descriptor->id],
      current, sizeof(current));
  }
  if (race_weapon_tuning.baseline_valid) {
    Race_WeaponTuning_FormatValue(descriptor,
      race_weapon_tuning.baseline.values[descriptor->id],
      baseline, sizeof(baseline));
  }
  Race_WeaponTuningService_Print(cl, "%s current=%s baseline=%s source=%s\n",
                                 key, current, baseline,
                                 race_weapon_tuning.identity);
}

static void Race_WeaponTuningService_Console(void) {
  Race_WeaponTuningService_Command(NULL, 1u);
}

static void Race_WeaponTuningService_Usage(g_client_t *cl) {
  Race_WeaponTuningService_Print(
    cl, "Usage: %s status|list|get|sync|apply|reset\n",
    cl ? "race tune" : "race_tune");
}

static void Race_WeaponTuningService_Command(g_client_t *cl,
                                              const size_t subcommand_index) {
  const size_t argc = (size_t) gi.Argc();
  size_t command_bytes = 0u;
  for (size_t i = 0u; i < argc; i++) {
    const size_t length = strlen(gi.Argv((int32_t) i));
    if (length >= 768u || command_bytes + length + (i ? 1u : 0u) >= 768u) {
      Race_WeaponTuningService_Print(cl,
                                     "Weapon tuning command exceeds 767 bytes\n");
      return;
    }
    command_bytes += length + (i ? 1u : 0u);
  }
  if (subcommand_index >= argc) {
    Race_WeaponTuningService_Usage(cl);
    return;
  }
  const char *subcommand = gi.Argv((int32_t) subcommand_index);
  size_t argument = subcommand_index + 1u;
  uint32_t request_id = 0u;
  if (argument < argc && !strncmp(gi.Argv((int32_t) argument), "req=", 4u)) {
    if (!Race_WeaponTuningService_RequestId(
          gi.Argv((int32_t) argument), &request_id)) {
      Race_WeaponTuningService_Print(cl, "Invalid request id\n");
      return;
    }
    argument++;
  }

  const bool mutation = !strcmp(subcommand, "apply") ||
                        !strcmp(subcommand, "reset");
  if (cl && mutation && !request_id) {
    Race_WeaponTuningService_Print(
      cl, "Weapon tuning: connected mutation requires nonzero req=\n");
    return;
  }

  if (!strcmp(subcommand, "status")) {
    if (argument == argc) {
      Race_WeaponTuningService_PrintStatus(cl);
    } else {
      Race_WeaponTuningService_Usage(cl);
    }
  } else if (!strcmp(subcommand, "list")) {
    if (argc - argument <= 1u) {
      Race_WeaponTuningService_List(cl, argument < argc
        ? gi.Argv((int32_t) argument) : NULL);
    } else {
      Race_WeaponTuningService_Usage(cl);
    }
  } else if (!strcmp(subcommand, "get")) {
    if (argument + 1u == argc) {
      Race_WeaponTuningService_Get(cl, gi.Argv((int32_t) argument));
    } else {
      Race_WeaponTuningService_Usage(cl);
    }
  } else if (!strcmp(subcommand, "sync")) {
    if (!cl || !request_id || argument != argc) {
      Race_WeaponTuningService_Result(cl, request_id,
        RACE_WEAPON_TUNING_OPERATION_SYNC,
        RACE_WEAPON_TUNING_RESULT_INVALID, "invalid sync request");
      return;
    }
    const uint16_t slot = cl->ps.client;
    if (slot >= MAX_CLIENTS ||
        (race_weapon_tuning.sync_times[slot] &&
         g_level.time - race_weapon_tuning.sync_times[slot] <
           RACE_WEAPON_TUNING_SYNC_INTERVAL)) {
      Race_WeaponTuningService_Result(cl, request_id,
        RACE_WEAPON_TUNING_OPERATION_SYNC,
        RACE_WEAPON_TUNING_RESULT_DENIED, "sync rate limited");
      return;
    }
    race_weapon_tuning.sync_times[slot] = g_level.time;
    Race_WeaponTuningService_SendFull(cl, request_id);
  } else if (!strcmp(subcommand, "apply") && argument + 1u < argc) {
    Race_WeaponTuningService_Apply(cl, request_id,
      gi.Argv((int32_t) argument), argument + 1u);
  } else if (!strcmp(subcommand, "reset") && argument + 2u == argc) {
    Race_WeaponTuningService_Reset(cl, request_id,
      gi.Argv((int32_t) argument), gi.Argv((int32_t) argument + 1));
  } else {
    Race_WeaponTuningService_Usage(cl);
  }
}

void Race_WeaponTuningService_Init(void) {
  memset(&race_weapon_tuning, 0, sizeof(race_weapon_tuning));
  race_weapon_tuning.state = RACE_WEAPON_TUNING_STATE_INACTIVE;
  race_weapon_tuning.preset = RACE_PHYSICS_PRESET_INVALID;
  q_strlcpy(race_weapon_tuning.identity, "none",
            sizeof(race_weapon_tuning.identity));
}

void Race_WeaponTuningService_PostInit(void) {
  gi.AddCmd("race_tune", Race_WeaponTuningService_Console, CMD_GAME,
            "Authoritative Race weapon tuning service");
  char error[RACE_WEAPON_TUNING_ERROR_SIZE];
  if (!Race_WeaponTuningService_Ensure(error, sizeof(error))) {
    gi.Warn(__func__, "Weapon tuning disabled: %s\n",
            *error ? error : "baseline unavailable");
  }
}

void Race_WeaponTuningService_ConfigureLevel(void) {
  char error[RACE_WEAPON_TUNING_ERROR_SIZE];
  if (!Race_WeaponTuningService_Ensure(error, sizeof(error))) {
    gi.Warn(__func__, "Weapon tuning disabled: %s\n",
            *error ? error : "baseline unavailable");
  }
  // Sv_ClearState drops game ConfigStrings on every map transition while the
  // tuning service and its cached baseline survive. Re-publish the complete
  // identity before clients fetch the new level's ConfigStrings.
  Race_WeaponTuningService_Status();
}

void Race_WeaponTuningService_Shutdown(void) {
  race_weapon_tuning.baseline_valid = false;
  race_weapon_tuning.current_valid = false;
  race_weapon_tuning.state = RACE_WEAPON_TUNING_STATE_INACTIVE;
  race_weapon_tuning.preset = RACE_PHYSICS_PRESET_INVALID;
  q_strlcpy(race_weapon_tuning.identity, "none",
            sizeof(race_weapon_tuning.identity));
}

void Race_WeaponTuningService_ClientBegin(g_client_t *cl) {
  char error[RACE_WEAPON_TUNING_ERROR_SIZE];
  Race_WeaponTuningService_Ensure(error, sizeof(error));
  if (cl && cl->in_use && !Race_WeaponTuningService_Rankable() &&
      !cl->persistent.spectator) {
    cl->persistent.race_mode = RACE_MODE_PRACTICE;
    cl->ps.stats[STAT_RACE_MODE] = RACE_MODE_PRACTICE;
  }
  Race_WeaponTuningService_SendFull(cl, 0u);
}

bool Race_WeaponTuningService_ClientCommand(g_client_t *cl,
                                             const char *cmd) {
  if (!cl || !cmd) {
    return false;
  }
  if (!strcmp(cmd, "race_tune")) {
    Race_WeaponTuningService_Command(cl, 1u);
    return true;
  }
  if (!strcmp(cmd, "race") && gi.Argc() >= 2 &&
      !strcmp(gi.Argv(1), "tune")) {
    Race_WeaponTuningService_Command(cl, 2u);
    return true;
  }
  return false;
}

bool Race_WeaponTuningService_Active(void) {
  return race_weapon_tuning.state == RACE_WEAPON_TUNING_STATE_ACTIVE &&
         race_weapon_tuning.baseline_valid &&
         race_weapon_tuning.current_valid &&
         !Race_WeaponTuning_SnapshotEqual(&race_weapon_tuning.current,
                                           &race_weapon_tuning.baseline);
}

bool Race_WeaponTuningService_Rankable(void) {
  if (race_weapon_tuning.state != RACE_WEAPON_TUNING_STATE_INACTIVE) {
    return false;
  }
  if (!race_weapon_tuning.baseline_valid &&
      !race_weapon_tuning.current_valid) {
    return true;
  }
  return race_weapon_tuning.baseline_valid &&
         race_weapon_tuning.current_valid &&
         Race_WeaponTuning_SnapshotEqual(&race_weapon_tuning.current,
                                          &race_weapon_tuning.baseline);
}

uint64_t Race_WeaponTuningService_Generation(void) {
  return race_weapon_tuning.generation;
}

const race_weapon_tuning_snapshot_t *Race_WeaponTuningService_Current(void) {
  return Race_WeaponTuningService_Active()
    ? &race_weapon_tuning.current : NULL;
}

const race_weapon_tuning_snapshot_t *
Race_WeaponTuningService_CurrentForPreset(
    const race_physics_preset_id_t preset) {
  (void) preset;
  return Race_WeaponTuningService_Current();
}

int32_t Race_WeaponTuningService_Int(const race_weapon_tuning_id_t id,
                                     const int32_t legacy_value) {
  const race_weapon_tuning_snapshot_t *snapshot =
    Race_WeaponTuningService_Current();
  const race_weapon_tuning_descriptor_t *descriptor =
    Race_WeaponTuning_Descriptor(id);
  return snapshot && descriptor &&
         descriptor->type == RACE_WEAPON_TUNING_TYPE_INT32
    ? Race_WeaponTuning_Int(snapshot, id) : legacy_value;
}

uint32_t Race_WeaponTuningService_Uint(const race_weapon_tuning_id_t id,
                                       const uint32_t legacy_value) {
  const race_weapon_tuning_snapshot_t *snapshot =
    Race_WeaponTuningService_Current();
  const race_weapon_tuning_descriptor_t *descriptor =
    Race_WeaponTuning_Descriptor(id);
  return snapshot && descriptor &&
         descriptor->type == RACE_WEAPON_TUNING_TYPE_UINT32
    ? Race_WeaponTuning_Uint(snapshot, id) : legacy_value;
}

float Race_WeaponTuningService_Float(const race_weapon_tuning_id_t id,
                                     const float legacy_value) {
  const race_weapon_tuning_snapshot_t *snapshot =
    Race_WeaponTuningService_Current();
  const race_weapon_tuning_descriptor_t *descriptor =
    Race_WeaponTuning_Descriptor(id);
  return snapshot && descriptor &&
         descriptor->type == RACE_WEAPON_TUNING_TYPE_FLOAT
    ? Race_WeaponTuning_Float(snapshot, id) : legacy_value;
}

int32_t Race_WeaponTuningService_IntForPreset(
    const race_physics_preset_id_t preset,
    const race_weapon_tuning_id_t id, const int32_t legacy_value) {
  const race_weapon_tuning_snapshot_t *snapshot =
    Race_WeaponTuningService_CurrentForPreset(preset);
  const race_weapon_tuning_descriptor_t *descriptor =
    Race_WeaponTuning_Descriptor(id);
  return snapshot && descriptor &&
         descriptor->type == RACE_WEAPON_TUNING_TYPE_INT32
    ? Race_WeaponTuning_Int(snapshot, id) : legacy_value;
}

uint32_t Race_WeaponTuningService_UintForPreset(
    const race_physics_preset_id_t preset,
    const race_weapon_tuning_id_t id, const uint32_t legacy_value) {
  const race_weapon_tuning_snapshot_t *snapshot =
    Race_WeaponTuningService_CurrentForPreset(preset);
  const race_weapon_tuning_descriptor_t *descriptor =
    Race_WeaponTuning_Descriptor(id);
  return snapshot && descriptor &&
         descriptor->type == RACE_WEAPON_TUNING_TYPE_UINT32
    ? Race_WeaponTuning_Uint(snapshot, id) : legacy_value;
}

float Race_WeaponTuningService_FloatForPreset(
    const race_physics_preset_id_t preset,
    const race_weapon_tuning_id_t id, const float legacy_value) {
  const race_weapon_tuning_snapshot_t *snapshot =
    Race_WeaponTuningService_CurrentForPreset(preset);
  const race_weapon_tuning_descriptor_t *descriptor =
    Race_WeaponTuning_Descriptor(id);
  return snapshot && descriptor &&
         descriptor->type == RACE_WEAPON_TUNING_TYPE_FLOAT
    ? Race_WeaponTuning_Float(snapshot, id) : legacy_value;
}

void Race_WeaponTuningService_StampProjectile(g_entity_t *projectile) {
  const race_weapon_tuning_snapshot_t *snapshot = projectile
    ? Race_WeaponTuningService_CurrentForPreset(
        projectile->race_physics_preset) : NULL;
  if (!projectile || !snapshot) {
    return;
  }
  projectile->race_weapon_tuning_generation = race_weapon_tuning.generation;
  projectile->race_weapon_self_knockback = Race_WeaponTuning_Float(
    snapshot, RACE_WEAPON_TUNING_GLOBAL_SELF_KNOCKBACK);
  projectile->race_weapon_hyper_climb_range = Race_WeaponTuning_Float(
    snapshot, RACE_WEAPON_TUNING_HYPER_CLIMB_RANGE);
  projectile->race_weapon_hyper_climb_impulse_z = Race_WeaponTuning_Float(
    snapshot, RACE_WEAPON_TUNING_HYPER_CLIMB_IMPULSE_Z);
  projectile->race_weapon_hyper_climb_in = Race_WeaponTuning_Float(
    snapshot, RACE_WEAPON_TUNING_HYPER_CLIMB_IN);
  projectile->race_weapon_hyper_climb_velocity_boost = Race_WeaponTuning_Float(
    snapshot, RACE_WEAPON_TUNING_HYPER_CLIMB_VELOCITY_BOOST);
  projectile->race_weapon_hyper_climb_3d = Race_WeaponTuning_Float(
    snapshot, RACE_WEAPON_TUNING_HYPER_CLIMB_3D);
  projectile->race_weapon_hyper_climb_3d_up = Race_WeaponTuning_Float(
    snapshot, RACE_WEAPON_TUNING_HYPER_CLIMB_3D_UP);
  projectile->race_weapon_hyper_climb_3d_side = Race_WeaponTuning_Float(
    snapshot, RACE_WEAPON_TUNING_HYPER_CLIMB_3D_SIDE);
  projectile->race_weapon_hyper_climb_3d_in = Race_WeaponTuning_Float(
    snapshot, RACE_WEAPON_TUNING_HYPER_CLIMB_3D_IN);
}

float Race_WeaponTuningService_ProjectileSelfKnockback(
    const g_entity_t *projectile, const float legacy_value) {
  return projectile && projectile->race_weapon_tuning_generation
    ? projectile->race_weapon_self_knockback : legacy_value;
}

float Race_WeaponTuningService_ProjectileHyperRange(
    const g_entity_t *projectile, const float legacy_value) {
  return projectile && projectile->race_weapon_tuning_generation
    ? projectile->race_weapon_hyper_climb_range : legacy_value;
}

float Race_WeaponTuningService_ProjectileHyperImpulse(
    const g_entity_t *projectile, const float legacy_value) {
  return projectile && projectile->race_weapon_tuning_generation
    ? projectile->race_weapon_hyper_climb_impulse_z : legacy_value;
}

bool Race_WeaponTuningService_ProjectileHyperParameters(
    const g_entity_t *projectile, const float legacy_range,
    const float legacy_climb_up,
    race_hyperblaster_climb_parameters_t *parameters) {
  if (!projectile || !parameters) {
    return false;
  }
  *parameters = (race_hyperblaster_climb_parameters_t) {
    .range = Race_WeaponTuningService_ProjectileHyperRange(
      projectile, legacy_range),
    .climb_up = Race_WeaponTuningService_ProjectileHyperImpulse(
      projectile, legacy_climb_up),
    .climb_3d_up = 1.f,
    .climb_3d_side = 1.f
  };
  if (projectile->race_weapon_tuning_generation) {
    parameters->climb_in = projectile->race_weapon_hyper_climb_in;
    parameters->velocity_boost =
      projectile->race_weapon_hyper_climb_velocity_boost;
    parameters->climb_3d = projectile->race_weapon_hyper_climb_3d;
    parameters->climb_3d_up = projectile->race_weapon_hyper_climb_3d_up;
    parameters->climb_3d_side = projectile->race_weapon_hyper_climb_3d_side;
    parameters->climb_3d_in = projectile->race_weapon_hyper_climb_3d_in;
  }
  return true;
}
