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

#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

namespace
{

struct ThisUnit
{
    MariaDBParser::Extractor extractor;
} this_unit;

}

// static
const MariaDBParser::Extractor& MariaDBParser::Extractor::get()
{
    return this_unit.extractor;
}

std::string_view MariaDBParser::Extractor::get_sql(const GWBUF& packet) const
{
    return mariadb::get_sql(packet);
}


MariaDBParser::MariaDBParser(std::unique_ptr<Parser> sParser)
    : mxs::CachingParser(std::move(sParser))
{
}

MariaDBParser::~MariaDBParser()
{
}
