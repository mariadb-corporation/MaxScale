/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXB_MODULE_NAME "nosqlprotocol"

#include <maxscale/ccdefs.hh>
#include <maxbase/assert.hh>
#include <maxscale/log.hh>
#include <maxscale/buffer.hh>

namespace nosql
{
// Conversion functions.
// TODO: Use GWBUF values in nosqlprotocol (MXS-4931)
inline GWBUF* gwbuf_to_gwbufptr(GWBUF&& buffer)
{
    return new GWBUF(std::move(buffer));
}

inline GWBUF gwbufptr_to_gwbuf(GWBUF* buffer)
{
    GWBUF rval(std::move(*buffer));
    delete buffer;
    return rval;
}
}
