/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_persistence.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <aclapi.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

static bool Race_Persistence_VirtualPathValid(const char *path) {
  if (!path || !*path || *path == '/' || *path == '\\') {
    return false;
  }
  const char *component = path;
  for (const char *cursor = path; ; cursor++) {
    if (*cursor == '\\' || *cursor == ':') {
      return false;
    }
    if (*cursor != '/' && *cursor) {
      continue;
    }
    const size_t length = (size_t) (cursor - component);
    if (!length || (length == 1u && component[0] == '.') ||
        (length == 2u && component[0] == '.' && component[1] == '.')) {
      return false;
    }
    if (!*cursor) {
      return true;
    }
    component = cursor + 1;
  }
}

bool Race_Persistence_CopyRealPath(const char *virtual_path,
                                   const char *real_path,
                                   char *output, const size_t capacity) {
  if (!Race_Persistence_VirtualPathValid(virtual_path) ||
      !real_path || !*real_path || !output || !capacity) {
    return false;
  }
  const size_t virtual_length = strlen(virtual_path);
  const size_t real_length = strlen(real_path);
  if (real_length >= capacity || real_length <= virtual_length) {
    return false;
  }
  const char *suffix = real_path + real_length - virtual_length;
  if ((suffix[-1] != '/' && suffix[-1] != '\\') ||
      memcmp(suffix, virtual_path, virtual_length)) {
    return false;
  }
  memcpy(output, real_path, real_length + 1u);
  return true;
}

#if defined(_WIN32)

typedef struct {
  TOKEN_USER *token_user;
  PACL acl;
  SECURITY_DESCRIPTOR descriptor;
  SECURITY_ATTRIBUTES attributes;
} race_persistence_private_security_t;

static race_persistence_result_t Race_Persistence_WindowsError(void) {
  const DWORD error = GetLastError();
  if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
    return RACE_PERSISTENCE_NOT_FOUND;
  }
  if (error == ERROR_CANT_ACCESS_FILE || error == ERROR_REPARSE_TAG_INVALID ||
      error == ERROR_REPARSE_TAG_MISMATCH) {
    return RACE_PERSISTENCE_UNSAFE_PATH;
  }
  return RACE_PERSISTENCE_IO_ERROR;
}

static wchar_t *Race_Persistence_WindowsFullPath(const char *path) {
  if (!path || !*path) {
    return NULL;
  }
  const int32_t wide_length = MultiByteToWideChar(
    CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
  if (wide_length <= 0) {
    return NULL;
  }
  wchar_t *wide = malloc((size_t) wide_length * sizeof(*wide));
  if (!wide || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                                    wide, wide_length) != wide_length) {
    free(wide);
    return NULL;
  }
  const DWORD full_length = GetFullPathNameW(wide, 0, NULL, NULL);
  if (!full_length) {
    free(wide);
    return NULL;
  }
  wchar_t *full = malloc((size_t) full_length * sizeof(*full));
  if (!full || GetFullPathNameW(wide, full_length, full, NULL) !=
                 full_length - 1u) {
    free(full);
    free(wide);
    return NULL;
  }
  free(wide);
  return full;
}

static const wchar_t *Race_Persistence_WindowsDisplayPath(
  const wchar_t *path) {
  if (!wcsncmp(path, L"\\\\?\\UNC\\", 8u)) {
    return path + 6u;
  }
  return !wcsncmp(path, L"\\\\?\\", 4u) ? path + 4u : path;
}

static bool Race_Persistence_WindowsHandleMatches(HANDLE handle,
                                                   const wchar_t *expected) {
  const DWORD length = GetFinalPathNameByHandleW(
    handle, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!length) {
    return false;
  }
  wchar_t *actual = malloc((size_t) length * sizeof(*actual));
  if (!actual || GetFinalPathNameByHandleW(
        handle, actual, length,
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS) != length - 1u) {
    free(actual);
    return false;
  }
  const bool matches = !_wcsicmp(
    Race_Persistence_WindowsDisplayPath(actual),
    Race_Persistence_WindowsDisplayPath(expected));
  free(actual);
  return matches;
}

