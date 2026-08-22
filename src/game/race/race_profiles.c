/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_profiles.h"

#include "race_persistence.h"
#include "race_profile.h"

typedef enum {
  RACE_PROFILE_LOAD_OK,
  RACE_PROFILE_LOAD_MISSING,
  RACE_PROFILE_LOAD_INVALID,
  RACE_PROFILE_LOAD_ERROR
} race_profile_load_result_t;

static bool Race_Profiles_RealPaths(const char *uid,
                                    char committed[MAX_OS_PATH],
                                    char candidate[MAX_OS_PATH]) {
  char committed_virtual[MAX_OS_PATH];
  char candidate_virtual[MAX_OS_PATH];

  if (!Race_Profile_Paths(uid,
                          committed_virtual, sizeof(committed_virtual),
                          candidate_virtual, sizeof(candidate_virtual))) {
    return false;
  }

  if (!Race_Persistence_CopyRealPath(committed_virtual,
                                     gi.RealPath(committed_virtual),
                                     committed, MAX_OS_PATH)) {
    return false;
  }

  if (!Race_Persistence_CopyRealPath(candidate_virtual,
                                     gi.RealPath(candidate_virtual),
                                     candidate, MAX_OS_PATH)) {
    return false;
  }

  return true;
}

static bool Race_Profiles_Save(const race_profile_t *profile,
                               const char *committed, const char *candidate) {
  char serialized[RACE_PROFILE_SERIALIZED_MAX];
  size_t serialized_length;

  if (!Race_Profile_Serialize(profile, serialized, sizeof(serialized), &serialized_length)) {
    G_Warn("Could not serialize Race profile metadata\n");
    return false;
  }

  race_persistence_result_t result = Race_Persistence_WriteCandidate(candidate,
                                                                     serialized,
                                                                     serialized_length);
  if (result != RACE_PERSISTENCE_OK) {
    G_Warn("Could not write Race profile candidate: %s\n",
           Race_Persistence_ResultName(result));
    return false;
  }

  result = Race_Persistence_Promote(candidate, committed);
  if (result != RACE_PERSISTENCE_OK) {
    G_Warn("Could not promote Race profile candidate: %s\n",
           Race_Persistence_ResultName(result));
    return false;
  }

  return true;
}

static race_profile_load_result_t Race_Profiles_Load(const char *expected_uid,
                                                     const char *committed,
                                                     race_profile_t *profile) {
  char serialized[RACE_PROFILE_SERIALIZED_MAX];
  size_t serialized_length;

  const race_persistence_result_t read_result = Race_Persistence_Read(committed,
                                                                     serialized,
                                                                     sizeof(serialized),
                                                                     &serialized_length);
  if (read_result == RACE_PERSISTENCE_NOT_FOUND) {
    return RACE_PROFILE_LOAD_MISSING;
  }

  if (read_result != RACE_PERSISTENCE_OK) {
    G_Warn("Could not read Race profile: %s\n",
           Race_Persistence_ResultName(read_result));
    return read_result == RACE_PERSISTENCE_TOO_LARGE
      ? RACE_PROFILE_LOAD_INVALID
      : RACE_PROFILE_LOAD_ERROR;
  }

  const race_profile_parse_result_t parse_result = Race_Profile_Parse(serialized,
                                                                      serialized_length,
                                                                      profile);
  if (parse_result != RACE_PROFILE_PARSE_OK || q_strcmp(profile->uid, expected_uid)) {
    if (parse_result == RACE_PROFILE_PARSE_UNKNOWN_VERSION) {
      G_Warn("Race profile uses an unsupported version; committed data was left unchanged\n");
    } else {
      G_Warn("Race profile is malformed; committed data was left unchanged\n");
    }
    return RACE_PROFILE_LOAD_INVALID;
  }

  return RACE_PROFILE_LOAD_OK;
}

static bool Race_Profiles_DuplicateIdentity(const g_client_t *cl, const char *uid) {
  bool duplicate = false;

  G_ForEachClient(other, {
    if (other != cl && other->persistent.race_profile.ready &&
        !q_strcmp(other->persistent.race_profile.uid, uid)) {
      duplicate = true;
      break;
    }
  });

  return duplicate;
}

void Race_Profiles_Init(void) {
  if (!gi.Mkdir(RACE_PROFILE_DIRECTORY)) {
    G_Warn("Could not prepare the Race profile storage directory\n");
  }
}

void Race_Profiles_ClientUserInfoChanged(g_client_t *cl) {
  race_profile_association_t *association = &cl->persistent.race_profile;
  memset(association, 0, sizeof(*association));

  if (cl->ai) {
    return;
  }

  char uid[RACE_PROFILE_UID_SIZE];
  if (!Race_Profile_CanonicalizeUid(cl->persistent.guid, uid)) {
    if (*cl->persistent.guid) {
      G_Warn("Client %s supplied an invalid profile GUID; using an unregistered session\n",
             cl->persistent.net_name);
    }
    return;
  }

  q_strlcpy(association->uid, uid, sizeof(association->uid));

  if (Race_Profiles_DuplicateIdentity(cl, uid)) {
    G_Warn("Client %s duplicated an active profile identity; using an unregistered session\n",
           cl->persistent.net_name);
    return;
  }

  char committed[MAX_OS_PATH];
  char candidate[MAX_OS_PATH];
  if (!Race_Profiles_RealPaths(uid, committed, candidate)) {
    G_Warn("Could not resolve Race profile storage paths for %s\n",
           cl->persistent.net_name);
    return;
  }

  race_profile_t profile;
  const race_profile_load_result_t load_result = Race_Profiles_Load(uid, committed, &profile);

  if (load_result == RACE_PROFILE_LOAD_MISSING) {
    if (!Race_Profile_Init(&profile, uid, cl->persistent.net_name) ||
        !Race_Profiles_Save(&profile, committed, candidate)) {
      G_Warn("Could not register a durable Race profile for %s\n",
             cl->persistent.net_name);
      return;
    }
  } else if (load_result != RACE_PROFILE_LOAD_OK) {
    return;
  } else if (q_strcmp(profile.display_name, cl->persistent.net_name)) {
    race_profile_t updated = profile;
    if (!Race_Profile_SetDisplayName(&updated, cl->persistent.net_name) ||
        !Race_Profiles_Save(&updated, committed, candidate)) {
      G_Warn("Could not persist updated Race profile name for %s; stable identity remains associated\n",
             cl->persistent.net_name);
    }
  }

  association->ready = true;
}
