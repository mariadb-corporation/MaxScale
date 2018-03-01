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

#include <maxscale/router.h>
#include "rwsplit_internal.h"

/**
 * @file rwsplit_session_cmd.c   The functions that provide session command
 * handling for the read write split router.
 *
 * @verbatim
 * Revision History
 *
 * Date          Who                 Description
 * 08/08/2016    Martin Brampton     Initial implementation
 *
 * @endverbatim
 */

static bool sescmd_cursor_history_empty(sescmd_cursor_t *scur);
static void sescmd_cursor_reset(sescmd_cursor_t *scur);
static bool sescmd_cursor_next(sescmd_cursor_t *scur);
static rses_property_t *mysql_sescmd_get_property(mysql_sescmd_t *scmd);

/*
 * The following functions, all to do with the handling of session commands,
 * are called from other modules of the read write split router:
 */

/**
 * Router session must be locked.
 * Return session command pointer if succeed, NULL if failed.
 */
mysql_sescmd_t *rses_property_get_sescmd(rses_property_t *prop)
{
    mysql_sescmd_t *sescmd;

    if (prop == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return NULL;
    }

    CHK_RSES_PROP(prop);

    sescmd = &prop->rses_prop_data.sescmd;

    if (sescmd != NULL)
    {
        CHK_MYSQL_SESCMD(sescmd);
    }
    return sescmd;
}

/**
 * Create session command property.
 */
mysql_sescmd_t *mysql_sescmd_init(rses_property_t *rses_prop,
                                  GWBUF *sescmd_buf,
                                  unsigned char packet_type,
                                  ROUTER_CLIENT_SES *rses)
{
    mysql_sescmd_t *sescmd;

    CHK_RSES_PROP(rses_prop);
    /** Can't call rses_property_get_sescmd with uninitialized sescmd */
    sescmd = &rses_prop->rses_prop_data.sescmd;
    sescmd->my_sescmd_prop = rses_prop; /*< reference to owning property */
#if defined(SS_DEBUG)
    sescmd->my_sescmd_chk_top = CHK_NUM_MY_SESCMD;
    sescmd->my_sescmd_chk_tail = CHK_NUM_MY_SESCMD;
#endif
    /** Set session command buffer */
    sescmd->my_sescmd_buf = sescmd_buf;
    sescmd->my_sescmd_packet_type = packet_type;
    sescmd->position = atomic_add(&rses->pos_generator, 1);

    return sescmd;
}

void mysql_sescmd_done(mysql_sescmd_t *sescmd)
{
    if (sescmd == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return;
    }
    CHK_RSES_PROP(sescmd->my_sescmd_prop);
    gwbuf_free(sescmd->my_sescmd_buf);
    memset(sescmd, 0, sizeof(mysql_sescmd_t));
}

/**
 * All cases where backend message starts at least with one response to session
 * command are handled here.
 * Read session commands from property list. If command is already replied,
 * discard packet. Else send reply to client. In both cases move cursor forward
 * until all session command replies are handled.
 *
 * Cases that are expected to happen and which are handled:
 * s = response not yet replied to client, S = already replied response,
 * q = query
 * 1. q+        for example : select * from mysql.user
 * 2. s+        for example : set autocommit=1
 * 3. S+
 * 4. sq+
 * 5. Sq+
 * 6. Ss+
 * 7. Ss+q+
 * 8. S+q+
 * 9. s+q+
 */
