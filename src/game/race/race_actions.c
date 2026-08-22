/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "g_local.h"

#include <errno.h>
#include <string.h>

#include "race_actions.h"
#include "race_kick_broker.h"

#define RACE_KICK_COMMIT_COMMAND "race_kick_commit"

static uint64_t race_connection_counter;

static size_t Race_Actions_MaxClients(void) {
  if (!sv_max_clients || sv_max_clients->integer <= 0) {
    return 0u;
  }
  return (size_t) Mini(sv_max_clients->integer, MAX_CLIENTS);
}

static bool Race_Actions_ParseUint64(const char *text, const uint64_t max,
                                     uint64_t *value) {
  if (!text || !*text || !value || *text == '-') {
    return false;
  }

  errno = 0;
  char *end = NULL;
  const uint64_t parsed = strtoull(text, &end, 10);
  if (errno == ERANGE || end == text || *end || parsed > max) {
    return false;
  }

  *value = parsed;
  return true;
}

/**
 * @brief Private same-buffer-drain identity revalidation stage.
 */
static void Race_Actions_KickCommit_f(void) {
  uint64_t slot_value, connection_id;
  if (gi.Argc() != 3 ||
      !Race_Actions_ParseUint64(gi.Argv(1), UINT16_MAX, &slot_value) ||
      !Race_Actions_ParseUint64(gi.Argv(2), UINT64_MAX, &connection_id)) {
    return;
  }

  const race_kick_ticket_t ticket = {
    .slot = (uint16_t) slot_value,
    .connection_id = connection_id,
  };
  const size_t max_clients = Race_Actions_MaxClients();
  const g_client_t *cl = ticket.slot < max_clients
    ? ge.clients[ticket.slot] : NULL;

  if (Race_KickBrokerValidate(ticket, max_clients, cl && cl->in_use,
                              cl ? cl->persistent.race_connection_id : 0u) !=
      RACE_KICK_BROKER_EXECUTE) {
    return;
  }

  char command[64];
  if (Race_KickBrokerFormatStockKick(command, sizeof(command), ticket)) {
    gi.Cbuf(command);
  }
}

void Race_Actions_Init(void) {
  gi.AddCmd(RACE_KICK_COMMIT_COMMAND, Race_Actions_KickCommit_f, CMD_GAME,
            "Private Race kick identity revalidation stage");
}

void Race_Actions_Shutdown(void) {
  race_connection_counter = 0u;
}

bool Race_Actions_ValidateMap(const char *input,
                              char canonical[RACE_MAP_IDENTITY_SIZE]) {
  if (!Race_MapState_CanonicalizeMap(input, canonical)) {
    return false;
  }

  char path[MAX_QPATH + sizeof("maps/.bsp")];
  q_snprintf(path, sizeof(path), "maps/%s.bsp", canonical);
  return gi.FileExists(path);
}

bool Race_Actions_ScheduleMap(const char *canonical) {
  char validated[RACE_MAP_IDENTITY_SIZE];
  if (!Race_Actions_ValidateMap(canonical, validated) ||
      strcmp(canonical, validated)) {
    return false;
  }

  gi.Cbuf(va("map %s\n", validated));
  return true;
}

bool Race_Actions_KickClient(g_client_t *target, const char *reason) {
  if (!target || !target->in_use || !reason || !*reason) {
    return false;
  }

  const size_t max_clients = Race_Actions_MaxClients();
  const size_t slot = target->ps.client;
  race_kick_ticket_t ticket;
  if (slot >= max_clients ||
      !Race_KickBrokerCapture((uint16_t) slot, max_clients, true,
                              &target->persistent.race_connection_id,
                              &race_connection_counter, &ticket)) {
    return false;
  }

  char command[MAX_STRING_CHARS];
  if (!Race_KickBrokerFormatCommit(command, sizeof(command),
                                   RACE_KICK_COMMIT_COMMAND, ticket)) {
    return false;
  }

  // Stock kick supplies its own disconnect reason; custom reason parity is
  // intentionally not provided by the standalone module.
  gi.Cbuf(command);
  return true;
}
