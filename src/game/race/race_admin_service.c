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
#include "race_admin_admission.h"
#include "race_admin_allowlist.h"
#include "race_admin_auth.h"
#include "race_admin_password.h"
#include "race_admin_service.h"
#include "race_admin_store.h"
#include "race_persistence.h"
#include "race_settings.h"

#define RACE_ADMIN_DIRECTORY "race"
#define RACE_ADMIN_COMMITTED RACE_ADMIN_DIRECTORY "/admins.db"
#define RACE_ADMIN_CANDIDATE RACE_ADMIN_DIRECTORY "/admins.candidate"
#define RACE_ADMIN_ALLOWLIST_COMMITTED \
  RACE_ADMIN_DIRECTORY "/operator_cvars.txt"
#define RACE_ADMIN_ALLOWLIST_CANDIDATE \
  RACE_ADMIN_DIRECTORY "/operator_cvars.candidate"
#define RACE_ADMIN_LEGACY_LOCAL "admins.cfg"
#define RACE_ADMIN_LEGACY_DEFAULT "race/admins.cfg"
#define RACE_ADMIN_DUMMY_PASSWORD "race-admin-dummy-password"
#define RACE_ADMIN_CHALLENGE_LIFETIME 15000u

typedef enum {
  RACE_ADMIN_SERVICE_READY,
  RACE_ADMIN_SERVICE_CORRUPT,
  RACE_ADMIN_SERVICE_LEGACY_BLOCKED,
  RACE_ADMIN_SERVICE_UNAVAILABLE
} race_admin_service_status_t;

typedef enum {
  RACE_ADMIN_ALLOWLIST_SERVICE_READY,
  RACE_ADMIN_ALLOWLIST_SERVICE_CORRUPT,
  RACE_ADMIN_ALLOWLIST_SERVICE_UNAVAILABLE
} race_admin_allowlist_service_status_t;

static race_admin_document_t race_admin_document;
static race_admin_service_status_t race_admin_status;
static race_admin_allowlist_t race_admin_operator_cvars;
static race_admin_allowlist_t race_admin_v3_embedded_cvars;
static race_admin_allowlist_service_status_t race_admin_allowlist_status;
static char race_admin_committed[MAX_OS_PATH];
static char race_admin_candidate[MAX_OS_PATH];
static char race_admin_allowlist_committed[MAX_OS_PATH];
static char race_admin_allowlist_candidate[MAX_OS_PATH];
static char race_admin_dummy_credential[RACE_ADMIN_CREDENTIAL_SIZE];
static bool race_admin_dummy_ready;
static bool race_admin_cvars_ready;
static bool race_admin_v3_policy_unextracted;
static bool race_admin_v3_account_rewrite_pending;
static race_admin_admission_t race_admin_admission;

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

