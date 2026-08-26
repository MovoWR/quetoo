/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "race_admin.h"

typedef enum {
  RACE_ADMIN_STORE_OK,
  RACE_ADMIN_STORE_MISSING,
  RACE_ADMIN_STORE_CORRUPT,
  RACE_ADMIN_STORE_IO_ERROR,
  RACE_ADMIN_STORE_INVALID_ARGUMENT
} race_admin_store_result_t;

race_admin_store_result_t Race_AdminStore_Load(
  const char *committed, race_admin_document_t *document,
  race_admin_parse_result_t *parse_result);
race_admin_store_result_t Race_AdminStore_LoadWithInfo(
  const char *committed, race_admin_document_t *document,
  race_admin_parse_result_t *parse_result, race_admin_parse_info_t *info);
race_admin_store_result_t Race_AdminStore_Commit(
  const char *committed, const char *candidate,
  const race_admin_document_t *document,
  race_admin_parse_result_t *parse_result);
const char *Race_AdminStore_ResultName(race_admin_store_result_t result);
