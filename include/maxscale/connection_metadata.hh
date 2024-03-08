/*
 * Copyright (c) 2023 MariaDB plc
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
#pragma once

#include <maxscale/ccdefs.hh>

#include <map>

namespace maxscale
{
// Binary version of a value in information_schema.COLLATIONS
struct Collation
{
    std::string collation;
    std::string character_set;
};

// Struct containing the metadata that MaxScale generates for the handshake
struct ConnectionMetadata
{
    std::map<std::string, std::string> metadata;
    std::map<int, Collation>           collations;
};
}
