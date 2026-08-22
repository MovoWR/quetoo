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

typedef struct g_client_s g_client_t;

void Race_SettingsService_Init(void);
void Race_SettingsService_Load(const char *map);

bool Race_SettingsService_FinishCueEnabled(void);
float Race_SettingsService_FinishCueGain(void);
bool Race_SettingsService_CheckpointTimeEnabled(void);
int32_t Race_SettingsService_VotingTime(void);
int32_t Race_SettingsService_MaxVotes(void);
int32_t Race_SettingsService_VoteMenuDuration(void);
int32_t Race_SettingsService_VoteMenuChoices(void);
bool Race_SettingsService_VoteAllowSpectators(void);
bool Race_SettingsService_WeaponsEnabled(void);

bool Race_SettingsService_ClientInspect(g_client_t *cl, const char *key,
                                        bool include_sources);
bool Race_SettingsService_ClientMutate(g_client_t *cl, bool reset,
                                       const char *source, const char *key,
                                       const char *value);
