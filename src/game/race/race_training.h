/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "bg_pmove.h"

/**
 * @brief Versioned Race input flags stored in STAT_RACE_INPUT and QRPL v1.
 */
#define RACE_INPUT_FORWARD (1 << 0)
#define RACE_INPUT_BACK    (1 << 1)
#define RACE_INPUT_LEFT    (1 << 2)
#define RACE_INPUT_RIGHT   (1 << 3)
#define RACE_INPUT_JUMP    (1 << 4)
#define RACE_INPUT_CROUCH  (1 << 5)
#define RACE_INPUT_ATTACK  (1 << 6)
#define RACE_INPUT_HOOK    (1 << 7)
#define RACE_INPUT_WALK    (1 << 8)

#define RACE_INPUT_ACTION_MASK 0x01FF
#define RACE_INPUT_FORMAT_MASK 0xFE00
#define RACE_INPUT_FORMAT_V1   0x2A00

static inline int16_t Race_InputFlags(const pm_cmd_t *cmd) {
  int16_t flags = RACE_INPUT_FORMAT_V1;

  if (!cmd) {
    return flags;
  }
  if (cmd->forward > 0) {
    flags |= RACE_INPUT_FORWARD;
  } else if (cmd->forward < 0) {
    flags |= RACE_INPUT_BACK;
  }
  if (cmd->right < 0) {
    flags |= RACE_INPUT_LEFT;
  } else if (cmd->right > 0) {
    flags |= RACE_INPUT_RIGHT;
  }
  if (cmd->up > 0) {
    flags |= RACE_INPUT_JUMP;
  } else if (cmd->up < 0) {
    flags |= RACE_INPUT_CROUCH;
  }
  if (cmd->buttons & BUTTON_ATTACK) {
    flags |= RACE_INPUT_ATTACK;
  }
  if (cmd->buttons & BUTTON_HOOK) {
    flags |= RACE_INPUT_HOOK;
  }
  if (cmd->buttons & BUTTON_WALK) {
    flags |= RACE_INPUT_WALK;
  }
  return flags;
}

static inline bool Race_InputFlagsValid(const int16_t flags) {
  return (flags & RACE_INPUT_FORMAT_MASK) == RACE_INPUT_FORMAT_V1;
}

/**
 * @brief The pre-acceleration sample consumed by CGAZ and persisted in QRPL v1.
 */
typedef struct {
  bool active;
  vec3_t forward;
  vec3_t velocity;
  vec3_t wishdir;
  float wishspeed;
  float accel;
  float frametime;
  float view_yaw;
} race_strafe_sample_t;

typedef void (*race_strafe_observer_t)(
  const race_strafe_sample_t *sample, void *context);

/**
 * @brief Installs one observer for the next Race Pm_Move invocation.
 * @details Pm_Move always clears the observer before returning.
 */
void Pm_RaceTraining_SetObserver(race_strafe_observer_t observer,
                                 void *context);
void Pm_RaceTraining_ClearObserver(void);
