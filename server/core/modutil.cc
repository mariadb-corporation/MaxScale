/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file modutil.c  - Implementation of useful routines for modules
 */

#include <string.h>
#include <strings.h>

#include <array>
#include <iterator>
#include <mutex>
#include <functional>
#include <cctype>
#include <mysql.h>

#include <maxsql/mariadb.hh>
#include <maxbase/alloc.hh>
#include <maxscale/buffer.hh>
#include <maxscale/modutil.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/utils.hh>
#include <maxscale/mysql_utils.hh>

const char format_str[] = "COM_UNKNOWN(%02hhx)";

// The message always fits inside the buffer
thread_local char unknow_type[sizeof(format_str)] = "";

const char* STRPACKETTYPE(int p)
{
    switch (p)
    {
    case MXS_COM_SLEEP:
        return "COM_SLEEP";

    case MXS_COM_QUIT:
        return "COM_QUIT";

    case MXS_COM_INIT_DB:
        return "COM_INIT_DB";

    case MXS_COM_QUERY:
        return "COM_QUERY";

    case MXS_COM_FIELD_LIST:
        return "COM_FIELD_LIST";

    case MXS_COM_CREATE_DB:
        return "COM_CREATE_DB";

    case MXS_COM_DROP_DB:
        return "COM_DROP_DB";

    case MXS_COM_REFRESH:
        return "COM_REFRESH";

    case MXS_COM_SHUTDOWN:
        return "COM_SHUTDOWN";

    case MXS_COM_STATISTICS:
        return "COM_STATISTICS";

    case MXS_COM_PROCESS_INFO:
        return "COM_PROCESS_INFO";

    case MXS_COM_CONNECT:
        return "COM_CONNECT";

    case MXS_COM_PROCESS_KILL:
        return "COM_PROCESS_KILL";

    case MXS_COM_DEBUG:
        return "COM_DEBUG";

    case MXS_COM_PING:
        return "COM_PING";

    case MXS_COM_TIME:
        return "COM_TIME";

    case MXS_COM_DELAYED_INSERT:
        return "COM_DELAYED_INSERT";

    case MXS_COM_CHANGE_USER:
        return "COM_CHANGE_USER";

    case MXS_COM_BINLOG_DUMP:
        return "COM_BINLOG_DUMP";

    case MXS_COM_TABLE_DUMP:
        return "COM_TABLE_DUMP";

    case MXS_COM_CONNECT_OUT:
        return "COM_CONNECT_OUT";

    case MXS_COM_REGISTER_SLAVE:
        return "COM_REGISTER_SLAVE";

    case MXS_COM_STMT_PREPARE:
        return "COM_STMT_PREPARE";

    case MXS_COM_STMT_EXECUTE:
        return "COM_STMT_EXECUTE";

    case MXS_COM_STMT_SEND_LONG_DATA:
        return "COM_STMT_SEND_LONG_DATA";

    case MXS_COM_STMT_CLOSE:
        return "COM_STMT_CLOSE";

    case MXS_COM_STMT_RESET:
        return "COM_STMT_RESET";

    case MXS_COM_SET_OPTION:
        return "COM_SET_OPTION";

    case MXS_COM_STMT_FETCH:
        return "COM_STMT_FETCH";

    case MXS_COM_DAEMON:
        return "COM_DAEMON";

    case MXS_COM_RESET_CONNECTION:
        return "COM_RESET_CONNECTION";

    case MXS_COM_STMT_BULK_EXECUTE:
        return "COM_STMT_BULK_EXECUTE";

    case MXS_COM_MULTI:
        return "COM_MULTI";

    case MXS_COM_XPAND_REPL:
        return "COM_XPAND_REPL";
    }

    snprintf(unknow_type, sizeof(unknow_type), format_str, static_cast<unsigned char>(p));

    return unknow_type;
}

namespace maxscale
{

}
