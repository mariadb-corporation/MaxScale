/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <my_config.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <router.h>
#include <readwritesplit.h>
#include <rwsplit_internal.h>

#include <mysql.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <query_classifier.h>
#include <dcb.h>
#include <spinlock.h>
#include <modinfo.h>
#include <modutil.h>
#include <mysql_client_server_protocol.h>
#include <mysqld_error.h>
#include <maxscale/alloc.h>

#if defined(SS_DEBUG)
#include <mysql_client_server_protocol.h>
#endif

#define RWSPLIT_TRACE_MSG_LEN 1000

/**
 * @file rwsplit_mysql.c   Functions within the read-write split router that
 * are specific to MySQL. The aim is to either remove these into a separate
 * module or to move them into the MySQL protocol modules.
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
 * The following functions are called from elsewhere in the router and
 * are defined in rwsplit_internal.h.  They are not intended to be called
 * from outside this router.
 */

/* This could be placed in the protocol, with a new API entry point
 * It is certainly MySQL specific.
 *  */
int
determine_packet_type(GWBUF *querybuf, bool *non_empty_packet)
{
    mysql_server_cmd_t packet_type;
    uint8_t *packet = GWBUF_DATA(querybuf);

    if (gw_mysql_get_byte3(packet) == 0)
    {
        /** Empty packet signals end of LOAD DATA LOCAL INFILE, send it to master*/
        *non_empty_packet = false;
        packet_type = MYSQL_COM_UNDEFINED;
    }
    else
    {
        *non_empty_packet = true;
        packet_type = packet[4];
    }
    return (int)packet_type;
}

/*
 * This appears to be MySQL specific
 */
bool
is_packet_a_query(int packet_type)
{
    return (packet_type == MYSQL_COM_QUERY);
}

/*
 * This looks MySQL specific
 */
bool
is_packet_a_one_way_message(int packet_type)
{
    return (packet_type == MYSQL_COM_STMT_SEND_LONG_DATA ||
        packet_type == MYSQL_COM_QUIT || packet_type == MYSQL_COM_STMT_CLOSE);
}

/*
 * This one is problematic because it is MySQL specific, but also router
 * specific.
 */