GWBUF *sescmd_cursor_process_replies(GWBUF *replybuf,
                                     backend_ref_t *bref,
                                     bool *reconnect)
{
    sescmd_cursor_t *scur = &bref->bref_sescmd_cur;
    mysql_sescmd_t *scmd = sescmd_cursor_get_command(scur);
    ROUTER_CLIENT_SES *ses = (*scur->scmd_cur_ptr_property)->rses_prop_rsession;
    CHK_GWBUF(replybuf);

    /**
     * Walk through packets in the message and the list of session
     * commands.
     */
    while (scmd != NULL && replybuf != NULL)
    {
        bref->reply_cmd = *((unsigned char *)replybuf->start + 4);
        scur->position = scmd->position;
        /** Faster backend has already responded to client : discard */
        if (scmd->my_sescmd_is_replied)
        {
            bool last_packet = false;

            CHK_GWBUF(replybuf);

            while (!last_packet)
            {
                int buflen;

                buflen = GWBUF_LENGTH(replybuf);
                last_packet = GWBUF_IS_TYPE_RESPONSE_END(replybuf);
                /** discard packet */
                replybuf = gwbuf_consume(replybuf, buflen);
            }
            /** Set response status received */
            bref_clear_state(bref, BREF_WAITING_RESULT);

            if (bref->reply_cmd != scmd->reply_cmd && BREF_IS_IN_USE(bref))
            {
                MXS_ERROR("Slave server '%s': response differs from master's response. "
                          "Closing connection due to inconsistent session state.",
                          bref->ref->server->unique_name);
                close_failed_bref(bref, true);

                RW_CHK_DCB(bref, bref->bref_dcb);
                dcb_close(bref->bref_dcb);
                RW_CLOSE_BREF(bref);
                *reconnect = true;
                gwbuf_free(replybuf);
                replybuf = NULL;
            }
        }
        /** This is a response from the master and it is the "right" one.
         * A slave server's response will be compared to this and if
         * their response differs from the master server's response, they
         * are dropped from the valid list of backend servers.
         * Response is in the buffer and it will be sent to client.
         *
         * If we have no master server, the first slave's response is considered
         * the "right" one. */
        else if (ses->rses_master_ref == NULL ||
                 !BREF_IS_IN_USE(ses->rses_master_ref) ||
                 ses->rses_master_ref->bref_dcb == bref->bref_dcb)
        {
            /** Mark the rest session commands as replied */
            scmd->my_sescmd_is_replied = true;
            scmd->reply_cmd = *((unsigned char *)replybuf->start + 4);

            MXS_INFO("Server '%s' responded to a session command, sending the response "
                     "to the client.", bref->ref->server->unique_name);

            for (int i = 0; i < ses->rses_nbackends; i++)
            {
                if (!BREF_IS_WAITING_RESULT(&ses->rses_backend_ref[i]))
                {
                    /** This backend has already received a response */
                    if (ses->rses_backend_ref[i].reply_cmd != scmd->reply_cmd &&
                        !BREF_IS_CLOSED(&ses->rses_backend_ref[i]) &&
                        BREF_IS_IN_USE(&ses->rses_backend_ref[i]))
                    {
                        close_failed_bref(&ses->rses_backend_ref[i], true);

                        if (ses->rses_backend_ref[i].bref_dcb)
                        {
                            RW_CHK_DCB(&ses->rses_backend_ref[i], ses->rses_backend_ref[i].bref_dcb);
                            dcb_close(ses->rses_backend_ref[i].bref_dcb);
                            RW_CLOSE_BREF(&ses->rses_backend_ref[i]);
                        }
                        *reconnect = true;
                        MXS_INFO("Disabling slave [%s]:%d, result differs from "
                                 "master's result. Master: %d Slave: %d",
                                 ses->rses_backend_ref[i].ref->server->name,
                                 ses->rses_backend_ref[i].ref->server->port,
                                 bref->reply_cmd, ses->rses_backend_ref[i].reply_cmd);
                    }
                }
            }

        }
        else
        {
            MXS_INFO("Slave '%s' responded before master to a session command. Result: %d",
                     bref->ref->server->unique_name,
                     (int)bref->reply_cmd);
            if (bref->reply_cmd == 0xff)
            {
                SERVER *serv = bref->ref->server;
                MXS_ERROR("Slave '%s' (%s:%u) failed to execute session command.",
                          serv->unique_name, serv->name, serv->port);
            }

            gwbuf_free(replybuf);
            replybuf = NULL;
        }

        if (sescmd_cursor_next(scur))
        {
            scmd = sescmd_cursor_get_command(scur);
        }
        else
        {
            scmd = NULL;
            /** All session commands are replied */
            scur->scmd_cur_active = false;
        }
    }
    ss_dassert(replybuf == NULL || *scur->scmd_cur_ptr_property == NULL);

    return replybuf;
}

/**
 * Get the address of current session command.
 *
 * Router session must be locked */
mysql_sescmd_t *sescmd_cursor_get_command(sescmd_cursor_t *scur)
{
    mysql_sescmd_t *scmd;

    scur->scmd_cur_cmd = rses_property_get_sescmd(*scur->scmd_cur_ptr_property);

    CHK_MYSQL_SESCMD(scur->scmd_cur_cmd);

    scmd = scur->scmd_cur_cmd;

    return scmd;
}

/** router must be locked */
bool sescmd_cursor_is_active(sescmd_cursor_t *sescmd_cursor)
{
    bool succp;

    if (sescmd_cursor == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return false;
    }

    succp = sescmd_cursor->scmd_cur_active;
    return succp;
}

/** router must be locked */
void sescmd_cursor_set_active(sescmd_cursor_t *sescmd_cursor,
                              bool value)
{
    /** avoid calling unnecessarily */
    ss_dassert(sescmd_cursor->scmd_cur_active != value);
    sescmd_cursor->scmd_cur_active = value;
}

