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

#define RACE_ADMIN_ACCOUNT_ID_MAX 32
#define RACE_ADMIN_ACCOUNT_ID_SIZE (RACE_ADMIN_ACCOUNT_ID_MAX + 1)
#define RACE_ADMIN_CHALLENGE_NONCE_SIZE 32

/**
 * @brief The deliberately small set of Race administrator capabilities.
 *
 * These values are server-owned policy. The current client receives the mask
 * for presentation, but the assignments are not a public protocol contract.
 */
typedef enum {
  RACE_ADMIN_CAP_SETTINGS_MUTATE = 1u << 0,
  RACE_ADMIN_CAP_MAP_CHANGE = 1u << 1,
  RACE_ADMIN_CAP_PLAYER_KICK = 1u << 2,
  RACE_ADMIN_CAP_PLAYER_BAN = 1u << 3,
  RACE_ADMIN_CAP_VOTE_ADMIN = 1u << 4,
  RACE_ADMIN_CAP_ACCOUNT_MANAGE = 1u << 5,
  RACE_ADMIN_CAP_SERVER_CVAR = 1u << 6,
  RACE_ADMIN_CAP_CVAR_ALLOWLIST_MANAGE = 1u << 7,

  RACE_ADMIN_CAP_ALL = (1u << 8) - 1u
} race_admin_capability_t;

/**
 * @brief Named, bounded capability sets. Numeric legacy levels are not used.
 */
typedef enum {
  RACE_ADMIN_ROLE_NONE,
  RACE_ADMIN_ROLE_MODERATOR,
  RACE_ADMIN_ROLE_OPERATOR,
  RACE_ADMIN_ROLE_OWNER,

  RACE_ADMIN_ROLE_TOTAL
} race_admin_role_t;

/**
 * @brief The concrete Race administrator actions.
 *
 * These values are server-private policy identifiers, not a wire contract.
 */
typedef enum {
  RACE_ADMIN_ACTION_SETTINGS_MUTATE,
  RACE_ADMIN_ACTION_MAP_CHANGE,
  RACE_ADMIN_ACTION_PLAYER_KICK,
  RACE_ADMIN_ACTION_VOTE_CANCEL,
  RACE_ADMIN_ACTION_ACCOUNT_MANAGE,
  RACE_ADMIN_ACTION_SERVER_CVAR,
  RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE,

  RACE_ADMIN_ACTION_TOTAL
} race_admin_action_t;

typedef enum {
  RACE_ADMIN_KICK_TARGET_OK,
  RACE_ADMIN_KICK_TARGET_INVALID,
  RACE_ADMIN_KICK_TARGET_SELF,
  RACE_ADMIN_KICK_TARGET_UNAVAILABLE
} race_admin_kick_target_result_t;

/**
 * @brief Connection-local, server-authoritative administrator session.
 *
 * This structure is embedded in Race's module-owned g_client_t persistent
 * state. Common respawn and map changes retain it for the connection lifetime;
 * disconnect and reconnect clear the persistent record. It is never written to
 * disk.
 */
typedef struct {
  bool authenticated;
  char account_id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  uint64_t account_revision;
  uint32_t capabilities;
} race_admin_session_t;

/**
 * @brief One-use proof challenge bound to one exact connection and account.
 */
typedef struct {
  bool issued;
  char account_id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  uint8_t nonce[RACE_ADMIN_CHALLENGE_NONCE_SIZE];
  uint64_t issued_at;
} race_admin_challenge_t;

/**
 * @brief Fixed account- or address-local failed-login throttle state.
 */
typedef struct {
  uint64_t window_started_at;
  uint64_t blocked_until;
  uint8_t failures;
} race_admin_login_throttle_t;
