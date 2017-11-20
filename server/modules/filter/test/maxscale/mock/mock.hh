#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <string>
#include <string.h>
#include <maxscale/buffer.h>
#include <maxscale/modutil.h>


namespace maxscale
{

namespace mock
{

/**
 * Create a COM_QUERY packet containing the provided statement.
 *
 * @param zStatement  Null terminated string containing an SQL statement.
 *
 * @return A buffer containing a COM_QUERY packet.
 */
inline GWBUF* create_com_query(const char* zStatement)
{
    return modutil_create_query(zStatement);
}

/**
 * Create a COM_QUERY packet containing the provided statement.
 *
 * @param statement  String containing an SQL statement.
 *
 * @return A buffer containing a COM_QUERY packet.
 */
inline GWBUF* create_com_query(const std::string& statement)
{
    return create_com_query(statement.c_str());
}

}

}
