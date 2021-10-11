/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
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
 * @brief  detect_special_query - Quickly determine if the query is
 *                                special and needs futher handling.
 *
 * @param  ppSql     In:  pSql points to the start of the query.
 *                   Out: pSql points to the start of the special query
 *                   (i.e points to "USE", "SET ROLE" or "KILL").
 *                   Unmodified if the query is not special.
 * @param  pEnd      Points to one passed end of sql
 * @return bool      true if the query is special. ppSql set as described above.
 */
bool detect_special_query(const char** ppSql, const char* pEnd);