static race_persistence_result_t Race_Persistence_WindowsValidateHandle(
  HANDLE handle, const wchar_t *expected, const bool directory) {
  FILE_ATTRIBUTE_TAG_INFO attributes;
  if (!GetFileInformationByHandleEx(
        handle, FileAttributeTagInfo, &attributes, sizeof(attributes))) {
    return RACE_PERSISTENCE_IO_ERROR;
  }
  if (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    return RACE_PERSISTENCE_UNSAFE_PATH;
  }
  const bool is_directory =
    (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  if (is_directory != directory) {
    return RACE_PERSISTENCE_NOT_REGULAR;
  }
  BY_HANDLE_FILE_INFORMATION information;
  if (!GetFileInformationByHandle(handle, &information)) {
    return RACE_PERSISTENCE_IO_ERROR;
  }
  if (!directory && information.nNumberOfLinks != 1u) {
    return RACE_PERSISTENCE_UNSAFE_PATH;
  }
  return Race_Persistence_WindowsHandleMatches(handle, expected)
    ? RACE_PERSISTENCE_OK
    : RACE_PERSISTENCE_UNSAFE_PATH;
}

static race_persistence_result_t Race_Persistence_WindowsOpenDirectory(
  const wchar_t *path, HANDLE *handle) {
  *handle = CreateFileW(
    path, FILE_READ_ATTRIBUTES,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    NULL, OPEN_EXISTING,
    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  if (*handle == INVALID_HANDLE_VALUE) {
    return Race_Persistence_WindowsError();
  }
  const race_persistence_result_t result =
    Race_Persistence_WindowsValidateHandle(*handle, path, true);
  if (result != RACE_PERSISTENCE_OK) {
    CloseHandle(*handle);
    *handle = INVALID_HANDLE_VALUE;
  }
  return result;
}

static race_persistence_result_t Race_Persistence_WindowsOpenParent(
  const wchar_t *path, HANDLE *handle) {
  *handle = INVALID_HANDLE_VALUE;
  wchar_t *parent = _wcsdup(path);
  if (!parent) {
    return RACE_PERSISTENCE_IO_ERROR;
  }
  wchar_t *separator = wcsrchr(parent, L'\\');
  if (!separator) {
    separator = wcsrchr(parent, L'/');
  }
  if (!separator || separator == parent ||
      (separator == parent + 2 && parent[1] == L':')) {
    free(parent);
    return RACE_PERSISTENCE_UNSAFE_PATH;
  }
  *separator = L'\0';
  const race_persistence_result_t result =
    Race_Persistence_WindowsOpenDirectory(parent, handle);
  free(parent);
  return result;
}

static bool Race_Persistence_WindowsPrivateSecurity(
  race_persistence_private_security_t *security) {
  memset(security, 0, sizeof(*security));
  HANDLE token;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return false;
  }
  DWORD token_length = 0;
  GetTokenInformation(token, TokenUser, NULL, 0, &token_length);
  security->token_user = malloc(token_length);
  if (!security->token_user || !GetTokenInformation(
        token, TokenUser, security->token_user, token_length,
        &token_length)) {
    CloseHandle(token);
    free(security->token_user);
    security->token_user = NULL;
    return false;
  }
  CloseHandle(token);

  uint8_t system_sid[SECURITY_MAX_SID_SIZE];
  DWORD system_sid_length = sizeof(system_sid);
  if (!CreateWellKnownSid(WinLocalSystemSid, NULL, system_sid,
                          &system_sid_length)) {
    free(security->token_user);
    security->token_user = NULL;
    return false;
  }

  EXPLICIT_ACCESSW entries[2] = { 0 };
  entries[0].grfAccessPermissions = GENERIC_ALL;
  entries[0].grfAccessMode = SET_ACCESS;
  entries[0].grfInheritance = NO_INHERITANCE;
  entries[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  entries[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
  entries[0].Trustee.ptstrName =
    (LPWSTR) security->token_user->User.Sid;
  entries[1].grfAccessPermissions = GENERIC_ALL;
  entries[1].grfAccessMode = SET_ACCESS;
  entries[1].grfInheritance = NO_INHERITANCE;
  entries[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  entries[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  entries[1].Trustee.ptstrName = (LPWSTR) system_sid;
  if (SetEntriesInAclW(2, entries, NULL, &security->acl) != ERROR_SUCCESS ||
      !InitializeSecurityDescriptor(
        &security->descriptor, SECURITY_DESCRIPTOR_REVISION) ||
      !SetSecurityDescriptorDacl(
        &security->descriptor, true, security->acl, false)) {
    if (security->acl) {
      LocalFree(security->acl);
    }
    free(security->token_user);
    memset(security, 0, sizeof(*security));
    return false;
  }
  security->attributes.nLength = sizeof(security->attributes);
  security->attributes.lpSecurityDescriptor = &security->descriptor;
  return true;
}

static bool Race_Persistence_WindowsApplyPrivateSecurity(
  HANDLE handle, const race_persistence_private_security_t *security) {
  return SetSecurityInfo(
           handle, SE_FILE_OBJECT,
           DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
           NULL, NULL, security->acl, NULL) == ERROR_SUCCESS;
}

static void Race_Persistence_WindowsFreePrivateSecurity(
  race_persistence_private_security_t *security) {
  if (security->acl) {
    LocalFree(security->acl);
  }
  free(security->token_user);
  memset(security, 0, sizeof(*security));
}

static void Race_Persistence_WindowsDiscardCreated(HANDLE handle,
                                                   const bool discard) {
  if (discard) {
    FILE_DISPOSITION_INFO disposition = { .DeleteFile = true };
    SetFileInformationByHandle(
      handle, FileDispositionInfo, &disposition, sizeof(disposition));
  }
}

static race_persistence_result_t Race_Persistence_WindowsOpenFile(
  const char *path, const DWORD access, const DWORD sharing,
  const DWORD disposition, const bool owner_only,
  HANDLE *handle, wchar_t **full_path) {
  *handle = INVALID_HANDLE_VALUE;
  *full_path = Race_Persistence_WindowsFullPath(path);
  if (!*full_path) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }
  HANDLE parent;
  const race_persistence_result_t parent_result =
    Race_Persistence_WindowsOpenParent(*full_path, &parent);
  if (parent_result != RACE_PERSISTENCE_OK) {
    free(*full_path);
    *full_path = NULL;
    return parent_result;
  }

  race_persistence_private_security_t security = { 0 };
  if (owner_only && !Race_Persistence_WindowsPrivateSecurity(&security)) {
    CloseHandle(parent);
    free(*full_path);
    *full_path = NULL;
    return RACE_PERSISTENCE_IO_ERROR;
  }

  bool created = false;
  DWORD effective_disposition = disposition;
  if (disposition == CREATE_ALWAYS) {
    effective_disposition = OPEN_EXISTING;
  }
  *handle = CreateFileW(
    *full_path, access, sharing,
    owner_only ? &security.attributes : NULL, effective_disposition,
    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  if (*handle == INVALID_HANDLE_VALUE && disposition == CREATE_ALWAYS &&
      (GetLastError() == ERROR_FILE_NOT_FOUND ||
       GetLastError() == ERROR_PATH_NOT_FOUND)) {
    *handle = CreateFileW(
      *full_path, access, sharing,
      owner_only ? &security.attributes : NULL, CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    created = *handle != INVALID_HANDLE_VALUE;
  }
  CloseHandle(parent);
  if (*handle == INVALID_HANDLE_VALUE) {
    const race_persistence_result_t result = Race_Persistence_WindowsError();
    if (owner_only) {
      Race_Persistence_WindowsFreePrivateSecurity(&security);
    }
    free(*full_path);
    *full_path = NULL;
    return result;
  }

  race_persistence_result_t result =
    Race_Persistence_WindowsValidateHandle(*handle, *full_path, false);
  if (result == RACE_PERSISTENCE_OK && owner_only &&
      !Race_Persistence_WindowsApplyPrivateSecurity(*handle, &security)) {
    result = RACE_PERSISTENCE_IO_ERROR;
  }
  if (result == RACE_PERSISTENCE_OK && disposition == CREATE_ALWAYS) {
    const LARGE_INTEGER zero = { 0 };
    if (!SetFilePointerEx(*handle, zero, NULL, FILE_BEGIN) ||
        !SetEndOfFile(*handle)) {
      result = RACE_PERSISTENCE_IO_ERROR;
    }
  }
  if (owner_only) {
    Race_Persistence_WindowsFreePrivateSecurity(&security);
  }
  if (result != RACE_PERSISTENCE_OK) {
    Race_Persistence_WindowsDiscardCreated(*handle, created);
    CloseHandle(*handle);
    *handle = INVALID_HANDLE_VALUE;
    free(*full_path);
    *full_path = NULL;
  }
  return result;
}

bool Race_Persistence_RestrictOwner(const char *path) {
  if (!path || !*path) {
    return false;
  }
  HANDLE handle;
  wchar_t *full_path;
  if (Race_Persistence_WindowsOpenFile(
        path, READ_CONTROL | FILE_READ_ATTRIBUTES | WRITE_DAC,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        OPEN_EXISTING, false, &handle, &full_path) !=
      RACE_PERSISTENCE_OK) {
    return false;
  }
  race_persistence_private_security_t security = { 0 };
  const bool ok = Race_Persistence_WindowsPrivateSecurity(&security) &&
    Race_Persistence_WindowsApplyPrivateSecurity(handle, &security);
  Race_Persistence_WindowsFreePrivateSecurity(&security);
  const bool closed = CloseHandle(handle) != 0;
  free(full_path);
  return ok && closed;
}

race_persistence_result_t Race_Persistence_Read(const char *path,
                                                void *buffer, size_t capacity,
                                                size_t *length) {
  if (!path || !*path || !buffer || !capacity || !length) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }
  *length = 0u;
  HANDLE handle;
  wchar_t *full_path;
  race_persistence_result_t result = Race_Persistence_WindowsOpenFile(
    path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, false,
    &handle, &full_path);
  if (result != RACE_PERSISTENCE_OK) {
    return result;
  }

  LARGE_INTEGER size = { 0 };
  if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0) {
    result = RACE_PERSISTENCE_IO_ERROR;
  } else if ((uint64_t) size.QuadPart > capacity) {
    result = RACE_PERSISTENCE_TOO_LARGE;
  } else {
    size_t offset = 0u;
    while (result == RACE_PERSISTENCE_OK && offset < (size_t) size.QuadPart) {
      const size_t remaining = (size_t) size.QuadPart - offset;
      const DWORD request = (DWORD) (remaining < (size_t) UINT32_MAX ?
                                      remaining : (size_t) UINT32_MAX);
      DWORD count = 0;
      if (!ReadFile(handle, (uint8_t *) buffer + offset,
                    request, &count, NULL) || count != request) {
        result = RACE_PERSISTENCE_IO_ERROR;
      }
      offset += count;
    }
    if (result == RACE_PERSISTENCE_OK) {
      uint8_t extra;
      DWORD count = 0;
      if (!ReadFile(handle, &extra, 1u, &count, NULL) || count) {
        result = RACE_PERSISTENCE_IO_ERROR;
      } else {
        *length = (size_t) size.QuadPart;
      }
    }
  }
  if (!CloseHandle(handle) && result == RACE_PERSISTENCE_OK) {
    result = RACE_PERSISTENCE_IO_ERROR;
    *length = 0u;
  }
  free(full_path);
  return result;
}

static race_persistence_result_t Race_Persistence_WriteCandidateInternal(
  const char *path, const void *data, const size_t length,
  const bool owner_only) {
  if (!path || !*path || (!data && length)) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }
  HANDLE handle;
  wchar_t *full_path;
  race_persistence_result_t result = Race_Persistence_WindowsOpenFile(
    path, GENERIC_WRITE | FILE_READ_ATTRIBUTES | WRITE_DAC, 0,
    CREATE_ALWAYS, owner_only, &handle, &full_path);
  if (result != RACE_PERSISTENCE_OK) {
    return result;
  }
  size_t offset = 0u;
  while (offset < length) {
    const size_t remaining = length - offset;
    const DWORD request = (DWORD) (remaining < (size_t) UINT32_MAX ?
                                    remaining : (size_t) UINT32_MAX);
    DWORD count = 0;
    if (!WriteFile(handle, (const uint8_t *) data + offset,
                   request, &count, NULL) || count != request) {
      result = RACE_PERSISTENCE_IO_ERROR;
      break;
    }
    offset += count;
  }
  if (result == RACE_PERSISTENCE_OK && !FlushFileBuffers(handle)) {
    result = RACE_PERSISTENCE_IO_ERROR;
  }
  if (result != RACE_PERSISTENCE_OK) {
    Race_Persistence_WindowsDiscardCreated(handle, true);
  }
  if (!CloseHandle(handle) && result == RACE_PERSISTENCE_OK) {
    result = RACE_PERSISTENCE_IO_ERROR;
  }
  free(full_path);
  return result;
}

race_persistence_result_t Race_Persistence_Promote(const char *candidate,
                                                   const char *committed) {
  if (!candidate || !*candidate || !committed || !*committed ||
      !strcmp(candidate, committed)) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }
  HANDLE candidate_handle;
  wchar_t *candidate_path;
  race_persistence_result_t result = Race_Persistence_WindowsOpenFile(
    candidate, GENERIC_READ | GENERIC_WRITE | DELETE,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    OPEN_EXISTING, false, &candidate_handle, &candidate_path);
  if (result != RACE_PERSISTENCE_OK) {
    return result;
  }

  wchar_t *committed_path = Race_Persistence_WindowsFullPath(committed);
  HANDLE parent = INVALID_HANDLE_VALUE;
  if (!committed_path) {
    result = RACE_PERSISTENCE_INVALID_ARGUMENT;
  } else {
    result = Race_Persistence_WindowsOpenParent(committed_path, &parent);
  }
  if (result == RACE_PERSISTENCE_OK) {
    HANDLE existing;
    wchar_t *existing_path;
    const race_persistence_result_t existing_result =
      Race_Persistence_WindowsOpenFile(
        committed, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        OPEN_EXISTING, false, &existing, &existing_path);
    if (existing_result == RACE_PERSISTENCE_OK) {
      CloseHandle(existing);
      free(existing_path);
    } else if (existing_result != RACE_PERSISTENCE_NOT_FOUND) {
      result = existing_result;
    }
  }
  if (result == RACE_PERSISTENCE_OK) {
    const size_t name_bytes = wcslen(committed_path) * sizeof(wchar_t);
    FILE_RENAME_INFO *rename_info = calloc(
      1u, sizeof(*rename_info) + name_bytes);
    if (!rename_info) {
      result = RACE_PERSISTENCE_IO_ERROR;
    } else {
      rename_info->ReplaceIfExists = true;
      rename_info->FileNameLength = (DWORD) name_bytes;
      memcpy(rename_info->FileName, committed_path, name_bytes);
      if (!SetFileInformationByHandle(
            candidate_handle, FileRenameInfo, rename_info,
            (DWORD) (sizeof(*rename_info) + name_bytes)) ||
          !FlushFileBuffers(candidate_handle)) {
        result = RACE_PERSISTENCE_IO_ERROR;
      }
      free(rename_info);
    }
  }
  if (parent != INVALID_HANDLE_VALUE) {
    CloseHandle(parent);
  }
  if (!CloseHandle(candidate_handle) && result == RACE_PERSISTENCE_OK) {
    result = RACE_PERSISTENCE_IO_ERROR;
  }
  free(candidate_path);
  free(committed_path);
  return result;
}

race_persistence_result_t Race_Persistence_Remove(const char *path) {
  if (!path || !*path) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }
  HANDLE handle;
  wchar_t *full_path;
  race_persistence_result_t result = Race_Persistence_WindowsOpenFile(
    path, DELETE | FILE_READ_ATTRIBUTES,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    OPEN_EXISTING, false, &handle, &full_path);
  if (result != RACE_PERSISTENCE_OK) {
    return result;
  }
  FILE_DISPOSITION_INFO disposition = { .DeleteFile = true };
  if (!SetFileInformationByHandle(
        handle, FileDispositionInfo, &disposition, sizeof(disposition))) {
    result = RACE_PERSISTENCE_IO_ERROR;
  }
  if (!CloseHandle(handle) && result == RACE_PERSISTENCE_OK) {
    result = RACE_PERSISTENCE_IO_ERROR;
  }
  free(full_path);
  return result;
}

#else

typedef struct {
  int descriptor;
  char *leaf;
} race_persistence_parent_t;

static race_persistence_result_t Race_Persistence_PosixError(void) {
  if (errno == ENOENT) {
    return RACE_PERSISTENCE_NOT_FOUND;
  }
  if (errno == ELOOP || errno == ENOTDIR) {
    return RACE_PERSISTENCE_UNSAFE_PATH;
  }
  return RACE_PERSISTENCE_IO_ERROR;
}

static bool Race_Persistence_CloseParent(race_persistence_parent_t *parent) {
  bool ok = true;
  if (parent->descriptor >= 0) {
    ok = close(parent->descriptor) == 0;
  }
  free(parent->leaf);
  parent->descriptor = -1;
  parent->leaf = NULL;
  return ok;
}

static bool Race_Persistence_PosixComponentValid(const char *component,
                                                 const size_t length) {
  return length && !(length == 1u && component[0] == '.') &&
         !(length == 2u && component[0] == '.' && component[1] == '.');
}

static race_persistence_result_t Race_Persistence_OpenParent(
  const char *path, race_persistence_parent_t *parent) {
  parent->descriptor = -1;
  parent->leaf = NULL;
  if (!path || !*path) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }
  const char *last_separator = strrchr(path, '/');
  const char *leaf = last_separator ? last_separator + 1 : path;
  const size_t leaf_length = strlen(leaf);
  if (!Race_Persistence_PosixComponentValid(leaf, leaf_length)) {
    return RACE_PERSISTENCE_UNSAFE_PATH;
  }
  parent->leaf = malloc(leaf_length + 1u);
  if (!parent->leaf) {
    return RACE_PERSISTENCE_IO_ERROR;
  }
  memcpy(parent->leaf, leaf, leaf_length + 1u);

  int descriptor = open(path[0] == '/' ? "/" : ".",
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (descriptor < 0) {
    const race_persistence_result_t result = Race_Persistence_PosixError();
    Race_Persistence_CloseParent(parent);
    return result;
  }
  const char *cursor = path + (path[0] == '/');
  const char *parent_end = last_separator ? last_separator : path;
  while (cursor < parent_end) {
    const char *separator = memchr(cursor, '/', (size_t) (parent_end - cursor));
    const char *component_end = separator ? separator : parent_end;
    const size_t component_length = (size_t) (component_end - cursor);
    if (!Race_Persistence_PosixComponentValid(cursor, component_length)) {
      close(descriptor);
      Race_Persistence_CloseParent(parent);
      return RACE_PERSISTENCE_UNSAFE_PATH;
    }
    char *component = malloc(component_length + 1u);
    if (!component) {
      close(descriptor);
      Race_Persistence_CloseParent(parent);
      return RACE_PERSISTENCE_IO_ERROR;
    }
    memcpy(component, cursor, component_length);
    component[component_length] = '\0';
    int next;
    do {
      next = openat(descriptor, component,
                    O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    } while (next < 0 && errno == EINTR);
    free(component);
    if (next < 0) {
      const race_persistence_result_t result = Race_Persistence_PosixError();
      close(descriptor);
      Race_Persistence_CloseParent(parent);
      return result;
    }
    if (close(descriptor)) {
      close(next);
      Race_Persistence_CloseParent(parent);
      return RACE_PERSISTENCE_IO_ERROR;
    }
    descriptor = next;
    cursor = component_end + (separator != NULL);
  }
  parent->descriptor = descriptor;
  return RACE_PERSISTENCE_OK;
}

static race_persistence_result_t Race_Persistence_RegularDescriptor(
  const int descriptor, size_t *size) {
  struct stat info;
  if (fstat(descriptor, &info)) {
    return RACE_PERSISTENCE_IO_ERROR;
  }
  if (!S_ISREG(info.st_mode)) {
    return RACE_PERSISTENCE_NOT_REGULAR;
  }
  if (info.st_nlink != 1) {
    return RACE_PERSISTENCE_UNSAFE_PATH;
  }
  if (info.st_size < 0 || (uint64_t) info.st_size > SIZE_MAX) {
    return RACE_PERSISTENCE_TOO_LARGE;
  }
  if (size) {
    *size = (size_t) info.st_size;
  }
  return RACE_PERSISTENCE_OK;
}

static bool Race_Persistence_SyncFile(FILE *file) {
  const int descriptor = fileno(file);
  if (descriptor < 0) {
    return false;
  }
  int result;
  do {
#if defined(__APPLE__)
    result = fcntl(descriptor, F_FULLFSYNC);
#else
    result = fsync(descriptor);
#endif
  } while (result && errno == EINTR);
  return result == 0;
}

static bool Race_Persistence_SyncDirectory(const int descriptor) {
  int result;
  do {
    result = fsync(descriptor);
  } while (result && errno == EINTR);
  return result == 0;
}

bool Race_Persistence_RestrictOwner(const char *path) {
  race_persistence_parent_t parent;
  if (Race_Persistence_OpenParent(path, &parent) != RACE_PERSISTENCE_OK) {
    return false;
  }
  int descriptor;
  do {
    descriptor = openat(parent.descriptor, parent.leaf,
                        O_WRONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  } while (descriptor < 0 && errno == EINTR);
  bool ok = descriptor >= 0 &&
    Race_Persistence_RegularDescriptor(descriptor, NULL) ==
      RACE_PERSISTENCE_OK;
  if (ok) {
    int result;
    do {
      result = fchmod(descriptor, S_IRUSR | S_IWUSR);
    } while (result && errno == EINTR);
    ok = !result;
  }
  if (descriptor >= 0 && close(descriptor)) {
    ok = false;
  }
  return Race_Persistence_CloseParent(&parent) && ok;
}

race_persistence_result_t Race_Persistence_Read(const char *path,
                                                void *buffer, size_t capacity,
                                                size_t *length) {
  if (!path || !*path || !buffer || !capacity || !length) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }
  *length = 0u;
  race_persistence_parent_t parent;
  race_persistence_result_t result = Race_Persistence_OpenParent(path, &parent);
  if (result != RACE_PERSISTENCE_OK) {
    return result;
  }
  int descriptor;
  do {
    descriptor = openat(parent.descriptor, parent.leaf,
                        O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  } while (descriptor < 0 && errno == EINTR);
  if (descriptor < 0) {
    result = Race_Persistence_PosixError();
    Race_Persistence_CloseParent(&parent);
    return result;
  }

  size_t size = 0u;
  result = Race_Persistence_RegularDescriptor(descriptor, &size);
  if (result == RACE_PERSISTENCE_OK && size > capacity) {
    result = RACE_PERSISTENCE_TOO_LARGE;
  }
  FILE *file = NULL;
  if (result == RACE_PERSISTENCE_OK) {
    file = fdopen(descriptor, "rb");
    if (!file) {
      result = RACE_PERSISTENCE_IO_ERROR;
    }
  }
  if (file) {
    bool ok = (!size || fread(buffer, 1u, size, file) == size) &&
              fgetc(file) == EOF && !ferror(file);
    if (fclose(file)) {
      ok = false;
    }
    descriptor = -1;
    if (ok) {
      *length = size;
    } else {
      result = RACE_PERSISTENCE_IO_ERROR;
    }
  }
  if (descriptor >= 0 && close(descriptor) &&
      result == RACE_PERSISTENCE_OK) {
    result = RACE_PERSISTENCE_IO_ERROR;
  }
  if (!Race_Persistence_CloseParent(&parent) &&
      result == RACE_PERSISTENCE_OK) {
    result = RACE_PERSISTENCE_IO_ERROR;
    *length = 0u;
  }
  return result;
}

static race_persistence_result_t Race_Persistence_WriteCandidateInternal(
  const char *path, const void *data, const size_t length,
  const bool owner_only) {
  if (!path || !*path || (!data && length)) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }
  race_persistence_parent_t parent;
  race_persistence_result_t result = Race_Persistence_OpenParent(path, &parent);
  if (result != RACE_PERSISTENCE_OK) {
    return result;
  }
  int descriptor = -1;
  bool created = false;
  for (size_t attempt = 0u; attempt < 2u && descriptor < 0; attempt++) {
    do {
      descriptor = openat(
        parent.descriptor, parent.leaf,
        O_WRONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor >= 0 || errno != ENOENT) {
      break;
    }
    do {
      descriptor = openat(
        parent.descriptor, parent.leaf,
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC,
        owner_only ? S_IRUSR | S_IWUSR :
          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor >= 0) {
      created = true;
    } else if (errno != EEXIST) {
      break;
    }
  }
  if (descriptor < 0) {
    result = Race_Persistence_PosixError();
    Race_Persistence_CloseParent(&parent);
    return result;
  }
  result = Race_Persistence_RegularDescriptor(descriptor, NULL);
  if (result == RACE_PERSISTENCE_OK && owner_only) {
    int chmod_result;
    do {
      chmod_result = fchmod(descriptor, S_IRUSR | S_IWUSR);
    } while (chmod_result && errno == EINTR);
    if (chmod_result) {
      result = RACE_PERSISTENCE_IO_ERROR;
    }
  }
  if (result == RACE_PERSISTENCE_OK) {
    int truncate_result;
    do {
      truncate_result = ftruncate(descriptor, 0);
    } while (truncate_result && errno == EINTR);
    if (truncate_result) {
      result = RACE_PERSISTENCE_IO_ERROR;
    }
  }
  FILE *file = NULL;
  if (result == RACE_PERSISTENCE_OK) {
    file = fdopen(descriptor, "wb");
    if (!file) {
      result = RACE_PERSISTENCE_IO_ERROR;
    }
  }
  if (file) {
    bool ok = (!length || fwrite(data, 1u, length, file) == length) &&
              !fflush(file) && Race_Persistence_SyncFile(file);
    if (fclose(file)) {
      ok = false;
    }
    descriptor = -1;
    if (!ok) {
      result = RACE_PERSISTENCE_IO_ERROR;
    }
  }
  if (descriptor >= 0 && close(descriptor) &&
      result == RACE_PERSISTENCE_OK) {
    result = RACE_PERSISTENCE_IO_ERROR;
  }
  if (created && result != RACE_PERSISTENCE_OK) {
    unlinkat(parent.descriptor, parent.leaf, 0);
  }
  if (!Race_Persistence_CloseParent(&parent) &&
      result == RACE_PERSISTENCE_OK) {
    result = RACE_PERSISTENCE_IO_ERROR;
  }
  return result;
}

race_persistence_result_t Race_Persistence_Promote(const char *candidate,
                                                   const char *committed) {
  if (!candidate || !*candidate || !committed || !*committed ||
      !strcmp(candidate, committed)) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }
  race_persistence_parent_t source, target;
  race_persistence_result_t result = Race_Persistence_OpenParent(
    candidate, &source);
  if (result != RACE_PERSISTENCE_OK) {
    return result;
  }
  result = Race_Persistence_OpenParent(committed, &target);
  if (result != RACE_PERSISTENCE_OK) {
    Race_Persistence_CloseParent(&source);
    return result;
  }

  struct stat source_info;
  if (fstatat(source.descriptor, source.leaf, &source_info,
              AT_SYMLINK_NOFOLLOW)) {
    result = Race_Persistence_PosixError();
  } else if (!S_ISREG(source_info.st_mode)) {
    result = S_ISLNK(source_info.st_mode)
      ? RACE_PERSISTENCE_UNSAFE_PATH
      : RACE_PERSISTENCE_NOT_REGULAR;
  } else if (source_info.st_nlink != 1) {
    result = RACE_PERSISTENCE_UNSAFE_PATH;
  } else {
    struct stat existing_info;
    if (!fstatat(target.descriptor, target.leaf, &existing_info,
                 AT_SYMLINK_NOFOLLOW)) {
      if (!S_ISREG(existing_info.st_mode)) {
        result = S_ISLNK(existing_info.st_mode)
          ? RACE_PERSISTENCE_UNSAFE_PATH
          : RACE_PERSISTENCE_NOT_REGULAR;
      } else if (existing_info.st_nlink != 1) {
        result = RACE_PERSISTENCE_UNSAFE_PATH;
      }
    } else if (errno != ENOENT) {
      result = RACE_PERSISTENCE_IO_ERROR;
    }
    if (result == RACE_PERSISTENCE_OK) {
      if (renameat(source.descriptor, source.leaf,
                   target.descriptor, target.leaf)) {
        result = RACE_PERSISTENCE_IO_ERROR;
      } else {
        struct stat target_info;
        if (fstatat(target.descriptor, target.leaf, &target_info,
                    AT_SYMLINK_NOFOLLOW) || !S_ISREG(target_info.st_mode) ||
            target_info.st_nlink != 1 ||
            source_info.st_dev != target_info.st_dev ||
            source_info.st_ino != target_info.st_ino) {
          result = RACE_PERSISTENCE_UNSAFE_PATH;
        } else if (!Race_Persistence_SyncDirectory(target.descriptor) ||
                   (source.descriptor != target.descriptor &&
                    !Race_Persistence_SyncDirectory(source.descriptor))) {
          result = RACE_PERSISTENCE_IO_ERROR;
        }
      }
    }
  }
  const bool target_closed = Race_Persistence_CloseParent(&target);
  const bool source_closed = Race_Persistence_CloseParent(&source);
  if ((!target_closed || !source_closed) &&
      result == RACE_PERSISTENCE_OK) {
    result = RACE_PERSISTENCE_IO_ERROR;
  }
  return result;
}

race_persistence_result_t Race_Persistence_Remove(const char *path) {
  if (!path || !*path) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }
  race_persistence_parent_t parent;
  race_persistence_result_t result = Race_Persistence_OpenParent(path, &parent);
  if (result != RACE_PERSISTENCE_OK) {
    return result;
  }
  struct stat info;
  if (fstatat(parent.descriptor, parent.leaf, &info, AT_SYMLINK_NOFOLLOW)) {
    result = Race_Persistence_PosixError();
  } else if (!S_ISREG(info.st_mode)) {
    result = S_ISLNK(info.st_mode)
      ? RACE_PERSISTENCE_UNSAFE_PATH
      : RACE_PERSISTENCE_NOT_REGULAR;
  } else if (info.st_nlink != 1) {
    result = RACE_PERSISTENCE_UNSAFE_PATH;
  } else if (unlinkat(parent.descriptor, parent.leaf, 0)) {
    result = RACE_PERSISTENCE_IO_ERROR;
  } else if (!Race_Persistence_SyncDirectory(parent.descriptor)) {
    result = RACE_PERSISTENCE_IO_ERROR;
  }
  if (!Race_Persistence_CloseParent(&parent) &&
      result == RACE_PERSISTENCE_OK) {
    result = RACE_PERSISTENCE_IO_ERROR;
  }
  return result;
}

#endif

race_persistence_result_t Race_Persistence_WriteCandidate(const char *path,
                                                          const void *data,
                                                          const size_t length) {
  return Race_Persistence_WriteCandidateInternal(path, data, length, false);
}

race_persistence_result_t Race_Persistence_WriteCandidateOwnerOnly(
  const char *path, const void *data, const size_t length) {
  return Race_Persistence_WriteCandidateInternal(path, data, length, true);
}

const char *Race_Persistence_ResultName(const race_persistence_result_t result) {
  switch (result) {
    case RACE_PERSISTENCE_OK:
      return "ok";
    case RACE_PERSISTENCE_NOT_FOUND:
      return "not found";
    case RACE_PERSISTENCE_NOT_REGULAR:
      return "not a regular file";
    case RACE_PERSISTENCE_TOO_LARGE:
      return "too large";
    case RACE_PERSISTENCE_IO_ERROR:
      return "I/O error";
    case RACE_PERSISTENCE_INVALID_ARGUMENT:
      return "invalid argument";
    case RACE_PERSISTENCE_UNSAFE_PATH:
      return "unsafe path";
  }
  return "unknown";
}
