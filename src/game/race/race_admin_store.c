/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "race_admin_store.h"

#include <string.h>

#include "race_persistence.h"

race_admin_store_result_t Race_AdminStore_Load(
  const char *committed, race_admin_document_t *document,
  race_admin_parse_result_t *parse_result) {
  if (!committed || !*committed || !document) {
    return RACE_ADMIN_STORE_INVALID_ARGUMENT;
  }

  char serialized[RACE_ADMIN_SERIALIZED_MAX];
  size_t serialized_length;
  const race_persistence_result_t persisted = Race_Persistence_Read(
    committed, serialized, sizeof(serialized), &serialized_length);
  if (persisted == RACE_PERSISTENCE_NOT_FOUND) {
    Race_Admin_DocumentInit(document);
    if (parse_result) {
      *parse_result = RACE_ADMIN_PARSE_OK;
    }
    return RACE_ADMIN_STORE_MISSING;
  }
  if (persisted != RACE_PERSISTENCE_OK) {
    if (parse_result && persisted == RACE_PERSISTENCE_TOO_LARGE) {
      *parse_result = RACE_ADMIN_PARSE_TOO_LARGE;
    }
    return persisted == RACE_PERSISTENCE_TOO_LARGE
      ? RACE_ADMIN_STORE_CORRUPT
      : RACE_ADMIN_STORE_IO_ERROR;
  }

  race_admin_document_t parsed;
  const race_admin_parse_result_t result = Race_Admin_Parse(
    serialized, serialized_length, &parsed);
  if (parse_result) {
    *parse_result = result;
  }
  if (result != RACE_ADMIN_PARSE_OK) {
    return RACE_ADMIN_STORE_CORRUPT;
  }

  *document = parsed;
  return RACE_ADMIN_STORE_OK;
}

race_admin_store_result_t Race_AdminStore_Commit(
  const char *committed, const char *candidate,
  const race_admin_document_t *document,
  race_admin_parse_result_t *parse_result) {
  if (!committed || !*committed || !candidate || !*candidate ||
      !strcmp(committed, candidate) ||
      !Race_Admin_DocumentValid(document, true)) {
    return RACE_ADMIN_STORE_INVALID_ARGUMENT;
  }

  char serialized[RACE_ADMIN_SERIALIZED_MAX];
  size_t serialized_length;
  if (!Race_Admin_Serialize(document, serialized, sizeof(serialized),
                            &serialized_length)) {
    return RACE_ADMIN_STORE_INVALID_ARGUMENT;
  }

  race_persistence_result_t persisted = Race_Persistence_WriteCandidate(
    candidate, serialized, serialized_length);
  if (persisted != RACE_PERSISTENCE_OK) {
    return RACE_ADMIN_STORE_IO_ERROR;
  }

  char verified_bytes[RACE_ADMIN_SERIALIZED_MAX];
  size_t verified_length;
  persisted = Race_Persistence_Read(candidate, verified_bytes,
                                    sizeof(verified_bytes), &verified_length);
  if (persisted != RACE_PERSISTENCE_OK ||
      verified_length != serialized_length ||
      memcmp(verified_bytes, serialized, serialized_length)) {
    return RACE_ADMIN_STORE_IO_ERROR;
  }

  race_admin_document_t verified;
  const race_admin_parse_result_t parsed = Race_Admin_Parse(
    verified_bytes, verified_length, &verified);
  if (parse_result) {
    *parse_result = parsed;
  }
  if (parsed != RACE_ADMIN_PARSE_OK ||
      !Race_Admin_DocumentEquals(document, &verified)) {
    return RACE_ADMIN_STORE_CORRUPT;
  }

  persisted = Race_Persistence_Promote(candidate, committed);
  return persisted == RACE_PERSISTENCE_OK
    ? RACE_ADMIN_STORE_OK
    : RACE_ADMIN_STORE_IO_ERROR;
}

const char *Race_AdminStore_ResultName(race_admin_store_result_t result) {
  switch (result) {
    case RACE_ADMIN_STORE_OK:
      return "ok";
    case RACE_ADMIN_STORE_MISSING:
      return "missing";
    case RACE_ADMIN_STORE_CORRUPT:
      return "corrupt";
    case RACE_ADMIN_STORE_IO_ERROR:
      return "I/O error";
    case RACE_ADMIN_STORE_INVALID_ARGUMENT:
      return "invalid argument";
  }
  return "unknown";
}
