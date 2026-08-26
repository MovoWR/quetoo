/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#include "cg_local.h"

#include "cg_race_client_file.h"

bool Cg_RaceClientFile_WriteVerified(const char *path,
                                     const void *data, size_t length) {
  if (!path || !*path || (!data && length)) {
    return false;
  }

  file_t *file = cgi.OpenFileWrite(path);
  if (!file) {
    return false;
  }

  const int64_t written = cgi.WriteFile(file, data, 1u, length);
  const bool closed = cgi.CloseFile(file);
  if (written != (int64_t) length || !closed) {
    return false;
  }

  void *loaded = NULL;
  const int64_t loaded_length = cgi.LoadFile(path, &loaded);
  const bool verified = loaded_length == (int64_t) length &&
                        (!length || (loaded && !memcmp(loaded, data, length)));
  if (loaded) {
    cgi.FreeFile(loaded);
  }
  return verified;
}
