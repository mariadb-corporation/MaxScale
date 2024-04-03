/*
 * Copyright (c) 2023 MariaDB plc
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

#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

namespace
{

struct ThisUnit
{
    MariaDBParser::Helper helper;
} this_unit;

mxs::Parser::PacketTypeMask command_to_typemask(uint8_t cmd)
{
    uint32_t type_mask = mxs::sql::TYPE_UNKNOWN;
    mxs::Parser::TypeMaskStatus status = mxs::Parser::TypeMaskStatus::FINAL;

    switch (cmd)
    {
    case MXS_COM_QUIT:                  /*< 1 QUIT will close all sessions */
    case MXS_COM_INIT_DB:               /*< 2 DDL must go to the master */
    case MXS_COM_REFRESH:               /*< 7 - I guess this is session but not sure */
    case MXS_COM_DEBUG:                 /*< 0d all servers dump debug info to stdout */
    case MXS_COM_PING:                  /*< 0e all servers are pinged */
    case MXS_COM_CHANGE_USER:           /*< 11 all servers change it accordingly */
    case MXS_COM_SET_OPTION:            /*< 1b send options to all servers */
    case MXS_COM_RESET_CONNECTION:      /*< 1f resets the state of all connections */
    case MXS_COM_STMT_RESET:            /*< resets the data of a prepared statement */
        type_mask = mxs::sql::TYPE_SESSION_WRITE;
        break;

    case MXS_COM_STMT_CLOSE:            /*< free prepared statement */
        type_mask = mxs::sql::TYPE_SESSION_WRITE | mxs::sql::TYPE_DEALLOC_PREPARE;
        break;

    case MXS_COM_CREATE_DB:                 /**< 5 DDL must go to the master */
    case MXS_COM_DROP_DB:                   /**< 6 DDL must go to the master */
    case MXS_COM_STMT_SEND_LONG_DATA:       /*< send data to column */
        type_mask = mxs::sql::TYPE_WRITE;
        break;

    case MXS_COM_FIELD_LIST:        /**< This is essentially SHOW COLUMNS */
        type_mask = mxs::sql::TYPE_READ;
        break;

    case MXS_COM_QUERY:
        status = mxs::Parser::TypeMaskStatus::NEEDS_PARSING;
        break;

    case MXS_COM_STMT_PREPARE:
        status = mxs::Parser::TypeMaskStatus::NEEDS_PARSING;
        break;

    case MXS_COM_STMT_EXECUTE:
        /** Parsing is not needed for this type of packet */
        type_mask = mxs::sql::TYPE_EXEC_STMT;
        break;

    case MXS_COM_SHUTDOWN:          /**< 8 where should shutdown be routed ? */
    case MXS_COM_STATISTICS:        /**< 9 ? */
    case MXS_COM_PROCESS_INFO:      /**< 0a ? */
    case MXS_COM_CONNECT:           /**< 0b ? */
    case MXS_COM_PROCESS_KILL:      /**< 0c ? */
    case MXS_COM_TIME:              /**< 0f should this be run in gateway ? */
    case MXS_COM_DELAYED_INSERT:    /**< 10 ? */
    case MXS_COM_DAEMON:            /**< 1d ? */
    default:
        break;
    }

    return mxs::Parser::PacketTypeMask {type_mask, status};
}
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

const char* MariaDBParser::Helper::client_command_to_string(uint32_t cmd) const
{
    return mariadb::cmd_to_string(cmd);
}

bool MariaDBParser::Helper::command_will_respond(uint32_t cmd) const
{
    return mariadb::command_will_respond(cmd);
}

bool MariaDBParser::Helper::continues_ps(const GWBUF& packet, uint32_t prev_cmd) const
{
    bool rv = false;

    uint32_t cmd = get_command(packet);

    if (prev_cmd == MXS_COM_STMT_SEND_LONG_DATA
        && (cmd == MXS_COM_STMT_EXECUTE || cmd == MXS_COM_STMT_SEND_LONG_DATA))
    {
        // PS execution must be sent to the same server where the data was sent
        rv = true;
    }
    else if (cmd == MXS_COM_STMT_FETCH)
    {
        // COM_STMT_FETCH should always go to the same target as the COM_STMT_EXECUTE
        rv = true;
    }

    return rv;
}