void
log_transaction_status(ROUTER_CLIENT_SES *rses, GWBUF *querybuf, qc_query_type_t qtype)
{
     if (!rses->rses_load_active)
     {
         uint8_t *packet = GWBUF_DATA(querybuf);
         unsigned char ptype = packet[4];
         size_t len = MIN(GWBUF_LENGTH(querybuf),
                MYSQL_GET_PACKET_LEN((unsigned char *)querybuf->start) - 1);
         char *data = (char *)&packet[5];
         char *contentstr = strndup(data, MIN(len, RWSPLIT_TRACE_MSG_LEN));
         char *qtypestr = qc_get_qtype_str(qtype);
         MXS_INFO("> Autocommit: %s, trx is %s, cmd: %s, type: %s, stmt: %s%s %s",
           (rses->rses_autocommit_enabled ? "[enabled]" : "[disabled]"),
           (rses->rses_transaction_active ? "[open]" : "[not open]"),
           STRPACKETTYPE(ptype), (qtypestr == NULL ? "N/A" : qtypestr),
           contentstr, (querybuf->hint == NULL ? "" : ", Hint:"),
           (querybuf->hint == NULL ? "" : STRHINTTYPE(querybuf->hint->type)));
         MXS_FREE(contentstr);
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
bool
handle_target_is_all(route_target_t route_target,
        ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
        GWBUF *querybuf, int packet_type, qc_query_type_t qtype)
{
    bool result;

    /** Multiple, conflicting routing target. Return error */
    if (TARGET_IS_MASTER(route_target) || TARGET_IS_SLAVE(route_target))
    {
        backend_ref_t *bref = rses->rses_backend_ref;

        /* NOTE: modutil_get_query is MySQL specific */
        char *query_str = modutil_get_query(querybuf);
        char *qtype_str = qc_get_qtype_str(qtype);

        /* NOTE: packet_type is MySQL specific */
        MXS_ERROR("Can't route %s:%s:\"%s\". SELECT with session data "
          "modification is not supported if configuration parameter "
          "use_sql_variables_in=all .", STRPACKETTYPE(packet_type),
          qtype_str, (query_str == NULL ? "(empty)" : query_str));

        MXS_INFO("Unable to route the query without losing session data "
         "modification from other servers. <");

        while (bref != NULL && !BREF_IS_IN_USE(bref))
        {
            bref++;
        }

        if (bref != NULL && BREF_IS_IN_USE(bref))
        {
            /** Create and add MySQL error to eventqueue */
            modutil_reply_parse_error(bref->bref_dcb,
                      MXS_STRDUP_A("Routing query to backend failed. "
                           "See the error log for further "
                           "details."), 0);
            result = true;
        }
        else
        {
            /**
             * If there were no available backend references
             * available return false - session will be closed
             */
            MXS_ERROR("Sending error message to client "
              "failed. Router doesn't have any "
              "available backends. Session will be "
              "closed.");
            result = false;
        }
        /* Test shouldn't be needed */
        if (query_str)
        {
            MXS_FREE(query_str);
        }
        if (qtype_str)
        {
            MXS_FREE(qtype_str);
        }
        return result;
    }
    /**
     * It is not sure if the session command in question requires
     * response. Statement is examined in route_session_write.
     * Router locking is done inside the function.
     */
    result = route_session_write(rses, gwbuf_clone(querybuf), inst,
                packet_type, qtype);

    if (result)
    {
        atomic_add(&inst->stats.n_all, 1);
    }
    return result;
}

/* This is MySQL specific */
void
session_lock_failure_handling(GWBUF *querybuf, int packet_type, qc_query_type_t qtype)
{
    if (packet_type != MYSQL_COM_QUIT)
    {
        /* NOTE: modutil_get_query is MySQL specific */
        char *query_str = modutil_get_query(querybuf);

        MXS_ERROR("Can't route %s:%s:\"%s\" to "
              "backend server. Router is closed.",
              STRPACKETTYPE(packet_type), STRQTYPE(qtype),
              (query_str == NULL ? "(empty)" : query_str));
        MXS_FREE(query_str);
    }
}

/*
 * Probably MySQL specific because of modutil function
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

/*
 * Probably MySQL specific because of modutil function
 */
void live_session_reply(GWBUF **querybuf, ROUTER_CLIENT_SES *rses)
{
    GWBUF *tmpbuf = *querybuf;
    if (GWBUF_IS_TYPE_UNDEFINED(tmpbuf))
    {
        /* Note that many modutil functions are MySQL specific */
        *querybuf = modutil_get_complete_packets(&tmpbuf);
        if (tmpbuf)
        {
            rses->client_dcb->dcb_readqueue = gwbuf_append(rses->client_dcb->dcb_readqueue, tmpbuf);
        }
        *querybuf = gwbuf_make_contiguous(*querybuf);

        /** Mark buffer to as MySQL type */
        gwbuf_set_type(*querybuf, GWBUF_TYPE_MYSQL);
        gwbuf_set_type(*querybuf, GWBUF_TYPE_SINGLE_STMT);
    }
}

/*
 * Uses MySQL specific mechanisms
 */
void print_error_packet(ROUTER_CLIENT_SES *rses, GWBUF *buf, DCB *dcb)
{
#if defined(SS_DEBUG)
    if (GWBUF_IS_TYPE_MYSQL(buf))
    {
        while (gwbuf_length(buf) > 0)
        {
            /**
             * This works with MySQL protocol only !
             * Protocol specific packet print functions would be nice.
             */
            uint8_t *ptr = GWBUF_DATA(buf);
            size_t len = MYSQL_GET_PACKET_LEN(ptr);

            if (MYSQL_GET_COMMAND(ptr) == 0xff)
            {
                SERVER *srv = NULL;
                backend_ref_t *bref = rses->rses_backend_ref;
                int i;
                char *bufstr;

                for (i = 0; i < rses->rses_nbackends; i++)
                {
                    if (bref[i].bref_dcb == dcb)
                    {
                        srv = bref[i].bref_backend->backend_server;
                    }
                }
                ss_dassert(srv != NULL);
                char *str = (char *)&ptr[7];
                bufstr = strndup(str, len - 3);

                MXS_ERROR("Backend server %s:%d responded with "
                          "error : %s",
                          srv->name, srv->port, bufstr);
                MXS_FREE(bufstr);
            }
            buf = gwbuf_consume(buf, len + 4);
        }
    }
    else
    {
        gwbuf_free(buf);
    }
#endif /*< SS_DEBUG */
}

/*
 * Uses MySQL specific mechanisms
 */
void check_session_command_reply(GWBUF *writebuf, sescmd_cursor_t *scur, backend_ref_t *bref)
{
        if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_ERR) &&
            MYSQL_IS_ERROR_PACKET(((uint8_t *)GWBUF_DATA(writebuf))))
        {
            uint8_t *buf = (uint8_t *)GWBUF_DATA((scur->scmd_cur_cmd->my_sescmd_buf));
            uint8_t *replybuf = (uint8_t *)GWBUF_DATA(writebuf);
            size_t len = MYSQL_GET_PACKET_LEN(buf);
            size_t replylen = MYSQL_GET_PACKET_LEN(replybuf);
            char *err = strndup(&((char *)replybuf)[8], 5);
            char *replystr = strndup(&((char *)replybuf)[13], replylen - 4 - 5);

            ss_dassert(len + 4 == GWBUF_LENGTH(scur->scmd_cur_cmd->my_sescmd_buf));

            MXS_ERROR("Failed to execute session command in %s:%d. Error was: %s %s",
                      bref->bref_backend->backend_server->name,
                      bref->bref_backend->backend_server->port, err, replystr);
            MXS_FREE(err);
            MXS_FREE(replystr);
        }
}

