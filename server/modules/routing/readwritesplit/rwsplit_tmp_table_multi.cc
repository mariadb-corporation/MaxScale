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

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <maxscale/modutil.h>
#include <maxscale/alloc.h>
#include <maxscale/router.h>

/**
 * The functions that carry out checks on statements to see if they involve
 * various operations involving temporary tables or multi-statement queries.
 */

/*
 * The following are to do with checking whether the statement refers to
 * temporary tables, or is a multi-statement request. Maybe they belong
 * somewhere else, outside this router. Perhaps in the query classifier?
 */

/**
 * @brief Check for dropping of temporary tables
 *
 * Check if the query is a DROP TABLE... query and
 * if it targets a temporary table, remove it from the hashtable.
 * @param router_cli_ses Router client session
 * @param querybuf GWBUF containing the query
 * @param type The type of the query resolved so far
 */
void check_drop_tmp_table(RWSplitSession *router_cli_ses, GWBUF *querybuf)
{
    if (qc_is_drop_table_query(querybuf))
    {
        const QC_FIELD_INFO* info;
        size_t n_infos;
        qc_get_field_info(querybuf, &info, &n_infos);

        for (size_t i = 0; i < n_infos; i++)
        {
            const char* db = mxs_mysql_get_current_db(router_cli_ses->client_dcb->session);
            std::string table = info[i].database ? info[i].database : db;
            table += ".";

            if (info[i].table)
            {
                table += info[i].table;
            }

            router_cli_ses->temp_tables.erase(table);
        }
    }
}

/**
 * Check if the query targets a temporary table.
 * @param router_cli_ses Router client session
 * @param querybuf GWBUF containing the query
 * @param type The type of the query resolved so far
 * @return The type of the query
 */
bool is_read_tmp_table(RWSplitSession *router_cli_ses,
                       GWBUF *querybuf,
                       uint32_t qtype)
{
    ss_dassert(router_cli_ses && querybuf && router_cli_ses->client_dcb);
    bool rval = false;

    if (qtype & (QUERY_TYPE_READ |
                 QUERY_TYPE_LOCAL_READ |
                 QUERY_TYPE_USERVAR_READ |
                 QUERY_TYPE_SYSVAR_READ |
                 QUERY_TYPE_GSYSVAR_READ))
    {
        const QC_FIELD_INFO* info;
        size_t n_infos;
        qc_get_field_info(querybuf, &info, &n_infos);

        for (size_t i = 0; i < n_infos; i++)
        {
            const char* db = mxs_mysql_get_current_db(router_cli_ses->client_dcb->session);
            std::string table = info[i].database ? info[i].database : db;
            table += ".";

            if (info[i].table)
            {
                table += info[i].table;
            }

            if (router_cli_ses->temp_tables.find(table) !=
                router_cli_ses->temp_tables.end())
            {
                rval = true;
                MXS_INFO("Query targets a temporary table: %s", table.c_str());
                break;
            }
        }
    }

    return rval;
}

/**
 * If query is of type QUERY_TYPE_CREATE_TMP_TABLE then find out
 * the database and table name, create a hashvalue and
 * add it to the router client session's property. If property
 * doesn't exist then create it first.
 * @param router_cli_ses Router client session
 * @param querybuf GWBUF containing the query
 * @param type The type of the query resolved so far
 */
void check_create_tmp_table(RWSplitSession *router_cli_ses,
                            GWBUF *querybuf, uint32_t type)
{
    if (qc_query_is_type(type, QUERY_TYPE_CREATE_TMP_TABLE))
    {
        ss_dassert(router_cli_ses && querybuf && router_cli_ses->client_dcb &&
                   router_cli_ses->client_dcb->data);

        router_cli_ses->have_tmp_tables = true;
        char* tblname = qc_get_created_table_name(querybuf);
        std::string table;

        if (tblname && *tblname)
        {
            const char* db = mxs_mysql_get_current_db(router_cli_ses->client_dcb->session);
            table += db;
            table += ".";
            table += tblname;
        }

        /** Add the table to the set of temporary tables */
        router_cli_ses->temp_tables.insert(table);

        MXS_FREE(tblname);
    }
}