uint32_t MariaDBParser::Helper::get_command(const GWBUF& packet) const
{
    return mariadb::get_command(packet);
}

mxs::Parser::PacketTypeMask MariaDBParser::Helper::get_packet_type_mask(const GWBUF& packet) const
{
    if (packet.length() <= MYSQL_HEADER_LEN)
    {
        return mxs::Parser::PacketTypeMask{mxs::sql::TYPE_UNKNOWN, TypeMaskStatus::FINAL};
    }

    return command_to_typemask(packet[MYSQL_HEADER_LEN]);
}

uint32_t MariaDBParser::Helper::get_ps_id(const GWBUF& packet) const
{
    return mxs_mysql_extract_ps_id(packet);
}

std::string_view MariaDBParser::Helper::get_sql(const GWBUF& packet) const
{
    return mariadb::get_sql(packet);
}

bool MariaDBParser::Helper::is_empty(const GWBUF& packet) const
{
    return packet.length() == MYSQL_HEADER_LEN;
}

bool MariaDBParser::Helper::is_execute_immediately_ps(uint32_t id) const
{
    return id == MARIADB_PS_DIRECT_EXEC_ID;
}

bool MariaDBParser::Helper::is_multi_part_packet(const GWBUF& packet) const
{
    uint32_t buflen = packet.length();

    // The buffer should contain at most (2^24 - 1) + 4 bytes ...
    mxb_assert(buflen <= MYSQL_HEADER_LEN + GW_MYSQL_MAX_PACKET_LEN);
    // ... and the payload should be buflen - 4 bytes
    mxb_assert(MYSQL_GET_PAYLOAD_LEN(packet.data()) == buflen - MYSQL_HEADER_LEN);

    return buflen == MYSQL_HEADER_LEN + GW_MYSQL_MAX_PACKET_LEN;
}

bool MariaDBParser::Helper::is_prepare(const GWBUF& packet) const
{
    return mariadb::is_com_prepare(packet);
}

bool MariaDBParser::Helper::is_ps_direct_exec_id(uint32_t id) const
{
    return id == MARIADB_PS_DIRECT_EXEC_ID;
}

bool MariaDBParser::Helper::is_ps_packet(const GWBUF& packet) const
{
    return packet.length() > MYSQL_HEADER_LEN ? mxs_mysql_is_ps_command(packet[MYSQL_HEADER_LEN]) : false;
}

bool MariaDBParser::Helper::is_query(const GWBUF& packet) const
{
    return mariadb::is_com_query(packet);
}

mxs::Parser::QueryInfo MariaDBParser::Helper::get_query_info(const GWBUF& packet) const
{
    QueryInfo rval;
    uint32_t len = packet.length();
    rval.empty = len == MYSQL_HEADER_LEN;

    if (!rval.empty)
    {
        uint8_t cmd = packet.data()[MYSQL_HEADER_LEN];
        rval.command = cmd;
        rval.query = cmd == MXS_COM_QUERY;
        rval.prepare = cmd == MXS_COM_STMT_PREPARE;
        rval.multi_part_packet = len == MYSQL_HEADER_LEN + GW_MYSQL_MAX_PACKET_LEN;

        std::tie(rval.type_mask, rval.type_mask_status) = command_to_typemask(cmd);

        if (mxs_mysql_is_ps_command(cmd))
        {
            rval.ps_id = mxs_mysql_extract_ps_id(packet);
            rval.ps_direct_exec_id = rval.ps_id == MARIADB_PS_DIRECT_EXEC_ID;
            rval.execute_immediately_ps = rval.ps_direct_exec_id;
            rval.ps_packet = cmd != MXS_COM_STMT_CLOSE && cmd != MXS_COM_STMT_RESET;
        }
    }

    return rval;
}

MariaDBParser::MariaDBParser(std::unique_ptr<Parser> sParser)
    : mxs::CachingParser(std::move(sParser))
{
}

MariaDBParser::~MariaDBParser()
{
}
