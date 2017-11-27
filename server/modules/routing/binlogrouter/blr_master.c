/*lint -e662 */
/*lint -e661 */

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

/**
 * @file blr_master.c - contains code for the router to master communication
 *
 * The binlog router is designed to be used in replication environments to
 * increase the replication fanout of a master server. It provides a transparant
 * mechanism to read the binlog entries for multiple slaves while requiring
 * only a single connection to the actual master to support the slaves.
 *
 * The current prototype implement is designed to support MySQL 5.6 and has
 * a number of limitations. This prototype is merely a proof of concept and
 * should not be considered production ready.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                 Description
 * 02/04/2014   Mark Riddoch        Initial implementation
 * 07/05/2015   Massimiliano Pinto  Added MariaDB 10 Compatibility
 * 25/05/2015   Massimiliano Pinto  Added BLRM_SLAVE_STOPPED state
 * 08/06/2015   Massimiliano Pinto  Added m_errno and m_errmsg
 * 23/06/2015   Massimiliano Pinto  Master communication goes into BLRM_SLAVE_STOPPED state
 *                                  when an error is encountered in BLRM_BINLOGDUMP state.
 *                                  Server error code and msg are reported via SHOW SLAVE STATUS
 * 03/08/2015   Massimiliano Pinto  Initial implementation of transaction safety
 * 13/08/2015   Massimiliano Pinto  Addition of heartbeat check
 * 23/08/2015   Massimiliano Pinto  Added strerror_r
 * 26/08/2015   Massimiliano Pinto  Added MariaDB 10 GTID event check with flags = 0
 *                                  This is the current supported condition for detecting
 *                                  MariaDB 10 transaction start point.
 *                                  It's no longer using QUERY_EVENT with BEGIN
 * 25/09/2015   Massimiliano Pinto  Addition of lastEventReceived for slaves
 * 23/10/2015   Markus Makela       Added current_safe_event
 * 26/04/2016   Massimiliano Pinto  Added MariaDB 10.0 and 10.1 GTID event flags detection
 * 22/07/2016   Massimiliano Pinto  Added semi_sync replication support
 * 24/08/2016   Massimiliano Pinto  Added slave notification and blr_distribute_binlog_record removed
 * 01/09/2016   Massimiliano Pinto  Added support for ANNOTATE_ROWS_EVENT in COM_BINLOG_DUMP
 * 11/11/2016   Massimiliano Pinto  Encryption context is freed and set to null a new binlog file
 *                                  is being created due to ROTATE event.
 *
 * @endverbatim
 */

#include "blr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale/service.h>
#include <maxscale/server.h>
#include <maxscale/router.h>
#include <maxscale/atomic.h>
#include <maxscale/session.h>
#include <maxscale/dcb.h>
#include <maxscale/spinlock.h>
#include <maxscale/housekeeper.h>
#include <maxscale/buffer.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <maxscale/log_manager.h>

#include <maxscale/thread.h>

/* Temporary requirement for auth data */
#include <maxscale/protocol/mysql.h>
#include <maxscale/alloc.h>



static GWBUF *blr_make_query(DCB *dcb, char *query);
static GWBUF *blr_make_registration(ROUTER_INSTANCE *router);
static GWBUF *blr_make_binlog_dump(ROUTER_INSTANCE *router);
void encode_value(unsigned char *data, unsigned int value, int len);
void blr_handle_binlog_record(ROUTER_INSTANCE *router, GWBUF *pkt);
static int  blr_rotate_event(ROUTER_INSTANCE *router, uint8_t *pkt, REP_HEADER *hdr);
static void *CreateMySQLAuthData(char *username, char *password, char *database);
void blr_extract_header(uint8_t *pkt, REP_HEADER *hdr);
static void blr_log_packet(int priority, char *msg, uint8_t *ptr, int len);
void blr_master_close(ROUTER_INSTANCE *);
char *blr_extract_column(GWBUF *buf, int col);
void poll_fake_write_event(DCB *dcb);
GWBUF *blr_read_events_from_pos(ROUTER_INSTANCE *router, unsigned long long pos, REP_HEADER *hdr,
                                unsigned long long pos_end);
static void blr_check_last_master_event(void *inst);
extern int blr_check_heartbeat(ROUTER_INSTANCE *router);
static void blr_log_identity(ROUTER_INSTANCE *router);
static void blr_extract_header_semisync(uint8_t *pkt, REP_HEADER *hdr);
static int blr_send_semisync_ack (ROUTER_INSTANCE *router, uint64_t pos);
static int blr_get_master_semisync(GWBUF *buf);

static void blr_terminate_master_replication(ROUTER_INSTANCE *router, uint8_t* ptr, int len);
void blr_notify_all_slaves(ROUTER_INSTANCE *router);
extern bool blr_notify_waiting_slave(ROUTER_SLAVE *slave);

static int keepalive = 1;

/** Transaction-Safety feature */
typedef enum
{
    BLRM_NO_TRANSACTION, /*< No transaction */
    BLRM_TRANSACTION_START, /*< A transaction is open*/
    BLRM_COMMIT_SEEN, /*< Received COMMIT event in the current transaction */
    BLRM_XID_EVENT_SEEN /*< Received XID event of current transaction */
} master_transaction_t;

/** Master Semi-Sync capability */
typedef enum
{
    MASTER_SEMISYNC_NOT_AVAILABLE, /*< Semi-Sync replication not available */
    MASTER_SEMISYNC_DISABLED, /*< Semi-Sync is disabled */
    MASTER_SEMISYNC_ENABLED /*< Semi-Sync is enabled */
} master_semisync_capability_t;

#define MASTER_BYTES_BEFORE_EVENT 5
#define MASTER_BYTES_BEFORE_EVENT_SEMI_SYNC MASTER_BYTES_BEFORE_EVENT + 2
/* Semi-Sync indicator in network packet (byte 6) */
#define BLR_MASTER_SEMI_SYNC_INDICATOR  0xef
/* Semi-Sync flag ACK_REQ in network packet (byte 7) */
#define BLR_MASTER_SEMI_SYNC_ACK_REQ    0x01

/**
 * blr_start_master - controls the connection of the binlog router to the
 * master MySQL server and triggers the slave registration process for
 * the router.
 *
 * @param   router      The router instance
 */
void
blr_start_master(void* data)
{
    ROUTER_INSTANCE *router = (ROUTER_INSTANCE*)data;
    DCB *client;

    router->stats.n_binlogs_ses = 0;
    spinlock_acquire(&router->lock);
    if (router->master_state != BLRM_UNCONNECTED)
    {
        if (router->master_state != BLRM_SLAVE_STOPPED)
        {
            MXS_ERROR("%s: Master Connect: Unexpected master state %s\n",
                      router->service->name, blrm_states[router->master_state]);
        }
        else
        {
            MXS_NOTICE("%s: Master Connect: binlog state is %s\n",
                       router->service->name, blrm_states[router->master_state]);
        }
        spinlock_release(&router->lock);
        return;
    }
    router->master_state = BLRM_CONNECTING;

    spinlock_release(&router->lock);
    if ((client = dcb_alloc(DCB_ROLE_INTERNAL, NULL)) == NULL)
    {
        MXS_ERROR("failed to create DCB for dummy client");
        return;
    }
    router->client = client;
    client->state = DCB_STATE_POLLING;  /* Fake the client is reading */
    client->data = CreateMySQLAuthData(router->user, router->password, "");
    if ((router->session = session_alloc(router->service, client)) == NULL)
    {
        MXS_ERROR("failed to create session for connection to master");
        return;
    }
    client->session = router->session;
    if ((router->master = dcb_connect(router->service->dbref->server, router->session, BLR_PROTOCOL)) == NULL)
    {
        char *name = MXS_MALLOC(strlen(router->service->name) + strlen(" Master") + 1);

        if (name)
        {
            sprintf(name, "%s Master", router->service->name);
            hktask_oneshot(name, blr_start_master, router,
                           BLR_MASTER_BACKOFF_TIME * router->retry_backoff++);
            MXS_FREE(name);
        }
        if (router->retry_backoff > BLR_MAX_BACKOFF)
        {
            router->retry_backoff = BLR_MAX_BACKOFF;
        }
        MXS_ERROR("failed to connect to master server '%s'",
                  router->service->dbref->server->unique_name);
        return;
    }
    router->master->remote = MXS_STRDUP_A(router->service->dbref->server->name);

    MXS_NOTICE("%s: attempting to connect to master server [%s]:%d, binlog %s, pos %lu",
               router->service->name, router->service->dbref->server->name,
               router->service->dbref->server->port, router->binlog_name, router->current_pos);

    router->connect_time = time(0);

    if (setsockopt(router->master->fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive , sizeof(keepalive )))
    {
        perror("setsockopt");
    }

    router->master_state = BLRM_AUTHENTICATED;
    router->master->func.write(router->master, blr_make_query(router->master, "SELECT UNIX_TIMESTAMP()"));
    router->master_state = BLRM_TIMESTAMP;

    router->stats.n_masterstarts++;
}

/**
 * Reconnect to the master server.
 *
 * IMPORTANT - must be called with router->active_logs set by the
 * thread that set active_logs.
 *
 * @param   router      The router instance
 */
static void
blr_restart_master(ROUTER_INSTANCE *router)
{
    dcb_close(router->client);

    /* Now it is safe to unleash other threads on this router instance */
    spinlock_acquire(&router->lock);
    router->reconnect_pending = 0;
    router->active_logs = 0;
    spinlock_release(&router->lock);
    if (router->master_state < BLRM_BINLOGDUMP)
    {
        router->master_state = BLRM_UNCONNECTED;

        char *name = (char*)MXS_MALLOC(strlen(router->service->name)  + strlen(" Master") + 1);

        if (name)
        {
            sprintf(name, "%s Master", router->service->name);
            hktask_oneshot(name, blr_start_master, router,
                           BLR_MASTER_BACKOFF_TIME * router->retry_backoff++);
            MXS_FREE(name);
        }
        if (router->retry_backoff > BLR_MAX_BACKOFF)
        {
            router->retry_backoff = BLR_MAX_BACKOFF;
        }
    }
    else
    {
        router->master_state = BLRM_UNCONNECTED;
        blr_start_master(router);
    }
}

/**
 * Request a reconnect to the master.
 *
 * If another thread is active processing messages from the master
 * then merely set a flag for that thread to do the restart. If no
 * threads are active then directly call the restart routine to
 * reconnect to the master.
 *
 * @param   router      The router instance
 */