/**
 * Clone session command's command buffer.
 * Router session must be locked
 */
GWBUF *sescmd_cursor_clone_querybuf(sescmd_cursor_t *scur)
{
    GWBUF *buf;
    if (scur == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return NULL;
    }
    ss_dassert(scur->scmd_cur_cmd != NULL);

    buf = gwbuf_clone(scur->scmd_cur_cmd->my_sescmd_buf);

    CHK_GWBUF(buf);
    return buf;
}

bool execute_sescmd_history(backend_ref_t *bref)
{
    bool succp = true;
    sescmd_cursor_t *scur;
    if (bref == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return false;
    }
    CHK_BACKEND_REF(bref);

    scur = &bref->bref_sescmd_cur;
    CHK_SESCMD_CUR(scur);

    if (!sescmd_cursor_history_empty(scur))
    {
        sescmd_cursor_reset(scur);
        succp = execute_sescmd_in_backend(bref);
    }

    return succp;
}

static bool sescmd_cursor_history_empty(sescmd_cursor_t *scur)
{
    bool succp;

    if (scur == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return true;
    }
    CHK_SESCMD_CUR(scur);

    if (scur->scmd_cur_rses->rses_properties[RSES_PROP_TYPE_SESCMD] == NULL)
    {
        succp = true;
    }
    else
    {
        succp = false;
    }

    return succp;
}

/*
 * End of functions called from other modules of the read write split router;
 * start of functions that are internal to this module.
 */

static void sescmd_cursor_reset(sescmd_cursor_t *scur)
{
    ROUTER_CLIENT_SES *rses;
    if (scur == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return;
    }
    CHK_SESCMD_CUR(scur);
    CHK_CLIENT_RSES(scur->scmd_cur_rses);
    rses = scur->scmd_cur_rses;

    scur->scmd_cur_ptr_property = &rses->rses_properties[RSES_PROP_TYPE_SESCMD];

    CHK_RSES_PROP((*scur->scmd_cur_ptr_property));
    scur->scmd_cur_active = false;
    scur->scmd_cur_cmd = &(*scur->scmd_cur_ptr_property)->rses_prop_data.sescmd;
}

/**
 * Moves cursor to next property and copied address of its sescmd to cursor.
 * Current propery must be non-null.
 * If current property is the last on the list, *scur->scmd_ptr_property == NULL
 *
 * Router session must be locked
 */
static bool sescmd_cursor_next(sescmd_cursor_t *scur)
{
    bool succp = false;
    rses_property_t *prop_curr;
    rses_property_t *prop_next;

    if (scur == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return false;
    }

    ss_dassert(scur != NULL);
    ss_dassert(*(scur->scmd_cur_ptr_property) != NULL);

    /** Illegal situation */
    if (scur == NULL || *scur->scmd_cur_ptr_property == NULL ||
        scur->scmd_cur_cmd == NULL)
    {
        /** Log error */
        goto return_succp;
    }
    prop_curr = *(scur->scmd_cur_ptr_property);

    CHK_MYSQL_SESCMD(scur->scmd_cur_cmd);
    ss_dassert(prop_curr == mysql_sescmd_get_property(scur->scmd_cur_cmd));
    CHK_RSES_PROP(prop_curr);

    /** Copy address of pointer to next property */
    scur->scmd_cur_ptr_property = &(prop_curr->rses_prop_next);
    prop_next = *scur->scmd_cur_ptr_property;
    ss_dassert(prop_next == *(scur->scmd_cur_ptr_property));

    /** If there is a next property move forward */
    if (prop_next != NULL)
    {
        CHK_RSES_PROP(prop_next);
        CHK_RSES_PROP((*(scur->scmd_cur_ptr_property)));

        /** Get pointer to next property's sescmd */
        scur->scmd_cur_cmd = rses_property_get_sescmd(prop_next);

        ss_dassert(prop_next == scur->scmd_cur_cmd->my_sescmd_prop);
        CHK_MYSQL_SESCMD(scur->scmd_cur_cmd);
        CHK_RSES_PROP(scur->scmd_cur_cmd->my_sescmd_prop);
    }
    else
    {
        /** No more properties, can't proceed. */
        goto return_succp;
    }

    if (scur->scmd_cur_cmd != NULL)
    {
        succp = true;
    }
    else
    {
        ss_dassert(false); /*< Log error, sescmd shouldn't be NULL */
    }
return_succp:
    return succp;
}

static rses_property_t *mysql_sescmd_get_property(mysql_sescmd_t *scmd)
{
    CHK_MYSQL_SESCMD(scmd);
    return scmd->my_sescmd_prop;
}
