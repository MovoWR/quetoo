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
#include <SDL3/SDL.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

bool Race_Persistence_CopyRealPath(const char *virtual_path,
                                   const char *real_path,
                                   char *output, size_t capacity) {
  if (!virtual_path || !*virtual_path || !real_path || !*real_path ||
      !output || !capacity) {
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

  memcpy(output, real_path, real_length + 1);
  return true;
}

#if !defined(_WIN32)
static race_persistence_result_t Race_Persistence_PathInfo(const char *path,
                                                          size_t *size) {
  struct stat info;
  if (stat(path, &info)) {
    return errno == ENOENT
      ? RACE_PERSISTENCE_NOT_FOUND
      : RACE_PERSISTENCE_IO_ERROR;
  }

  if (!S_ISREG(info.st_mode)) {
    return RACE_PERSISTENCE_NOT_REGULAR;
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

static void Race_Persistence_SyncParent(const char *path) {
  const size_t length = strlen(path);
  if (!length || length == SIZE_MAX) {
    return;
  }

  char *directory = malloc(length + 1);
  if (!directory) {
    return;
  }
  memcpy(directory, path, length + 1);
  char *separator = strrchr(directory, '/');
  if (separator) {
    *separator = '\0';
  } else {
    strcpy(directory, ".");
  }

  const int descriptor = open(*directory ? directory : "/", O_RDONLY);
  if (descriptor >= 0) {
    int result;
    do {
      result = fsync(descriptor);
    } while (result && errno == EINTR);
    close(descriptor);
  }
  free(directory);
}
#endif

race_persistence_result_t Race_Persistence_Read(const char *path,
                                                void *buffer, size_t capacity,
                                                size_t *length) {
  if (!path || !*path || !buffer || !capacity || !length) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }

  *length = 0;

#if defined(_WIN32)
  SDL_PathInfo info = { 0 };
  if (!SDL_GetPathInfo(path, &info)) {
    return RACE_PERSISTENCE_NOT_FOUND;
  }

  if (info.type != SDL_PATHTYPE_FILE) {
    return RACE_PERSISTENCE_NOT_REGULAR;
  }

  if (info.size > capacity) {
    return RACE_PERSISTENCE_TOO_LARGE;
  }

  SDL_IOStream *file = SDL_IOFromFile(path, "rb");
  if (!file) {
    return RACE_PERSISTENCE_IO_ERROR;
  }

  const size_t size = (size_t) info.size;
  bool ok = !size || SDL_ReadIO(file, buffer, size) == size;

  uint8_t extra;
  if (ok && SDL_ReadIO(file, &extra, 1)) {
    ok = false;
  }

  if (!SDL_CloseIO(file)) {
    ok = false;
  }

  if (!ok) {
    return RACE_PERSISTENCE_IO_ERROR;
  }

  *length = size;
  return RACE_PERSISTENCE_OK;
#else
  size_t size;
  const race_persistence_result_t path_result = Race_Persistence_PathInfo(path, &size);
  if (path_result != RACE_PERSISTENCE_OK) {
    return path_result;
  }

  if (size > capacity) {
    return RACE_PERSISTENCE_TOO_LARGE;
  }

  FILE *file = fopen(path, "rb");
  if (!file) {
    return RACE_PERSISTENCE_IO_ERROR;
  }

  bool ok = !size || fread(buffer, 1, size, file) == size;
  if (ok && fgetc(file) != EOF) {
    ok = false;
  }

  if (fclose(file)) {
    ok = false;
  }

  if (!ok) {
    return RACE_PERSISTENCE_IO_ERROR;
  }

  *length = size;
  return RACE_PERSISTENCE_OK;
#endif
}

race_persistence_result_t Race_Persistence_WriteCandidate(const char *path,
                                                          const void *data,
                                                          size_t length) {
  if (!path || !*path || (!data && length)) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }

#if defined(_WIN32)
  SDL_IOStream *file = SDL_IOFromFile(path, "wb");
  if (!file) {
    return RACE_PERSISTENCE_IO_ERROR;
  }

  bool ok = !length || SDL_WriteIO(file, data, length) == length;
  if (ok && !SDL_FlushIO(file)) {
    ok = false;
  }

  if (!SDL_CloseIO(file)) {
    ok = false;
  }

  if (!ok) {
    SDL_RemovePath(path);
    return RACE_PERSISTENCE_IO_ERROR;
  }

  return RACE_PERSISTENCE_OK;
#else
  FILE *file = fopen(path, "wb");
  if (!file) {
    return RACE_PERSISTENCE_IO_ERROR;
  }

  bool ok = !length || fwrite(data, 1, length, file) == length;
  if (ok && fflush(file)) {
    ok = false;
  }
  if (ok && !Race_Persistence_SyncFile(file)) {
    ok = false;
  }
  if (fclose(file)) {
    ok = false;
  }

  if (!ok) {
    remove(path);
    return RACE_PERSISTENCE_IO_ERROR;
  }

  return RACE_PERSISTENCE_OK;
#endif
}

race_persistence_result_t Race_Persistence_Promote(const char *candidate,
                                                   const char *committed) {
  if (!candidate || !*candidate || !committed || !*committed ||
      !strcmp(candidate, committed)) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }

#if defined(_WIN32)
  SDL_PathInfo info = { 0 };
  if (!SDL_GetPathInfo(candidate, &info)) {
    return RACE_PERSISTENCE_NOT_FOUND;
  }

  if (info.type != SDL_PATHTYPE_FILE) {
    return RACE_PERSISTENCE_NOT_REGULAR;
  }

  return SDL_RenamePath(candidate, committed)
    ? RACE_PERSISTENCE_OK
    : RACE_PERSISTENCE_IO_ERROR;
#else
  const race_persistence_result_t path_result = Race_Persistence_PathInfo(candidate, NULL);
  if (path_result != RACE_PERSISTENCE_OK) {
    return path_result;
  }

  if (rename(candidate, committed)) {
    return RACE_PERSISTENCE_IO_ERROR;
  }

  Race_Persistence_SyncParent(committed);
  return RACE_PERSISTENCE_OK;
#endif
}

race_persistence_result_t Race_Persistence_Remove(const char *path) {
  if (!path || !*path) {
    return RACE_PERSISTENCE_INVALID_ARGUMENT;
  }

#if defined(_WIN32)
  SDL_PathInfo info = { 0 };
  if (!SDL_GetPathInfo(path, &info)) {
    return RACE_PERSISTENCE_NOT_FOUND;
  }
  if (info.type != SDL_PATHTYPE_FILE) {
    return RACE_PERSISTENCE_NOT_REGULAR;
  }
  return SDL_RemovePath(path)
    ? RACE_PERSISTENCE_OK
    : RACE_PERSISTENCE_IO_ERROR;
#else
  const race_persistence_result_t path_result = Race_Persistence_PathInfo(path, NULL);
  if (path_result != RACE_PERSISTENCE_OK) {
    return path_result;
  }
  if (remove(path)) {
    return RACE_PERSISTENCE_IO_ERROR;
  }
  Race_Persistence_SyncParent(path);
  return RACE_PERSISTENCE_OK;
#endif
}

const char *Race_Persistence_ResultName(race_persistence_result_t result) {
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
  }

  return "unknown";
}
