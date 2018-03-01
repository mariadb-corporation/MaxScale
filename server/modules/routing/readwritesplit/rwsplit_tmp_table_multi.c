/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "readwritesplit.h"

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <maxscale/alloc.h>

/* Note that modutil contains much MySQL specific code */
#include <maxscale/modutil.h>

#include <maxscale/router.h>
#include "rwsplit_internal.h"
/**
 * @file rwsplit_tmp_table.c   The functions that carry out checks on
 * statements to see if they involve various operations involving temporary
 * tables or multi-statement queries.
 *
 * @verbatim
 * Revision History
 *
 * Date          Who                 Description
 * 08/08/2016    Martin Brampton     Initial implementation
 *
 * @endverbatim
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
void check_drop_tmp_table(ROUTER_CLIENT_SES *router_cli_ses, GWBUF *querybuf,
                          mysql_server_cmd_t packet_type)
{
    if (packet_type != MYSQL_COM_QUERY && packet_type != MYSQL_COM_DROP_DB)
    {
        return;
    }

    int tsize = 0, klen = 0, i;
    char **tbl = NULL;
    char *hkey, *dbname;
    MYSQL_session *my_data;
    rses_property_t *rses_prop_tmp;
    MYSQL_session *data = (MYSQL_session *)router_cli_ses->client_dcb->data;

    rses_prop_tmp = router_cli_ses->rses_properties[RSES_PROP_TYPE_TMPTABLES];
    dbname = (char *)data->db;

    if (qc_is_drop_table_query(querybuf))
    {
        tbl = qc_get_table_names(querybuf, &tsize, false);
        if (tbl != NULL)
        {
            for (i = 0; i < tsize; i++)
            {
                /* Not clear why the next six lines are outside the if block */
                klen = strlen(dbname) + strlen(tbl[i]) + 2;
                hkey = MXS_CALLOC(klen, sizeof(char));
                MXS_ABORT_IF_NULL(hkey);
                strcpy(hkey, dbname);
                strcat(hkey, ".");
                strcat(hkey, tbl[i]);

                if (rses_prop_tmp && rses_prop_tmp->rses_prop_data.temp_tables)
                {
                    if (hashtable_delete(rses_prop_tmp->rses_prop_data.temp_tables,
                                         (void *)hkey))
                    {
                        MXS_INFO("Temporary table dropped: %s", hkey);
                    }
                }
                MXS_FREE(tbl[i]);
                MXS_FREE(hkey);
            }

            MXS_FREE(tbl);
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
bool is_read_tmp_table(ROUTER_CLIENT_SES *router_cli_ses,
                       GWBUF *querybuf,
                       qc_query_type_t qtype)
{

    bool target_tmp_table = false;
    int tsize = 0, klen = 0, i;
    char **tbl = NULL;
    char *dbname;
    char hkey[MYSQL_DATABASE_MAXLEN + MYSQL_TABLE_MAXLEN + 2];
    MYSQL_session *data;
    bool rval = false;
    rses_property_t *rses_prop_tmp;

    if (router_cli_ses == NULL || querybuf == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameters passed: %p %p", __FUNCTION__,
                  router_cli_ses, querybuf);
        return false;
    }

    if (router_cli_ses->client_dcb == NULL)
    {
        MXS_ERROR("[%s] Error: Client DCB is NULL.", __FUNCTION__);
        return false;
    }

    rses_prop_tmp = router_cli_ses->rses_properties[RSES_PROP_TYPE_TMPTABLES];
    data = (MYSQL_session *)router_cli_ses->client_dcb->data;

    if (data == NULL)
    {
        MXS_ERROR("[%s] Error: User data in client DBC is NULL.", __FUNCTION__);
        return false;
    }

    dbname = (char *)data->db;

    if (qc_query_is_type(qtype, QUERY_TYPE_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_LOCAL_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_USERVAR_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ))
    {
        tbl = qc_get_table_names(querybuf, &tsize, false);

        if (tbl != NULL && tsize > 0)
        {
            /** Query targets at least one table */
            for (i = 0; i < tsize && !target_tmp_table && tbl[i]; i++)
            {
                sprintf(hkey, "%s.%s", dbname, tbl[i]);
                if (rses_prop_tmp && rses_prop_tmp->rses_prop_data.temp_tables)
                {
                    if (hashtable_fetch(rses_prop_tmp->rses_prop_data.temp_tables, hkey))
                    {
                        /**Query target is a temporary table*/
                        rval = true;
                        MXS_INFO("Query targets a temporary table: %s", hkey);
                        break;
                    }
                }
            }
        }
    }

    if (tbl != NULL)
    {
        for (i = 0; i < tsize; i++)
        {
            MXS_FREE(tbl[i]);
        }
        MXS_FREE(tbl);
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
void check_create_tmp_table(ROUTER_CLIENT_SES *router_cli_ses,
                            GWBUF *querybuf, qc_query_type_t type)
{
    if (!qc_query_is_type(type, QUERY_TYPE_CREATE_TMP_TABLE))
    {
        return;
    }

    int klen = 0;
    char *hkey, *dbname;
    MYSQL_session *data;
    rses_property_t *rses_prop_tmp;
    HASHTABLE *h;

    if (router_cli_ses == NULL || querybuf == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameters passed: %p %p", __FUNCTION__,
                  router_cli_ses, querybuf);
        return;
    }

    if (router_cli_ses->client_dcb == NULL)
    {
        MXS_ERROR("[%s] Error: Client DCB is NULL.", __FUNCTION__);
        return;
    }

    router_cli_ses->have_tmp_tables = true;
    rses_prop_tmp = router_cli_ses->rses_properties[RSES_PROP_TYPE_TMPTABLES];
    data = (MYSQL_session *)router_cli_ses->client_dcb->data;

    if (data == NULL)
    {
        MXS_ERROR("[%s] Error: User data in master server DBC is NULL.",
                  __FUNCTION__);
        return;
    }

    dbname = (char *)data->db;

    bool is_temp = true;
    char *tblname = NULL;

    tblname = qc_get_created_table_name(querybuf);

    if (tblname && strlen(tblname) > 0)
    {
        klen = strlen(dbname) + strlen(tblname) + 2;
        hkey = MXS_CALLOC(klen, sizeof(char));
        MXS_ABORT_IF_NULL(hkey);
        strcpy(hkey, dbname);
        strcat(hkey, ".");
        strcat(hkey, tblname);
    }
    else
    {
        hkey = NULL;
    }

    if (rses_prop_tmp == NULL)
    {
        if ((rses_prop_tmp = (rses_property_t *)MXS_CALLOC(1, sizeof(rses_property_t))))
        {
#if defined(SS_DEBUG)
            rses_prop_tmp->rses_prop_chk_top = CHK_NUM_ROUTER_PROPERTY;
            rses_prop_tmp->rses_prop_chk_tail = CHK_NUM_ROUTER_PROPERTY;
#endif
            rses_prop_tmp->rses_prop_rsession = router_cli_ses;
            rses_prop_tmp->rses_prop_refcount = 1;
            rses_prop_tmp->rses_prop_next = NULL;
            rses_prop_tmp->rses_prop_type = RSES_PROP_TYPE_TMPTABLES;
            router_cli_ses->rses_properties[RSES_PROP_TYPE_TMPTABLES] = rses_prop_tmp;
        }
    }
    if (rses_prop_tmp)
    {
        if (rses_prop_tmp->rses_prop_data.temp_tables == NULL)
        {
            h = hashtable_alloc(7, rwsplit_hashkeyfun, rwsplit_hashcmpfun);
            hashtable_memory_fns(h, rwsplit_hstrdup, NULL, rwsplit_hfree, NULL);
            if (h != NULL)
            {
                rses_prop_tmp->rses_prop_data.temp_tables = h;
            }
            else
            {
                MXS_ERROR("Failed to allocate a new hashtable.");
            }
        }

        if (hkey && rses_prop_tmp->rses_prop_data.temp_tables &&
            hashtable_add(rses_prop_tmp->rses_prop_data.temp_tables, (void *)hkey,
                          (void *)is_temp) == 0) /*< Conflict in hash table */
        {
            MXS_INFO("Temporary table conflict in hashtable: %s", hkey);
        }
#if defined(SS_DEBUG)
        {
            bool retkey = hashtable_fetch(rses_prop_tmp->rses_prop_data.temp_tables, hkey);
            if (retkey)
            {
                MXS_INFO("Temporary table added: %s", hkey);
            }
        }
#endif
    }

    MXS_FREE(hkey);
    MXS_FREE(tblname);
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
bool check_for_multi_stmt(GWBUF *buf, void *protocol, mysql_server_cmd_t packet_type)
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

bool check_for_sp_call(GWBUF *buf, mysql_server_cmd_t packet_type)
{
    return packet_type == MYSQL_COM_QUERY && qc_get_operation(buf) == QUERY_OP_CALL;
}

/**
 * @brief Determine the type of a query
 *
 * @param querybuf      GWBUF containing the query
 * @param packet_type   Integer denoting DB specific enum
 * @param non_empty_packet  Boolean to be set by this function
 *
 * @return qc_query_type_t the query type; also the non_empty_packet bool is set
 */
qc_query_type_t
determine_query_type(GWBUF *querybuf, int packet_type, bool non_empty_packet)
{
    qc_query_type_t qtype = QUERY_TYPE_UNKNOWN;

    if (non_empty_packet)
    {
        mysql_server_cmd_t my_packet_type = (mysql_server_cmd_t)packet_type;
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