void
blr_master_reconnect(ROUTER_INSTANCE *router)
{
    int do_reconnect = 0;

    if (router->master_state == BLRM_SLAVE_STOPPED)
    {
        return;
    }

    spinlock_acquire(&router->lock);
    if (router->active_logs)
    {
        /* Currently processing a response, set a flag
         * and get the thread that is process a response
         * to deal with the reconnect.
         */
        router->reconnect_pending = 1;
        router->stats.n_delayedreconnects++;
    }
    else
    {
        router->active_logs = 1;
        do_reconnect = 1;
    }
    spinlock_release(&router->lock);
    if (do_reconnect)
    {
        blr_restart_master(router);
        spinlock_acquire(&router->lock);
        router->active_logs = 0;
        spinlock_release(&router->lock);
    }
}

/**
 * Shutdown a connection to the master
 *
 * @param router    The router instance
 */
void
blr_master_close(ROUTER_INSTANCE *router)
{
    dcb_close(router->master);
    router->master_state = BLRM_UNCONNECTED;
    router->master_event_state = BLR_EVENT_DONE;
    gwbuf_free(router->stored_event);
    router->stored_event = NULL;
}

/**
 * Mark this master connection for a delayed reconnect, used during
 * error recovery to cause a reconnect after 60 seconds.
 *
 * @param router    The router instance
 */
void
blr_master_delayed_connect(ROUTER_INSTANCE *router)
{
    char *name = (char*)MXS_MALLOC(strlen(router->service->name) + strlen(" Master Recovery") + 1);

    if (name)
    {
        sprintf(name, "%s Master Recovery", router->service->name);
        hktask_oneshot(name, blr_start_master, router, 60);
        MXS_FREE(name);
    }
}

/**
 * Binlog router master side state machine event handler.
 *
 * Handles an incoming response from the master server to the binlog
 * router.
 *
 * @param router    The router instance
 * @param buf       The incoming packet
 */
void
blr_master_response(ROUTER_INSTANCE *router, GWBUF *buf)
{
    char query[BLRM_MASTER_REGITRATION_QUERY_LEN + 1];
    char task_name[BLRM_TASK_NAME_LEN + 1] = "";

    atomic_add(&router->handling_threads, 1);
    ss_dassert(router->handling_threads == 1);
    spinlock_acquire(&router->lock);
    router->active_logs = 1;
    spinlock_release(&router->lock);
    if (router->master_state > BLRM_MAXSTATE)
    {
        MXS_ERROR("Invalid master state machine state (%d) for binlog router.",
                  router->master_state);
        gwbuf_free(buf);

        spinlock_acquire(&router->lock);
        if (router->reconnect_pending)
        {
            router->active_logs = 0;
            spinlock_release(&router->lock);
            atomic_add(&router->handling_threads, -1);
            MXS_ERROR("%s: Pending reconnect in state %s.",
                      router->service->name,
                      blrm_states[router->master_state]);
            blr_restart_master(router);
            return;
        }
        router->active_logs = 0;
        spinlock_release(&router->lock);
        atomic_add(&router->handling_threads, -1);
        return;
    }

    if (router->master_state == BLRM_GTIDMODE && MYSQL_RESPONSE_ERR(buf))
    {
        /*
         * If we get an error response to the GTID Mode then we
         * asusme the server does not support GTID modes and
         * continue. The error is saved and replayed to slaves if
         * they also request the GTID mode.
         */
        MXS_ERROR("%s: Master server does not support GTID Mode.",
                  router->service->name);
    }
    else if (router->master_state != BLRM_BINLOGDUMP && MYSQL_RESPONSE_ERR(buf))
    {
        char *msg_err = NULL;
        int msg_len = 0;
        int len = gwbuf_length(buf);
        unsigned long mysql_errno = extract_field(MYSQL_ERROR_CODE(buf), 16);

        msg_len = len - 7 - 6; // +7 is where msg starts, 6 is skipped the status message (#42000)
        msg_err = (char *)MXS_MALLOC(msg_len + 1);

        if (msg_err)
        {
            // skip status message only as MYSQL_RESPONSE_ERR(buf) points to GWBUF_DATA(buf) +7
            memcpy(msg_err, (char *)(MYSQL_ERROR_MSG(buf) + 6), msg_len);

            /* NULL terminated error string */
            *(msg_err + msg_len) = '\0';
        }

        MXS_ERROR("%s: Received error: %lu, '%s' from master during '%s' phase "
                  "of the master state machine.",
                  router->service->name,
                  mysql_errno,
                  msg_err ? msg_err : "(memory failure)",
                  blrm_states[router->master_state]);
        gwbuf_free(buf);

        spinlock_acquire(&router->lock);

        /* set mysql errno */
        router->m_errno = mysql_errno;

        /* set mysql error message */
        if (router->m_errmsg)
        {
            MXS_FREE(router->m_errmsg);
        }
        router->m_errmsg = msg_err ? msg_err : "(memory failure)";

        router->active_logs = 0;
        if (router->reconnect_pending)
        {
            spinlock_release(&router->lock);
            atomic_add(&router->handling_threads, -1);
            blr_restart_master(router);
            return;
        }
        spinlock_release(&router->lock);
        atomic_add(&router->handling_threads, -1);
        return;
    }
    switch (router->master_state)
    {
    case BLRM_TIMESTAMP:
        // Response to a timestamp message, no need to save this.
        gwbuf_free(buf);
        buf = blr_make_query(router->master, "SHOW VARIABLES LIKE 'SERVER_ID'");
        router->master_state = BLRM_SERVERID;
        router->master->func.write(router->master, buf);
        router->retry_backoff = 1;
        break;
    case BLRM_SERVERID:
        {
            char *val = blr_extract_column(buf, 2);

            // Response to fetch of master's server-id
            if (router->saved_master.server_id)
            {
                GWBUF_CONSUME_ALL(router->saved_master.server_id);
            }
            router->saved_master.server_id = buf;
            blr_cache_response(router, "serverid", buf);

            // set router->masterid from master server-id if it's not set by the config option
            if (router->masterid == 0)
            {
                router->masterid = atoi(val);
            }

            {
                char str[BLRM_SET_HEARTBEAT_QUERY_LEN];
                sprintf(str, "SET @master_heartbeat_period = %lu000000000", router->heartbeat);
                buf = blr_make_query(router->master, str);
            }
            router->master_state = BLRM_HBPERIOD;
            router->master->func.write(router->master, buf);
            MXS_FREE(val);
            break;
        }
    case BLRM_HBPERIOD:
        // Response to set the heartbeat period
        if (router->saved_master.heartbeat)
        {
            GWBUF_CONSUME_ALL(router->saved_master.heartbeat);
        }
        router->saved_master.heartbeat = buf;
        blr_cache_response(router, "heartbeat", buf);
        buf = blr_make_query(router->master, "SET @master_binlog_checksum = @@global.binlog_checksum");
        router->master_state = BLRM_CHKSUM1;
        router->master->func.write(router->master, buf);
        break;
    case BLRM_CHKSUM1:
        // Response to set the master binlog checksum
        if (router->saved_master.chksum1)
        {
            GWBUF_CONSUME_ALL(router->saved_master.chksum1);
        }
        router->saved_master.chksum1 = buf;
        blr_cache_response(router, "chksum1", buf);
        buf = blr_make_query(router->master, "SELECT @master_binlog_checksum");
        router->master_state = BLRM_CHKSUM2;
        router->master->func.write(router->master, buf);
        break;
    case BLRM_CHKSUM2:
        {
            // Set checksum from master reply
            blr_set_checksum(router, buf);

            // Response to the master_binlog_checksum, should be stored
            if (router->saved_master.chksum2)
            {
                GWBUF_CONSUME_ALL(router->saved_master.chksum2);
            }
            router->saved_master.chksum2 = buf;
            blr_cache_response(router, "chksum2", buf);

            if (router->mariadb10_compat)
            {
                buf = blr_make_query(router->master, "SET @mariadb_slave_capability=4");
                router->master_state = BLRM_MARIADB10;
            }
            else
            {
                buf = blr_make_query(router->master, "SELECT @@GLOBAL.GTID_MODE");
                router->master_state = BLRM_GTIDMODE;
            }
            router->master->func.write(router->master, buf);
            break;
        }
    case BLRM_MARIADB10:
        // Response to the SET @mariadb_slave_capability=4, should be stored
        if (router->saved_master.mariadb10)
        {
            GWBUF_CONSUME_ALL(router->saved_master.mariadb10);
        }
        router->saved_master.mariadb10 = buf;
        blr_cache_response(router, "mariadb10", buf);
        buf = blr_make_query(router->master, "SHOW VARIABLES LIKE 'SERVER_UUID'");
        router->master_state = BLRM_MUUID;
        router->master->func.write(router->master, buf);
        break;
    case BLRM_GTIDMODE:
        // Response to the GTID_MODE, should be stored
        if (router->saved_master.gtid_mode)
        {
            GWBUF_CONSUME_ALL(router->saved_master.gtid_mode);
        }
        router->saved_master.gtid_mode = buf;
        blr_cache_response(router, "gtidmode", buf);
        buf = blr_make_query(router->master, "SHOW VARIABLES LIKE 'SERVER_UUID'");
        router->master_state = BLRM_MUUID;
        router->master->func.write(router->master, buf);
        break;
    case BLRM_MUUID:
        {
            char *key;
            char *val = NULL;

            key = blr_extract_column(buf, 1);
            if (key && strlen(key))
            {
                val = blr_extract_column(buf, 2);
            }
            if (key)
            {
                MXS_FREE(key);
            }

            /* set the master_uuid from master if not set by the option */
            if (!router->set_master_uuid)
            {
                MXS_FREE(router->master_uuid);
                router->master_uuid = val;
            }

            // Response to the SERVER_UUID, should be stored
            if (router->saved_master.uuid)
            {
                GWBUF_CONSUME_ALL(router->saved_master.uuid);
            }
            router->saved_master.uuid = buf;
            blr_cache_response(router, "uuid", buf);
            sprintf(query, "SET @slave_uuid='%s'", router->uuid);
            buf = blr_make_query(router->master, query);
            router->master_state = BLRM_SUUID;
            router->master->func.write(router->master, buf);
            break;
        }
    case BLRM_SUUID:
        // Response to the SET @server_uuid, should be stored
        if (router->saved_master.setslaveuuid)
        {
            GWBUF_CONSUME_ALL(router->saved_master.setslaveuuid);
        }
        router->saved_master.setslaveuuid = buf;
        blr_cache_response(router, "ssuuid", buf);
        buf = blr_make_query(router->master, "SET NAMES latin1");
        router->master_state = BLRM_LATIN1;
        router->master->func.write(router->master, buf);
        break;
    case BLRM_LATIN1:
        // Response to the SET NAMES latin1, should be stored
        if (router->saved_master.setnames)
        {
            GWBUF_CONSUME_ALL(router->saved_master.setnames);
        }
        router->saved_master.setnames = buf;
        blr_cache_response(router, "setnames", buf);
        buf = blr_make_query(router->master, "SET NAMES utf8");
        router->master_state = BLRM_UTF8;
        router->master->func.write(router->master, buf);
        break;
    case BLRM_UTF8:
        // Response to the SET NAMES utf8, should be stored
        if (router->saved_master.utf8)
        {
            GWBUF_CONSUME_ALL(router->saved_master.utf8);
        }
        router->saved_master.utf8 = buf;
        blr_cache_response(router, "utf8", buf);
        buf = blr_make_query(router->master, "SELECT 1");
        router->master_state = BLRM_SELECT1;
        router->master->func.write(router->master, buf);
        break;
    case BLRM_SELECT1:
        // Response to the SELECT 1, should be stored
        if (router->saved_master.select1)
        {
            GWBUF_CONSUME_ALL(router->saved_master.select1);
        }
        router->saved_master.select1 = buf;
        blr_cache_response(router, "select1", buf);
        buf = blr_make_query(router->master, "SELECT VERSION()");
        router->master_state = BLRM_SELECTVER;
        router->master->func.write(router->master, buf);
        break;
    case BLRM_SELECTVER:
        // Response to SELECT VERSION should be stored
        if (router->saved_master.selectver)
        {
            GWBUF_CONSUME_ALL(router->saved_master.selectver);
        }
        router->saved_master.selectver = buf;
        blr_cache_response(router, "selectver", buf);
        buf = blr_make_query(router->master, "SELECT @@version_comment limit 1");
        router->master_state = BLRM_SELECTVERCOM;
        router->master->func.write(router->master, buf);
        break;
    case BLRM_SELECTVERCOM:
        // Response to SELECT @@version_comment should be stored
        if (router->saved_master.selectvercom)
        {
            GWBUF_CONSUME_ALL(router->saved_master.selectvercom);
        }
        router->saved_master.selectvercom = buf;
        blr_cache_response(router, "selectvercom", buf);
        buf = blr_make_query(router->master, "SELECT @@hostname");
        router->master_state = BLRM_SELECTHOSTNAME;
        router->master->func.write(router->master, buf);
        break;
    case BLRM_SELECTHOSTNAME:
        // Response to SELECT @@hostname should be stored
        if (router->saved_master.selecthostname)
        {
            GWBUF_CONSUME_ALL(router->saved_master.selecthostname);
        }
        router->saved_master.selecthostname = buf;
        blr_cache_response(router, "selecthostname", buf);
        buf = blr_make_query(router->master, "SELECT @@max_allowed_packet");
        router->master_state = BLRM_MAP;
        router->master->func.write(router->master, buf);
        break;
    case BLRM_MAP:
        // Response to SELECT @@max_allowed_packet should be stored
        if (router->saved_master.map)
        {
            GWBUF_CONSUME_ALL(router->saved_master.map);
        }
        router->saved_master.map = buf;
        blr_cache_response(router, "map", buf);
        buf = blr_make_registration(router);
        router->master_state = BLRM_REGISTER;
        router->master->func.write(router->master, buf);
        break;
    case BLRM_REGISTER:
        /* discard master reply to COM_REGISTER_SLAVE */
        gwbuf_free(buf);

        /* if semisync option is set, check for master semi-sync availability */
        if (router->request_semi_sync)
        {
            MXS_NOTICE("%s: checking Semi-Sync replication capability for master server [%s]:%d",
                       router->service->name,
                       router->service->dbref->server->name,
                       router->service->dbref->server->port);

            buf = blr_make_query(router->master, "SHOW VARIABLES LIKE 'rpl_semi_sync_master_enabled'");
            router->master_state = BLRM_CHECK_SEMISYNC;
            router->master->func.write(router->master, buf);

            break;
        }
        else
        {
            /* Continue */
            router->master_state = BLRM_REQUEST_BINLOGDUMP;
        }
    case BLRM_CHECK_SEMISYNC:
        {
            /**
             * This branch could be reached as fallthrough from BLRM_REGISTER
             * if request_semi_sync option is false
             */
            if (router->master_state == BLRM_CHECK_SEMISYNC)
            {
                /* Get master semi-sync installed, enabled, disabled */
                router->master_semi_sync = blr_get_master_semisync(buf);

                /* Discard buffer */
                gwbuf_free(buf);

                if (router->master_semi_sync == MASTER_SEMISYNC_NOT_AVAILABLE)
                {
                    /* not installed */
                    MXS_NOTICE("%s: master server [%s]:%d doesn't have semi_sync capability",
                               router->service->name,
                               router->service->dbref->server->name,
                               router->service->dbref->server->port);

                    /* Continue */
                    router->master_state = BLRM_REQUEST_BINLOGDUMP;

                }
                else
                {
                    if (router->master_semi_sync == MASTER_SEMISYNC_DISABLED)
                    {
                        /* Installed but not enabled,  right now */
                        MXS_NOTICE("%s: master server [%s]:%d doesn't have semi_sync enabled right now, "
                                   "Requesting Semi-Sync Replication",
                                   router->service->name,
                                   router->service->dbref->server->name,
                                   router->service->dbref->server->port);
                    }
                    else
                    {
                        /* Installed and enabled */
                        MXS_NOTICE("%s: master server [%s]:%d has semi_sync enabled, Requesting Semi-Sync Replication",
                                   router->service->name,
                                   router->service->dbref->server->name,
                                   router->service->dbref->server->port);
                    }

                    buf = blr_make_query(router->master, "SET @rpl_semi_sync_slave = 1");
                    router->master_state = BLRM_REQUEST_SEMISYNC;
                    router->master->func.write(router->master, buf);

                    break;
                }
            }
        }
    case BLRM_REQUEST_SEMISYNC:
        /**
         * This branch could be reached as fallthrough from BLRM_REGISTER or BLRM_CHECK_SEMISYNC
         * if request_semi_sync option is false or master doesn't support semisync or it's not enabled
         */
        if (router->master_state == BLRM_REQUEST_SEMISYNC)
        {
            /* discard master reply */
            gwbuf_free(buf);

            /* Continue */
            router->master_state = BLRM_REQUEST_BINLOGDUMP;
        }

    case BLRM_REQUEST_BINLOGDUMP:
        /**
         * This branch is reached after semi-sync check/request or
         * just after sending COM_REGISTER_SLAVE if request_semi_sync option is false
         */

        /* Request now a dump of the binlog file */
        buf = blr_make_binlog_dump(router);

        router->master_state = BLRM_BINLOGDUMP;

        router->master->func.write(router->master, buf);
        MXS_NOTICE("%s: Request binlog records from %s at "
                   "position %lu from master server [%s]:%d",
                   router->service->name, router->binlog_name,
                   router->current_pos,
                   router->service->dbref->server->name,
                   router->service->dbref->server->port);

        /* Log binlog router identity */
        blr_log_identity(router);

        break;

    case BLRM_BINLOGDUMP:
        /**
         * Main body, we have received a binlog record from the master
         */
        blr_handle_binlog_record(router, buf);

        /**
         * Set heartbeat check task
         */
        snprintf(task_name, BLRM_TASK_NAME_LEN, "%s heartbeat", router->service->name);
        hktask_add(task_name, blr_check_last_master_event, router, router->heartbeat);

        break;
    }

    if (router->reconnect_pending)
    {
        blr_restart_master(router);
    }
    spinlock_acquire(&router->lock);
    router->active_logs = 0;
    spinlock_release(&router->lock);
    atomic_add(&router->handling_threads, -1);
}

