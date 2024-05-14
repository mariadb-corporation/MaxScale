/*
 * Copyright (c) 2023 Pg plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.pg.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "postgresprotocol.hh"
#include "pgparser.hh"

namespace
{

struct ThisUnit
{
    PgParser::Helper helper;
} this_unit;
}

// static
const PgParser::Helper& PgParser::Helper::get()
{
    return this_unit.helper;
}

GWBUF PgParser::Helper::create_packet(std::string_view sql) const
{
    return pg::create_query_packet(sql);
}

const char* PgParser::Helper::client_command_to_string(uint32_t cmd) const
{
    return pg::client_command_to_str(cmd);
}

bool PgParser::Helper::command_will_respond(uint32_t cmd) const
{
    return pg::will_respond(cmd);
}

bool PgParser::Helper::continues_ps(const GWBUF& packet, uint32_t prev_cmd) const
{
    return false;
}

uint32_t PgParser::Helper::get_command(const GWBUF& packet) const
{
    uint32_t cmd = 0;

    if (packet.length() > 0)
    {
        cmd = packet[0];
    }

    return cmd;
}

mxs::Parser::PacketTypeMask PgParser::Helper::get_packet_type_mask(const GWBUF& packet) const
{
    uint32_t type_mask = mxs::sql::TYPE_UNKNOWN;
    TypeMaskStatus status = TypeMaskStatus::FINAL;

    if (packet.length() > 1)
    {
        switch (*packet.data())
        {
        case pg::QUERY:
        case pg::PARSE:
            status = TypeMaskStatus::NEEDS_PARSING;
            break;

            // TODO: Handle other packets appropriately.
        default:
            break;
        }
    }

    return PacketTypeMask {type_mask, status};
}

uint32_t PgParser::Helper::get_ps_id(const GWBUF& packet) const
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    return 0;
}

std::string_view PgParser::Helper::get_sql(const GWBUF& packet) const
{
    return pg::get_sql(packet);
}

bool PgParser::Helper::is_empty(const GWBUF& packet) const
{
    return packet.length() == pg::HEADER_LEN;
}

bool PgParser::Helper::is_execute_immediately_ps(uint32_t id) const
{
    return false;
}

bool PgParser::Helper::is_multi_part_packet(const GWBUF& packet) const
{
    return false;
}

bool PgParser::Helper::is_prepare(const GWBUF& packet) const
{
    return pg::is_prepare(packet);
}

bool PgParser::Helper::is_ps_direct_exec_id(uint32_t id) const
{
    return false;
}

bool PgParser::Helper::is_ps_packet(const GWBUF& packet) const
{
    return packet[0] == pg::PARSE;
}

bool PgParser::Helper::is_query(const GWBUF& packet) const
{
    return pg::is_query(packet);
}

mxs::Parser::QueryInfo PgParser::Helper::get_query_info(const GWBUF& packet) const
{
    mxb_assert_message(!true, "TODO: Implement this");
    return QueryInfo{};
}

PgParser::PgParser(std::unique_ptr<Parser> sParser)
    : mxs::CachingParser(std::move(sParser))
{
}

PgParser::~PgParser()
{
}
