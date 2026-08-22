/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "g_local.h"

#include <string.h>

#include "race_admin.h"
#include "race_admin_service.h"
#include "race_admin_store.h"
#include "race_persistence.h"

#define RACE_ADMIN_COMMITTED "admins.db"
#define RACE_ADMIN_CANDIDATE "admins.candidate"
#define RACE_ADMIN_LEGACY_LOCAL "admins.cfg"
#define RACE_ADMIN_LEGACY_DEFAULT "race/admins.cfg"

typedef enum {
  RACE_ADMIN_SERVICE_READY,
  RACE_ADMIN_SERVICE_CORRUPT,
  RACE_ADMIN_SERVICE_LEGACY_BLOCKED,
  RACE_ADMIN_SERVICE_UNAVAILABLE
} race_admin_service_status_t;

static race_admin_document_t race_admin_document;
static race_admin_service_status_t race_admin_status;
static char race_admin_committed[MAX_OS_PATH];
static char race_admin_candidate[MAX_OS_PATH];

static bool Race_AdminService_ClientAuthenticated(const g_client_t *cl);

static const char *Race_AdminService_StatusName(void) {
  switch (race_admin_status) {
    case RACE_ADMIN_SERVICE_READY:
      return "ready";
    case RACE_ADMIN_SERVICE_CORRUPT:
      return "corrupt-quarantined";
    case RACE_ADMIN_SERVICE_LEGACY_BLOCKED:
      return "legacy-reset-required";
    case RACE_ADMIN_SERVICE_UNAVAILABLE:
      return "unavailable";
  }
  return "unknown";
}

static bool Race_AdminService_RealPaths(void) {
  if (!Race_Persistence_CopyRealPath(RACE_ADMIN_COMMITTED,
                                     gi.RealPath(RACE_ADMIN_COMMITTED),
                                     race_admin_committed,
                                     sizeof(race_admin_committed))) {
    return false;
  }

  if (!Race_Persistence_CopyRealPath(RACE_ADMIN_CANDIDATE,
                                     gi.RealPath(RACE_ADMIN_CANDIDATE),
                                     race_admin_candidate,
                                     sizeof(race_admin_candidate))) {
    race_admin_committed[0] = '\0';
    return false;
  }
  return true;
}

/**
 * @brief Returns true only when the named admin file exists in the active
 * write directory.
 * @details Stock Quetoo keeps the normal user directory mounted as a read-only
 * fallback even when `--wpath` selects an isolated profile. Administrator
 * credentials and recovery candidates must not leak across that boundary.
 */
static bool Race_AdminService_WriteFileExists(const char *path) {
  char real[MAX_OS_PATH];
  if (!Race_Persistence_CopyRealPath(path, gi.RealPath(path),
                                     real, sizeof(real))) {
    return false;
  }

  FILE *file = fopen(real, "rb");
  if (!file) {
    return false;
  }
  fclose(file);
  return true;
}

static void Race_AdminService_ClearAccountSessions(const char *account_id) {
  G_ForEachClient(cl, {
    race_admin_session_t *session = &cl->persistent.race_admin_session;
    if (session->authenticated && !strcmp(session->account_id, account_id)) {
      Race_Admin_SessionClear(session);
      gi.ClientPrint(cl, PRINT_HIGH,
                     "Race administrator session invalidated\n");
    }
  });
}

static bool Race_AdminService_Commit(
  const race_admin_document_t *candidate, const char *invalidate_account) {
  if (race_admin_status != RACE_ADMIN_SERVICE_READY) {
    gi.Print("Race admin mutation rejected: database status=%s\n",
             Race_AdminService_StatusName());
    return false;
  }

  if (Race_Admin_DocumentEquals(&race_admin_document, candidate)) {
    gi.Print("Race admin mutation was a no-op\n");
    return true;
  }

  race_admin_parse_result_t parse_result = RACE_ADMIN_PARSE_OK;
  const race_admin_store_result_t stored = Race_AdminStore_Commit(
    race_admin_committed, race_admin_candidate, candidate, &parse_result);
  if (stored != RACE_ADMIN_STORE_OK) {
    G_Warn("Race admin mutation failed: store=%s parse=%s; committed state and sessions are unchanged\n",
           Race_AdminStore_ResultName(stored),
           Race_Admin_ParseResultName(parse_result));
    return false;
  }

  race_admin_document = *candidate;
  if (invalidate_account) {
    Race_AdminService_ClearAccountSessions(invalidate_account);
  }
  return true;
}

static void Race_AdminService_PrintStatus(void) {
  gi.Print("Race admin: status=%s generation=%llu accounts=%zu credentials=disabled client-login=deferred\n",
           Race_AdminService_StatusName(),
           (unsigned long long) race_admin_document.generation,
           race_admin_document.count);
}