/**
 * Build a MySQL query into a GWBUF that we can send to the master database
 *
 * The data is not written to @c dcb but the expected protocol state is fixed.
 *
 * @param   dcb         The DCB where this will be written
 * @param   query       The text of the query to send
 */
static GWBUF *
blr_make_query(DCB *dcb, char *query)
{
    GWBUF *buf;
    unsigned char *data;
    int len;

    if ((buf = gwbuf_alloc(strlen(query) + 5)) == NULL)
    {
        return NULL;
    }
    data = GWBUF_DATA(buf);
    len = strlen(query) + 1;
    encode_value(&data[0], len, 24);    // Payload length
    data[3] = 0;                // Sequence id
    // Payload
    data[4] = COM_QUERY;            // Command
    memcpy(&data[5], query, strlen(query));

    // This is hack to get the result set processing in order for binlogrouter
    MySQLProtocol *proto = (MySQLProtocol*)dcb->protocol;
    proto->current_command = MYSQL_COM_QUERY;

    return buf;
}

/**
 * Build a MySQL slave registration into a GWBUF that we can send to the
 * master database
 *
 * @param   router  The router instance
 * @return  A MySQL Replication registration message in a GWBUF structure
 */
static GWBUF *
blr_make_registration(ROUTER_INSTANCE *router)
{
    GWBUF *buf;
    unsigned char *data;
    int len = 18;    // Min size of COM_REGISTER_SLAVE payload
    int port = 3306;
    int hostname_len = 0;

    // Send router->set_slave_hostname
    if (router->set_slave_hostname && router->set_slave_hostname[0])
    {
        hostname_len = strlen(router->set_slave_hostname);
    }

    // Add hostname len
    len += hostname_len;

    if ((buf = gwbuf_alloc(len + MYSQL_HEADER_LEN)) == NULL)
    {
        return NULL;
    }

    data = GWBUF_DATA(buf);
    encode_value(&data[0], len, 24); // Payload length
    data[3] = 0;                     // Sequence ID
    data[4] = COM_REGISTER_SLAVE;    // Command
    encode_value(&data[5], router->serverid, 32);   // Slave Server ID

    // Point to hostname len offset
    data += 9;

    *data++ = hostname_len;               // Slave hostname length

    // Copy hostname
    if (hostname_len)
    {
        memcpy(data, router->set_slave_hostname, hostname_len);
    }

    // Point to user
    data += hostname_len;
    // Set empty user
    *data++ = 0;           // Slave username length
    // Set empty password
    *data++ = 0;           // Slave password length
    // Add port
    if (router->service->ports)
    {
        port = router->service->ports->port;
    }
    encode_value(&data[0], port, 16);               // Slave master port, 2 bytes
    encode_value(&data[2], 0, 32);                  // Replication rank, 4 bytes
    encode_value(&data[6], router->masterid, 32);   // Master server-id, 4 bytes

    // This is hack to get the result set processing in order for binlogrouter
    MySQLProtocol *proto = (MySQLProtocol*)router->master->protocol;
    proto->current_command = MYSQL_COM_REGISTER_SLAVE;

    return buf;
}