static const char *Race_AdminService_AllowlistStatusName(void) {
  switch (race_admin_allowlist_status) {
    case RACE_ADMIN_ALLOWLIST_SERVICE_READY:
      return "ready";
    case RACE_ADMIN_ALLOWLIST_SERVICE_CORRUPT:
      return "corrupt-quarantined";
    case RACE_ADMIN_ALLOWLIST_SERVICE_UNAVAILABLE:
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

  if (!Race_Persistence_CopyRealPath(
        RACE_ADMIN_ALLOWLIST_COMMITTED,
        gi.RealPath(RACE_ADMIN_ALLOWLIST_COMMITTED),
        race_admin_allowlist_committed,
        sizeof(race_admin_allowlist_committed)) ||
      !Race_Persistence_CopyRealPath(
        RACE_ADMIN_ALLOWLIST_CANDIDATE,
        gi.RealPath(RACE_ADMIN_ALLOWLIST_CANDIDATE),
        race_admin_allowlist_candidate,
        sizeof(race_admin_allowlist_candidate))) {
    race_admin_committed[0] = '\0';
    race_admin_candidate[0] = '\0';
    race_admin_allowlist_committed[0] = '\0';
    race_admin_allowlist_candidate[0] = '\0';
    return false;
  }
  return true;
}

static bool Race_AdminService_AllowlistCvarsValid(
  const race_admin_allowlist_t *allowlist) {
  if (!Race_AdminAllowlist_Valid(allowlist)) {
    return false;
  }

  for (size_t i = 0; i < allowlist->count; i++) {
    const char *name = allowlist->names[i];
    const cvar_t *var = gi.GetCvar(name);
    if (!var || strcmp(var->name, name) || (var->flags & CVAR_NO_SET)) {
      return false;
    }
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

static size_t Race_AdminService_CredentialCount(void) {
  size_t count = 0;
  for (size_t i = 0; i < race_admin_document.count; i++) {
    if (*race_admin_document.accounts[i].credential) {
      count++;
    }
  }
  return count;
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
  if (race_admin_v3_policy_unextracted) {
    race_admin_allowlist_parse_result_t allowlist_parse =
      RACE_ADMIN_ALLOWLIST_PARSE_OK;
    const race_admin_allowlist_store_result_t migrated =
      Race_AdminAllowlistStore_Commit(
        race_admin_allowlist_committed, race_admin_allowlist_candidate,
        &race_admin_v3_embedded_cvars, &allowlist_parse);
    if (migrated != RACE_ADMIN_ALLOWLIST_STORE_OK) {
      gi.Print("Race admin mutation rejected: V3 operator policy migration is incomplete\n");
      G_Warn("Race admin V3 operator cvar migration retry failed: store=%s parse=%s\n",
             Race_AdminAllowlistStore_ResultName(migrated),
             Race_AdminAllowlist_ParseResultName(allowlist_parse));
      return false;
    }
    race_admin_operator_cvars = race_admin_v3_embedded_cvars;
    race_admin_allowlist_status = RACE_ADMIN_ALLOWLIST_SERVICE_READY;
    race_admin_v3_policy_unextracted = false;
    gi.Print("Race admin V3 operator cvars migrated during account recovery\n");
  }

  if (Race_Admin_DocumentEquals(&race_admin_document, candidate) &&
      !race_admin_v3_account_rewrite_pending) {
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
  race_admin_v3_account_rewrite_pending = false;
  if (invalidate_account) {
    Race_AdminService_ClearAccountSessions(invalidate_account);
  }
  return true;
}

static bool Race_AdminService_CommitAllowlist(
  const race_admin_allowlist_t *candidate) {
  if (race_admin_allowlist_status != RACE_ADMIN_ALLOWLIST_SERVICE_READY) {
    gi.Print("Race operator cvar allowlist mutation rejected: status=%s\n",
             Race_AdminService_AllowlistStatusName());
    return false;
  }

  if (Race_AdminAllowlist_Equals(&race_admin_operator_cvars, candidate)) {
    return true;
  }

  if (race_admin_cvars_ready &&
      !Race_AdminService_AllowlistCvarsValid(candidate)) {
    G_Warn("Race operator cvar allowlist mutation rejected: an entry is unknown, non-canonical, or write protected\n");
    return false;
  }

  race_admin_allowlist_parse_result_t parse_result =
    RACE_ADMIN_ALLOWLIST_PARSE_OK;
  const race_admin_allowlist_store_result_t stored =
    Race_AdminAllowlistStore_Commit(
      race_admin_allowlist_committed, race_admin_allowlist_candidate,
      candidate, &parse_result);
  if (stored != RACE_ADMIN_ALLOWLIST_STORE_OK) {
    G_Warn("Race operator cvar allowlist mutation failed: store=%s parse=%s; committed and live policy are unchanged\n",
           Race_AdminAllowlistStore_ResultName(stored),
           Race_AdminAllowlist_ParseResultName(parse_result));
    return false;
  }

  race_admin_operator_cvars = *candidate;
  return true;
}

static void Race_AdminService_PrintStatus(void) {
  gi.Print("Race admin: status=%s generation=%llu accounts=%zu credentials=%zu operator-cvars=%zu operator-cvars-status=%s client-login=%s\n",
           Race_AdminService_StatusName(),
           (unsigned long long) race_admin_document.generation,
           race_admin_document.count,
           Race_AdminService_CredentialCount(),
           race_admin_operator_cvars.count,
           Race_AdminService_AllowlistStatusName(),
           race_admin_dummy_ready ? "enabled" : "unavailable");
}

static void Race_AdminService_List(void) {
  if (race_admin_status != RACE_ADMIN_SERVICE_READY) {
    Race_AdminService_PrintStatus();
    return;
  }

  gi.Print("Race administrator accounts (non-secret metadata):\n");
  for (size_t i = 0; i < race_admin_document.count; i++) {
    const race_admin_account_t *account = race_admin_document.accounts + i;
    gi.Print("  id=%s handle=%s role=%s enabled=%d revision=%llu credential=%s\n",
             account->id, account->handle,
             Race_Admin_RoleName(account->role), account->enabled,
             (unsigned long long) account->revision,
             *account->credential ? "set" : "missing");
  }
}

static void Race_AdminService_Bootstrap(void) {
  if (gi.Argc() != 4) {
    gi.Print("Usage: race_admin bootstrap <account> <password>\n");
    return;
  }

  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  char credential[RACE_ADMIN_CREDENTIAL_SIZE];
  race_admin_document_t candidate;
  if (race_admin_status != RACE_ADMIN_SERVICE_READY ||
      race_admin_document.count ||
      !Race_Admin_CanonicalizeAccountId(gi.Argv(2), id) ||
      !Race_AdminPassword_Hash(gi.Argv(3), credential) ||
      !Race_Admin_AddAccount(&race_admin_document, id, id,
                             RACE_ADMIN_ROLE_OWNER, credential,
                             &candidate)) {
    gi.Print("Race admin bootstrap rejected\n");
    return;
  }

  if (Race_AdminService_Commit(&candidate, NULL)) {
    gi.Print("Race admin owner bootstrapped: id=%s\n", id);
  }
}

static void Race_AdminService_SetPassword(void) {
  if (gi.Argc() != 4) {
    gi.Print("Usage: race_admin password <account> <password>\n");
    return;
  }

  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  char credential[RACE_ADMIN_CREDENTIAL_SIZE];
  race_admin_document_t candidate;
  race_admin_document_t next;
  if (race_admin_status != RACE_ADMIN_SERVICE_READY ||
      !Race_Admin_CanonicalizeAccountId(gi.Argv(2), id) ||
      !Race_Admin_AccountById(&race_admin_document, id) ||
      !Race_AdminPassword_Hash(gi.Argv(3), credential) ||
      !Race_Admin_SetAccountCredential(&race_admin_document, id, credential,
                                       &candidate)) {
    gi.Print("Race admin password reset rejected\n");
    return;
  }

  const bool recovery =
    !Race_Admin_DocumentHasEnabledCredentialedOwner(&race_admin_document);
  if (recovery) {
    if (!Race_Admin_SetAccountRole(&candidate, id, RACE_ADMIN_ROLE_OWNER,
                                   &next)) {
      gi.Print("Race admin password reset rejected\n");
      return;
    }
    candidate = next;
    if (!Race_Admin_SetAccountEnabled(&candidate, id, true, &next)) {
      gi.Print("Race admin password reset rejected\n");
      return;
    }
    candidate = next;
  }

  if (Race_AdminService_Commit(&candidate, id)) {
    gi.Print(recovery
               ? "Race admin owner recovered: id=%s role=owner enabled=1\n"
               : "Race admin password reset: id=%s\n",
             id);
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
  } else if (!strcmp(operation, "bootstrap")) {
    Race_AdminService_Bootstrap();
  } else if (!strcmp(operation, "password")) {
    Race_AdminService_SetPassword();
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
    gi.Print("Usage: race_admin <status|list|bootstrap|password|remove|role|enable|disable|handle|grant|revoke|sessions> ...\n");
  }
}

void Race_AdminService_Init(void) {
  Race_Admin_DocumentInit(&race_admin_document);
  Race_AdminAdmission_Init(&race_admin_admission);
  Race_AdminAllowlist_Init(&race_admin_operator_cvars);
  Race_AdminAllowlist_Init(&race_admin_v3_embedded_cvars);
  race_admin_status = RACE_ADMIN_SERVICE_UNAVAILABLE;
  race_admin_allowlist_status = RACE_ADMIN_ALLOWLIST_SERVICE_UNAVAILABLE;
  race_admin_committed[0] = '\0';
  race_admin_candidate[0] = '\0';
  race_admin_allowlist_committed[0] = '\0';
  race_admin_allowlist_candidate[0] = '\0';
  race_admin_dummy_credential[0] = '\0';
  race_admin_cvars_ready = false;
  race_admin_v3_policy_unextracted = false;
  race_admin_v3_account_rewrite_pending = false;
  uint8_t dummy_salt[RACE_ADMIN_PASSWORD_SALT_SIZE] = { 0 };
  race_admin_dummy_ready = Race_AdminPassword_RandomBytes(
      dummy_salt, sizeof(dummy_salt)) &&
    Race_AdminPassword_HashWithSalt(
      RACE_ADMIN_DUMMY_PASSWORD, dummy_salt, race_admin_dummy_credential);
  Race_AdminAuth_ClearSecret(dummy_salt, sizeof(dummy_salt));
  if (!race_admin_dummy_ready) {
    G_Warn("Race admin password verification unavailable: dummy credential initialization failed\n");
  }

  gi.Mkdir(RACE_ADMIN_DIRECTORY);
  if (Race_AdminService_WriteFileExists(RACE_ADMIN_LEGACY_LOCAL) ||
      Race_AdminService_WriteFileExists(RACE_ADMIN_LEGACY_DEFAULT)) {
    race_admin_status = RACE_ADMIN_SERVICE_LEGACY_BLOCKED;
    G_Warn("Race admin legacy file was rejected because it contains plaintext credentials; remove admins.cfg or race/admins.cfg manually and provision current accounts anew\n");
  } else if (!Race_AdminService_RealPaths()) {
    G_Warn("Could not resolve Race administrator database paths\n");
  } else {
    race_admin_parse_result_t parse_result = RACE_ADMIN_PARSE_OK;
    race_admin_parse_info_t parse_info = {
      .format = RACE_ADMIN_FORMAT_V4
    };
    Race_AdminAllowlist_Init(&parse_info.embedded_v3_allowlist);
    const race_admin_store_result_t loaded = Race_AdminStore_LoadWithInfo(
      race_admin_committed, &race_admin_document, &parse_result, &parse_info);
    if (loaded == RACE_ADMIN_STORE_OK || loaded == RACE_ADMIN_STORE_MISSING) {
      race_admin_status = RACE_ADMIN_SERVICE_READY;
      race_admin_v3_account_rewrite_pending =
        parse_info.format == RACE_ADMIN_FORMAT_V3;

      race_admin_allowlist_parse_result_t allowlist_parse =
        RACE_ADMIN_ALLOWLIST_PARSE_OK;
      const race_admin_allowlist_store_result_t allowlist_loaded =
        Race_AdminAllowlistStore_Load(
          race_admin_allowlist_committed, &race_admin_operator_cvars,
          &allowlist_parse);
      if (allowlist_loaded == RACE_ADMIN_ALLOWLIST_STORE_OK ||
          allowlist_loaded == RACE_ADMIN_ALLOWLIST_STORE_MISSING) {
        race_admin_allowlist_status = RACE_ADMIN_ALLOWLIST_SERVICE_READY;

        if (allowlist_loaded == RACE_ADMIN_ALLOWLIST_STORE_MISSING &&
            parse_info.format == RACE_ADMIN_FORMAT_V3) {
          if (!Race_AdminService_CommitAllowlist(
                &parse_info.embedded_v3_allowlist)) {
            race_admin_allowlist_status =
              RACE_ADMIN_ALLOWLIST_SERVICE_UNAVAILABLE;
            race_admin_v3_policy_unextracted = true;
            race_admin_v3_embedded_cvars =
              parse_info.embedded_v3_allowlist;
            G_Warn("Race admin V3 operator cvar migration failed; admins.db is preserved and Operator cvar changes fail closed\n");
          } else {
            gi.Print("Race admin V3 operator cvars migrated to operator_cvars.txt\n");
          }
        }

        if (parse_info.format == RACE_ADMIN_FORMAT_V3 &&
            !race_admin_v3_policy_unextracted) {
          race_admin_parse_result_t migrated_parse = RACE_ADMIN_PARSE_OK;
          const race_admin_store_result_t migrated = Race_AdminStore_Commit(
            race_admin_committed, race_admin_candidate,
            &race_admin_document, &migrated_parse);
          if (migrated == RACE_ADMIN_STORE_OK) {
            race_admin_v3_account_rewrite_pending = false;
            gi.Print("Race admin account database migrated from V3 to V4\n");
          } else {
            G_Warn("Race admin V3 account rewrite deferred: store=%s parse=%s; committed V3 data remains valid\n",
                   Race_AdminStore_ResultName(migrated),
                   Race_Admin_ParseResultName(migrated_parse));
          }
        }
      } else if (allowlist_loaded == RACE_ADMIN_ALLOWLIST_STORE_CORRUPT) {
        race_admin_allowlist_status = RACE_ADMIN_ALLOWLIST_SERVICE_CORRUPT;
        G_Warn("Race operator cvar allowlist quarantined: store=%s parse=%s; Operators fail closed and committed data is preserved\n",
               Race_AdminAllowlistStore_ResultName(allowlist_loaded),
               Race_AdminAllowlist_ParseResultName(allowlist_parse));
      } else {
        race_admin_allowlist_status = RACE_ADMIN_ALLOWLIST_SERVICE_UNAVAILABLE;
        G_Warn("Race operator cvar allowlist unavailable: store=%s; Operators fail closed\n",
               Race_AdminAllowlistStore_ResultName(allowlist_loaded));
      }

      gi.Print("Race admin database: status=ready source=%s generation=%llu accounts=%zu credentials=%zu operator-cvars=%zu operator-cvars-status=%s client-login=%s\n",
               loaded == RACE_ADMIN_STORE_OK ? "committed" : "empty",
               (unsigned long long) race_admin_document.generation,
               race_admin_document.count,
               Race_AdminService_CredentialCount(),
               race_admin_operator_cvars.count,
               Race_AdminService_AllowlistStatusName(),
               race_admin_dummy_ready ? "enabled" : "unavailable");
      if (Race_AdminService_WriteFileExists(RACE_ADMIN_CANDIDATE)) {
        gi.Print("Race admin database: stale candidate ignored\n");
      }
      if (Race_AdminService_WriteFileExists(RACE_ADMIN_ALLOWLIST_CANDIDATE)) {
        gi.Print("Race operator cvar allowlist: stale candidate ignored\n");
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
            "Bootstrap or recover Race administrator accounts");
}

void Race_AdminService_PostInit(void) {
  race_admin_cvars_ready = true;
  if (race_admin_allowlist_status != RACE_ADMIN_ALLOWLIST_SERVICE_READY ||
      Race_AdminService_AllowlistCvarsValid(&race_admin_operator_cvars)) {
    return;
  }

  Race_AdminAllowlist_Init(&race_admin_operator_cvars);
  race_admin_allowlist_status = RACE_ADMIN_ALLOWLIST_SERVICE_CORRUPT;
  G_Warn("Race operator cvar allowlist quarantined: an entry is unknown, non-canonical, or write protected; Operators fail closed\n");
}

static void Race_AdminService_DenyLogin(g_client_t *cl, const char *reason) {
  gi.ClientPrint(cl, PRINT_HIGH, "Race administrator login denied\n");
  gi.Print("Race admin login denied: slot=%u reason=%s\n", cl->ps.client,
           reason);
}

static void Race_AdminService_ClientAddress(
  const g_client_t *cl, char address[RACE_ADMIN_ADMISSION_KEY_SIZE]) {
  const char *source = cl
    ? InfoString_Get(cl->persistent.user_info, "ip")
    : NULL;
  if (!source || !*source ||
      strnlen(source, RACE_ADMIN_ADMISSION_KEY_SIZE) >=
        RACE_ADMIN_ADMISSION_KEY_SIZE) {
    q_strlcpy(address, "unknown", RACE_ADMIN_ADMISSION_KEY_SIZE);
    return;
  }
  q_strlcpy(address, source, RACE_ADMIN_ADMISSION_KEY_SIZE);
  char *port = strrchr(address, ':');
  if (port) {
    *port = '\0';
  }
  if (!*address) {
    q_strlcpy(address, "unknown", RACE_ADMIN_ADMISSION_KEY_SIZE);
  }
}

static void Race_AdminService_ClearChallenge(g_client_t *cl) {
  if (cl) {
    Race_AdminAuth_ClearSecret(&cl->persistent.race_admin_challenge,
                               sizeof(cl->persistent.race_admin_challenge));
  }
}

void Race_AdminService_ClientChallenge(g_client_t *cl,
                                       const char *account_input) {
  if (!cl || !cl->in_use) {
    return;
  }

  const uint64_t now = g_level.time;
  if (race_admin_status != RACE_ADMIN_SERVICE_READY ||
      !race_admin_dummy_ready) {
    Race_AdminService_DenyLogin(cl, "unavailable");
    return;
  }

  char account_id[RACE_ADMIN_ACCOUNT_ID_SIZE] = { 0 };
  const bool account_valid = Race_Admin_CanonicalizeAccountId(
    account_input, account_id);
  if (!account_valid) {
    q_strlcpy(account_id, "invalid", sizeof(account_id));
  }
  char address[RACE_ADMIN_ADMISSION_KEY_SIZE];
  Race_AdminService_ClientAddress(cl, address);
  if (!Race_AdminAdmission_BeginChallenge(
        &race_admin_admission, account_id, address, now)) {
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator login temporarily throttled\n");
    return;
  }
  if (!account_valid) {
    Race_AdminAdmission_RecordFailure(
      &race_admin_admission, account_id, address, now);
    Race_AdminService_DenyLogin(cl, "account");
    return;
  }

  const race_admin_account_t *account = account_valid
    ? Race_Admin_AccountById(&race_admin_document, account_id)
    : NULL;
  const bool eligible = account && account->enabled && *account->credential;
  const char *credential = eligible
    ? account->credential
    : race_admin_dummy_credential;
  uint8_t salt[RACE_ADMIN_AUTH_SALT_SIZE];
  race_admin_challenge_t challenge = { 0 };
  uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE];
  const bool issued = (eligible
      ? Race_AdminAuth_CredentialSalt(credential, salt)
      : Race_AdminAuth_DeriveDummySalt(credential, account_id, salt)) &&
    Race_AdminPassword_RandomBytes(nonce, sizeof(nonce)) &&
    Race_AdminAuth_IssueChallenge(&challenge, account_id, nonce, now);
  Race_AdminAuth_ClearSecret(nonce, sizeof(nonce));
  if (!issued) {
    Race_AdminAuth_ClearSecret(salt, sizeof(salt));
    Race_AdminService_DenyLogin(cl, "challenge-unavailable");
    return;
  }
  cl->persistent.race_admin_challenge = challenge;

  gi.WriteByte(SV_CMD_RACE_ADMIN_CHALLENGE);
  gi.WriteByte((int32_t) strlen(account_id));
  gi.WriteData(account_id, strlen(account_id));
  gi.WriteData(salt, sizeof(salt));
  gi.WriteData(challenge.nonce, sizeof(challenge.nonce));
  gi.Unicast(cl, true);
  Race_AdminAuth_ClearSecret(salt, sizeof(salt));
}

void Race_AdminService_ClientProof(g_client_t *cl, const char *account_input,
                                   const char *nonce_input,
                                   const char *proof_input) {
  if (!cl || !cl->in_use) {
    return;
  }

  const uint64_t now = g_level.time;
  char account_id[RACE_ADMIN_ACCOUNT_ID_SIZE] = { 0 };
  char address[RACE_ADMIN_ADMISSION_KEY_SIZE];
  Race_AdminService_ClientAddress(cl, address);
  uint8_t nonce[RACE_ADMIN_AUTH_NONCE_SIZE] = { 0 };
  uint8_t proof[RACE_ADMIN_AUTH_PROOF_SIZE] = { 0 };
  const bool input_valid = Race_Admin_CanonicalizeAccountId(
      account_input, account_id) &&
    Race_AdminAuth_HexDecode(nonce_input, nonce, sizeof(nonce)) &&
    Race_AdminAuth_HexDecode(proof_input, proof, sizeof(proof));
  const bool challenge_valid = Race_AdminAuth_ConsumeChallenge(
      &cl->persistent.race_admin_challenge,
      input_valid ? account_id : NULL, nonce, now,
      RACE_ADMIN_CHALLENGE_LIFETIME) && input_valid;
  if (!Race_AdminAdmission_ProofAllowed(
        &race_admin_admission,
        input_valid ? account_id : "invalid", address, now)) {
    Race_AdminAuth_ClearSecret(nonce, sizeof(nonce));
    Race_AdminAuth_ClearSecret(proof, sizeof(proof));
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator login temporarily throttled\n");
    return;
  }

  const race_admin_account_t *account = input_valid
    ? Race_Admin_AccountById(&race_admin_document, account_id)
    : NULL;
  const bool eligible = account && account->enabled && *account->credential;
  const char *credential = eligible
    ? account->credential
    : race_admin_dummy_credential;
  const bool verified = Race_AdminAuth_VerifyCredentialProof(
    input_valid ? account_id : "invalid", credential, nonce, proof);

  race_admin_session_t session;
  if (challenge_valid && eligible && verified &&
      Race_Admin_SessionGrant(&session, &race_admin_document, account->id)) {
    cl->persistent.race_admin_session = session;
    Race_AdminAdmission_RecordSuccess(&race_admin_admission, account_id, now);
    gi.Print("Race admin login accepted: account=%s slot=%u client=%s\n",
             account->id, cl->ps.client, cl->persistent.net_name);
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator login accepted: account=%s role=%s\n",
                   account->id, Race_Admin_RoleName(account->role));
    Race_AdminAuth_ClearSecret(nonce, sizeof(nonce));
    Race_AdminAuth_ClearSecret(proof, sizeof(proof));
    return;
  }

  Race_AdminAdmission_RecordFailure(
    &race_admin_admission, input_valid ? account_id : "invalid", address, now);
  Race_AdminAuth_ClearSecret(nonce, sizeof(nonce));
  Race_AdminAuth_ClearSecret(proof, sizeof(proof));
  Race_AdminService_DenyLogin(cl, "credentials-or-policy");
}

void Race_AdminService_ClientLogout(g_client_t *cl) {
  if (!cl) {
    return;
  }
  Race_AdminService_ClearChallenge(cl);
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

bool Race_AdminService_ClientCvarAllowed(const g_client_t *cl,
                                         const char *name) {
  if (!Race_AdminService_ClientHasCapability(
        cl, RACE_ADMIN_CAP_SERVER_CVAR)) {
    return false;
  }
  if (Race_AdminService_ClientHasCapability(
        cl, RACE_ADMIN_CAP_CVAR_ALLOWLIST_MANAGE)) {
    return true;
  }
  return race_admin_allowlist_status == RACE_ADMIN_ALLOWLIST_SERVICE_READY &&
         Race_AdminAllowlist_Contains(&race_admin_operator_cvars, name);
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
                   "Race administrator: unauthenticated; set radmin_password <password>, then use radmin <account>\n");
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
                  "Race administrator: account=%s handle=%s role=%s revision=%llu settings=%d cvars=%d cvarlist=%d map=%d kick=%d vote=%d accounts=%d\n",
                 account->id, account->handle,
                 Race_Admin_RoleName(account->role),
                 (unsigned long long) account->revision,
                 Race_AdminService_ClientHasCapability(
                   cl, RACE_ADMIN_CAP_SETTINGS_MUTATE),
                 Race_AdminService_ClientHasCapability(
                   cl, RACE_ADMIN_CAP_SERVER_CVAR),
                 Race_AdminService_ClientHasCapability(
                   cl, RACE_ADMIN_CAP_CVAR_ALLOWLIST_MANAGE),
                 Race_AdminService_ClientHasCapability(
                   cl, RACE_ADMIN_CAP_MAP_CHANGE),
                  Race_AdminService_ClientHasCapability(
                    cl, RACE_ADMIN_CAP_PLAYER_KICK),
                  Race_AdminService_ClientHasCapability(
                    cl, RACE_ADMIN_CAP_VOTE_ADMIN),
                  Race_AdminService_ClientHasCapability(
                    cl, RACE_ADMIN_CAP_ACCOUNT_MANAGE));
}

static void Race_AdminService_ClientCvarAllowlistUsage(g_client_t *cl) {
  gi.ClientPrint(cl, PRINT_HIGH,
                 "Race operator cvar allowlist commands:\n"
                 "  allowcvar list\n"
                 "  allowcvar add <name>\n"
                 "  allowcvar remove <name>\n"
                 "  allowcvar reload\n");
}

static void Race_AdminService_ClientCvarAllowlistList(g_client_t *cl) {
  gi.ClientPrint(cl, PRINT_HIGH,
                 "Race operator cvar allowlist (%zu, status=%s):\n",
                 race_admin_operator_cvars.count,
                 Race_AdminService_AllowlistStatusName());
  for (size_t i = 0; i < race_admin_operator_cvars.count; i++) {
    gi.ClientPrint(cl, PRINT_HIGH, "  %s\n",
                   race_admin_operator_cvars.names[i]);
  }
  Race_AdminService_AuditClientAction(
    cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, NULL, "listed");
}

static const char *Race_AdminService_ResolveCvarName(const char *name) {
  const race_setting_descriptor_t *descriptor =
    Race_Settings_DescriptorForName(name);
  return descriptor ? descriptor->cvar : name;
}

static void Race_AdminService_ClientCvarAllowlistAdd(g_client_t *cl,
                                                      const char *input) {
  if (race_admin_allowlist_status != RACE_ADMIN_ALLOWLIST_SERVICE_READY) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, NULL,
      "allowlist-unavailable");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist is unavailable; repair the file and reload it\n");
    return;
  }

  const char *resolved = Race_AdminService_ResolveCvarName(input);
  cvar_t *var = gi.GetCvar(resolved);
  if (!var) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, NULL, "unknown-cvar");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist rejected: unknown cvar\n");
    return;
  }
  if (var->flags & CVAR_NO_SET) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, var->name,
      "write-protected");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist rejected: %s is write protected\n",
                   var->name);
    return;
  }
  if (!Race_AdminAllowlist_NameValid(var->name)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, var->name,
      "delegation-forbidden");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist rejected: %s cannot be delegated\n",
                   var->name);
    return;
  }
  if (Race_AdminAllowlist_Contains(&race_admin_operator_cvars, var->name)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, var->name,
      "already-allowed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar already allowed: %s\n", var->name);
    return;
  }

  race_admin_allowlist_t candidate;
  if (!Race_AdminAllowlist_Add(&race_admin_operator_cvars, var->name,
                               &candidate)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, var->name,
      "add-rejected");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist add rejected\n");
    return;
  }
  if (Race_AdminService_CommitAllowlist(&candidate)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, var->name, "allowed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowed: %s\n", var->name);
  } else {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, var->name, "add-failed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist add failed\n");
  }
}

