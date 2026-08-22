#pragma once

#include "shared/shared.h"

void Cg_InitJumpViewer(void);
void Cg_ClearJumpViewer(void);
void Cg_JumpViewer_UpdatePredicted(const pm_state_t *pm);
void Cg_JumpViewer_UpdateFrame(const player_state_t *ps);
void Cg_DrawJumpViewer(const player_state_t *ps, int32_t bottom_offset);