/**
 * Build a Binlog dump command into a GWBUF that we can send to the
 * master database
 *
 * @param   router  The router instance
 * @return  A MySQL Replication COM_BINLOG_DUMP message in a GWBUF structure
 */
static GWBUF *
blr_make_binlog_dump(ROUTER_INSTANCE *router)
{
    GWBUF *buf;
    unsigned char *data;
    int binlog_file_len = strlen(router->binlog_name);
    /* COM_BINLOG_DUMP needs 11 bytes + binlogname (terminating NULL is not required) */
    int len = 11 + binlog_file_len;

    if ((buf = gwbuf_alloc(len + 4)) == NULL)
    {
        return NULL;
    }
    data = GWBUF_DATA(buf);

    encode_value(&data[0], len, 24);       // Payload length
    data[3] = 0;                           // Sequence ID
    data[4] = COM_BINLOG_DUMP;             // Command
    encode_value(&data[5],
                 router->current_pos, 32); // binlog position

    /* With mariadb10 always ask for annotate rows events */
    if (router->mariadb10_compat)
    {
        /* set flag for annotate rows event request */
        encode_value(&data[9], BLR_REQUEST_ANNOTATE_ROWS_EVENT, 16);
    }
    else
    {
        encode_value(&data[9], 0, 16);      // No flag set
    }

    encode_value(&data[11],
                 router->serverid, 32);    // Server-id of MaxScale
    memcpy((char *)&data[15], router->binlog_name,
           binlog_file_len);               // binlog filename

    // This is hack to get the result set processing in order for binlogrouter
    MySQLProtocol *proto = (MySQLProtocol*)router->master->protocol;
    proto->current_command = MYSQL_COM_BINLOG_DUMP;

    return buf;
}


/**
 * Encode a value into a number of bits in a MySQL packet
 *
 * @param   data    Point to location in target packet
 * @param   value   The value to pack
 * @param   len Number of bits to encode value into
 */
void
encode_value(unsigned char *data, unsigned int value, int len)
{
    while (len > 0)
    {
        *data++ = value & 0xff;
        value >>= 8;
        len -= 8;
    }
}

/**
 * Check that the stored event checksum matches the calculated checksum
 */
static bool verify_checksum(ROUTER_INSTANCE *router, size_t len, uint8_t *ptr)
{
    bool rval = true;
    uint32_t offset = MYSQL_HEADER_LEN + 1;
    uint32_t size = len - (offset + MYSQL_CHECKSUM_LEN);

    uint32_t checksum = crc32(0L, ptr + offset, size);
    uint32_t pktsum = EXTRACT32(ptr + offset + size);

    if (pktsum != checksum)
    {
        rval = false;
        MXS_ERROR("%s: Checksum error in event from master, "
                  "binlog %s @ %lu. Closing master connection.",
                  router->service->name, router->binlog_name,
                  router->current_pos);
        router->stats.n_badcrc++;
    }

    return rval;
}

/**
 * @brief Reset router errors
 *
 * @param router Router instance
 * @param hdr Replication header
 */
static void reset_errors(ROUTER_INSTANCE *router, REP_HEADER *hdr)
{
    spinlock_acquire(&router->lock);

    /* set mysql errno to 0 */
    router->m_errno = 0;

    /* Remove error message */
    if (router->m_errmsg)
    {
        MXS_FREE(router->m_errmsg);
    }
    router->m_errmsg = NULL;

    spinlock_release(&router->lock);
#ifdef SHOW_EVENTS
    printf("blr: len %lu, event type 0x%02x, flags 0x%04x, "
           "event size %d, event timestamp %lu\n",
           (unsigned long)len - 4,
           hdr->event_type,
           hdr->flags,
           hdr->event_size,
           (unsigned long)hdr->timestamp);
#endif
}

/**
 * blr_handle_binlog_record - we have received binlog records from
 * the master and we must now work out what to do with them.
 *
 * @param router    The router instance
 * @param pkt       The binlog records
 */