static void Race_AdminService_ClientCvarAllowlistRemove(g_client_t *cl,
                                                         const char *input) {
  if (race_admin_allowlist_status != RACE_ADMIN_ALLOWLIST_SERVICE_READY) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, NULL,
      "allowlist-unavailable");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist is unavailable; repair the file and reload it\n");
    return;
  }

  const char *resolved = Race_AdminService_ResolveCvarName(input);
  const cvar_t *var = gi.GetCvar(resolved);
  const char *name = var ? var->name : resolved;
  if (!Race_AdminAllowlist_NameValid(name)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, NULL, "remove-rejected");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist removal rejected\n");
    return;
  }
  if (!Race_AdminAllowlist_Contains(&race_admin_operator_cvars, name)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, name, "not-allowed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar is not allowed: %s\n", name);
    return;
  }

  race_admin_allowlist_t candidate;
  if (!Race_AdminAllowlist_Remove(&race_admin_operator_cvars, name,
                                  &candidate)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, name, "remove-rejected");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist removal rejected\n");
    return;
  }
  if (Race_AdminService_CommitAllowlist(&candidate)) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, name, "removed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar removed: %s\n", name);
  } else {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE, name, "remove-failed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist removal failed\n");
  }
}

static void Race_AdminService_ClientCvarAllowlistReload(g_client_t *cl) {
  race_admin_allowlist_t candidate;
  race_admin_allowlist_parse_result_t parse_result =
    RACE_ADMIN_ALLOWLIST_PARSE_OK;
  const race_admin_allowlist_store_result_t loaded =
    Race_AdminAllowlistStore_Load(race_admin_allowlist_committed, &candidate,
                                  &parse_result);
  if (loaded != RACE_ADMIN_ALLOWLIST_STORE_OK &&
      loaded != RACE_ADMIN_ALLOWLIST_STORE_MISSING) {
    race_admin_allowlist_status =
      loaded == RACE_ADMIN_ALLOWLIST_STORE_CORRUPT
        ? RACE_ADMIN_ALLOWLIST_SERVICE_CORRUPT
        : RACE_ADMIN_ALLOWLIST_SERVICE_UNAVAILABLE;
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE,
      RACE_ADMIN_ALLOWLIST_COMMITTED, "reload-failed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist reload failed; saved policy is retained and Operators are denied until recovery\n");
    G_Warn("Race operator cvar allowlist reload failed: store=%s parse=%s\n",
           Race_AdminAllowlistStore_ResultName(loaded),
           Race_AdminAllowlist_ParseResultName(parse_result));
    return;
  }
  if (race_admin_cvars_ready &&
      !Race_AdminService_AllowlistCvarsValid(&candidate)) {
    race_admin_allowlist_status = RACE_ADMIN_ALLOWLIST_SERVICE_CORRUPT;
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE,
      RACE_ADMIN_ALLOWLIST_COMMITTED, "reload-failed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist reload failed; saved policy is retained and Operators are denied until entries are existing mutable canonical cvars\n");
    return;
  }
  if (loaded == RACE_ADMIN_ALLOWLIST_STORE_MISSING &&
      race_admin_v3_policy_unextracted) {
    Race_AdminService_AuditClientAction(
      cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE,
      RACE_ADMIN_ALLOWLIST_COMMITTED, "reload-failed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race operator cvar allowlist reload failed: V3 migration still requires operator_cvars.txt\n");
    return;
  }

  race_admin_operator_cvars = candidate;
  race_admin_allowlist_status = RACE_ADMIN_ALLOWLIST_SERVICE_READY;
  if (race_admin_v3_policy_unextracted) {
    race_admin_v3_policy_unextracted = false;
    race_admin_parse_result_t migrated_parse = RACE_ADMIN_PARSE_OK;
    const race_admin_store_result_t migrated = Race_AdminStore_Commit(
      race_admin_committed, race_admin_candidate,
      &race_admin_document, &migrated_parse);
    if (migrated != RACE_ADMIN_STORE_OK) {
      G_Warn("Race admin V3 account rewrite deferred after allowlist recovery: store=%s parse=%s\n",
             Race_AdminStore_ResultName(migrated),
             Race_Admin_ParseResultName(migrated_parse));
    } else {
      race_admin_v3_account_rewrite_pending = false;
    }
  }

  Race_AdminService_AuditClientAction(
    cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE,
    RACE_ADMIN_ALLOWLIST_COMMITTED, "reloaded");
  gi.ClientPrint(cl, PRINT_HIGH,
                 "Race operator cvar allowlist reloaded: %zu entries\n",
                 race_admin_operator_cvars.count);
}

