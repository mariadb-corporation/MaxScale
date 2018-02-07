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

#include "readwritesplit.hh"
#include "rwsplit_internal.hh"

#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <maxscale/router.h>
#include <maxscale/log_manager.h>
#include <maxscale/query_classifier.h>
#include <maxscale/dcb.h>
#include <maxscale/spinlock.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/alloc.h>

#define RWSPLIT_TRACE_MSG_LEN 1000

/**
 * Functions within the read-write split router that are specific to
 * MySQL. The aim is to either remove these into a separate module or to
 * move them into the MySQL protocol modules.
 */

/*
 * The following functions are called from elsewhere in the router and
 * are defined in rwsplit_internal.hh.  They are not intended to be called
 * from outside this router.
 */

/* This could be placed in the protocol, with a new API entry point
 * It is certainly MySQL specific. Packet types are DB specific, but can be
 * assumed to be enums, which can be handled as integers without knowing
 * which DB is involved until the packet type needs to be interpreted.
 *
 */

/**
 * @brief Determine the type of a query
 *
 * @param querybuf      GWBUF containing the query
 * @param packet_type   Integer denoting DB specific enum
 * @param non_empty_packet  Boolean to be set by this function
 *
 * @return uint32_t the query type; also the non_empty_packet bool is set
 */
uint32_t determine_query_type(GWBUF *querybuf, int command)
{
    uint32_t type = QUERY_TYPE_UNKNOWN;

    switch (command)
    {
    case MXS_COM_QUIT: /*< 1 QUIT will close all sessions */
    case MXS_COM_INIT_DB: /*< 2 DDL must go to the master */
    case MXS_COM_REFRESH: /*< 7 - I guess this is session but not sure */
    case MXS_COM_DEBUG: /*< 0d all servers dump debug info to stdout */
    case MXS_COM_PING: /*< 0e all servers are pinged */
    case MXS_COM_CHANGE_USER: /*< 11 all servers change it accordingly */
    case MXS_COM_SET_OPTION: /*< 1b send options to all servers */
        type = QUERY_TYPE_SESSION_WRITE;
        break;

    case MXS_COM_CREATE_DB: /**< 5 DDL must go to the master */
    case MXS_COM_DROP_DB: /**< 6 DDL must go to the master */
    case MXS_COM_STMT_CLOSE: /*< free prepared statement */
    case MXS_COM_STMT_SEND_LONG_DATA: /*< send data to column */
    case MXS_COM_STMT_RESET: /*< resets the data of a prepared statement */
        type = QUERY_TYPE_WRITE;
        break;

    case MXS_COM_QUERY:
        type = qc_get_type_mask(querybuf);
        break;

    case MXS_COM_STMT_PREPARE:
        type = qc_get_type_mask(querybuf);
        type |= QUERY_TYPE_PREPARE_STMT;
        break;

    case MXS_COM_STMT_EXECUTE:
        /** Parsing is not needed for this type of packet */
        type = QUERY_TYPE_EXEC_STMT;
        break;

    case MXS_COM_SHUTDOWN: /**< 8 where should shutdown be routed ? */
    case MXS_COM_STATISTICS: /**< 9 ? */
    case MXS_COM_PROCESS_INFO: /**< 0a ? */
    case MXS_COM_CONNECT: /**< 0b ? */
    case MXS_COM_PROCESS_KILL: /**< 0c ? */
    case MXS_COM_TIME: /**< 0f should this be run in gateway ? */
    case MXS_COM_DELAYED_INSERT: /**< 10 ? */
    case MXS_COM_DAEMON: /**< 1d ? */
    default:
        break;
    }

    return type;
}

/*
 * This appears to be MySQL specific
 */
/**
 * @brief Determine if a packet contains a SQL query
 *
 * Packet type tells us this, but in a DB specific way. This function is
 * provided so that code that is not DB specific can find out whether a packet
 * contains a SQL query. Clearly, to be effective different functions must be
 * called for different DB types.
 *
 * @param packet_type   Type of packet (integer)
 * @return bool indicating whether packet contains a SQL query
 */
bool
is_packet_a_query(int packet_type)
{
    return (packet_type == MXS_COM_QUERY);
}

/*
 * This one is problematic because it is MySQL specific, but also router
 * specific.
 */
/**
 * @brief Log the transaction status
 *
 * The router session and the query buffer are used to log the transaction
 * status, along with the query type (which is a generic description that
 * should be usable across all DB types).
 *
 * @param rses      Router session
 * @param querybuf  Query buffer
 * @param qtype     Query type
 */
