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
 * @brief Map a function over the list of tables in the query
 *
 * @param rses     Router client session
 * @param querybuf The query to inspect
 * @param func     Callback that is called for each table
 *
 * @return True if all tables were iterated, false if the iteration was stopped early
 */
static bool foreach_table(RWSplitSession* rses, GWBUF* querybuf, bool (*func)(RWSplitSession*,
                                                                              const std::string&))
{
    bool rval = true;
    int n_tables;
    char** tables = qc_get_table_names(querybuf, &n_tables, true);

    for (int i = 0; i < n_tables; i++)
    {
        const char* db = mxs_mysql_get_current_db(rses->client_dcb->session);
        std::string table;

        if (strchr(tables[i], '.') == NULL)
        {
            table += db;
            table += ".";
        }

        table += tables[i];

        if (!func(rses, table))
        {
            rval = false;
            break;
        }
    }

    return rval;
}

/**
 * Delete callback for foreach_table
 */
bool delete_table(RWSplitSession *rses, const std::string& table)
{
    rses->temp_tables.erase(table);
    return true;
}

/**
 * Find callback for foreach_table
 */
bool find_table(RWSplitSession* rses, const std::string& table)
{
    if (rses->temp_tables.find(table) != rses->temp_tables.end())
    {
        MXS_INFO("Query targets a temporary table: %s", table.c_str());
        return false;
    }

    return true;
}

/**
 * @brief Check for dropping of temporary tables
 *
 * Check if the query is a DROP TABLE... query and
 * if it targets a temporary table, remove it from the hashtable.
 * @param router_cli_ses Router client session
 * @param querybuf GWBUF containing the query
 * @param type The type of the query resolved so far
 */
void check_drop_tmp_table(RWSplitSession *rses, GWBUF *querybuf)
{
    if (qc_is_drop_table_query(querybuf))
    {
        foreach_table(rses, querybuf, delete_table);
    }
}

/**
 * Check if the query targets a temporary table.
 * @param router_cli_ses Router client session
 * @param querybuf GWBUF containing the query
 * @param type The type of the query resolved so far
 * @return The type of the query
 */
bool is_read_tmp_table(RWSplitSession *rses,
                       GWBUF *querybuf,
                       uint32_t qtype)
{
    ss_dassert(rses && querybuf && rses->client_dcb);
    bool rval = false;

    if (qc_query_is_type(qtype, QUERY_TYPE_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_LOCAL_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_USERVAR_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ))
    {
        if (!foreach_table(rses, querybuf, find_table))
        {
            rval = true;
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

        if (tblname && *tblname && strchr(tblname, '.') == NULL)
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

inline bool have_semicolon(const char* ptr, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (ptr[i] == ';')
        {
            return true;
        }
    }

    return false;
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
        packet_type == MXS_COM_QUERY)
    {
        char *ptr, *data = (char*)GWBUF_DATA(buf) + 5;
        /** Payload size without command byte */
        int buflen = gw_mysql_get_byte3((uint8_t *)GWBUF_DATA(buf)) - 1;

        if (have_semicolon(data, buflen) && (ptr = strnchr_esc_mysql(data, ';', buflen)))
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

bool check_for_sp_call(GWBUF *buf, uint8_t packet_type)
{
    return packet_type == MXS_COM_QUERY && qc_get_operation(buf) == QUERY_OP_CALL;
}