void Race_AdminService_ClientCvarAllowlistCommand(g_client_t *cl,
                                                   const char *operation,
                                                   const char *name) {
  if (!cl || !cl->in_use) {
    return;
  }
  if (!Race_AdminService_AuthorizeClientAction(
        cl, RACE_ADMIN_ACTION_CVAR_ALLOWLIST_MANAGE)) {
    return;
  }

  if (operation && !strcmp(operation, "list") && (!name || !*name)) {
    Race_AdminService_ClientCvarAllowlistList(cl);
  } else if (operation && !strcmp(operation, "add") && name && *name) {
    Race_AdminService_ClientCvarAllowlistAdd(cl, name);
  } else if (operation && !strcmp(operation, "remove") && name && *name) {
    Race_AdminService_ClientCvarAllowlistRemove(cl, name);
  } else if (operation && !strcmp(operation, "reload") &&
             (!name || !*name)) {
    Race_AdminService_ClientCvarAllowlistReload(cl);
  } else {
    Race_AdminService_ClientCvarAllowlistUsage(cl);
  }
}

static void Race_AdminService_ClientAccountUsage(g_client_t *cl) {
  gi.ClientPrint(cl, PRINT_HIGH,
                 "Race admin account commands:\n"
                 "  race admin account list\n"
                 "  race admin account add <account> <password> <moderator|operator|owner>\n"
                 "  race admin account remove <account>\n"
                 "  race admin account role <account> <moderator|operator|owner>\n"
                 "  race admin account enable <account>\n"
                 "  race admin account disable <account>\n"
                 "  race admin account password <account> <new-password>\n");
}