static void Race_AdminService_List(void) {
  if (race_admin_status != RACE_ADMIN_SERVICE_READY) {
    Race_AdminService_PrintStatus();
    return;
  }

  gi.Print("Race administrator accounts (non-secret metadata):\n");
  for (size_t i = 0; i < race_admin_document.count; i++) {
    const race_admin_account_t *account = race_admin_document.accounts + i;
    gi.Print("  id=%s handle=%s role=%s enabled=%d revision=%llu\n",
             account->id, account->handle,
             Race_Admin_RoleName(account->role), account->enabled,
             (unsigned long long) account->revision);
  }
}

static void Race_AdminService_Create(void) {
  if (gi.Argc() != 5) {
    gi.Print("Usage: race_admin create <stable-id> <handle> <moderator|operator|owner>\n");
    return;
  }

  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  char handle[RACE_ADMIN_HANDLE_SIZE];
  const race_admin_role_t role = Race_Admin_RoleForName(gi.Argv(4));
  race_admin_document_t candidate;
  if (!Race_Admin_CanonicalizeAccountId(gi.Argv(2), id) ||
      !Race_Admin_CanonicalizeHandle(gi.Argv(3), handle) ||
      !Race_Admin_AddAccount(&race_admin_document, id, handle, role,
                             &candidate)) {
    gi.Print("Race admin account creation rejected: invalid or duplicate bounded account metadata\n");
    return;
  }

  if (Race_AdminService_Commit(&candidate, NULL)) {
    gi.Print("Race admin account created: id=%s handle=%s role=%s\n",
             id, handle, Race_Admin_RoleName(role));
  }
}

static void Race_AdminService_Remove(void) {
  if (gi.Argc() != 3) {
    gi.Print("Usage: race_admin remove <stable-id>\n");
    return;
  }

  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  race_admin_document_t candidate;
  if (!Race_Admin_CanonicalizeAccountId(gi.Argv(2), id) ||
      !Race_Admin_RemoveAccount(&race_admin_document, id, &candidate)) {
    gi.Print("Race admin account removal rejected\n");
    return;
  }
  if (Race_AdminService_Commit(&candidate, id)) {
    gi.Print("Race admin account removed: id=%s\n", id);
  }
}

static void Race_AdminService_SetRole(void) {
  if (gi.Argc() != 4) {
    gi.Print("Usage: race_admin role <stable-id> <moderator|operator|owner>\n");
    return;
  }

  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  const race_admin_role_t role = Race_Admin_RoleForName(gi.Argv(3));
  race_admin_document_t candidate;
  if (!Race_Admin_CanonicalizeAccountId(gi.Argv(2), id) ||
      !Race_Admin_SetAccountRole(&race_admin_document, id, role, &candidate)) {
    gi.Print("Race admin role change rejected\n");
    return;
  }
  if (Race_AdminService_Commit(&candidate, id)) {
    gi.Print("Race admin role updated: id=%s role=%s\n", id,
             Race_Admin_RoleName(role));
  }
}

static void Race_AdminService_SetEnabled(bool enabled) {
  if (gi.Argc() != 3) {
    gi.Print("Usage: race_admin %s <stable-id>\n",
             enabled ? "enable" : "disable");
    return;
  }

  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  race_admin_document_t candidate;
  if (!Race_Admin_CanonicalizeAccountId(gi.Argv(2), id) ||
      !Race_Admin_SetAccountEnabled(&race_admin_document, id, enabled,
                                    &candidate)) {
    gi.Print("Race admin enable state change rejected\n");
    return;
  }
  if (Race_AdminService_Commit(&candidate, id)) {
    gi.Print("Race admin account %s: id=%s\n",
             enabled ? "enabled" : "disabled", id);
  }
}

static void Race_AdminService_SetHandle(void) {
  if (gi.Argc() != 4) {
    gi.Print("Usage: race_admin handle <stable-id> <new-handle>\n");
    return;
  }

  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  race_admin_document_t candidate;
  if (!Race_Admin_CanonicalizeAccountId(gi.Argv(2), id) ||
      !Race_Admin_SetAccountHandle(&race_admin_document, id, gi.Argv(3),
                                   &candidate)) {
    gi.Print("Race admin handle change rejected\n");
    return;
  }
  if (Race_AdminService_Commit(&candidate, id)) {
    gi.Print("Race admin handle updated: id=%s\n", id);
  }
}

