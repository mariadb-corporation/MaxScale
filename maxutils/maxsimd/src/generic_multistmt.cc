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

#include <maxsimd/multistmt.hh>
#include <maxbase/assert.h>
#include <maxbase/string.hh>
#include <cstring>

namespace
{
bool have_semicolon(const char* ptr, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (ptr[i] == ';')
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Check if the string is the final part of a valid SQL statement
 *
 * This function checks whether the string pointed by @p start contains any
 * tokens that are interpreted as executable commands.
 * @param start String containing the statement
 * @param len Length of the string
 * @return True if statement contains no executable parts
 */
bool is_mysql_statement_end(const char* start, int len)
{
    const char* ptr = start;
    bool rval = false;

    while (ptr < start + len && (isspace(*ptr) || *ptr == ';'))
    {
        ptr++;
    }

    if (ptr < start + len)
    {
        switch (*ptr)
        {
        case '-':
            if (ptr < start + len - 2 && *(ptr + 1) == '-' && isspace(*(ptr + 2)))
            {
                rval = true;
            }
            break;

        case '#':
            rval = true;
            break;

        case '/':
            if (ptr < start + len - 1 && *(ptr + 1) == '*')
            {
                rval = true;
            }
            break;
        }
    }
    else
    {
        rval = true;
    }

    return rval;
}

/**
 * @brief Check if the token is the END part of a BEGIN ... END block.
 * @param ptr String with at least three non-whitespace characters in it
 * @return True if the token is the final part of a BEGIN .. END block
 */
bool is_mysql_sp_end(const char* start, int len)
{
    const char* ptr = start;

    while (ptr < start + len && (isspace(*ptr) || *ptr == ';'))
    {
        ptr++;
    }

    return ptr < start + len - 3 && strncasecmp(ptr, "end", 3) == 0;
}
}



namespace maxsimd
{
namespace generic
{
/**
 * @brief Detect multi-statement queries
 *
 * It is possible that the session state is modified inside a multi-statement
 * query which would leave any slave sessions in an inconsistent state. Due to
 * this, for the duration of this session, all queries will be sent to the
 * master
 * if the current query contains a multi-statement query.
 * @param rses Router client session
 * @param buf Buffer containing the full query
 * @return True if the query contains multiple statements
 */
bool is_multi_stmt_impl(const std::string& sql)
{
    bool rval = false;

    char* ptr;
    char* data = const_cast<char*>(sql.c_str());
    size_t buflen = sql.size();

    if (have_semicolon(data, buflen) && (ptr = mxb::strnchr_esc_mariadb(data, ';', buflen)))
    {
        /** Skip stored procedures etc. */
        while (ptr && is_mysql_sp_end(ptr, buflen - (ptr - data)))
        {
            ptr = mxb::strnchr_esc_mariadb(ptr + 1, ';', buflen - (ptr - data) - 1);
        }

        if (ptr)
        {
            if (ptr < data + buflen
                && !is_mysql_statement_end(ptr, buflen - (ptr - data)))
            {
                rval = true;
            }
        }
    }

    return rval;
}
}
}