static void Race_AdminService_AuditAccountResult(const char *actor,
                                                  const g_client_t *cl,
                                                  const char *subject,
                                                  const char *result) {
  gi.Print("Race admin action: account=%s slot=%u action=%s subject=%s result=%s\n",
           actor, cl->ps.client,
           Race_Admin_ActionName(RACE_ADMIN_ACTION_ACCOUNT_MANAGE),
           subject && *subject ? subject : "-",
           result && *result ? result : "unknown");
}

static void Race_AdminService_ClientAccountList(g_client_t *cl,
                                                 const char *actor) {
  if (gi.Argc() != 4) {
    Race_AdminService_ClientAccountUsage(cl);
    return;
  }

  gi.ClientPrint(cl, PRINT_HIGH, "Race administrator accounts:\n");
  for (size_t i = 0; i < race_admin_document.count; i++) {
    const race_admin_account_t *account = race_admin_document.accounts + i;
    gi.ClientPrint(cl, PRINT_HIGH,
                   "  account=%s handle=%s role=%s enabled=%d credential=%s\n",
                   account->id, account->handle,
                   Race_Admin_RoleName(account->role), account->enabled,
                   *account->credential ? "set" : "missing");
  }
  Race_AdminService_AuditAccountResult(actor, cl, NULL, "listed");
}

