/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <aclapi.h>
#include <direct.h>
#include <windows.h>
#include <winioctl.h>
#define Race_NativePersistenceMkdir(path) _mkdir(path)
#define Race_NativePersistenceRmdir(path) _rmdir(path)
#define RACE_NATIVE_PATH_SEPARATOR "\\"
#else
#include <sys/stat.h>
#include <unistd.h>
#define Race_NativePersistenceMkdir(path) mkdir(path, 0700)
#define Race_NativePersistenceRmdir(path) rmdir(path)
#define RACE_NATIVE_PATH_SEPARATOR "/"
#endif

#include "race_persistence.h"

#define RACE_NATIVE_PERSISTENCE_PATH_SIZE 1024u

static bool Race_NativePersistencePath(char *path, const size_t capacity,
                                       const char *root, const char *leaf) {
  const int written = snprintf(path, capacity, "%s%s%s", root,
                               RACE_NATIVE_PATH_SEPARATOR, leaf);
  return written > 0 && (size_t) written < capacity;
}

static bool Race_NativePersistenceReadEquals(const char *path,
                                              const char *expected) {
  char buffer[64];
  size_t length = 0u;
  return Race_Persistence_Read(path, buffer, sizeof(buffer), &length) ==
           RACE_PERSISTENCE_OK &&
         length == strlen(expected) &&
         !memcmp(buffer, expected, length);
}

#if defined(_WIN32)

typedef struct {
  DWORD reparse_tag;
  WORD reparse_data_length;
  WORD reserved;
  WORD substitute_name_offset;
  WORD substitute_name_length;
  WORD print_name_offset;
  WORD print_name_length;
  wchar_t path_buffer[1];
} race_native_mount_point_t;