void
blr_handle_binlog_record(ROUTER_INSTANCE *router, GWBUF *pkt)
{
    uint8_t *msg = NULL, *ptr;
    REP_HEADER hdr;
    unsigned int len = 0;
    int prev_length = -1;
    int n_bufs = -1, pn_bufs = -1;
    int check_packet_len;
    int semisync_bytes;
    int semi_sync_send_ack = 0;

    /*
     * Loop over all the packets while we still have some data
     * and the packet length is enough to hold a replication event
     * header.
     */
    while (pkt)
    {
        ptr = GWBUF_DATA(pkt);
        len = gw_mysql_get_byte3(ptr);
        semisync_bytes = 0;

        /*
         * ptr now points at the current message in a contiguous buffer,
         * this buffer is either within the GWBUF or in a malloc'd
         * copy if the message straddles GWBUF's.
         */

        if (len < BINLOG_EVENT_HDR_LEN && router->master_event_state != BLR_EVENT_ONGOING)
        {
            char *event_msg = "unknown";

            /* Packet is too small to be a binlog event */
            if (ptr[4] == 0xfe) /* EOF Packet */
            {
                event_msg = "end of file";
            }
            else if (ptr[4] == 0xff)    /* EOF Packet */
            {
                event_msg = "error";
            }
            MXS_NOTICE("Non-event message (%s) from master.", event_msg);
            gwbuf_free(pkt);
            break;
        }
        else
        {
            if (router->master_event_state == BLR_EVENT_DONE)
            {
                /** This is the start of a new event */
                spinlock_acquire(&router->lock);
                router->stats.n_binlogs++;
                router->stats.n_binlogs_ses++;
                spinlock_release(&router->lock);

                /* Check for semi-sync in event with OK byte[4]:
                 * move pointer 2 bytes ahead and set check_packet_len accordingly
                 */
                if (ptr[4] == 0 && router->master_semi_sync != MASTER_SEMISYNC_NOT_AVAILABLE &&
                    ptr[5] == BLR_MASTER_SEMI_SYNC_INDICATOR)
                {

                    check_packet_len = MASTER_BYTES_BEFORE_EVENT_SEMI_SYNC;
                    semi_sync_send_ack = ptr[6];

                    /* Extract header from the semi-sync event */
                    blr_extract_header_semisync(ptr, &hdr);

                    /** Remove the semi-sync bytes */
                    memmove(ptr, ptr + 2, 5);
                    ptr += 2;
                    semisync_bytes = 2;
                }
                else
                {
                    semi_sync_send_ack = 0;
                    check_packet_len = MASTER_BYTES_BEFORE_EVENT;

                    /* Extract header from thr event */
                    blr_extract_header(ptr, &hdr);
                }

                /* Sanity check */
                if (hdr.ok == 0)
                {
                    if (hdr.event_size != len - (check_packet_len - MYSQL_HEADER_LEN) &&
                        (hdr.event_size + (check_packet_len - MYSQL_HEADER_LEN)) < MYSQL_PACKET_LENGTH_MAX)
                    {
                        MXS_ERROR("Packet length is %d, but event size is %d, "
                                  "binlog file %s position %lu, "
                                  "length of previous event %d.",
                                  len, hdr.event_size,
                                  router->binlog_name,
                                  router->current_pos,
                                  prev_length);

                        blr_log_packet(LOG_ERR, "Packet:", ptr, len);

                        MXS_ERROR("This event (0x%x) was contained in %d GWBUFs, "
                                  "the previous events was contained in %d GWBUFs",
                                  router->lastEventReceived, n_bufs, pn_bufs);

                        if (msg)
                        {
                            MXS_FREE(msg);
                            /* msg = NULL; Not needed unless msg will be referred to again */
                        }

                        break;
                    }

                    /**
                     * This is the first (and possibly last) packet of a replication
                     * event. We store the header in case the event is large and
                     * it is transmitted over multiple network packets.
                     */
                    router->master_event_state = BLR_EVENT_STARTED;
                    memcpy(&router->stored_header, &hdr, sizeof(hdr));
                    reset_errors(router, &hdr);
                }
                else
                {
                    /* Terminate replication and exit from main loop */
                    blr_terminate_master_replication(router, ptr, len);

                    gwbuf_free(pkt);
                    pkt = NULL;

                    break;
                }
            }
            else
            {
                /** We're processing a multi-packet replication event */
                ss_dassert(router->master_event_state == BLR_EVENT_ONGOING);
            }

            /** Gather the event into one big buffer */
            GWBUF *part = gwbuf_split(&pkt, len + MYSQL_HEADER_LEN);

            if (semisync_bytes)
            {
                /** Consume the two semi-sync bytes */
                part = gwbuf_consume(part, semisync_bytes);
            }

            ss_dassert(router->master_event_state == BLR_EVENT_STARTED ||
                       router->master_event_state == BLR_EVENT_ONGOING);

            if (router->master_event_state == BLR_EVENT_ONGOING)
            {
                /**
                 * Consume the network header so that we can append the raw
                 * event data to the original buffer. This allows both checksum
                 * calculations and encryption to process it as a contiguous event
                 */
                part = gwbuf_consume(part, MYSQL_HEADER_LEN);
            }

            router->stored_event = gwbuf_append(router->stored_event, part);

            if (len < MYSQL_PACKET_LENGTH_MAX)
            {
                /**
                 * This is either the only packet for the event or the last
                 * packet in a series for this event. The buffer now contains
                 * the network header of the first packet (4 bytes) and one OK byte.
                 * The semi-sync bytes are always consumed at an earlier stage.
                 */
                ss_dassert(router->master_event_state != BLR_EVENT_DONE);

                if (router->master_event_state != BLR_EVENT_STARTED)
                {
                    /**
                     * This is not the first packet for this event. We must use
                     * the stored header.
                     */
                    memcpy(&hdr, &router->stored_header, sizeof(hdr));
                }

                /** The event is now complete */
                router->master_event_state = BLR_EVENT_DONE;
            }
            else
            {
                /**
                 * This packet is a part of a series of packets that contain an
                 * event larger than MYSQL_PACKET_LENGTH_MAX bytes.
                 *
                 * For each partial event chunk, we remove the network header and
                 * append it to router->stored_event. The first event is an
                 * exception to this and it is appended as-is with the network
                 * header and the extra OK byte.
                 */
                ss_dassert(len == MYSQL_PACKET_LENGTH_MAX);
                router->master_event_state = BLR_EVENT_ONGOING;
                continue;
            }

            /**
             *  We now have the complete event in one contiguous buffer:
             *  router->master_event_state is BLR_EVENT_DONE
             */
            router->stored_event = gwbuf_make_contiguous(router->stored_event);
            MXS_ABORT_IF_NULL(router->stored_event);

            ptr = GWBUF_DATA(router->stored_event);

            /**
             * len is now the length of the complete event plus 4 bytes of network
             * header and one OK byte. Semi-sync bytes are never stored.
             */
            len = gwbuf_length(router->stored_event);

            /**
             * If checksums are enabled, verify that the stored checksum
             * matches the one we calculated
             */
            if (router->master_chksum && !verify_checksum(router, len, ptr))
            {
                MXS_FREE(msg);
                blr_master_close(router);
                blr_master_delayed_connect(router);
                return;
            }

            if (hdr.ok == 0)
            {
                router->lastEventReceived = hdr.event_type;
                router->lastEventTimestamp = hdr.timestamp;

                /**
                 * Check for an open transaction, if the option is set
                 * Only complete transactions should be sent to sleves
                 *
                 * If a trasaction is pending router->binlog_position
                 * won't be updated to router->current_pos
                 */

                spinlock_acquire(&router->binlog_lock);
                if (router->trx_safe == 0 || (router->trx_safe && router->pending_transaction == BLRM_NO_TRANSACTION))
                {
                    /* no pending transaction: set current_pos to binlog_position */
                    router->binlog_position = router->current_pos;
                    router->current_safe_event = router->current_pos;
                }
                spinlock_release(&router->binlog_lock);

                /**
                 * Detect transactions in events
                 * Only complete transactions should be sent to sleves
                 */

                /**
                 * If MariaDB 10 compatibility:
                 * check for MARIADB10_GTID_EVENT with flags = 0
                 * This marks the transaction starts instead of
                 * QUERY_EVENT with "BEGIN"
                 */
                if (router->trx_safe)
                {
                    if (router->mariadb10_compat)
                    {
                        if (hdr.event_type == MARIADB10_GTID_EVENT)
                        {
                            uint64_t n_sequence;
                            uint32_t domainid;
                            unsigned int flags;
                            n_sequence = extract_field(ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN, 64);
                            domainid = extract_field(ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 8, 32);
                            flags = *(ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 8 + 4);

                            if ((flags & (MARIADB_FL_DDL | MARIADB_FL_STANDALONE)) == 0)
                            {
                                spinlock_acquire(&router->binlog_lock);

                                if (router->pending_transaction > 0)
                                {
                                    MXS_ERROR("A MariaDB 10 transaction "
                                              "is already open "
                                              "@ %lu (GTID %u-%u-%lu) and "
                                              "a new one starts @ %lu",
                                              router->binlog_position,
                                              domainid, hdr.serverid,
                                              n_sequence,
                                              router->current_pos);

                                    // An action should be taken here
                                }

                                router->pending_transaction = BLRM_TRANSACTION_START;

                                spinlock_release(&router->binlog_lock);
                            }
                        }
                    }

                    /**
                     * look for QUERY_EVENT [BEGIN / COMMIT] and XID_EVENT
                     */

                    if (hdr.event_type == QUERY_EVENT)
                    {
                        char *statement_sql;
                        int db_name_len, var_block_len, statement_len;
                        db_name_len = ptr[MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4];
                        var_block_len = ptr[MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4 + 1 + 2];

                        statement_len = len - (MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4 + 1 + 2 + 2 \
                                               + var_block_len + 1 + db_name_len);
                        statement_sql = MXS_CALLOC(1, statement_len + 1);
                        MXS_ABORT_IF_NULL(statement_sql);
                        memcpy(statement_sql,
                               (char *)ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4 + 1 + 2 + 2 \
                               + var_block_len + 1 + db_name_len,
                               statement_len);

                        spinlock_acquire(&router->binlog_lock);

                        /* Check for BEGIN (it comes for START TRANSACTION too) */
                        if (strncmp(statement_sql, "BEGIN", 5) == 0)
                        {
                            if (router->pending_transaction > BLRM_NO_TRANSACTION)
                            {
                                MXS_ERROR("A transaction is already open "
                                          "@ %lu and a new one starts @ %lu",
                                          router->binlog_position,
                                          router->current_pos);

                                // An action should be taken here
                            }

                            router->pending_transaction = BLRM_TRANSACTION_START;
                        }

                        /* Check for COMMIT in non transactional store engines */
                        if (strncmp(statement_sql, "COMMIT", 6) == 0)
                        {
                            router->pending_transaction = BLRM_COMMIT_SEEN;
                        }

                        spinlock_release(&router->binlog_lock);

                        MXS_FREE(statement_sql);
                    }

                    /* Check for COMMIT in Transactional engines, i.e InnoDB */
                    if (hdr.event_type == XID_EVENT)
                    {
                        spinlock_acquire(&router->binlog_lock);

                        if (router->pending_transaction)
                        {
                            router->pending_transaction = BLRM_XID_EVENT_SEEN;
                        }
                        spinlock_release(&router->binlog_lock);
                    }
                }

               /**
                 * Check Event Type limit:
                 * If supported, gather statistics about
                 * the replication event types
                 * else stop replication from master
                 */
                int event_limit = router->mariadb10_compat ?
                                  MAX_EVENT_TYPE_MARIADB10 : MAX_EVENT_TYPE;

                if (hdr.event_type <= event_limit)
                {
                    router->stats.events[hdr.event_type]++;
                }
                else
                {
                    char errmsg[BINLOG_ERROR_MSG_LEN + 1];
                    sprintf(errmsg,
                            "Event type [%d] not supported yet. "
                            "Check master server configuration and "
                            "disable any new feature. "
                            "Replication from master has been stopped.",
                            hdr.event_type);
                    MXS_ERROR(errmsg);
                    gwbuf_free(pkt);
                    pkt = NULL;

                    spinlock_acquire(&router->lock);

                    /* Handle error messages */
                    char* old_errmsg = router->m_errmsg;
                    router->m_errmsg = MXS_STRDUP_A(errmsg);
                    router->m_errno = 1235;

                    /* Set state to stopped */
                    router->master_state = BLRM_SLAVE_STOPPED;
                    router->stats.n_binlog_errors++;

                    spinlock_release(&router->lock);

                    MXS_FREE(old_errmsg);

                    /* Stop replication */
                    blr_master_close(router);
                    return;
                }

                if (hdr.event_type == FORMAT_DESCRIPTION_EVENT && hdr.next_pos == 0)
                {
                    // Fake format description message
                    MXS_DEBUG("Replication fake event. "
                              "Binlog %s @ %lu.",
                              router->binlog_name,
                              router->current_pos);
                    router->stats.n_fakeevents++;

                    if (hdr.event_type == FORMAT_DESCRIPTION_EVENT)
                    {
                        uint8_t *new_fde;
                        unsigned int new_fde_len;
                        /*
                         * We need to save this to replay to new
                         * slaves that attach later.
                         */
                        new_fde_len = hdr.event_size;
                        new_fde = MXS_MALLOC(hdr.event_size);

                        if (new_fde)
                        {
                            memcpy(new_fde, ptr + MYSQL_HEADER_LEN + 1, hdr.event_size);
                            if (router->saved_master.fde_event)
                            {
                                MXS_FREE(router->saved_master.fde_event);
                            }
                            router->saved_master.fde_event = new_fde;
                            router->saved_master.fde_len = new_fde_len;
                        }
                    }
                }
                else
                {
                    if (hdr.event_type == HEARTBEAT_EVENT)
                    {
#ifdef SHOW_EVENTS
                        printf("Replication heartbeat\n");
#endif
                        MXS_DEBUG("Replication heartbeat. "
                                  "Binlog %s @ %lu.",
                                  router->binlog_name,
                                  router->current_pos);

                        router->stats.n_heartbeats++;

                        if (router->pending_transaction)
                        {
                            router->stats.lastReply = time(0);
                        }
                    }
                    else if (hdr.flags != LOG_EVENT_ARTIFICIAL_F)
                    {
                        if (hdr.event_type == ROTATE_EVENT)
                        {
                            spinlock_acquire(&router->binlog_lock);
                            router->rotating = 1;
                            spinlock_release(&router->binlog_lock);
                        }

                        uint32_t offset = MYSQL_HEADER_LEN + 1; // Skip header and OK byte

                        /**
                         * Write the raw event data to disk without the network
                         * header or the OK byte
                         */
                        if (blr_write_binlog_record(router, &hdr, len - offset, ptr + offset) == 0)
                        {
                            gwbuf_free(pkt);
                            blr_master_close(router);
                            blr_master_delayed_connect(router);
                            return;
                        }

                        /* Check for rotate event */
                        if (hdr.event_type == ROTATE_EVENT)
                        {
                            if (!blr_rotate_event(router, ptr + offset, &hdr))
                            {
                                gwbuf_free(pkt);
                                blr_master_close(router);
                                blr_master_delayed_connect(router);
                                return;
                            }
                        }

                        /* Handle semi-sync request from master */
                        if (router->master_semi_sync != MASTER_SEMISYNC_NOT_AVAILABLE &&
                            semi_sync_send_ack == BLR_MASTER_SEMI_SYNC_ACK_REQ)
                        {

                            MXS_DEBUG("%s: binlog record in file %s, pos %lu has "
                                      "SEMI_SYNC_ACK_REQ and needs a Semi-Sync ACK packet to "
                                      "be sent to the master server [%s]:%d",
                                      router->service->name, router->binlog_name,
                                      router->current_pos,
                                      router->service->dbref->server->name,
                                      router->service->dbref->server->port);

                            /* Send Semi-Sync ACK packet to master server */
                            blr_send_semisync_ack(router, hdr.next_pos);

                            /* Reset ACK sending */
                            semi_sync_send_ack = 0;
                        }

                        /**
                         * Distributing binlog events to slaves
                         * may depend on pending transaction
                         */

                        spinlock_acquire(&router->binlog_lock);

                        if (router->trx_safe == 0 || (router->trx_safe && router->pending_transaction == BLRM_NO_TRANSACTION))
                        {
                            router->binlog_position = router->current_pos;
                            router->current_safe_event = router->last_event_pos;

                            spinlock_release(&router->binlog_lock);

                            /* Notify clients events can be read */
                            blr_notify_all_slaves(router);
                        }
                        else
                        {
                            /**
                             * If transaction is closed:
                             *
                             * 1) Notify clients events can be read
                             *  from router->binlog_position
                             * 2) set router->binlog_position to
                             *    router->current_pos
                             */

                            if (router->pending_transaction > BLRM_TRANSACTION_START)
                            {
                                spinlock_release(&router->binlog_lock);

                                /* Notify clients events can be read */
                                blr_notify_all_slaves(router);

                                /* update binlog_position and set pending to 0 */
                                spinlock_acquire(&router->binlog_lock);

                                router->binlog_position = router->current_pos;
                                router->pending_transaction = BLRM_NO_TRANSACTION;

                                spinlock_release(&router->binlog_lock);
                            }
                            else
                            {
                                spinlock_release(&router->binlog_lock);
                            }
                        }
                    }
                    else
                    {
                        router->stats.n_artificial++;
                        MXS_DEBUG("Artificial event not written "
                                  "to disk or distributed. "
                                  "Type 0x%x, Length %d, Binlog "
                                  "%s @ %lu.",
                                  hdr.event_type,
                                  hdr.event_size,
                                  router->binlog_name,
                                  router->current_pos);
                        ptr += MYSQL_HEADER_LEN + 1;
                        if (hdr.event_type == ROTATE_EVENT)
                        {
                            spinlock_acquire(&router->binlog_lock);
                            router->rotating = 1;
                            spinlock_release(&router->binlog_lock);
                            if (!blr_rotate_event(router, ptr, &hdr))
                            {
                                gwbuf_free(pkt);
                                blr_master_close(router);
                                blr_master_delayed_connect(router);
                                return;
                            }
                        }
                    }
                }
            }
            else
            {
                blr_terminate_master_replication(router, ptr, len);
            }

            /** Finished processing the event */
            gwbuf_free(router->stored_event);
            router->stored_event = NULL;
        }

        if (msg)
        {
            MXS_FREE(msg);
            msg = NULL;
        }
    }

    blr_file_flush(router);
}