/**
 * If session command cursor is passive, sends the command to backend for
 * execution.
 *
 * Returns true if command was sent or added successfully to the queue.
 * Returns false if command sending failed or if there are no pending session
 *  commands.
 *
 * Router session must be locked.
 */
/*
 * Uses MySQL specific values in the large switch statement, although it
 * may be possible to generalize them.
 */
bool execute_sescmd_in_backend(backend_ref_t *backend_ref)
{
    DCB *dcb;
    bool succp;
    int rc = 0;
    sescmd_cursor_t *scur;
    GWBUF *buf;
    if (backend_ref == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return false;
    }
    if (BREF_IS_CLOSED(backend_ref))
    {
        succp = false;
        goto return_succp;
    }
    dcb = backend_ref->bref_dcb;

    CHK_DCB(dcb);
    CHK_BACKEND_REF(backend_ref);

    /**
     * Get cursor pointer and copy of command buffer to cursor.
     */
    scur = &backend_ref->bref_sescmd_cur;

    /** Return if there are no pending ses commands */
    if (sescmd_cursor_get_command(scur) == NULL)
    {
        succp = true;
        MXS_INFO("Cursor had no pending session commands.");

        goto return_succp;
    }

    if (!sescmd_cursor_is_active(scur))
    {
        /** Cursor is left active when function returns. */
        sescmd_cursor_set_active(scur, true);
    }

    switch (scur->scmd_cur_cmd->my_sescmd_packet_type)
    {
        case MYSQL_COM_CHANGE_USER:
            /** This makes it possible to handle replies correctly */
            gwbuf_set_type(scur->scmd_cur_cmd->my_sescmd_buf, GWBUF_TYPE_SESCMD);
            buf = sescmd_cursor_clone_querybuf(scur);
            rc = dcb->func.auth(dcb, NULL, dcb->session, buf);
            break;

        case MYSQL_COM_INIT_DB:
        {
            /**
             * Record database name and store to session.
             */
            GWBUF *tmpbuf;
            MYSQL_session *data;
            unsigned int qlen;

            data = dcb->session->client_dcb->data;
            *data->db = 0;
            tmpbuf = scur->scmd_cur_cmd->my_sescmd_buf;
            qlen = MYSQL_GET_PACKET_LEN((unsigned char *) GWBUF_DATA(tmpbuf));
            if (qlen)
            {
                --qlen; // The COM_INIT_DB byte
                if (qlen > MYSQL_DATABASE_MAXLEN)
                {
                    MXS_ERROR("Too long a database name received in COM_INIT_DB, "
                              "trailing data will be cut.");
                    qlen = MYSQL_DATABASE_MAXLEN;
                }

                memcpy(data->db, (char*)GWBUF_DATA(tmpbuf) + 5, qlen);
                data->db[qlen] = 0;
            }
        }
        /** Fallthrough */
        case MYSQL_COM_QUERY:
        default:
            /**
             * Mark session command buffer, it triggers writing
             * MySQL command to protocol
             */

            gwbuf_set_type(scur->scmd_cur_cmd->my_sescmd_buf, GWBUF_TYPE_SESCMD);
            buf = sescmd_cursor_clone_querybuf(scur);
            rc = dcb->func.write(dcb, buf);
            break;
    }

    if (rc == 1)
    {
        succp = true;
    }
    else
    {
        succp = false;
    }
return_succp:
    return succp;
}

/*
 * End of functions called from other router modules; start of functions that
 * are internal to this module
 */

/**
 * Get client DCB pointer of the router client session.
 * This routine must be protected by Router client session lock.
 *
 * APPEARS TO NEVER BE USED!!
 *
 * @param rses  Router client session pointer
 *
 * @return Pointer to client DCB
 */
static DCB *rses_get_client_dcb(ROUTER_CLIENT_SES *rses)
{
    DCB *dcb = NULL;
    int i;

    for (i = 0; i < rses->rses_nbackends; i++)
    {
        if ((dcb = rses->rses_backend_ref[i].bref_dcb) != NULL &&
            BREF_IS_IN_USE(&rses->rses_backend_ref[i]) && dcb->session != NULL &&
            dcb->session->client_dcb != NULL)
        {
            return dcb->session->client_dcb;
        }
    }
    return NULL;
}

/*
 * The following are internal (directly or indirectly) to routing a statement
 * and should be moved to rwsplit_route_cmd.c if the MySQL specific code can
 * be removed.
 */

sescmd_cursor_t *backend_ref_get_sescmd_cursor(backend_ref_t *bref)
{
    sescmd_cursor_t *scur;
    CHK_BACKEND_REF(bref);

    scur = &bref->bref_sescmd_cur;
    CHK_SESCMD_CUR(scur);

    return scur;
}

/**
 * Send an error message to the client telling that the server is in read only mode
 * @param dcb Client DCB
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
