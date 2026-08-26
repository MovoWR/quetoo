/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "game/race/race_weapon_tuning_wire.h"

/**
 * @brief The immutable authoritative view reconstructed from one complete
 * GAME-authored tuning stream.
 * @remarks Baseline and current are deliberately separate complete snapshots.
 * No field in this structure is promoted from a local UI request.
 */
typedef struct {
  bool complete;
  bool synchronized;
  bool status_valid;
  uint32_t revision;
  uint32_t result_revision;

  race_weapon_tuning_status_t status;
  race_weapon_tuning_sync_begin_t metadata;
  race_weapon_tuning_descriptor_t
    descriptors[RACE_WEAPON_TUNING_VALUE_COUNT];

  bool baseline_valid;
  bool current_valid;
  race_weapon_tuning_snapshot_t baseline;
  race_weapon_tuning_snapshot_t current;

  bool result_valid;
  race_weapon_tuning_result_message_t result;
} cg_race_weapon_tuning_cache_t;

typedef struct {
  bool pending;
  bool awaiting_authoritative;
  uint32_t request_id;
  uint64_t expected_generation;
} cg_race_weapon_tuning_request_t;

void Cg_RaceWeaponTuning_Init(void);
void Cg_RaceWeaponTuning_Clear(void);
void Cg_RaceWeaponTuning_Update(void);

bool Cg_RaceWeaponTuning_ParseMessage(int32_t command);
bool Cg_RaceWeaponTuning_ParsePayload(const void *data, size_t length);
void Cg_RaceWeaponTuning_UpdateStatus(const char *wire);

const cg_race_weapon_tuning_cache_t *Cg_RaceWeaponTuning_Cache(void);
cg_race_weapon_tuning_request_t Cg_RaceWeaponTuning_RequestState(
  race_weapon_tuning_operation_t operation);
bool Cg_RaceWeaponTuning_MutationPending(void);

bool Cg_RaceWeaponTuning_ScalarValue(
  const race_weapon_tuning_descriptor_t *descriptor,
  race_weapon_tuning_scalar_t scalar, double *value);
bool Cg_RaceWeaponTuning_SnapshotValue(
  const race_weapon_tuning_snapshot_t *snapshot,
  race_weapon_tuning_id_t id, double *value);
bool Cg_RaceWeaponTuning_CanonicalValue(
  const race_weapon_tuning_descriptor_t *descriptor,
  double value, race_weapon_tuning_scalar_t *scalar);

/**
 * @brief Resolves the Hyper climb presentation policy.
 * @param range Receives the synchronized active override when one exists.
 * @param overridden Receives true for that active override, false while
 * inactive (the ordinary physics service remains the presentation source).
 * @return False while tuning is non-inactive but not synchronized; helpers must
 * not guess a range in that state.
 */
bool Cg_RaceWeaponTuning_ClimbPresentation(float *range, bool *overridden);

/**
 * @brief Returns the persistent safety warning, or NULL only for a completely
 * synchronized inactive service.
 */
const char *Cg_RaceWeaponTuning_Warning(void);

uint32_t Cg_RaceWeaponTuning_RequestSync(void);
uint32_t Cg_RaceWeaponTuning_RequestApply(uint64_t expected_generation,
                                          const char *pairs);
uint32_t Cg_RaceWeaponTuning_RequestResetAll(void);