/**
 * Populate a header structure for a replication message from a GWBUF structure.
 *
 * @param pkt   The incoming packet in a GWBUF chain
 * @param hdr   The packet header to populate
 */
void
blr_extract_header(register uint8_t *ptr, register REP_HEADER *hdr)
{

    hdr->payload_len = EXTRACT24(ptr);
    hdr->seqno = ptr[3];
    hdr->ok = ptr[4];
    hdr->timestamp = EXTRACT32(&ptr[5]);
    hdr->event_type = ptr[9];
    hdr->serverid = EXTRACT32(&ptr[10]);
    hdr->event_size = EXTRACT32(&ptr[14]);
    hdr->next_pos = EXTRACT32(&ptr[18]);
    hdr->flags = EXTRACT16(&ptr[22]);
}

/**
 * Process a binlog rotate event.
 *
 * @param router    The instance of the router
 * @param ptr       The packet containing the rotate event
 * @param hdr       The replication message header
 * @return          1 if the file could be rotated, 0 otherwise.
 */
static int
blr_rotate_event(ROUTER_INSTANCE *router, uint8_t *ptr, REP_HEADER *hdr)
{
    int len, slen;
    uint64_t pos;
    char file[BINLOG_FNAMELEN + 1];

    ptr += 19;      // Skip event header
    len = hdr->event_size - 19; // Event size minus header
    pos = extract_field(ptr + 4, 32);
    pos <<= 32;
    pos |= extract_field(ptr, 32);
    slen = len - (8 + 4);       // Allow for position and CRC
    if (router->master_chksum == 0)
    {
        slen += 4;
    }
    if (slen > BINLOG_FNAMELEN)
    {
        slen = BINLOG_FNAMELEN;
    }
    memcpy(file, ptr + 8, slen);
    file[slen] = 0;

#ifdef VERBOSE_ROTATE
    printf("binlog rotate: ");
    while (len--)
    {
        printf("0x%02x ", *ptr++);
    }
    printf("\n");
    printf("New file: %s @ %ld\n", file, pos);
#endif

    strcpy(router->prevbinlog, router->binlog_name);

    int rotated = 1;
    int remove_encrytion_ctx = 0;

    if (strncmp(router->binlog_name, file, slen) != 0)
    {
        remove_encrytion_ctx = 1;
        router->stats.n_rotates++;
        if (blr_file_rotate(router, file, pos) == 0)
        {
            rotated = 0;
        }
    }
    spinlock_acquire(&router->binlog_lock);
    router->rotating = 0;

    /* remove current binlog encryption context */
    if (remove_encrytion_ctx == 1)
    {
        MXS_FREE(router->encryption_ctx);
        router->encryption_ctx = NULL;
    }
    spinlock_release(&router->binlog_lock);
    return rotated;
}

/**
 * Create the auth data needed to be able to call dcb_connect.
 *
 * This doesn't really belong here and should be moved at some stage.
 */
static void *
CreateMySQLAuthData(char *username, char *password, char *database)
{
    MYSQL_session *auth_info;

    if (username == NULL || password == NULL)
    {
        MXS_ERROR("You must specify both username and password for the binlog router.");
        return NULL;
    }

    if (strlen(username) > MYSQL_USER_MAXLEN)
    {
        MXS_ERROR("Provided user name %s is longer than maximum length %d.",
                  username, MYSQL_USER_MAXLEN);
        return NULL;
    }

    if (strlen(database) > MYSQL_DATABASE_MAXLEN)
    {
        MXS_ERROR("Provided database %s is longer than maximum length %d.",
                  database, MYSQL_DATABASE_MAXLEN);
        return NULL;
    }

    if ((auth_info = MXS_CALLOC(1, sizeof(MYSQL_session))) == NULL)
    {
        return NULL;
    }

    strcpy(auth_info->user, username);
    strcpy(auth_info->db, database);
    gw_sha1_str((const uint8_t *)password, strlen(password), auth_info->client_sha1);

    return auth_info;
}

/** Actions that can be taken when an event is being distributed to the slaves*/
typedef enum
{
    SLAVE_SEND_EVENT, /*< Send the event to the slave */
    SLAVE_FORCE_CATCHUP, /*< Force the slave into catchup mode */
    SLAVE_EVENT_ALREADY_SENT /*< The slave already has the event, don't send it */
} slave_event_action_t;

/**
 * Write a raw event (the first 40 bytes at most) to a log file
 *
 * @param priority The syslog priority of the message (LOG_ERR, LOG_WARNING, etc.)
 * @param msg      A textual message to write before the packet
 * @param ptr      Pointer to the message buffer
 * @param len      Length of message packet
 */
static void
blr_log_packet(int priority, char *msg, uint8_t *ptr, int len)
{
    char buf[400] = "";
    char *bufp;
    int i;

    bufp = buf;
    bufp += sprintf(bufp, "%s length = %d: ", msg, len);
    for (i = 0; i < len && i < 40; i++)
    {
        bufp += sprintf(bufp, "0x%02x ", ptr[i]);
    }
    if (i < len)
    {
        MXS_LOG_MESSAGE(priority, "%s...", buf);
    }
    else
    {
        MXS_LOG_MESSAGE(priority, "%s", buf);
    }
}

/**
 * Check if the master connection is in place and we
 * are downlaoding binlogs
 *
 * @param router    The router instance
 * @return non-zero if we are recivign binlog records
 */
int
blr_master_connected(ROUTER_INSTANCE *router)
{
    return router->master_state == BLRM_BINLOGDUMP;
}

/**
 * Extract a result value from the set of messages that make up a
 * MySQL response packet.
 *
 * @param buf   The GWBUF containing the response
 * @param col   The column number to return
 * @return  The result form the column or NULL. The caller must free the result
 */
char *
blr_extract_column(GWBUF *buf, int col)
{
    uint8_t *ptr;
    int len, ncol, collen;
    char    *rval;

    if (buf == NULL)
    {
        return NULL;
    }

    ptr = (uint8_t *)GWBUF_DATA(buf);
    /* First packet should be the column count */
    len = EXTRACT24(ptr);
    ptr += 3;
    if (*ptr != 1)      // Check sequence number is 1
    {
        return NULL;
    }
    ptr++;
    ncol = *ptr++;
    if (ncol < col)     // Not that many column in result
    {
        return NULL;
    }
    // Now ptr points at the column definition
    while (ncol-- > 0)
    {
        len = EXTRACT24(ptr);
        ptr += 4;   // Skip to payload
        ptr += len; // Skip over payload
    }
    // Now we should have an EOF packet
    len = EXTRACT24(ptr);
    ptr += 4;       // Skip to payload
    if (*ptr != 0xfe)
    {
        return NULL;
    }
    ptr += len;

    // Finally we have reached the row
    len = EXTRACT24(ptr);
    ptr += 4;

    /** The first EOF packet signals the start of the resultset rows and the second
     EOF packet signals the end of the result set. If the resultset
     contains a second EOF packet right after the first one, the result set is empty and
     contains no rows. */
    if (len == 5 && *ptr == 0xfe)
    {
        return NULL;
    }

    while (--col > 0)
    {
        collen = *ptr++;
        ptr += collen;
    }
    collen = *ptr++;
    if ((rval = MXS_MALLOC(collen + 1)) == NULL)
    {
        return NULL;
    }
    memcpy(rval, ptr, collen);
    rval[collen] = 0;       // NULL terminate

    return rval;
}

/**
 * Read a replication event form current opened binlog into a GWBUF structure.
 *
 * @param router    The router instance
 * @param pos       Position of binlog record to read
 * @param hdr       Binlog header to populate
 * @return      The binlog record wrapped in a GWBUF structure
 */
