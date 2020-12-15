/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once
#include <maxscale/ccdefs.hh>

/**
 * The sqlite3_strlike() interface.  Return 0 on a match and non-zero for a miss.
 *
 * @param zPattern Pattern to match against
 * @param zStr The subject string
 * @param esc Escape character
 * @return 0 on match
 */
int sql_strlike(const char* zPattern, const char* zStr, unsigned int esc);

/**
 * Case-sensitive version of sql_strlike.
 */
int sql_strlike_case(const char* zPattern, const char* zStr, unsigned int esc);