static void Race_AdminService_ClientAccountAdd(g_client_t *cl,
                                                const char *actor) {
  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  char credential[RACE_ADMIN_CREDENTIAL_SIZE];
  const race_admin_role_t role = gi.Argc() == 7
    ? Race_Admin_RoleForName(gi.Argv(6))
    : RACE_ADMIN_ROLE_NONE;
  race_admin_document_t candidate;
  if (gi.Argc() != 7 ||
      !Race_Admin_CanonicalizeAccountId(gi.Argv(4), id) ||
      role == RACE_ADMIN_ROLE_NONE ||
      !Race_AdminPassword_Hash(gi.Argv(5), credential) ||
      !Race_Admin_AddAccount(&race_admin_document, id, id, role, credential,
                             &candidate)) {
    Race_AdminService_AuditAccountResult(actor, cl, NULL, "add-rejected");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator account add rejected\n");
    return;
  }

  if (Race_AdminService_Commit(&candidate, NULL)) {
    Race_AdminService_AuditAccountResult(actor, cl, id, "added");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator account added: account=%s role=%s\n",
                   id, Race_Admin_RoleName(role));
  } else {
    Race_AdminService_AuditAccountResult(actor, cl, id, "add-failed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator account add failed\n");
  }
}

static void Race_AdminService_ClientAccountRemove(g_client_t *cl,
                                                   const char *actor) {
  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  race_admin_document_t candidate;
  if (gi.Argc() != 5 ||
      !Race_Admin_CanonicalizeAccountId(gi.Argv(4), id) ||
      !Race_Admin_RemoveAccount(&race_admin_document, id, &candidate)) {
    Race_AdminService_AuditAccountResult(actor, cl, NULL, "remove-rejected");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator account removal rejected\n");
    return;
  }

  if (Race_AdminService_Commit(&candidate, id)) {
    Race_AdminService_AuditAccountResult(actor, cl, id, "removed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator account removed: account=%s\n", id);
  } else {
    Race_AdminService_AuditAccountResult(actor, cl, id, "remove-failed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator account removal failed\n");
  }
}