GWBUF
*blr_read_events_from_pos(ROUTER_INSTANCE *router,
                          unsigned long long pos,
                          REP_HEADER *hdr,
                          unsigned long long pos_end)
{
    unsigned long long end_pos = 0;
    uint8_t hdbuf[19];
    uint8_t *data;
    GWBUF *result;
    int n;
    int event_limit;

    /* Get current binnlog position */
    end_pos = pos_end;

    /* end of file reached, we're done */
    if (pos == end_pos)
    {
        return NULL;
    }

    /* error */
    if (pos > end_pos)
    {
        MXS_ERROR("Reading saved events, the specified pos %llu "
                  "is ahead of current pos %lu for file %s",
                  pos, router->current_pos, router->binlog_name);
        return NULL;
    }

    /* Read the event header information from the file */
    if ((n = pread(router->binlog_fd, hdbuf, 19, pos)) != 19)
    {
        switch (n)
        {
        case 0:
            MXS_DEBUG("Reading saved events: reached end of binlog file at %llu.", pos);
            break;
        case -1:
            {
                char err_msg[MXS_STRERROR_BUFLEN];
                MXS_ERROR("Reading saved events: failed to read binlog "
                          "file %s at position %llu"
                          " (%s).", router->binlog_name,
                          pos, strerror_r(errno, err_msg, sizeof(err_msg)));

                if (errno == EBADF)
                {
                    MXS_ERROR("Reading saved events: bad file descriptor for file %s"
                              ", descriptor %d.",
                              router->binlog_name, router->binlog_fd);
                }
            }
            break;
        default:
            MXS_ERROR("Reading saved events: short read when reading the header. "
                      "Expected 19 bytes but got %d bytes. "
                      "Binlog file is %s, position %llu",
                      n, router->binlog_name, pos);
            break;
        }

        return NULL;
    }

    hdr->timestamp = EXTRACT32(hdbuf);
    hdr->event_type = hdbuf[4];
    hdr->serverid = EXTRACT32(&hdbuf[5]);
    hdr->event_size = extract_field(&hdbuf[9], 32);
    hdr->next_pos = EXTRACT32(&hdbuf[13]);
    hdr->flags = EXTRACT16(&hdbuf[17]);

    event_limit = router->mariadb10_compat ? MAX_EVENT_TYPE_MARIADB10 : MAX_EVENT_TYPE;

    if (hdr->event_type > event_limit)
    {
        MXS_ERROR("Reading saved events: invalid event type 0x%x. "
                  "Binlog file is %s, position %llu",
                  hdr->event_type,
                  router->binlog_name, pos);
        return NULL;
    }

    if ((result = gwbuf_alloc(hdr->event_size)) == NULL)
    {
        MXS_ERROR("Reading saved events: failed to allocate memory for binlog entry, "
                  "size %d at %llu.",
                  hdr->event_size, pos);
        return NULL;
    }

    /* Copy event header*/
    data = GWBUF_DATA(result);
    memcpy(data, hdbuf, 19);

    /* Read event data and put int into buffer after header */
    if ((n = pread(router->binlog_fd, &data[19], hdr->event_size - 19, pos + 19)) != hdr->event_size - 19)
    {
        if (n == -1)
        {
            char err_msg[MXS_STRERROR_BUFLEN];
            MXS_ERROR("Reading saved events: the event at %llu in %s. "
                      "%s, expected %d bytes.",
                      pos, router->binlog_name,
                      strerror_r(errno, err_msg, sizeof(err_msg)), hdr->event_size - 19);
        }
        else
        {
            MXS_ERROR("Reading saved events: short read when reading "
                      "the event at %llu in %s. "
                      "Expected %d bytes got %d bytes.",
                      pos, router->binlog_name, hdr->event_size - 19, n);

            if (end_pos - pos < hdr->event_size)
            {
                MXS_ERROR("Reading saved events: binlog event "
                          "is close to the end of the binlog file, "
                          "current file size is %llu.", end_pos);
            }
        }

        /* free buffer */
        gwbuf_free(result);

        return NULL;
    }

    return result;
}

/**
 * Stop and start the master connection
 *
 * @param router    The router instance
 */
void
blr_stop_start_master(ROUTER_INSTANCE *router)
{
    if (router->master)
    {
        if (router->master->fd != -1 && router->master->state == DCB_STATE_POLLING)
        {
            blr_master_close(router);
        }
    }

    spinlock_acquire(&router->lock);

    router->master_state = BLRM_SLAVE_STOPPED;

    /* set last_safe_pos */
    router->last_safe_pos = router->binlog_position;

    /**
     * Set router->prevbinlog to router->binlog_name
     * The FDE event with current filename may arrive after STOP SLAVE is received
     */

    if (strcmp(router->binlog_name, router->prevbinlog) != 0)
    {
        strcpy(router->prevbinlog, router->binlog_name); // Same size
    }

    if (router->client)
    {
        if (router->client->fd != -1 && router->client->state == DCB_STATE_POLLING)
        {
            dcb_close(router->client);
            router->client = NULL;
        }
    }

    router->master_state = BLRM_UNCONNECTED;
    spinlock_release(&router->lock);

    blr_master_reconnect(router);
}

/**
 * The heartbeat check function called from the housekeeper.
 * We can try a new master connection if current one is seen out of date
 *
 * @param router    Current router instance
 */

static void
blr_check_last_master_event(void *inst)
{
    ROUTER_INSTANCE *router = (ROUTER_INSTANCE *)inst;
    int master_check = 1;
    int master_state =  BLRM_UNCONNECTED;
    char task_name[BLRM_TASK_NAME_LEN + 1] = "";

    spinlock_acquire(&router->lock);

    master_check = blr_check_heartbeat(router);

    master_state = router->master_state;

    spinlock_release(&router->lock);

    if (!master_check)
    {
        /*
         * stop current master connection
         * and try a new connection
         */
        blr_stop_start_master(router);
    }

    if ( (!master_check) || (master_state != BLRM_BINLOGDUMP) )
    {
        /*
         * Remove the task, it will be added again
         * when master state is back to BLRM_BINLOGDUMP
         * by blr_master_response()
         */
        snprintf(task_name, BLRM_TASK_NAME_LEN, "%s heartbeat", router->service->name);

        hktask_remove(task_name);
    }
}

/**
 * Check last heartbeat or last received event against router->heartbeat time interval
 *
 * checked interval is againts (router->heartbeat + BLR_NET_LATENCY_WAIT_TIME)
 * that is currently set to 1
 *
 * @param router    Current router instance
 * @return      0 if master connection must be closed and opened again, 1 otherwise
 */

int
blr_check_heartbeat(ROUTER_INSTANCE *router)
{
    time_t t_now = time(0);
    char *event_desc = NULL;

    if (router->master_state != BLRM_BINLOGDUMP)
    {
        return 1;
    }

    event_desc = blr_last_event_description(router);

    if (router->master_state == BLRM_BINLOGDUMP && router->lastEventReceived > 0)
    {
        if ((t_now - router->stats.lastReply) > (router->heartbeat + BLR_NET_LATENCY_WAIT_TIME))
        {
            MXS_ERROR("No event received from master [%s]:%d in heartbeat period (%lu seconds), "
                      "last event (%s %d) received %lu seconds ago. Assuming connection is dead "
                      "and reconnecting.",
                      router->service->dbref->server->name,
                      router->service->dbref->server->port,
                      router->heartbeat,
                      event_desc != NULL ? event_desc : "unknown",
                      router->lastEventReceived,
                      t_now - router->stats.lastReply);

            return 0;
        }
    }

    return 1;
}

/**
 * Log binlog router identy after master registration, state is BLRM_BINLOG_DUMP
 *
 * @param   router  The router instance
 */

static void blr_log_identity(ROUTER_INSTANCE *router)
{
    char *master_uuid;
    char *master_hostname;
    char *master_version;

    if (router->set_master_version)
    {
        master_version  = router->set_master_version;
    }
    else
    {
        master_version = blr_extract_column(router->saved_master.selectver, 1);
    }

    if (router->set_master_hostname)
    {
        master_hostname  = router->set_master_hostname;
    }
    else
    {
        master_hostname = blr_extract_column(router->saved_master.selecthostname, 1);
    }

    if (router->set_master_uuid)
    {
        master_uuid = router->master_uuid;
    }
    else
    {
        master_uuid = blr_extract_column(router->saved_master.uuid, 2);
    }

    /* Seen by the master */
    MXS_NOTICE("%s: identity seen by the master: "
               "Server_id: %d, Slave_UUID: %s, Host: %s",
               router->service->name,
               router->serverid,
               router->uuid == NULL ?
               "not available" :
               router->uuid,
               (router->set_slave_hostname && router->set_slave_hostname[0]) ?
               router->set_slave_hostname :
               "not set");

    /* Seen by the slaves */

    /* MariaDB 5.5 and MariaDB don't have the MASTER_UUID var */
    if (master_uuid == NULL)
    {
        MXS_NOTICE("%s: identity seen by the slaves: "
                   "server_id: %d, hostname: %s, MySQL version: %s",
                   router->service->name,
                   router->masterid, (master_hostname == NULL ? "not available" : master_hostname),
                   (master_version == NULL ? "not available" : master_version));
    }
    else
    {
        MXS_NOTICE("%s: identity seen by the slaves: "
                   "server_id: %d, uuid: %s, hostname: %s, MySQL version: %s",
                   router->service->name,
                   router->masterid, master_uuid,
                   (master_hostname == NULL ? "not available" : master_hostname),
                   (master_version == NULL ? "not available" : master_version));
    }
}

/**
 * @brief Write data into binlogs (incomplete event)
 *
 * Writes @c data_len bytes of data from @c buf into the current binlog being processed.
 *
 * @param router Router instance
 * @param data_len Number of bytes to write
 * @param buf Pointer where the data is read
 * @return Number of bytes written or 0 on error
 */
int
blr_write_data_into_binlog(ROUTER_INSTANCE *router, uint32_t data_len, uint8_t *buf)
{
    int n;

    if ((n = pwrite(router->binlog_fd, buf, data_len,
                    router->last_written)) != data_len)
    {
        char err_msg[MXS_STRERROR_BUFLEN];
        MXS_ERROR("%s: Failed to write binlog record at %lu of %s, %s. "
                  "Truncating to previous record.",
                  router->service->name, router->binlog_position,
                  router->binlog_name,
                  strerror_r(errno, err_msg, sizeof(err_msg)));

        /* Remove any partial event that was written */
        if (ftruncate(router->binlog_fd, router->binlog_position))
        {
            MXS_ERROR("%s: Failed to truncate binlog record at %lu of %s, %s. ",
                      router->service->name, router->last_written,
                      router->binlog_name,
                      strerror_r(errno, err_msg, sizeof(err_msg)));
        }
        return 0;
    }
    router->last_written += data_len;
    return n;
}