static void Race_AdminService_Grant(void) {
  if (gi.Argc() != 4) {
    gi.Print("Usage: race_admin grant <client-slot> <stable-id>\n");
    return;
  }

  int32_t slot;
  if (race_admin_status != RACE_ADMIN_SERVICE_READY ||
      !Race_Admin_ParseClientSlot(gi.Argv(2), sv_max_clients->integer, &slot)) {
    gi.Print("Race admin session grant rejected\n");
    return;
  }

  g_client_t *cl = ge.clients[slot];
  if (!cl->in_use ||
      !Race_Admin_SessionGrant(&cl->persistent.race_admin_session,
                               &race_admin_document, gi.Argv(3))) {
    gi.Print("Race admin session grant rejected\n");
    return;
  }

  gi.Print("Race admin session granted: slot=%d account=%s client=%s\n",
           slot, cl->persistent.race_admin_session.account_id,
           cl->persistent.net_name);
  gi.ClientPrint(cl, PRINT_HIGH, "Race administrator session granted\n");
}

static void Race_AdminService_Revoke(void) {
  if (gi.Argc() != 3) {
    gi.Print("Usage: race_admin revoke <client-slot>\n");
    return;
  }

  int32_t slot;
  if (!Race_Admin_ParseClientSlot(gi.Argv(2), sv_max_clients->integer, &slot) ||
      !ge.clients[slot]->in_use) {
    gi.Print("Race admin session revoke rejected\n");
    return;
  }

  Race_AdminService_ClientLogout(ge.clients[slot]);
  gi.Print("Race admin session revoked: slot=%d\n", slot);
}

static void Race_AdminService_Sessions(void) {
  gi.Print("Race administrator sessions:\n");
  G_ForEachClient(cl, {
    if (Race_AdminService_ClientAuthenticated(cl)) {
      gi.Print("  slot=%u account=%s client=%s\n", cl->ps.client,
               cl->persistent.race_admin_session.account_id,
               cl->persistent.net_name);
    }
  });
}

static void Race_AdminService_Command(void) {
  const char *operation = gi.Argc() > 1 ? gi.Argv(1) : "status";
  if (!strcmp(operation, "status")) {
    Race_AdminService_PrintStatus();
  } else if (!strcmp(operation, "list")) {
    Race_AdminService_List();
  } else if (!strcmp(operation, "create")) {
    Race_AdminService_Create();
  } else if (!strcmp(operation, "remove")) {
    Race_AdminService_Remove();
  } else if (!strcmp(operation, "role")) {
    Race_AdminService_SetRole();
  } else if (!strcmp(operation, "enable")) {
    Race_AdminService_SetEnabled(true);
  } else if (!strcmp(operation, "disable")) {
    Race_AdminService_SetEnabled(false);
  } else if (!strcmp(operation, "handle")) {
    Race_AdminService_SetHandle();
  } else if (!strcmp(operation, "grant")) {
    Race_AdminService_Grant();
  } else if (!strcmp(operation, "revoke")) {
    Race_AdminService_Revoke();
  } else if (!strcmp(operation, "sessions")) {
    Race_AdminService_Sessions();
  } else {
    gi.Print("Usage: race_admin <status|list|create|remove|role|enable|disable|handle|grant|revoke|sessions> ...\n");
  }
}

void Race_AdminService_Init(void) {
  Race_Admin_DocumentInit(&race_admin_document);
  race_admin_status = RACE_ADMIN_SERVICE_UNAVAILABLE;
  race_admin_committed[0] = '\0';
  race_admin_candidate[0] = '\0';

  gi.Mkdir("race");
  if (Race_AdminService_WriteFileExists(RACE_ADMIN_LEGACY_LOCAL) ||
      Race_AdminService_WriteFileExists(RACE_ADMIN_LEGACY_DEFAULT)) {
    race_admin_status = RACE_ADMIN_SERVICE_LEGACY_BLOCKED;
    G_Warn("Race admin legacy file was rejected because it contains plaintext credentials; remove admins.cfg or race/admins.cfg manually and provision current accounts anew\n");
  } else if (!Race_AdminService_RealPaths()) {
    G_Warn("Could not resolve Race administrator database paths\n");
  } else {
    race_admin_parse_result_t parse_result = RACE_ADMIN_PARSE_OK;
    const race_admin_store_result_t loaded = Race_AdminStore_Load(
      race_admin_committed, &race_admin_document, &parse_result);
    if (loaded == RACE_ADMIN_STORE_OK || loaded == RACE_ADMIN_STORE_MISSING) {
      race_admin_status = RACE_ADMIN_SERVICE_READY;
      gi.Print("Race admin database: status=ready source=%s generation=%llu accounts=%zu credentials=disabled\n",
               loaded == RACE_ADMIN_STORE_OK ? "committed" : "empty",
               (unsigned long long) race_admin_document.generation,
               race_admin_document.count);
      if (Race_AdminService_WriteFileExists(RACE_ADMIN_CANDIDATE)) {
        gi.Print("Race admin database: stale candidate ignored\n");
      }
    } else if (loaded == RACE_ADMIN_STORE_CORRUPT) {
      race_admin_status = RACE_ADMIN_SERVICE_CORRUPT;
      G_Warn("Race admin database quarantined: store=%s parse=%s; committed data is preserved and mutations are blocked\n",
             Race_AdminStore_ResultName(loaded),
             Race_Admin_ParseResultName(parse_result));
    } else {
      G_Warn("Race admin database unavailable: store=%s\n",
             Race_AdminStore_ResultName(loaded));
    }
  }

  gi.AddCmd("race_admin", Race_AdminService_Command, CMD_GAME,
            "Manage Race administrator accounts and trusted sessions");
}

