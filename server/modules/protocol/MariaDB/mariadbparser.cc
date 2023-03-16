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
    MariaDBParser::Helper helper;
} this_unit;

}

// static
const MariaDBParser::Helper& MariaDBParser::Helper::get()
{
    return this_unit.helper;
}

GWBUF MariaDBParser::Helper::create_packet(std::string_view sql) const
{
    return mariadb::create_query(sql);
}

std::string_view MariaDBParser::Helper::get_sql(const GWBUF& packet) const
{
    return mariadb::get_sql(packet);
}

bool MariaDBParser::Helper::is_prepare(const GWBUF& packet) const
{
    return mariadb::is_com_prepare(packet);
}


MariaDBParser::MariaDBParser(std::unique_ptr<Parser> sParser)
    : mxs::CachingParser(std::move(sParser))
{
}

MariaDBParser::~MariaDBParser()
{
}
