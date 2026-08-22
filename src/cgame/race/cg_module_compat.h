/*
 * Copyright(c) 2006 Quetoo.
 *
 * Race-local hooks that stock v1.0.79 common CGAME does not expose. Matching
 * common implementations are selected only by cgame-race.vcxproj.
 */

#pragma once

#include "game/common/bg_pmove.h"

box3_t Pm_PlayerBounds(bool ducked);
bool Cg_Module_DisablePrediction(void);
int32_t Cg_Module_PredictionClipMask(void);
cm_trace_t Cg_Module_TracePrediction(vec3_t start, vec3_t end,
                                    box3_t bounds);
void Cg_Module_PreparePredictionCommand(pm_move_t *pm, size_t index,
                                        size_t count);
void Cg_Module_CompletePredictionCommand(const pm_move_t *pm, size_t index,
                                         size_t count);
void Cg_Module_CompletePrediction(const pm_move_t *pm);
bool Cg_Module_ParseMessage(int32_t command);
void Cg_Module_ClearState(void);
void Cg_Module_LoadMedia(void);
void Cg_Module_PopulateScene(void);
bool Cg_Module_ShouldHideEntity(const cl_entity_t *entity);
void Cg_Module_UpdateUi(const player_state_t *ps);