static void Race_AdminService_ClientAccountRole(g_client_t *cl,
                                                 const char *actor) {
  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  const race_admin_role_t role = gi.Argc() == 6
    ? Race_Admin_RoleForName(gi.Argv(5))
    : RACE_ADMIN_ROLE_NONE;
  race_admin_document_t candidate;
  if (gi.Argc() != 6 ||
      !Race_Admin_CanonicalizeAccountId(gi.Argv(4), id) ||
      role == RACE_ADMIN_ROLE_NONE ||
      !Race_Admin_SetAccountRole(&race_admin_document, id, role,
                                 &candidate)) {
    Race_AdminService_AuditAccountResult(actor, cl, NULL, "role-rejected");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator role change rejected\n");
    return;
  }

  if (Race_AdminService_Commit(&candidate, id)) {
    Race_AdminService_AuditAccountResult(actor, cl, id, "role-updated");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator role updated: account=%s role=%s\n",
                   id, Race_Admin_RoleName(role));
  } else {
    Race_AdminService_AuditAccountResult(actor, cl, id, "role-failed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator role change failed\n");
  }
}

static void Race_AdminService_ClientAccountEnabled(g_client_t *cl,
                                                    const char *actor,
                                                    bool enabled) {
  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  race_admin_document_t candidate;
  if (gi.Argc() != 5 ||
      !Race_Admin_CanonicalizeAccountId(gi.Argv(4), id) ||
      !Race_Admin_SetAccountEnabled(&race_admin_document, id, enabled,
                                    &candidate)) {
    Race_AdminService_AuditAccountResult(
      actor, cl, NULL, enabled ? "enable-rejected" : "disable-rejected");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator enable state change rejected\n");
    return;
  }

  if (Race_AdminService_Commit(&candidate, id)) {
    Race_AdminService_AuditAccountResult(
      actor, cl, id, enabled ? "enabled" : "disabled");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator account %s: account=%s\n",
                   enabled ? "enabled" : "disabled", id);
  } else {
    Race_AdminService_AuditAccountResult(
      actor, cl, id, enabled ? "enable-failed" : "disable-failed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator enable state change failed\n");
  }
}