/**
 * Send a replication event packet to a slave
 *
 * The first replication event packet contains one byte set to either
 * 0x0, 0xfe or 0xff which signals what the state of the replication stream is.
 * If the data pointed by @c buf is not the start of the replication header
 * and part of the replication event is already sent, @c first must be set to
 * false so that the first status byte is not sent again.
 *
 * @param slave Slave where the packet is sent to
 * @param buf Buffer containing the data
 * @param len Length of the data
 * @param first If this is the first packet of a multi-packet event
 * @return True on success, false when memory allocation fails
 */
bool blr_send_packet(ROUTER_SLAVE *slave, uint8_t *buf, uint32_t len, bool first)
{
    bool rval = true;
    unsigned int datalen = len + (first ? 1 : 0);
    GWBUF *buffer = gwbuf_alloc(datalen + MYSQL_HEADER_LEN);
    if (buffer)
    {
        uint8_t *data = GWBUF_DATA(buffer);
        encode_value(data, datalen, 24);
        data += 3;
        *data++ = slave->seqno++;

        if (first)
        {
            *data++ = 0; // OK byte
        }

        if (len > 0)
        {
            memcpy(data, buf, len);
        }

        slave->stats.n_bytes += GWBUF_LENGTH(buffer);
        slave->dcb->func.write(slave->dcb, buffer);
    }
    else
    {
        MXS_ERROR("failed to allocate %u bytes of memory when writing an "
                  "event.", datalen + MYSQL_HEADER_LEN);
        rval = false;
    }
    return rval;
}

/**
 * Send a single replication event to a slave
 *
 * This sends the complete replication event to a slave. If the event size exceeds
 * the maximum size of a MySQL packet, it will be sent in multiple packets.
 *
 * @param role  What is the role of the caller, slave or master.
 * @param binlog_name The name of the binlogfile.
 * @param binlog_pos The position in the binlogfile.
 * @param slave Slave where the event is sent to
 * @param hdr   Replication header
 * @param buf   Pointer to the replication event as it was read from the disk
 * @return True on success, false if memory allocation failed
 */
bool blr_send_event(blr_thread_role_t role,
                    const char* binlog_name,
                    uint32_t binlog_pos,
                    ROUTER_SLAVE *slave,
                    REP_HEADER *hdr,
                    uint8_t *buf)
{
    bool rval = true;

    if ((strcmp(slave->lsi_binlog_name, binlog_name) == 0) &&
        (slave->lsi_binlog_pos == binlog_pos))
    {
        MXS_ERROR("Slave %s:%i, server-id %d, binlog '%s', position %u: "
                  "thread %lu in the role of %s could not send the event, "
                  "the event has already been sent by thread %lu in the role of %s. "
                  "%u bytes buffered for writing in DCB %p. %lu events received from master.",
                  slave->dcb->remote,
                  dcb_get_port(slave->dcb),
                  slave->serverid,
                  binlog_name,
                  binlog_pos,
                  thread_self(),
                  ROLETOSTR(role),
                  slave->lsi_sender_tid,
                  ROLETOSTR(slave->lsi_sender_role),
                  gwbuf_length(slave->dcb->writeq), slave->dcb,
                  slave->router->stats.n_binlogs);
        return false;
    }

    /** Check if the event and the OK byte fit into a single packet  */
    if (hdr->event_size + 1 < MYSQL_PACKET_LENGTH_MAX)
    {
        rval = blr_send_packet(slave, buf, hdr->event_size, true);
    }
    else
    {
        /** Total size of all the payloads in all the packets */
        int64_t len = hdr->event_size + 1;
        bool first = true;

        while (rval && len > 0)
        {
            uint64_t payload_len = first ? MYSQL_PACKET_LENGTH_MAX - 1 :
                                   MXS_MIN(MYSQL_PACKET_LENGTH_MAX, len);

            if (blr_send_packet(slave, buf, payload_len, first))
            {
                /** The check for exactly 0x00ffffff bytes needs to be done
                 * here as well */
                if (len == MYSQL_PACKET_LENGTH_MAX)
                {
                    blr_send_packet(slave, buf, 0, false);
                }

                /** Add the extra byte written by blr_send_packet */
                len -= first ? payload_len + 1 : payload_len;
                buf += payload_len;
                first = false;
            }
            else
            {
                rval = false;
            }
        }
    }

    slave->stats.n_events++;

    if (rval)
    {
        strcpy(slave->lsi_binlog_name, binlog_name);
        slave->lsi_binlog_pos = binlog_pos;
        slave->lsi_sender_role = role;
        slave->lsi_sender_tid = thread_self();
    }
    else
    {
        MXS_ERROR("Failed to send an event of %u bytes to slave at [%s]:%d.",
                  hdr->event_size, slave->dcb->remote,
                  dcb_get_port(slave->dcb));
    }
    return rval;
}

/**
 * Stop the slave connection and log errors
 *
 * @param router Router instance
 * @param ptr Pointer to the start of the packet
 * @param len Length of the packet
 */
static void blr_terminate_master_replication(ROUTER_INSTANCE* router, uint8_t* ptr, int len)
{
    unsigned long mysql_errno = extract_field(ptr + 5, 16);
    int msg_len = len - 7 - 6; // msg len is decreased by 7 and 6
    char *msg_err = (char *)MXS_MALLOC(msg_len + 1);
    MXS_ABORT_IF_NULL(msg_err);

    memcpy(msg_err, (char *)ptr + 7 + 6, msg_len);
    *(msg_err + msg_len) = '\0';

    spinlock_acquire(&router->lock);

    char* old_errmsg = router->m_errmsg;
    router->m_errmsg = msg_err;
    router->m_errno = mysql_errno;
    router->master_state = BLRM_SLAVE_STOPPED;
    router->stats.n_binlog_errors++;

    spinlock_release(&router->lock);

    MXS_FREE(old_errmsg);
    MXS_ERROR("Error packet in binlog stream.%s @ %lu.",
              router->binlog_name, router->current_pos);

}

/**
 * Populate a header structure for a replication message from a GWBUF structure with semi-sync enabled.
 *
 * @param pkt   The incoming packet in a GWBUF chain
 * @param hdr   The packet header to populate
 */
static void
blr_extract_header_semisync(register uint8_t *ptr, register REP_HEADER *hdr)
{

    hdr->payload_len = EXTRACT24(ptr);
    hdr->seqno = ptr[3];
    hdr->ok = ptr[4];
    /* Data available after 2 bytes (the 2 semisync bytes) */
    hdr->timestamp = EXTRACT32(&ptr[5 + 2]);
    hdr->event_type = ptr[9 + 2];
    hdr->serverid = EXTRACT32(&ptr[10 + 2]);
    hdr->event_size = EXTRACT32(&ptr[14 + 2]);
    hdr->next_pos = EXTRACT32(&ptr[18 + 2]);
    hdr->flags = EXTRACT16(&ptr[22 + 2]);
}

/**
 * Send a MySQL Replication Semi-Sync ACK to the master server.
 *
 * @param router The router instance.
 * @param pos The binlog position for the ACK reply.
 * @return 1 if the packect is sent, 0 on errors
 */

static int
blr_send_semisync_ack(ROUTER_INSTANCE *router, uint64_t pos)
{
    int seqno = 0;
    int semi_sync_flag = BLR_MASTER_SEMI_SYNC_INDICATOR;
    GWBUF   *buf;
    int     len;
    uint8_t *data;
    int     binlog_file_len = strlen(router->binlog_name);

    /* payload is: 1 byte semi-sync indicator + 8 bytes position + binlog name len */
    len = 1 + 8 + binlog_file_len;

    /* add network header to size */
    if ((buf = gwbuf_alloc(len + 4)) == NULL)
    {
        return 0;
    }

    data = GWBUF_DATA(buf);

    encode_value(&data[0], len, 24);  // Payload length
    data[3] = 0;                      // Sequence ID
    data[4] = semi_sync_flag;         // Semi-sync indicator

    /**
     * Next Bytes are: 8 bytes log position + len bin_log filename
     */

    /* Position */
    encode_value(&data[5], pos, 64);

    /* Binlog filename */
    memcpy((char *)&data[13], router->binlog_name, binlog_file_len);

    router->master->func.write(router->master, buf);

    return 1;
}

/**
 * Check the master semisync capability.
 *
 * @param buf The GWBUF data with master reply.
 * @return Semisync value: non available, enabled, disabled
 */
static int
blr_get_master_semisync(GWBUF *buf)
{
    char *key;
    char *val = NULL;
    int  master_semisync = MASTER_SEMISYNC_NOT_AVAILABLE;

    key = blr_extract_column(buf, 1);

    if (key && strlen(key))
    {
        val = blr_extract_column(buf, 2);
    }
    free(key);

    if (val)
    {
        if (strncasecmp(val, "ON", 4) == 0)
        {
            master_semisync = MASTER_SEMISYNC_ENABLED;
        }
        else
        {
            master_semisync = MASTER_SEMISYNC_DISABLED;
        }
    }
    free(val);

    return master_semisync;
}

/**
 * Notify all the registered slaves to read from binlog file
 * the new events just received
 *
 * @param   router      The router instance
 */
void blr_notify_all_slaves(ROUTER_INSTANCE *router)
{
    ROUTER_SLAVE *slave;
    int notified = 0;

    spinlock_acquire(&router->lock);
    slave = router->slaves;
    while (slave)
    {
        /* Notify a slave that has CS_WAIT_DATA bit set */
        if (slave->state == BLRS_DUMPING &&
            blr_notify_waiting_slave(slave))
        {
            notified++;
        }

        slave = slave->next;
    }
    spinlock_release(&router->lock);

    if (notified > 0)
    {
        MXS_DEBUG("Notified %d slaves about new data.", notified);
    }
}

/**
 * Set checksum value in router instance
 *
 * @param inst    The router instance
 * @param buf     The buffer with checksum value
 */
void blr_set_checksum(ROUTER_INSTANCE *inst, GWBUF *buf)
{
    if (buf)
    {
        char *val = blr_extract_column(buf, 1);
        if (val && strncasecmp(val, "NONE", 4) == 0)
        {
            inst->master_chksum = false;
        }
        if (val)
        {
            MXS_FREE(val);
        }
    }
}