void
log_transaction_status(RWSplitSession *rses, GWBUF *querybuf, uint32_t qtype)
{
    if (rses->large_query)
    {
        MXS_INFO("> Processing large request with more than 2^24 bytes of data");
    }
    else if (rses->load_data_state == LOAD_DATA_INACTIVE)
    {
        uint8_t *packet = GWBUF_DATA(querybuf);
        unsigned char command = packet[4];
        int len = 0;
        char* sql;
        char *qtypestr = qc_typemask_to_string(qtype);
        if (!modutil_extract_SQL(querybuf, &sql, &len))
        {
            sql = (char*)"<non-SQL>";
        }

        if (len > RWSPLIT_TRACE_MSG_LEN)
        {
            len = RWSPLIT_TRACE_MSG_LEN;
        }

        MXS_SESSION *ses = rses->client_dcb->session;
        const char *autocommit = session_is_autocommit(ses) ? "[enabled]" : "[disabled]";
        const char *transaction = session_trx_is_active(ses) ? "[open]" : "[not open]";
        const char *querytype = qtypestr == NULL ? "N/A" : qtypestr;
        const char *hint = querybuf->hint == NULL ? "" : ", Hint:";
        const char *hint_type = querybuf->hint == NULL ? "" : STRHINTTYPE(querybuf->hint->type);

        MXS_INFO("> Autocommit: %s, trx is %s, cmd: (0x%02x) %s, type: %s, stmt: %.*s%s %s",
                 autocommit, transaction, command, STRPACKETTYPE(command),
                 querytype, len, sql, hint, hint_type);

        MXS_FREE(qtypestr);
    }
    else
    {
        MXS_INFO("> Processing LOAD DATA LOCAL INFILE: %lu bytes sent.",
                 rses->rses_load_data_sent);
    }
}

/*
 * This is mostly router code, but it contains MySQL specific operations that
 * maybe could be moved to the protocol module. The modutil functions are mostly
 * MySQL specific and could migrate to the MySQL protocol; likewise the
 * utility to convert packet type to a string. The aim is for most of this
 * code to remain as part of the router.
 */
/**
 * @brief Operations to be carried out if request is for all backend servers
 *
 * If the choice of sending to all backends is in conflict with other bit
 * settings in route_target, then error messages are written to the log.
 *
 * Otherwise, the function route_session_write is called to carry out the
 * actual routing.
 *
 * @param route_target  Bit map indicating where packet should be routed
 * @param inst          Router instance
 * @param rses          Router session
 * @param querybuf      Query buffer containing packet
 * @param packet_type   Integer (enum) indicating type of packet
 * @param qtype         Query type
 * @return bool indicating whether the session can continue
 */
bool handle_target_is_all(route_target_t route_target, RWSplit *inst,
                          RWSplitSession *rses, GWBUF *querybuf,
                          int packet_type, uint32_t qtype)
{
    bool result = false;

    if (TARGET_IS_MASTER(route_target) || TARGET_IS_SLAVE(route_target))
    {
        /**
         * Conflicting routing targets. Return an error to the client.
         */

        char *query_str = modutil_get_query(querybuf);
        char *qtype_str = qc_typemask_to_string(qtype);

        MXS_ERROR("Can't route %s:%s:\"%s\". SELECT with session data "
                  "modification is not supported if configuration parameter "
                  "use_sql_variables_in=all .", STRPACKETTYPE(packet_type),
                  qtype_str, (query_str == NULL ? "(empty)" : query_str));

        GWBUF *errbuf = modutil_create_mysql_err_msg(1, 0, 1064, "42000",
                                                     "Routing query to backend failed. "
                                                     "See the error log for further details.");

        if (errbuf)
        {
            rses->client_dcb->func.write(rses->client_dcb, errbuf);
            result = true;
        }

        MXS_FREE(query_str);
        MXS_FREE(qtype_str);
    }
    else if (route_session_write(rses, gwbuf_clone(querybuf), packet_type, qtype))
    {

        result = true;
        atomic_add_uint64(&inst->stats().n_all, 1);
    }

    return result;
}

/*
 * Probably MySQL specific because of modutil function
 */
/**
 * @brief Write an error message to the log for closed session
 *
 * This happens if a request is received for a session that is already
 * closing down.
 *
 * @param querybuf      Query buffer containing packet
 */
void closed_session_reply(GWBUF *querybuf)
{
    uint8_t* data = GWBUF_DATA(querybuf);

    if (GWBUF_LENGTH(querybuf) >= 5 && !MYSQL_IS_COM_QUIT(data))
    {
        /* Note that most modutil functions are MySQL specific */
        char *query_str = modutil_get_query(querybuf);
        MXS_ERROR("Can't route %s:\"%s\" to backend server. Router is closed.",
                  STRPACKETTYPE(data[4]), query_str ? query_str : "(empty)");
        MXS_FREE(query_str);
    }
}

/**
 * @brief Send an error message to the client telling that the server is in read only mode
 *
 * @param dcb Client DCB
 *
 * @return True if sending the message was successful, false if an error occurred
 */
bool send_readonly_error(DCB *dcb)
{
    bool succp = false;
    const char* errmsg = "The MariaDB server is running with the --read-only"
                         " option so it cannot execute this statement";
    GWBUF* err = modutil_create_mysql_err_msg(1, 0, ER_OPTION_PREVENTS_STATEMENT,
                                              "HY000", errmsg);

    if (err)
    {
        succp = dcb->func.write(dcb, err);
    }
    else
    {
        MXS_ERROR("Memory allocation failed when creating client error message.");
    }

    return succp;
}
