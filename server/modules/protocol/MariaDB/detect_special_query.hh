/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <string>
#include <cstring>
#include <vector>

/**
 * @brief  detect_special_query - Quickly determine if the query is potentially
 *                                special and needs futher handling.
 *
 * @param  ppSql     In:  pSql points to the start of the query.
 *                   Out: pSql points to a possible start of the special query
 *                   (points to "USE", "SET" or "KIL", yes only 3 characters are checked).
 *                   Unmodified if the query does not have the prefix.
 * @param  pEnd      Points to one passed end of sql
 * @return bool      true if the query has the prefix. ppSql set as described above.
 */
bool detect_special_query(const char** ppSql, const char* pEnd);
