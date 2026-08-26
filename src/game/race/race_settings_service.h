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
#include <stdint.h>

#include "race_settings.h"

typedef struct g_client_s g_client_t;

void Race_SettingsService_Init(void);
void Race_SettingsService_PostInit(void);
void Race_SettingsService_PrepareLevel(const char *map);
void Race_SettingsService_FinalizeLevel(const char *map);

bool Race_SettingsService_ClientCommand(g_client_t *cl, const char *command);
void Race_SettingsService_PrintMigrationHint(g_client_t *cl);

bool Race_SettingsService_EffectiveValue(race_setting_id_t id,
                                         race_setting_value_t *value);
bool Race_SettingsService_HasMapOverride(race_setting_id_t id);

bool Race_SettingsService_FinishCueEnabled(void);
float Race_SettingsService_FinishCueGain(void);
bool Race_SettingsService_CheckpointTimeEnabled(void);
int32_t Race_SettingsService_VotingTime(void);
int32_t Race_SettingsService_MaxVotes(void);
int32_t Race_SettingsService_VoteMenuDuration(void);
int32_t Race_SettingsService_VoteMenuChoices(void);
bool Race_SettingsService_VoteAllowSpectators(void);
bool Race_SettingsService_WeaponsEnabled(void);