static void Race_AdminService_ClientAccountPassword(g_client_t *cl,
                                                     const char *actor) {
  char id[RACE_ADMIN_ACCOUNT_ID_SIZE];
  char credential[RACE_ADMIN_CREDENTIAL_SIZE];
  race_admin_document_t candidate;
  if (gi.Argc() != 6 ||
      !Race_Admin_CanonicalizeAccountId(gi.Argv(4), id) ||
      !Race_Admin_AccountById(&race_admin_document, id) ||
      !Race_AdminPassword_Hash(gi.Argv(5), credential) ||
      !Race_Admin_SetAccountCredential(&race_admin_document, id, credential,
                                       &candidate)) {
    Race_AdminService_AuditAccountResult(actor, cl, NULL,
                                         "password-rejected");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator password change rejected\n");
    return;
  }

  if (Race_AdminService_Commit(&candidate, id)) {
    Race_AdminService_AuditAccountResult(actor, cl, id, "password-updated");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator password changed: account=%s\n", id);
  } else {
    Race_AdminService_AuditAccountResult(actor, cl, id, "password-failed");
    gi.ClientPrint(cl, PRINT_HIGH,
                   "Race administrator password change failed\n");
  }
}

void Race_AdminService_ClientAccountCommand(g_client_t *cl) {
  if (!cl || !cl->in_use) {
    return;
  }
  if (!Race_AdminService_AuthorizeClientAction(
        cl, RACE_ADMIN_ACTION_ACCOUNT_MANAGE)) {
    return;
  }

  char actor[RACE_ADMIN_ACCOUNT_ID_SIZE];
  q_strlcpy(actor, cl->persistent.race_admin_session.account_id,
            sizeof(actor));
  const char *operation = gi.Argc() > 3 ? gi.Argv(3) : "";
  if (!strcmp(operation, "list")) {
    Race_AdminService_ClientAccountList(cl, actor);
  } else if (!strcmp(operation, "add")) {
    Race_AdminService_ClientAccountAdd(cl, actor);
  } else if (!strcmp(operation, "remove")) {
    Race_AdminService_ClientAccountRemove(cl, actor);
  } else if (!strcmp(operation, "role")) {
    Race_AdminService_ClientAccountRole(cl, actor);
  } else if (!strcmp(operation, "enable")) {
    Race_AdminService_ClientAccountEnabled(cl, actor, true);
  } else if (!strcmp(operation, "disable")) {
    Race_AdminService_ClientAccountEnabled(cl, actor, false);
  } else if (!strcmp(operation, "password")) {
    Race_AdminService_ClientAccountPassword(cl, actor);
  } else {
    Race_AdminService_ClientAccountUsage(cl);
  }
}
