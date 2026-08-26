/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"
#include "cg_race_admin_auth.h"

#include "race_admin_auth.h"
#include "race_admin_password.h"

static cvar_t *cg_radmin_password;

void Cg_RaceAdminAuth_Init(void) {
  cg_radmin_password = cgi.AddCvar(
    "radmin_password", "", 0,
    "One-use Race administrator password; never sent to the server.");
}

void Cg_RaceAdminAuth_Clear(void) {
  if (cg_radmin_password && cg_radmin_password->string &&
      *cg_radmin_password->string) {
    cgi.ForceSetCvarString(cg_radmin_password->name, "");
  }
}

void Cg_RaceAdminAuth_Shutdown(void) {
  Cg_RaceAdminAuth_Clear();
  cg_radmin_password = NULL;
}

static void Cg_RaceAdminAuth_Drain(const size_t length) {
  uint8_t discard[32];
  size_t remaining = length;
  while (remaining) {
    const size_t count = min(remaining, sizeof(discard));
    cgi.ReadData(discard, count);
    remaining -= count;
  }
}

bool Cg_RaceAdminAuth_ParseMessage(const int32_t command) {
  if (command != SV_CMD_RACE_ADMIN_CHALLENGE) {
    return false;
  }

  char account[RACE_ADMIN_ACCOUNT_ID_SIZE] = { 0 };
  const int32_t account_length = cgi.ReadByte();
  const bool account_length_valid = account_length > 0 &&
    account_length <= RACE_ADMIN_ACCOUNT_ID_MAX;
  if (account_length_valid) {
    cgi.ReadData(account, (size_t) account_length);
  } else if (account_length > 0) {
    Cg_RaceAdminAuth_Drain((size_t) account_length);
  }

  uint8_t salt[RACE_ADMIN_AUTH_SALT_SIZE];
  uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE];
  cgi.ReadData(salt, sizeof(salt));
  cgi.ReadData(nonce, sizeof(nonce));

  if (!account_length_valid || !Race_AdminAuth_AccountValid(account)) {
    Cg_Warn("Rejected malformed Race administrator challenge\n");
    Cg_RaceAdminAuth_Clear();
    return true;
  }

  char password[RACE_ADMIN_PASSWORD_MAX + 1u];
  const size_t password_length = cg_radmin_password &&
    cg_radmin_password->string
      ? strnlen(cg_radmin_password->string, sizeof(password))
      : 0u;
  if (!password_length || password_length >= sizeof(password)) {
    cgi.Print("Set radmin_password locally, then use radmin <account>.\n");
    Cg_RaceAdminAuth_Clear();
    return true;
  }
  memcpy(password, cg_radmin_password->string, password_length + 1u);
  Cg_RaceAdminAuth_Clear();

  uint8_t proof[RACE_ADMIN_AUTH_PROOF_SIZE];
  char proof_command[RACE_ADMIN_AUTH_PROOF_COMMAND_SIZE];
  const bool valid = Race_AdminAuth_CreatePasswordProof(
    account, password, salt, nonce, proof) &&
    Race_AdminAuth_FormatProofCommand(account, nonce, proof, proof_command);
  Race_AdminAuth_ClearSecret(password, sizeof(password));
  Race_AdminAuth_ClearSecret(salt, sizeof(salt));
  Race_AdminAuth_ClearSecret(nonce, sizeof(nonce));
  Race_AdminAuth_ClearSecret(proof, sizeof(proof));
  if (!valid) {
    Cg_Warn("Could not create Race administrator login proof\n");
    return true;
  }

  cgi.Cbuf(proof_command);
  return true;
}
