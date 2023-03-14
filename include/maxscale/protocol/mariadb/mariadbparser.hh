/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/cachingparser.hh>


class MariaDBParser : public maxscale::CachingParser
{
public:
    MariaDBParser(const MariaDBParser&) = delete;
    MariaDBParser& operator=(const MariaDBParser&) = delete;

    MariaDBParser(mxs::ParserPlugin* pPlugin);
    ~MariaDBParser();

    static MariaDBParser& get();
};