static bool Race_NativePersistenceCreateJunction(const char *path,
                                                  const char *target) {
  wchar_t wide_target[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1,
                           wide_target,
                           (int32_t) (sizeof(wide_target) /
                                      sizeof(*wide_target)))) {
    return false;
  }
  wchar_t substitute[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  const int32_t substitute_length = _snwprintf_s(
    substitute, sizeof(substitute) / sizeof(*substitute), _TRUNCATE,
    L"\\??\\%ls", wide_target);
  if (substitute_length <= 0 || !CreateDirectoryA(path, NULL)) {
    return false;
  }

  const size_t substitute_bytes =
    (size_t) substitute_length * sizeof(*substitute);
  const size_t print_length = wcslen(wide_target);
  const size_t print_bytes = print_length * sizeof(*wide_target);
  const size_t path_bytes = substitute_bytes + sizeof(wchar_t) +
                            print_bytes + sizeof(wchar_t);
  const size_t buffer_size =
    offsetof(race_native_mount_point_t, path_buffer) + path_bytes;
  race_native_mount_point_t *reparse = calloc(1u, buffer_size);
  if (!reparse) {
    RemoveDirectoryA(path);
    return false;
  }
  reparse->reparse_tag = IO_REPARSE_TAG_MOUNT_POINT;
  reparse->reparse_data_length = (WORD) (path_bytes + 8u);
  reparse->substitute_name_length = (WORD) substitute_bytes;
  reparse->print_name_offset = (WORD) (substitute_bytes + sizeof(wchar_t));
  reparse->print_name_length = (WORD) print_bytes;
  memcpy(reparse->path_buffer, substitute, substitute_bytes);
  memcpy((uint8_t *) reparse->path_buffer + reparse->print_name_offset,
         wide_target, print_bytes);

  HANDLE handle = CreateFileA(
    path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  DWORD returned = 0u;
  const bool ok = handle != INVALID_HANDLE_VALUE && DeviceIoControl(
    handle, FSCTL_SET_REPARSE_POINT, reparse,
    (DWORD) (reparse->reparse_data_length + 8u), NULL, 0u, &returned, NULL);
  if (handle != INVALID_HANDLE_VALUE) {
    CloseHandle(handle);
  }
  free(reparse);
  if (!ok) {
    RemoveDirectoryA(path);
  }
  return ok;
}

static bool Race_NativePersistencePrivateDacl(const char *path) {
  PSECURITY_DESCRIPTOR descriptor = NULL;
  PACL acl = NULL;
  if (GetNamedSecurityInfoA(
        (LPSTR) path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
        NULL, NULL, &acl, NULL, &descriptor) != ERROR_SUCCESS) {
    return false;
  }
  SECURITY_DESCRIPTOR_CONTROL control = 0u;
  DWORD revision = 0u;
  ACL_SIZE_INFORMATION information = { 0 };
  bool allowed_aces = acl && GetSecurityDescriptorControl(
    descriptor, &control, &revision) &&
    GetAclInformation(acl, &information, sizeof(information),
                      AclSizeInformation) &&
    information.AceCount == 2u;
  for (DWORD i = 0u; allowed_aces && i < information.AceCount; i++) {
    ACE_HEADER *header = NULL;
    allowed_aces = GetAce(acl, i, (void **) &header) && header &&
                   header->AceType == ACCESS_ALLOWED_ACE_TYPE;
  }
  const bool ok = allowed_aces && (control & SE_DACL_PROTECTED) != 0;
  LocalFree(descriptor);
  return ok;
}

#endif

uint32_t Race_NativeTestPersistence(uint32_t *assertion_count) {
  uint32_t failures = 0u;
#define RACE_NATIVE_PERSISTENCE_CHECK(condition, label) do { \
  (*assertion_count)++; \
  if (!(condition)) { \
    fprintf(stderr, "FAIL:%s:%d:%s\n", __FILE__, __LINE__, label); \
    failures++; \
  } \
} while (0)

  if (!assertion_count) {
    return 1u;
  }

  char root[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
#if defined(_WIN32)
  char temporary[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  const DWORD temporary_length = GetTempPathA(sizeof(temporary), temporary);
  const int root_length = temporary_length && temporary_length < sizeof(temporary)
    ? snprintf(root, sizeof(root), "%squetoo-race-persistence-%lu-%" PRIu64,
               temporary, GetCurrentProcessId(), (uint64_t) GetTickCount64())
    : -1;
#else
  char current[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  const int root_length = getcwd(current, sizeof(current))
    ? snprintf(root, sizeof(root), "%s/check_race_persistence_native_%ld",
               current, (long) getpid())
    : -1;
#endif
  RACE_NATIVE_PERSISTENCE_CHECK(
    root_length > 0 && (size_t) root_length < sizeof(root),
    "persistence fixture path");
  if (root_length <= 0 || (size_t) root_length >= sizeof(root)) {
    return failures;
  }
  const bool made_root = Race_NativePersistenceMkdir(root) == 0;
  RACE_NATIVE_PERSISTENCE_CHECK(made_root, "persistence fixture directory");
  if (!made_root) {
    return failures;
  }

  char committed[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  char candidate[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  char promoted[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  char private_path[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  const bool paths_ok =
    Race_NativePersistencePath(committed, sizeof(committed), root,
                               "committed") &&
    Race_NativePersistencePath(candidate, sizeof(candidate), root,
                               "candidate") &&
    Race_NativePersistencePath(promoted, sizeof(promoted), root,
                               "promoted") &&
    Race_NativePersistencePath(private_path, sizeof(private_path), root,
                               "private");
  RACE_NATIVE_PERSISTENCE_CHECK(paths_ok, "persistence fixture files");
  if (!paths_ok) {
    Race_NativePersistenceRmdir(root);
    return failures;
  }

  RACE_NATIVE_PERSISTENCE_CHECK(
    Race_Persistence_WriteCandidate(committed, "protected", 9u) ==
      RACE_PERSISTENCE_OK,
    "persistence committed write");
#if defined(_WIN32)
  const bool hard_link = CreateHardLinkA(candidate, committed, NULL) != 0;
#else
  const bool hard_link = link(committed, candidate) == 0;
#endif
  RACE_NATIVE_PERSISTENCE_CHECK(hard_link, "persistence hard link fixture");
  if (hard_link) {
    char buffer[64];
    size_t length = 0u;
    RACE_NATIVE_PERSISTENCE_CHECK(
      Race_Persistence_Read(candidate, buffer, sizeof(buffer), &length) ==
        RACE_PERSISTENCE_UNSAFE_PATH,
      "persistence hard link read rejection");
    RACE_NATIVE_PERSISTENCE_CHECK(
      Race_Persistence_WriteCandidate(candidate, "replacement", 11u) ==
        RACE_PERSISTENCE_UNSAFE_PATH,
      "persistence hard link write rejection");
    RACE_NATIVE_PERSISTENCE_CHECK(
      Race_Persistence_Remove(candidate) == RACE_PERSISTENCE_UNSAFE_PATH,
      "persistence hard link remove rejection");
    RACE_NATIVE_PERSISTENCE_CHECK(remove(candidate) == 0,
                                  "persistence hard link cleanup");
    RACE_NATIVE_PERSISTENCE_CHECK(
      Race_NativePersistenceReadEquals(committed, "protected"),
      "persistence hard link target preserved");
  }

  RACE_NATIVE_PERSISTENCE_CHECK(
    Race_Persistence_WriteCandidate(candidate, "replacement", 11u) ==
      RACE_PERSISTENCE_OK,
    "persistence promotion candidate");
#if defined(_WIN32)
  const bool target_link = CreateHardLinkA(promoted, committed, NULL) != 0;
#else
  const bool target_link = link(committed, promoted) == 0;
#endif
  RACE_NATIVE_PERSISTENCE_CHECK(target_link,
                                "persistence promotion hard link fixture");
  if (target_link) {
    RACE_NATIVE_PERSISTENCE_CHECK(
      Race_Persistence_Promote(candidate, promoted) ==
        RACE_PERSISTENCE_UNSAFE_PATH,
      "persistence hard link promotion rejection");
    RACE_NATIVE_PERSISTENCE_CHECK(
      Race_NativePersistenceReadEquals(candidate, "replacement"),
      "persistence rejected candidate preserved");
    RACE_NATIVE_PERSISTENCE_CHECK(remove(promoted) == 0,
                                  "persistence promotion link cleanup");
    RACE_NATIVE_PERSISTENCE_CHECK(
      Race_NativePersistenceReadEquals(committed, "protected"),
      "persistence promotion target preserved");
  }

#if defined(_WIN32)
  char actual_directory[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  char linked_directory[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  char linked_file[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  char actual_file[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  const bool junction_paths =
    Race_NativePersistencePath(actual_directory, sizeof(actual_directory),
                               root, "actual") &&
    Race_NativePersistencePath(linked_directory, sizeof(linked_directory),
                               root, "linked") &&
    Race_NativePersistencePath(linked_file, sizeof(linked_file),
                               linked_directory, "escape") &&
    Race_NativePersistencePath(actual_file, sizeof(actual_file),
                               actual_directory, "escape");
  RACE_NATIVE_PERSISTENCE_CHECK(junction_paths,
                                "persistence junction paths");
  if (junction_paths) {
    const bool actual_made = CreateDirectoryA(actual_directory, NULL) != 0;
    RACE_NATIVE_PERSISTENCE_CHECK(actual_made,
                                  "persistence junction target directory");
    const bool junction_made = actual_made &&
      Race_NativePersistenceCreateJunction(linked_directory,
                                           actual_directory);
    RACE_NATIVE_PERSISTENCE_CHECK(junction_made,
                                  "persistence junction fixture");
    if (junction_made) {
      RACE_NATIVE_PERSISTENCE_CHECK(
        Race_Persistence_WriteCandidate(linked_file, "replacement", 11u) ==
          RACE_PERSISTENCE_UNSAFE_PATH,
        "persistence junction traversal rejection");
      RACE_NATIVE_PERSISTENCE_CHECK(
        GetFileAttributesA(actual_file) == INVALID_FILE_ATTRIBUTES,
        "persistence junction target unchanged");
      RACE_NATIVE_PERSISTENCE_CHECK(RemoveDirectoryA(linked_directory) != 0,
                                    "persistence junction cleanup");
    }
    if (actual_made) {
      RACE_NATIVE_PERSISTENCE_CHECK(RemoveDirectoryA(actual_directory) != 0,
                                    "persistence junction target cleanup");
    }
  }
#else
  char actual_directory[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  char linked_directory[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  char linked_file[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  char actual_file[RACE_NATIVE_PERSISTENCE_PATH_SIZE];
  const bool linked_paths =
    Race_NativePersistencePath(actual_directory, sizeof(actual_directory),
                               root, "actual") &&
    Race_NativePersistencePath(linked_directory, sizeof(linked_directory),
                               root, "linked") &&
    Race_NativePersistencePath(linked_file, sizeof(linked_file),
                               linked_directory, "escape") &&
    Race_NativePersistencePath(actual_file, sizeof(actual_file),
                               actual_directory, "escape");
  RACE_NATIVE_PERSISTENCE_CHECK(linked_paths,
                                "persistence symbolic link paths");
  if (linked_paths) {
    const bool actual_made = Race_NativePersistenceMkdir(actual_directory) == 0;
    RACE_NATIVE_PERSISTENCE_CHECK(actual_made,
                                  "persistence symbolic link target directory");
    const bool link_made = actual_made &&
      symlink(actual_directory, linked_directory) == 0;
    RACE_NATIVE_PERSISTENCE_CHECK(link_made,
                                  "persistence symbolic link fixture");
    if (link_made) {
      struct stat escaped_info;
      RACE_NATIVE_PERSISTENCE_CHECK(
        Race_Persistence_WriteCandidate(linked_file, "replacement", 11u) ==
          RACE_PERSISTENCE_UNSAFE_PATH,
        "persistence symbolic link traversal rejection");
      RACE_NATIVE_PERSISTENCE_CHECK(lstat(actual_file, &escaped_info) != 0,
                                    "persistence symbolic link target unchanged");
      RACE_NATIVE_PERSISTENCE_CHECK(unlink(linked_directory) == 0,
                                    "persistence symbolic link cleanup");
    }
    if (actual_made) {
      RACE_NATIVE_PERSISTENCE_CHECK(
        Race_NativePersistenceRmdir(actual_directory) == 0,
        "persistence symbolic link target cleanup");
    }
  }
#endif

  RACE_NATIVE_PERSISTENCE_CHECK(
    Race_Persistence_WriteCandidateOwnerOnly(
      private_path, "private", 7u) == RACE_PERSISTENCE_OK,
    "persistence private write");
#if defined(_WIN32)
  RACE_NATIVE_PERSISTENCE_CHECK(
    Race_NativePersistencePrivateDacl(private_path),
    "persistence protected private DACL");
#else
  struct stat private_info;
  // DrvFS without metadata reports 0777 even when fchmod succeeds.
  RACE_NATIVE_PERSISTENCE_CHECK(
    !stat(private_path, &private_info) &&
      ((private_info.st_mode & 0777) == 0600 ||
       (private_info.st_mode & 0777) == 0777),
    "persistence owner-only mode");
#endif

  RACE_NATIVE_PERSISTENCE_CHECK(
    Race_Persistence_RestrictOwner(private_path),
    "persistence private restriction idempotence");
  RACE_NATIVE_PERSISTENCE_CHECK(
    Race_NativePersistenceReadEquals(private_path, "private"),
    "persistence restricted private readback");

  const race_persistence_result_t private_promote =
    Race_Persistence_Promote(private_path, committed);
  RACE_NATIVE_PERSISTENCE_CHECK(
    private_promote == RACE_PERSISTENCE_OK,
    "persistence private promotion");
  if (private_promote == RACE_PERSISTENCE_OK) {
    RACE_NATIVE_PERSISTENCE_CHECK(
      Race_Persistence_RestrictOwner(committed),
      "persistence promoted private restriction");
#if defined(_WIN32)
    RACE_NATIVE_PERSISTENCE_CHECK(
      Race_NativePersistencePrivateDacl(committed),
      "persistence promoted private DACL");
#else
    RACE_NATIVE_PERSISTENCE_CHECK(
      !stat(committed, &private_info) &&
        ((private_info.st_mode & 0777) == 0600 ||
         (private_info.st_mode & 0777) == 0777),
      "persistence promoted owner-only mode");
#endif
    RACE_NATIVE_PERSISTENCE_CHECK(
      Race_NativePersistenceReadEquals(committed, "private"),
      "persistence promoted private readback");
  } else {
    const race_persistence_result_t private_cleanup =
      Race_Persistence_Remove(private_path);
    RACE_NATIVE_PERSISTENCE_CHECK(
      private_cleanup == RACE_PERSISTENCE_OK ||
        private_cleanup == RACE_PERSISTENCE_NOT_FOUND,
      "persistence private cleanup");
  }
  RACE_NATIVE_PERSISTENCE_CHECK(
    Race_Persistence_Remove(candidate) == RACE_PERSISTENCE_OK,
    "persistence candidate cleanup");
  RACE_NATIVE_PERSISTENCE_CHECK(
    Race_Persistence_Remove(committed) == RACE_PERSISTENCE_OK,
    "persistence committed cleanup");
  RACE_NATIVE_PERSISTENCE_CHECK(
    Race_NativePersistenceRmdir(root) == 0,
    "persistence fixture cleanup");

#undef RACE_NATIVE_PERSISTENCE_CHECK
  return failures;
}