void Race_AdminService_ClientLogout(g_client_t *cl) {
  if (!cl) {
    return;
  }
  const bool authenticated = cl->persistent.race_admin_session.authenticated;
  char account[RACE_ADMIN_ACCOUNT_ID_SIZE] = "none";
  if (authenticated) {
    q_strlcpy(account, cl->persistent.race_admin_session.account_id,
              sizeof(account));
  }
  Race_Admin_SessionClear(&cl->persistent.race_admin_session);
  if (authenticated && cl->in_use) {
    gi.Print("Race admin session cleared: account=%s slot=%u\n",
             account, cl->ps.client);
    gi.ClientPrint(cl, PRINT_HIGH, "Race administrator session cleared\n");
  }
}

static bool Race_AdminService_ClientAuthenticated(const g_client_t *cl) {
  return cl && race_admin_status == RACE_ADMIN_SERVICE_READY &&
         Race_Admin_SessionAuthenticated(&cl->persistent.race_admin_session,
                                         &race_admin_document);
}

uint32_t Race_AdminService_ClientCapabilities(const g_client_t *cl) {
  return Race_AdminService_ClientAuthenticated(cl)
    ? cl->persistent.race_admin_session.capabilities & RACE_ADMIN_CAP_ALL
    : 0u;
}

static bool Race_AdminService_ClientHasCapability(
  const g_client_t *cl, race_admin_capability_t capability) {
  return cl && race_admin_status == RACE_ADMIN_SERVICE_READY &&
         Race_Admin_SessionHasCapability(&cl->persistent.race_admin_session,
                                          &race_admin_document, capability);
}

bool Race_AdminService_AuthorizeClientAction(g_client_t *cl,
                                             race_admin_action_t action) {
  const race_admin_capability_t capability = Race_Admin_ActionCapability(action);
  const bool authorized = cl && cl->in_use &&
    race_admin_status == RACE_ADMIN_SERVICE_READY &&
    Race_Admin_SessionCanPerform(&cl->persistent.race_admin_session,
                                 &race_admin_document, action);
  if (authorized) {
    return true;
  }

  if (cl && cl->in_use) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator action denied: %s capability required\n",
                   Race_Admin_CapabilityName(capability));
  }
  Race_AdminService_AuditClientAction(cl, action, NULL, "denied");
  return false;
}

void Race_AdminService_AuditClientAction(const g_client_t *cl,
                                         race_admin_action_t action,
                                         const char *subject,
                                         const char *result) {
  const race_admin_session_t *session = cl
    ? &cl->persistent.race_admin_session
    : NULL;
  const char *account = session && session->authenticated && *session->account_id
    ? session->account_id
    : "none";
  const int32_t slot = cl ? cl->ps.client : -1;
  gi.Print("Race admin action: account=%s slot=%d action=%s subject=%s result=%s\n",
           account, slot, Race_Admin_ActionName(action),
           subject && *subject ? subject : "-",
           result && *result ? result : "unknown");
}

void Race_AdminService_PrintClientStatus(g_client_t *cl) {
  if (!cl || !cl->in_use) {
    return;
  }
  if (!Race_AdminService_ClientAuthenticated(cl)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator: unauthenticated; trusted server grant required\n");
    return;
  }

  const race_admin_session_t *session = &cl->persistent.race_admin_session;
  const race_admin_account_t *account = Race_Admin_AccountById(
    &race_admin_document, session->account_id);
  if (!account) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator: session unavailable\n");
    return;
  }

  gi.ClientPrint(cl, PRINT_HIGH,
                  "Race administrator: account=%s handle=%s role=%s revision=%llu settings=%d map=%d kick=%d vote=%d\n",
                 account->id, account->handle,
                 Race_Admin_RoleName(account->role),
                 (unsigned long long) account->revision,
                 Race_AdminService_ClientHasCapability(
                   cl, RACE_ADMIN_CAP_SETTINGS_MUTATE),
                 Race_AdminService_ClientHasCapability(
                   cl, RACE_ADMIN_CAP_MAP_CHANGE),
                  Race_AdminService_ClientHasCapability(
                    cl, RACE_ADMIN_CAP_PLAYER_KICK),
                  Race_AdminService_ClientHasCapability(
                    cl, RACE_ADMIN_CAP_VOTE_ADMIN));
}