/**
 * @brief Detect multi-statement queries
 *
 * It is possible that the session state is modified inside a multi-statement
 * query which would leave any slave sessions in an inconsistent state. Due to
 * this, for the duration of this session, all queries will be sent to the
 * master
 * if the current query contains a multi-statement query.
 * @param rses Router client session
 * @param buf Buffer containing the full query
 * @return True if the query contains multiple statements
 */
bool check_for_multi_stmt(GWBUF *buf, void *protocol, uint8_t packet_type)
{
    MySQLProtocol *proto = (MySQLProtocol *)protocol;
    bool rval = false;

    if (proto->client_capabilities & GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS &&
        packet_type == MYSQL_COM_QUERY)
    {
        char *ptr, *data = (char*)GWBUF_DATA(buf) + 5;
        /** Payload size without command byte */
        int buflen = gw_mysql_get_byte3((uint8_t *)GWBUF_DATA(buf)) - 1;

        if ((ptr = strnchr_esc_mysql(data, ';', buflen)))
        {
            /** Skip stored procedures etc. */
            while (ptr && is_mysql_sp_end(ptr, buflen - (ptr - data)))
            {
                ptr = strnchr_esc_mysql(ptr + 1, ';', buflen - (ptr - data) - 1);
            }

            if (ptr)
            {
                if (ptr < data + buflen &&
                    !is_mysql_statement_end(ptr, buflen - (ptr - data)))
                {
                    rval = true;
                }
            }
        }
    }

    return rval;
}

/**
 * @brief Determine the type of a query
 *
 * @param querybuf      GWBUF containing the query
 * @param packet_type   Integer denoting DB specific enum
 * @param non_empty_packet  Boolean to be set by this function
 *
 * @return uint32_t the query type; also the non_empty_packet bool is set
 */
uint32_t
determine_query_type(GWBUF *querybuf, int packet_type, bool non_empty_packet)
{
    uint32_t qtype = QUERY_TYPE_UNKNOWN;

    if (non_empty_packet)
    {
        uint8_t my_packet_type = (uint8_t)packet_type;
        switch (my_packet_type)
        {
        case MYSQL_COM_QUIT:        /*< 1 QUIT will close all sessions */
        case MYSQL_COM_INIT_DB:     /*< 2 DDL must go to the master */
        case MYSQL_COM_REFRESH:     /*< 7 - I guess this is session but not sure */
        case MYSQL_COM_DEBUG:       /*< 0d all servers dump debug info to stdout */
        case MYSQL_COM_PING:        /*< 0e all servers are pinged */
        case MYSQL_COM_CHANGE_USER: /*< 11 all servers change it accordingly */
        case MYSQL_COM_SET_OPTION:  /*< 1b send options to all servers */
            qtype = QUERY_TYPE_SESSION_WRITE;
            break;

        case MYSQL_COM_CREATE_DB:           /**< 5 DDL must go to the master */
        case MYSQL_COM_DROP_DB:             /**< 6 DDL must go to the master */
        case MYSQL_COM_STMT_CLOSE:          /*< free prepared statement */
        case MYSQL_COM_STMT_SEND_LONG_DATA: /*< send data to column */
        case MYSQL_COM_STMT_RESET: /*< resets the data of a prepared statement */
            qtype = QUERY_TYPE_WRITE;
            break;

        case MYSQL_COM_QUERY:
            qtype = qc_get_type_mask(querybuf);
            break;

        case MYSQL_COM_STMT_PREPARE:
            qtype = qc_get_type_mask(querybuf);
            qtype |= QUERY_TYPE_PREPARE_STMT;
            break;

        case MYSQL_COM_STMT_EXECUTE:
            /** Parsing is not needed for this type of packet */
            qtype = QUERY_TYPE_EXEC_STMT;
            break;

        case MYSQL_COM_SHUTDOWN:       /**< 8 where should shutdown be routed ? */
        case MYSQL_COM_STATISTICS:     /**< 9 ? */
        case MYSQL_COM_PROCESS_INFO:   /**< 0a ? */
        case MYSQL_COM_CONNECT:        /**< 0b ? */
        case MYSQL_COM_PROCESS_KILL:   /**< 0c ? */
        case MYSQL_COM_TIME:           /**< 0f should this be run in gateway ? */
        case MYSQL_COM_DELAYED_INSERT: /**< 10 ? */
        case MYSQL_COM_DAEMON:         /**< 1d ? */
        default:
            break;
        } /**< switch by packet type */
    }
    return qtype;
}
