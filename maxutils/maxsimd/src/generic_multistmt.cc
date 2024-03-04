/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxsimd/multistmt.hh>
#include <maxbase/assert.hh>
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
bool is_multi_stmt_impl(std::string_view sql)
{
    bool rval = false;

    const char* ptr;
    const char* data = sql.data();
    size_t buflen = sql.size();

    if (have_semicolon(data, buflen) && (ptr = mxb::strnchr_esc_mariadb(data, ';', buflen)))
    {
        // A semicolon has been seen, what follows must be only space,
        // semicolons or comments.
        while (!rval && ptr < data + buflen)
        {
            if (isspace(*ptr) || *ptr == ';')
            {
                ++ptr;
                continue;
            }

            auto ptr_before = ptr;
            ptr = maxbase::consume_comment(ptr, data + buflen);
            if (ptr != ptr_before)
            {
                continue;
            }

            rval = true;
        }
    }

    return rval;
}
}
}
