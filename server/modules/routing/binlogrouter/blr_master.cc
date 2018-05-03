/*lint -e662 */
/*lint -e661 */

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
 */

#include "blr.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/buffer.h>
#include <maxscale/dcb.h>
#include <maxscale/housekeeper.h>
#include <maxscale/log_manager.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/router.h>
#include <maxscale/routingworker.h>
#include <maxscale/server.h>
#include <maxscale/service.h>
#include <maxscale/session.h>
#include <maxscale/spinlock.h>
#include <maxscale/thread.h>
#include <maxscale/utils.h>

static GWBUF *blr_make_query(DCB *dcb, char *query);
static GWBUF *blr_make_registration(ROUTER_INSTANCE *router);
static GWBUF *blr_make_binlog_dump(ROUTER_INSTANCE *router);
void encode_value(unsigned char *data, unsigned int value, int len);
void blr_handle_binlog_record(ROUTER_INSTANCE *router, GWBUF *pkt);
static int blr_rotate_event(ROUTER_INSTANCE *router, uint8_t *pkt, REP_HEADER *hdr);
static void *CreateMySQLAuthData(const char *username,
                                 const char *password,
                                 const char *database);
void blr_extract_header(uint8_t *pkt, REP_HEADER *hdr);
static void blr_log_packet(int priority, const char *msg, uint8_t *ptr, int len);
void blr_master_close(ROUTER_INSTANCE *);
char *blr_extract_column(GWBUF *buf, int col);
static bool blr_check_last_master_event(void *inst);
extern int blr_check_heartbeat(ROUTER_INSTANCE *router);
static void blr_log_identity(ROUTER_INSTANCE *router);
static void blr_extract_header_semisync(uint8_t *pkt, REP_HEADER *hdr);
static int blr_send_semisync_ack (ROUTER_INSTANCE *router, uint64_t pos);
static int blr_get_master_semisync(GWBUF *buf);
static void blr_terminate_master_replication(ROUTER_INSTANCE *router,
                                             uint8_t* ptr,
                                             int len);
void blr_notify_all_slaves(ROUTER_INSTANCE *router);
extern bool blr_notify_waiting_slave(ROUTER_SLAVE *slave);
extern bool blr_save_mariadb_gtid(ROUTER_INSTANCE *inst);
static void blr_register_serverid(ROUTER_INSTANCE *router, GWBUF *buf);
static bool blr_register_heartbeat(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_setchecksum(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_getchecksum(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_handle_checksum(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_serveruuid(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_slaveuuid(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_utf8(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_selectversion(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_selectvercomment(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_selecthostname(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_selectmap(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_mxw_binlogvars(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_mxw_tables(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_getsemisync(ROUTER_INSTANCE *router, GWBUF *buf);
static bool blr_register_setsemisync(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_mxw_handlelowercase(ROUTER_INSTANCE *router,
                                             GWBUF *buf);
static void blr_register_send_command(ROUTER_INSTANCE *router,
                                      const char *command,
                                      unsigned int state);
static void blr_register_cache_response(ROUTER_INSTANCE *router,
                                        GWBUF **save_buf,
                                        const char *save_tag,
                                        GWBUF *in_buf);
static void blr_start_master_registration(ROUTER_INSTANCE *router, GWBUF *buf);
static void blr_register_mariadb_gtid_request(ROUTER_INSTANCE *router,
                                              GWBUF *buf);
static bool blr_handle_fake_rotate(ROUTER_INSTANCE *router,
                                   REP_HEADER *hdr,
                                   uint8_t *ptr);
static void blr_handle_fake_gtid_list(ROUTER_INSTANCE *router,
                                      REP_HEADER *hdr,
                                      uint8_t *ptr);
extern int blr_write_special_event(ROUTER_INSTANCE *router,
                                   uint32_t file_offset,
                                   uint32_t hole_size,
                                   REP_HEADER *hdr,
                                   int type);
extern int blr_file_new_binlog(ROUTER_INSTANCE *router, char *file);
static bool blr_handle_missing_files(ROUTER_INSTANCE *router,
                                     char *new_file);
static void worker_cb_start_master(int worker_id, void* data);
extern void blr_file_update_gtid(ROUTER_INSTANCE *router);
static int blr_check_connect_retry(ROUTER_INSTANCE *router);

static int keepalive = 1;

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
static void blr_start_master(void* data)
{
    ROUTER_INSTANCE *router = (ROUTER_INSTANCE*)data;
    ss_dassert(mxs_rworker_get_current() == mxs_rworker_get(MXS_RWORKER_MAIN));

    if (router->client)
    {
        dcb_close(router->client);
        router->client = NULL;
    }

    router->stats.n_binlogs_ses = 0;
    spinlock_acquire(&router->lock);

    if (router->master_state != BLRM_UNCONNECTED)
    {
        if (router->master_state != BLRM_SLAVE_STOPPED &&
            router->master_state != BLRM_CONNECTING)
        {
            MXS_ERROR("%s: Master Connect: Unexpected master state [%s]\n",
                      router->service->name,
                      blrm_states[router->master_state]);
        }
        else
        {
            MXS_NOTICE("%s: Master Connect: binlog current state is [%s]\n",
                       router->service->name,
                       blrm_states[router->master_state]);
        }

        /* Return only if state is not BLRM_CONNECTING */
        if (router->master_state != BLRM_CONNECTING)
        {
            spinlock_release(&router->lock);
            return;
        }
    }

    /* Check whether master connection can be started */
    int connect_retry;
    if ((connect_retry = blr_check_connect_retry(router)) == 0)
    {
        /* Force stopped state */
        router->master_state = BLRM_SLAVE_STOPPED;
        spinlock_release(&router->lock);

        MXS_ERROR("%s: failure while connecting to master server '%s', "
                  "reached %d maximum number of retries. "
                  "Replication is stopped.",
                  router->service->name,
                  router->service->dbref->server->name,
                  router->retry_limit);
        return;
    }

    /* Force connecting state */
    router->master_state = BLRM_CONNECTING;

    spinlock_release(&router->lock);

    DCB* client = dcb_alloc(DCB_ROLE_INTERNAL, NULL);

    /* Create fake 'client' DCB */
    if (client == NULL)
    {
        MXS_ERROR("failed to create DCB for dummy client");
        return;
    }
    router->client = client;

    /* Fake the client is reading */
    client->state = DCB_STATE_POLLING;  /* Fake the client is reading */

    /* Create MySQL Athentication from configured user/passwd */
    client->data = CreateMySQLAuthData(router->user, router->password, "");

    /* Create a session for dummy client DCB */
    if ((router->session = session_alloc(router->service, client)) == NULL)
    {
        MXS_ERROR("failed to create session for connection to master");
        return;
    }
    client->session = router->session;
    client->service = router->service;

    /**
     * 'client' is the fake DCB that emulates a client session:
     * we need to set the poll.thread.id for the "dummy client"
     */
    client->session->client_dcb->poll.thread.id = mxs_rworker_get_current_id();

    /* Connect to configured master server */
    if ((router->master = dcb_connect(router->service->dbref->server,
                                      router->session,
                                      BLR_PROTOCOL)) == NULL)
    {
        spinlock_acquire(&router->lock);
        router->retry_count++;
        spinlock_release(&router->lock);
        /* Set reconnection task */
        static const char master[] = "Master";
        char *name = (char *)MXS_MALLOC(strlen(router->service->name) + sizeof(master));
        if (name)
        {
            sprintf(name, "%s %s", router->service->name, master);
            hktask_add(name,
                       blr_start_master_in_main,
                       router,
                       connect_retry);
            MXS_FREE(name);
        }

        MXS_ERROR("%s: failure while connecting to master server '%s', "
                  "retrying in %d seconds",
                  router->service->name,
                  router->service->dbref->server->name,
                  connect_retry);
        return;
    }
    router->master->remote = MXS_STRDUP_A(router->service->dbref->server->address);
    router->master->service = router->service;

    MXS_NOTICE("%s: attempting to connect to master"
               " server [%s]:%d, binlog='%s', pos=%lu%s%s",
               router->service->name,
               router->service->dbref->server->address,
               router->service->dbref->server->port,
               router->binlog_name,
               router->current_pos,
               router->mariadb10_master_gtid ? ", GTID=" : "",
               router->mariadb10_master_gtid ? router->last_mariadb_gtid : "");

    router->connect_time = time(0);

    if (setsockopt(router->master->fd,
                   SOL_SOCKET,
                   SO_KEEPALIVE,
                   &keepalive,
                   sizeof(keepalive)))
    {
        perror("setsockopt");
    }

    router->master_state = BLRM_AUTHENTICATED;

    /**
     * Start the slave protocol registration phase.
     * This is the first step: SELECT UNIX_TIMESTAMP()
     *
     * Next states are handled by blr_master_response()
     */

    blr_register_send_command(router,
                              "SELECT UNIX_TIMESTAMP()",
                              BLRM_TIMESTAMP);

    router->stats.n_masterstarts++;
}

/**
 * Callback start function to be called in the context of the main worker.
 *
 * @param worker_id  The id of the worker in whose context the function is called.
 * @param data       The data to be passed to `blr_start_master`
 */
static void worker_cb_start_master(int worker_id, void* data)
{
    // This is itended to be called only in the main worker.
    ss_dassert(worker_id == 0);

    blr_start_master(data);
}

/**
 * Start master in the main Worker.
 *
 * @param data  Data intended for `blr_start_master`.
 */
bool blr_start_master_in_main(void* data)
{
    // The master should be connected to in the main worker, so we post it a
    // message and call `blr_start_master` there.

    MXS_WORKER* worker = mxs_rworker_get(MXS_RWORKER_MAIN); // The worker running in the main thread.
    ss_dassert(worker);

    intptr_t arg1 = (intptr_t)worker_cb_start_master;
    intptr_t arg2 = (intptr_t)data;

    if (!mxs_worker_post_message(worker, MXS_WORKER_MSG_CALL, arg1, arg2))
    {
        MXS_ERROR("Could not post 'blr_start_master' message to main worker.");
    }

    return false;
}

/**
 * Callback close function to be called in the context of the main worker.
 *
 * @param worker_id  The id of the worker in whose context the function is called.
 * @param data       The data to be passed to `blr_start_master`
 */
static void worker_cb_close_master(int worker_id, void* data)
{
    // This is itended to be called only in the main worker.
    ss_dassert(worker_id == 0);

    blr_master_close(static_cast<ROUTER_INSTANCE*>(data));
}

/**
 * Close master connection in the main Worker.
 *
 * @param data  Data intended for `blr_master_close`.
 */
void blr_close_master_in_main(void* data)
{
    // The master should be connected to in the main worker, so we post it a
    // message and call `blr_master_close` there.

    MXS_WORKER* worker = mxs_rworker_get(MXS_RWORKER_MAIN); // The worker running in the main thread.
    ss_dassert(worker);

    intptr_t arg1 = (intptr_t)worker_cb_close_master;
    intptr_t arg2 = (intptr_t)data;

    if (!mxs_worker_post_message(worker, MXS_WORKER_MSG_CALL, arg1, arg2))
    {
        MXS_ERROR("Could not post 'blr_master_close' message to main worker.");
    }
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
    /* Now it is safe to unleash other threads on this router instance */
    spinlock_acquire(&router->lock);
    router->reconnect_pending = 0;
    router->active_logs = 0;

    if (router->master_state < BLRM_BINLOGDUMP)
    {
        int connect_retry;
        if ((connect_retry = blr_check_connect_retry(router)) == 0)
        {
            /* Force stopped state */
            router->master_state = BLRM_SLAVE_STOPPED;
            spinlock_release(&router->lock);

            MXS_ERROR("%s: failed to connect to master server '%s', "
                      "reached %d maximum number of retries. "
                      "Replication is stopped.",
                      router->service->name,
                      router->service->dbref->server->name,
                      router->retry_limit);
            return;
        }

        /* Force unconnected state */
        router->master_state = BLRM_UNCONNECTED;
        router->retry_count++;
        spinlock_release(&router->lock);

        /* Set reconnection task */
        static const char master[] = "Master";
        char *name = (char *)MXS_MALLOC(strlen(router->service->name) + sizeof(master));

        if (name)
        {
            sprintf(name, "%s %s", router->service->name, master);
            hktask_add(name,
                       blr_start_master_in_main,
                       router,
                       connect_retry);
            MXS_FREE(name);

            MXS_ERROR("%s: failed to connect to master server '%s', "
                      "retrying in %d seconds",
                      router->service->name,
                      router->service->dbref->server->name,
                      connect_retry);
        }
    }
    else
    {
        /* Force connecting state */
        router->master_state = BLRM_CONNECTING;
        spinlock_release(&router->lock);

        blr_start_master_in_main(router);
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
    router->master = NULL;

    spinlock_acquire(&router->lock);
    if (router->master_state != BLRM_SLAVE_STOPPED)
    {
        router->master_state = BLRM_UNCONNECTED;
    }
    router->master_event_state = BLR_EVENT_DONE;
    spinlock_release(&router->lock);

    gwbuf_free(router->stored_event);
    router->stored_event = NULL;
}

/**
 * Mark this master connection for a delayed reconnect, used during
 * error recovery to cause a reconnect after router->retry_interval seconds.
 *
 * @param router    The router instance
 */
void
blr_master_delayed_connect(ROUTER_INSTANCE *router)
{
    static const char master[] = "Master Recovery";
    char *name = (char *)MXS_MALLOC(strlen(router->service->name) + sizeof(master));

    if (name)
    {
        sprintf(name, "%s %s", router->service->name, master);
        hktask_add(name,
                   blr_start_master_in_main,
                   router,
                   router->retry_interval);
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
        router->m_errmsg = msg_err ? msg_err : MXS_STRDUP("(memory failure)");

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

    // Start the Slave Protocol registration with Master server
    blr_start_master_registration(router, buf);

    // Check whether re-connect to master is needed
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

    if ((buf = gwbuf_alloc(strlen(query) + MYSQL_HEADER_LEN + 1)) == NULL)
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
    proto->current_command = MXS_COM_QUERY;

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
    proto->current_command = MXS_COM_REGISTER_SLAVE;

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

    if ((buf = gwbuf_alloc(len + MYSQL_HEADER_LEN)) == NULL)
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
    proto->current_command = MXS_COM_BINLOG_DUMP;

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
    printf("blr: event type 0x%02x, flags 0x%04x, "
           "event size %d, event timestamp %" PRIu32 "\n",
           hdr->event_type,
           hdr->flags,
           hdr->event_size,
           hdr->timestamp);
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
            const char *event_msg = "unknown";

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
                if (router->trx_safe == 0 ||
                    (router->trx_safe &&
                     router->pending_transaction.state == BLRM_NO_TRANSACTION))
                {
                    /* no pending transaction: set current_pos to binlog_position */
                    router->binlog_position = router->current_pos;
                    router->current_safe_event = router->current_pos;
                }
                spinlock_release(&router->binlog_lock);

                /**
                 * Detect transactions in events if trx_safe is set:
                 * Only complete transactions should be sent to sleves
                 *
                 * Now looking for:
                 * - QUERY_EVENT: BEGIN | START TRANSACTION | COMMIT
                 * - MariadDB 10 GTID_EVENT
                 * - XID_EVENT for transactional storage engines
                 */

                if (router->trx_safe)
                {
                    // MariaDB 10 GTID event check
                    if (router->mariadb10_compat &&
                        hdr.event_type == MARIADB10_GTID_EVENT)
                    {
                        /**
                         * If MariaDB 10 compatibility:
                         * check for MARIADB10_GTID_EVENT with flags:
                         * this is the TRASACTION START detection.
                         */

                        uint64_t n_sequence;
                        uint32_t domainid;
                        unsigned int flags;
                        n_sequence = extract_field(ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN, 64);
                        domainid = extract_field(ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 8, 32);
                        flags = *(ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 8 + 4);

                        spinlock_acquire(&router->binlog_lock);

                        /**
                        * Detect whether it's a standalone transaction:
                        * there is no terminating COMMIT event.
                        * i.e: a DDL or FLUSH TABLES etc
                        */
                        router->pending_transaction.standalone = flags & MARIADB_FL_STANDALONE;

                        /**
                         * Now mark the new open transaction
                         */
                        if (router->pending_transaction.state > BLRM_NO_TRANSACTION)
                        {
                            MXS_ERROR("A MariaDB 10 transaction "
                                      "is already open "
                                      "@ %lu (GTID %u-%u-%lu) and "
                                      "a new one starts @ %lu",
                                      router->binlog_position,
                                      domainid,
                                      hdr.serverid,
                                      n_sequence,
                                      router->current_pos);
                        }

                        router->pending_transaction.state = BLRM_TRANSACTION_START;

                        /* Handle MariaDB 10 GTID */
                        if (router->mariadb10_gtid)
                        {
                            char mariadb_gtid[GTID_MAX_LEN + 1];
                            snprintf(mariadb_gtid, GTID_MAX_LEN, "%u-%u-%lu",
                                     domainid,
                                     hdr.serverid,
                                     n_sequence);

                            MXS_DEBUG("MariaDB GTID received: (%s). Current file %s, pos %lu",
                                      mariadb_gtid,
                                      router->binlog_name,
                                      router->current_pos);

                            /* Save the pending GTID string value */
                            strcpy(router->pending_transaction.gtid, mariadb_gtid);
                            /* Save the pending GTID components */
                            router->pending_transaction.gtid_elms.domain_id = domainid;
                            /* This is the master id, no override */
                            router->pending_transaction.gtid_elms.server_id = hdr.serverid;
                            router->pending_transaction.gtid_elms.seq_no = n_sequence;
                        }

                        router->pending_transaction.start_pos = router->current_pos;
                        router->pending_transaction.end_pos = 0;

                        spinlock_release(&router->binlog_lock);
                    }

                    // Query Event check
                    if (hdr.event_type == QUERY_EVENT)
                    {
                        char *statement_sql;
                        int db_name_len, var_block_len, statement_len;
                        db_name_len = ptr[MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4];
                        var_block_len = ptr[MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4 + 1 + 2];

                        statement_len = len - (MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4 + 1 + 2 + 2 \
                                               + var_block_len + 1 + db_name_len);
                        statement_sql =
                            static_cast<char*>(MXS_CALLOC(1, statement_len + 1));
                        MXS_ABORT_IF_NULL(statement_sql);
                        memcpy(statement_sql,
                               (char *)ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4 + 1 + 2 + 2 \
                               + var_block_len + 1 + db_name_len,
                               statement_len);

                        spinlock_acquire(&router->binlog_lock);

                        /* Check for BEGIN (it comes for START TRANSACTION too) */
                        if (strncmp(statement_sql, "BEGIN", 5) == 0)
                        {
                            if (router->pending_transaction.state > BLRM_NO_TRANSACTION)
                            {
                                MXS_ERROR("A transaction is already open "
                                          "@ %lu and a new one starts @ %lu",
                                          router->binlog_position,
                                          router->current_pos);

                            }

                            router->pending_transaction.state = BLRM_TRANSACTION_START;
                            router->pending_transaction.start_pos = router->current_pos;
                            router->pending_transaction.end_pos = 0;
                        }

                        /* Check for COMMIT in non transactional store engines */
                        if (strncmp(statement_sql, "COMMIT", 6) == 0)
                        {
                            router->pending_transaction.state = BLRM_COMMIT_SEEN;
                        }

                        /**
                         * If it's a standalone transaction event we're done:
                         * this query event, only one, terminates the transaction.
                         */
                        if (router->pending_transaction.state > BLRM_NO_TRANSACTION &&
                            router->pending_transaction.standalone)
                        {
                            router->pending_transaction.state = BLRM_STANDALONE_SEEN;
                        }

                        spinlock_release(&router->binlog_lock);

                        MXS_FREE(statement_sql);
                    }

                    /* Check for COMMIT in Transactional engines, i.e InnoDB */
                    if (hdr.event_type == XID_EVENT)
                    {
                        spinlock_acquire(&router->binlog_lock);

                        if (router->pending_transaction.state >= BLRM_TRANSACTION_START)
                        {
                            router->pending_transaction.state = BLRM_XID_EVENT_SEEN;
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
                    MXS_ERROR("%s", errmsg);
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


                /*
                 * FORMAT_DESCRIPTION_EVENT with next_pos = 0
                 * should not be saved
                 */
                if (hdr.event_type == FORMAT_DESCRIPTION_EVENT && hdr.next_pos == 0)
                {
                    router->stats.n_fakeevents++;
                    MXS_DEBUG("Replication Fake FORMAT_DESCRIPTION_EVENT event. "
                              "Binlog %s @ %lu.",
                              router->binlog_name,
                              router->current_pos);
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

                        if (router->pending_transaction.state > BLRM_NO_TRANSACTION)
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
                                      router->service->dbref->server->address,
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

                        if (router->trx_safe == 0 ||
                            (router->trx_safe &&
                             router->pending_transaction.state == BLRM_NO_TRANSACTION))
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
                             *    from router->binlog_position
                             * 2) Update last seen MariaDB 10 GTID
                             * 3) set router->binlog_position to
                             *    router->current_pos
                             */

                            if (router->pending_transaction.state > BLRM_TRANSACTION_START)
                            {
                                if (router->mariadb10_compat)
                                {
                                    /**
                                     * The transaction has been saved.
                                     * this poins to end of binlog:
                                     * i.e. the position of a new event
                                     */
                                    router->pending_transaction.end_pos = router->current_pos;

                                    if (router->mariadb10_compat &&
                                        router->mariadb10_gtid)
                                    {
                                        /* Update last seen MariaDB GTID */
                                        strcpy(router->last_mariadb_gtid,
                                               router->pending_transaction.gtid);
                                        /**
                                         * Save MariaDB GTID into repo
                                         */
                                        blr_save_mariadb_gtid(router);
                                    }
                                }

                                spinlock_release(&router->binlog_lock);

                                /* Notify clients events can be read */
                                blr_notify_all_slaves(router);

                                /* update binlog_position and set pending to NO_TRX */
                                spinlock_acquire(&router->binlog_lock);

                                router->binlog_position = router->current_pos;

                                /* Set no pending transaction and no standalone */
                                router->pending_transaction.state = BLRM_NO_TRANSACTION;
                                router->pending_transaction.standalone = false;

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
                        /**
                         * Here we handle Artificial event, the ones with
                         * LOG_EVENT_ARTIFICIAL_F hdr.flags
                         */
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

                        // Fake Rotate event is always sent as first packet from master
                        if (hdr.event_type == ROTATE_EVENT)
                        {
                            if (!blr_handle_fake_rotate(router, &hdr, ptr))
                            {
                                gwbuf_free(pkt);
                                blr_master_close(router);
                                blr_master_delayed_connect(router);
                                return;
                            }

                            MXS_INFO("Fake ROTATE_EVENT received: "
                                     "binlog file %s, pos %" PRIu64 "",
                                     router->binlog_name,
                                     router->current_pos);
                        }
                        else if (hdr.event_type == MARIADB10_GTID_GTID_LIST_EVENT)
                        {
                            /*
                             * MariaDB10 event:
                             * it could be sent as part of GTID registration
                             * before sending change data events.
                             */
                            blr_handle_fake_gtid_list(router, &hdr, ptr);
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

    ptr += BINLOG_EVENT_HDR_LEN;      // Skip event header
    len = hdr->event_size - BINLOG_EVENT_HDR_LEN; // Event size minus header
    pos = extract_field(ptr + 4, 32);
    pos <<= 32;
    pos |= extract_field(ptr, 32);
    slen = len - (8 + BINLOG_EVENT_CRC_SIZE); // Allow for position and CRC
    if (!router->master_chksum)
    {
        slen += BINLOG_EVENT_CRC_SIZE;
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

    /* Different file name in rotate event or missing current binlog file */
    if ((strncmp(router->binlog_name, file, slen) != 0) ||
        !blr_binlog_file_exists(router, NULL))
    {
        remove_encrytion_ctx = 1;
        router->stats.n_rotates++;
        if (blr_file_rotate(router, file, pos) == 0)
        {
            rotated = 0;
        }
    }
    else
    {
        /**
         * ROTATE_EVENT reports a binlog file which is the same
         * as router->binlog_name.
         *
         * If mariadb10_gtid is On, let's Add/Update into GTID repo:
         * this allows SHOW BINARY LOGS to list all files
         * including the ones without GTID events.
         */

        if (router->mariadb10_compat &&
            router->mariadb10_gtid)
        {
            blr_file_update_gtid(router);
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
CreateMySQLAuthData(const char *username, const char *password, const char *database)
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

    if ((auth_info =
         static_cast<MYSQL_session*>(MXS_CALLOC(1, sizeof(MYSQL_session)))) == NULL)
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
blr_log_packet(int priority, const char *msg, uint8_t *ptr, int len)
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
    if ((rval = static_cast<char*>(MXS_MALLOC(collen + 1))) == NULL)
    {
        return NULL;
    }
    memcpy(rval, ptr, collen);
    rval[collen] = 0;       // NULL terminate

    return rval;
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
        if (router->master->fd != -1 &&
            router->master->state == DCB_STATE_POLLING)
        {
            blr_close_master_in_main(router);
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
        if (router->client->fd != -1 &&
            router->client->state == DCB_STATE_POLLING)
        {
            // Is this dead code? dcb->fd for internal DCBs is always -1
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

static bool
blr_check_last_master_event(void *inst)
{
    bool rval = true;
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
        snprintf(task_name, BLRM_TASK_NAME_LEN,
                 "%s heartbeat",
                 router->service->name);

        rval = false;
    }

    return rval;
}

/**
 * Check last heartbeat or last received event against router->heartbeat time interval
 *
 * checked interval is againts (router->heartbeat + BLR_NET_LATENCY_WAIT_TIME)
 * that is currently set to 1
 *
 * @param router    Current router instance
 * @return          0 if master connection must be closed and opened again, 1 otherwise
 */

int
blr_check_heartbeat(ROUTER_INSTANCE *router)
{
    time_t t_now = time(0);
    const char *event_desc = NULL;

    if (router->master_state != BLRM_BINLOGDUMP)
    {
        return 1;
    }

    event_desc = blr_last_event_description(router);

    if (router->master_state == BLRM_BINLOGDUMP &&
        router->lastEventReceived > 0)
    {
        if (static_cast<unsigned long>(t_now - router->stats.lastReply) >
            (router->heartbeat + BLR_NET_LATENCY_WAIT_TIME))
        {
            MXS_ERROR("No event received from master [%s]:%d in heartbeat period (%lu seconds), "
                      "last event (%s %d) received %lu seconds ago. Assuming connection is dead "
                      "and reconnecting.",
                      router->service->dbref->server->address,
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
                   router->masterid,
                   (master_hostname == NULL ? "not available" : master_hostname),
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
                    router->last_written)) != static_cast<int64_t>(data_len))
    {
        MXS_ERROR("%s: Failed to write binlog record at %lu of %s, %s. "
                  "Truncating to previous record.",
                  router->service->name, router->binlog_position,
                  router->binlog_name,
                  mxs_strerror(errno));

        /* Remove any partial event that was written */
        if (ftruncate(router->binlog_fd, router->binlog_position))
        {
            MXS_ERROR("%s: Failed to truncate binlog record at %lu of %s, %s. ",
                      router->service->name, router->last_written,
                      router->binlog_name,
                      mxs_strerror(errno));
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
        MXS_SESSION_ROUTE_REPLY(slave->dcb->session, buffer);
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
                  (uint64_t)thread_self(),
                  ROLETOSTR(role),
                  (uint64_t)slave->lsi_sender_tid,
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
 * @param len Length of the packet payload
 */
static void blr_terminate_master_replication(ROUTER_INSTANCE* router,
                                             uint8_t* ptr,
                                             int len)
{
    // Point to errno: begin + 4 bytes header + 1 byte flag
    unsigned long mysql_errno = extract_field(ptr + 5, 16);
    // Error message starts at begin + 4 header + 1 flag + 2 bytes errno + 6 bytes status msg
    int err_msg_offset = 4 + 1 + 2 + 6;
    // Error message size is: len - (err_msg_offset - 4 bytes header)
    int msg_len = len - (err_msg_offset - 4);
    char *msg_err = (char *)MXS_MALLOC(msg_len + 1);
    MXS_ABORT_IF_NULL(msg_err);

    memcpy(msg_err, (char *)ptr + err_msg_offset, msg_len);
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
 * Populate a header structure for a replication messager
 * from a GWBUF structure with semi-sync enabled.
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
    if ((buf = gwbuf_alloc(len + MYSQL_HEADER_LEN)) == NULL)
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
    MXS_FREE(key);

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
    MXS_FREE(val);

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
 * Slave Protocol registration to Master:
 *
 * Handles previous reply from Master and sends
 * SET @master_heartbeat_period to master
 *
 * The statement is sent only if router->heartbeat > 0
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 * @return          True if hearbeat request has been sent
 *                  false otherwise (router->heartbeat = 0)
 */
static bool blr_register_heartbeat(ROUTER_INSTANCE *router, GWBUF *buf)
{
    char query[BLRM_SET_HEARTBEAT_QUERY_LEN];
    char *val = blr_extract_column(buf, 2);

    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.server_id,
                                "serverid",
                                buf);

    /**
     * Keep the original master server id
     * for any further reference.
     */
    router->orig_masterid = atoi(val);

    /**
     * Set router->masterid from master server-id
     * if it's not set by the config option
     */
    if (router->masterid == 0)
    {
        router->masterid = atoi(val);
    }
    MXS_FREE(val);

    /* Send Heartbeat request ony if router->heartbeat is set */
    if (router->heartbeat > 0)
    {
        // Prepare new registration message
        sprintf(query,
                "SET @master_heartbeat_period = %lu000000000",
                router->heartbeat);
        blr_register_send_command(router, query, BLRM_HBPERIOD);
    }

    return router->heartbeat != 0;
}

/**
 * Slave Protocol registration to Master:
 *
 * Handles previous reply from Master and sends
 * SET @master_binlog_checksum to master
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_setchecksum(ROUTER_INSTANCE *router, GWBUF *buf)
{
    /**
     * Response from master (set heartbeat reply) should be stored
     * only if router->heartbeat is set
     */
    if (router->heartbeat > 0)
    {
        blr_register_cache_response(router,
                                    &router->saved_master.heartbeat,
                                    "heartbeat",
                                    buf);
    }

    // New registration message
    blr_register_send_command(router,
                              "SET @master_binlog_checksum ="
                              " @@global.binlog_checksum",
                              BLRM_CHKSUM1);
}

/**
 * Slave Protocol registration to Master:
 *
 * Handles previous reply from Master and sends
 * SELECT @master_binlog_checksum to master
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_getchecksum(ROUTER_INSTANCE *router, GWBUF *buf)
{
    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.chksum1,
                                "chksum1",
                                buf);
    // New registration message
    blr_register_send_command(router,
                              "SELECT @master_binlog_checksum",
                              BLRM_CHKSUM2);
}

/**
 * Slave Protocol registration to Master:
 *
 * Handles the reply from Master which
 * contains the Binlog checksum algorithm in use:
 * NONE or CRC32.
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_handle_checksum(ROUTER_INSTANCE *router, GWBUF *buf)
{
    // Set checksum from master reply
    blr_set_checksum(router, buf);

    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.chksum2,
                                "chksum2",
                                buf);
}

/**
 * Slave Protocol registration to Master:
 *
 * Handles the reply from Master and
 * sends SHOW VARIABLES LIKE 'SERVER_UUID' to MySQL 5.6/5.7 Master
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_serveruuid(ROUTER_INSTANCE *router, GWBUF *buf)
{
    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.gtid_mode,
                                "gtidmode",
                                buf);
    // New registration message
    blr_register_send_command(router,
                              "SHOW VARIABLES LIKE 'SERVER_UUID'",
                              BLRM_MUUID);
}

/**
 * Slave Protocol registration to Master:
 *
 * Handles the SERVER_UUID reply from MySQL 5.6/5.7 Master
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_slaveuuid(ROUTER_INSTANCE *router, GWBUF *buf)
{
    char *key;
    char *val = NULL;
    char query[BLRM_MASTER_REGITRATION_QUERY_LEN + 1];

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

    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.uuid,
                                "uuid",
                                buf);
    // New registration message
    sprintf(query, "SET @slave_uuid='%s'", router->uuid);
    blr_register_send_command(router, query, BLRM_SUUID);
}

/**
 * Slave Protocol registration to Master:
 *
 * Sends SET NAMES utf8 to Master
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_utf8(ROUTER_INSTANCE *router, GWBUF *buf)
{
    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.setnames,
                                "setnames",
                                buf);
    // New registration message
    blr_register_send_command(router,
                              "SET NAMES utf8",
                              BLRM_UTF8);
}

/**
 * Slave Protocol registration to Master:
 *
 * Handles previous reply from Master and
 * sends SELECT VERSION() to Master
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_selectversion(ROUTER_INSTANCE *router, GWBUF *buf)
{
    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.select1,
                                "select1",
                                buf);
    // New registration message
    blr_register_send_command(router,
                              "SELECT VERSION()",
                              BLRM_SELECTVER);
}

/**
 * Slave Protocol registration to Master:
 *
 * Handles previous reply from Master and
 * sends SELECT @@version_comment to Master
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_selectvercomment(ROUTER_INSTANCE *router, GWBUF *buf)
{
    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.selectver,
                                "selectver",
                                buf);
    // New registration message
    blr_register_send_command(router,
                              "SELECT @@version_comment limit 1",
                              BLRM_SELECTVERCOM);
}

/**
 * Slave Protocol registration to Master:
 *
 * Handles previous reply from Master and
 * sends SELECT @@version_comment to Master
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_selecthostname(ROUTER_INSTANCE *router, GWBUF *buf)
{
    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.selectvercom,
                                "selectvercom",
                                buf);
    // New registration message
    blr_register_send_command(router,
                              "SELECT @@hostname",
                              BLRM_SELECTHOSTNAME);
}

/**
 * Slave Protocol registration to Master:
 *
 * Handles previous reply from Master and
 * sends SELECT @@max_allowed_packet to Master
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_selectmap(ROUTER_INSTANCE *router, GWBUF *buf)
{
    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.selecthostname,
                                "selecthostname",
                                buf);
    // New registration message
    blr_register_send_command(router,
                              "SELECT @@max_allowed_packet",
                              BLRM_MAP);
}

/**
 * Slave Protocol registration to Master (MaxWell compatibility):
 *
 * Handles previous reply from Master and
 * sends SELECT IF(@@global.log_bin ...) to Master
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_mxw_binlogvars(ROUTER_INSTANCE *router, GWBUF *buf)
{
    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.server_vars,
                                "server_vars",
                                buf);
    // New registration message
    blr_register_send_command(router,
                              "SELECT IF(@@global.log_bin, 'ON', 'OFF'), "
                              "@@global.binlog_format, @@global.binlog_row_image",
                              BLRM_BINLOG_VARS);
}

/**
 * Slave Protocol registration to Master (MaxWell compatibility):
 *
 * Handles previous reply from Master and
 * sends select @@lower_case_table_names to Master
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_mxw_tables(ROUTER_INSTANCE *router, GWBUF *buf)
{
    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.binlog_vars,
                                "binlog_vars",
                                buf);
    // New registration message
    blr_register_send_command(router,
                              "select @@lower_case_table_names",
                              BLRM_LOWER_CASE_TABLES);
}

/**
 * Slave protocol registration: check Master SEMI-SYNC replication
 *
 * Ask master server for Semi-Sync replication capability
 *
 * Note: Master server must have rpl_semi_sync_master plugin installed
 * in order to start the Semi-Sync replication
 *
 * @param router    Current router instance
 * @param buf       The GWBUF to fill with new request
 */
static void blr_register_getsemisync(ROUTER_INSTANCE *router, GWBUF *buf)
{
    MXS_NOTICE("%s: checking Semi-Sync replication capability for master server [%s]:%d",
               router->service->name,
               router->service->dbref->server->address,
               router->service->dbref->server->port);

    // New registration message
    blr_register_send_command(router,
                              "SHOW VARIABLES LIKE 'rpl_semi_sync_master_enabled'",
                              BLRM_CHECK_SEMISYNC);
}

/**
 * Slave protocol registration: handle SEMI-SYNC replication
 *
 * Get master semisync capability
 * and if installed start the SEMI-SYNC replication
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 * @return          True is semi-sync can be started or false
 */
static bool blr_register_setsemisync(ROUTER_INSTANCE *router, GWBUF *buf)
{
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
                       router->service->dbref->server->address,
                       router->service->dbref->server->port);

            /* Continue without semisync */
            router->master_state = BLRM_REQUEST_BINLOGDUMP;

            return false;
        }
        else
        {
            if (router->master_semi_sync == MASTER_SEMISYNC_DISABLED)
            {
                /* Installed but not enabled, right now */
                MXS_NOTICE("%s: master server [%s]:%d doesn't have semi_sync"
                           " enabled right now, Request Semi-Sync Replication anyway",
                           router->service->name,
                           router->service->dbref->server->address,
                           router->service->dbref->server->port);
            }
            else
            {
                /* Installed and enabled */
                MXS_NOTICE("%s: master server [%s]:%d has semi_sync enabled,"
                           " Requesting Semi-Sync Replication",
                           router->service->name,
                           router->service->dbref->server->address,
                           router->service->dbref->server->port);
            }

            /* Request semisync */
            blr_register_send_command(router,
                                      "SET @rpl_semi_sync_slave = 1",
                                      BLRM_REQUEST_SEMISYNC);
            return true;
        }
    }

    return false;
}

/**
 * Slave Protocol registration to Master (MaxWell compatibility):
 *
 * Handles previous reply from Master and
 * sets the state to BLRM_REGISTER_READY
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_mxw_handlelowercase(ROUTER_INSTANCE *router,
                                             GWBUF *buf)
{
    // Response from master should be stored
    blr_register_cache_response(router,
                                &router->saved_master.lower_case_tables,
                                "lower_case_tables",
                                buf);
    // Set the new state
    router->master_state = BLRM_REGISTER_READY;
}

/**
 * Slave Protocol registration to Master: generic send command
 *
 * Sends a SQL statement to Master server
 *
 * @param router     Current router instance
 * @param command    The SQL command to send
 * @param state      The next registration state value
 */
static void blr_register_send_command(ROUTER_INSTANCE *router,
                                      const char *command,
                                      unsigned int state)
{
    // Create MySQL protocol packet
    GWBUF *buf = blr_make_query(router->master, (char *)command);
    // Set the next registration phase state
    router->master_state = state;
    // Send the packet
    router->master->func.write(router->master, buf);
}

/**
 * Slave Protocol registration to Master:
 *
 * Saves previous reply from Master
 *
 * @param router      Current router instance
 * @param save_buf    The saved GWBUF to update
 * @param save_tag    Tag name for disk writing
 * @param in_buf      GWBUF with server reply to previous
 *                    registration command
 */
static void blr_register_cache_response(ROUTER_INSTANCE *router,
                                        GWBUF **save_buf,
                                        const char *save_tag,
                                        GWBUF *in_buf)
{
    if (*save_buf)
    {
        gwbuf_free(*save_buf);
    }
    // New value in memory
    *save_buf = in_buf;
    // New value saved to disk
    blr_cache_response(router, (char *)save_tag, in_buf);
}

/**
 * Slave Protocol registration to Master:
 *
 * Handling the registration process:
 *
 * Note: some phases are specific to MySQL 5.6/5.7,
 * others to MariaDB10.
 *
 * @param router      Current router instance
 * @param in_buf      GWBUF with previous phase server response
 */
static void blr_start_master_registration(ROUTER_INSTANCE *router, GWBUF *buf)
{
    switch (router->master_state)
    {
    case BLRM_TIMESTAMP:
        /**
         * Previous state was BLRM_TIMESTAMP
         * No need to save the server reply
         */
        gwbuf_free(buf);
        blr_register_send_command(router,
                                  "SHOW VARIABLES LIKE 'SERVER_ID'",
                                  BLRM_SERVERID);
        router->retry_count = 0;
        break;
    case BLRM_SERVERID:
        // If set heartbeat is not being sent, next state is BLRM_HBPERIOD
        if (blr_register_heartbeat(router, buf))
        {
            break;
        }
    case BLRM_HBPERIOD:
        blr_register_setchecksum(router, buf);
        break;
    case BLRM_CHKSUM1:
        blr_register_getchecksum(router, buf);
        break;
    case BLRM_CHKSUM2:
        // Set router->master_chksum based on server reply
        blr_register_handle_checksum(router, buf);
        // Next state is BLRM_MARIADB10 or BLRM_GTIDMODE
        {
            unsigned int state = router->mariadb10_compat ?
                                 BLRM_MARIADB10 :
                                 BLRM_GTIDMODE;
            const char *command = router->mariadb10_compat ?
                                  "SET @mariadb_slave_capability=4" :
                                  "SELECT @@GLOBAL.GTID_MODE";

            blr_register_send_command(router, command, state);
        }
        break;
    case BLRM_MARIADB10: // MariaDB10 Only
        // Save server response
        blr_register_cache_response(router,
                                    &router->saved_master.mariadb10,
                                    "mariadb10",
                                    buf);
        // Next state is BLRM_MARIADB10_GTID_DOMAIN or BLRM_LATIN1
        {
            /**
             * Always request "gtid_domain_id" to Master server
             * if MariaDB 10 Compatibilty is On
             */
            unsigned int state = router->mariadb10_compat ?
                                 BLRM_MARIADB10_GTID_DOMAIN :
                                 BLRM_LATIN1;
            const char *command = router->mariadb10_compat ?
                                  "SELECT @@GLOBAL.gtid_domain_id" :
                                  "SET NAMES latin1";
            blr_register_send_command(router, command, state);
        }
        break;
    case BLRM_MARIADB10_GTID_DOMAIN: // MariaDB10 Only
        {
            // Extract GTID domain
            char *val = blr_extract_column(buf, 1);
            // Store the Master GTID domain
            router->mariadb10_gtid_domain = atol(val);
            MXS_FREE(val);
            // Don't save the server response
            gwbuf_free(buf);
        }

        // Next state is BLRM_MARIADB10_REQUEST_GTID or BLRM_LATIN1
        if (!router->mariadb10_master_gtid)
        {
            blr_register_send_command(router,
                                      "SET NAMES latin1",
                                      BLRM_LATIN1);
        }
        else
        {
            blr_register_mariadb_gtid_request(router, buf);
        }
        break;
    case BLRM_MARIADB10_REQUEST_GTID: // MariaDB10 Only
        // Don't save GTID request
        gwbuf_free(buf);
        blr_register_send_command(router,
                                  "SET @slave_gtid_strict_mode=1",
                                  BLRM_MARIADB10_GTID_STRICT);
        break;
    case BLRM_MARIADB10_GTID_STRICT: // MariaDB10 Only
        // Don't save GTID strict
        gwbuf_free(buf);
        blr_register_send_command(router,
                                  "SET @slave_gtid_ignore_duplicates=1",
                                  BLRM_MARIADB10_GTID_NO_DUP);
        break;
    case BLRM_MARIADB10_GTID_NO_DUP: // MariaDB10 Only
        // Don't save GTID ignore
        gwbuf_free(buf);
        blr_register_send_command(router,
                                  "SET NAMES latin1",
                                  BLRM_LATIN1);
        break;
    case BLRM_GTIDMODE: // MySQL 5.6/5.7 only
        blr_register_serveruuid(router, buf);
        break;
    case BLRM_MUUID:    // MySQL 5.6/5.7 only
        blr_register_slaveuuid(router, buf);
        break;
    case BLRM_SUUID:    // MySQL 5.6/5.7 only
        // Save server response
        blr_register_cache_response(router,
                                    &router->saved_master.setslaveuuid,
                                    "ssuuid",
                                    buf);
        blr_register_send_command(router,
                                  "SET NAMES latin1",
                                  BLRM_LATIN1);
        break;
    case BLRM_LATIN1:
        blr_register_utf8(router, buf);
        break;
    case BLRM_UTF8:
        // Save server response
        blr_register_cache_response(router,
                                    &router->saved_master.utf8,
                                    "utf8",
                                    buf);
        // Next state is MAXWELL BLRM_RESULTS_CHARSET or BLRM_SELECT1
        {
            unsigned int state = router->maxwell_compat ?
                                 BLRM_RESULTS_CHARSET :
                                 BLRM_SELECT1;
            const char *command = router->maxwell_compat ?
                                  "SET character_set_results = NULL" :
                                  "SELECT 1";

            blr_register_send_command(router, command, state);
            break;
        }
    case BLRM_RESULTS_CHARSET: // MAXWELL only
        gwbuf_free(buf); // Discard server reply, don't save it
        blr_register_send_command(router,
                                  MYSQL_CONNECTOR_SQL_MODE_QUERY,
                                  BLRM_SQL_MODE);
        break;
    case BLRM_SQL_MODE: // MAXWELL only
        gwbuf_free(buf); // Discard server reply, don't save it
        blr_register_send_command(router,
                                  "SELECT 1",
                                  BLRM_SELECT1);
        break;
    case BLRM_SELECT1:
        blr_register_selectversion(router, buf);
        break;
    case BLRM_SELECTVER:
        blr_register_selectvercomment(router, buf);
        break;
    case BLRM_SELECTVERCOM:
        blr_register_selecthostname(router, buf);
        break;
    case BLRM_SELECTHOSTNAME:
        blr_register_selectmap(router, buf);
        break;
    case BLRM_MAP:
        // Save server response
        blr_register_cache_response(router,
                                    &router->saved_master.map,
                                    "map",
                                    buf);
        if (router->maxwell_compat)
        {
            blr_register_send_command(router,
                                      MYSQL_CONNECTOR_SERVER_VARS_QUERY,
                                      BLRM_SERVER_VARS);
            break;
        }
        else
        {
            // Continue: ready for the registration, nothing to write/read
            router->master_state = BLRM_REGISTER_READY;
        }
    case BLRM_SERVER_VARS: // MAXWELL only
        /**
         * This branch could be reached as fallthrough from BLRM_MAP
         * with new state BLRM_REGISTER_READY
         * Go ahead if maxwell_compat is not set
         */
        if (router->master_state == BLRM_SERVER_VARS && router->maxwell_compat)
        {
            blr_register_mxw_binlogvars(router, buf);
            break;
        }
    case BLRM_BINLOG_VARS: // MAXWELL only
        /**
         * This branch could be reached as fallthrough from BLRM_MAP
         * with new state BLRM_REGISTER_READY.
         * Go ahead if maxwell_compat is not set
         */
        if (router->master_state == BLRM_BINLOG_VARS && router->maxwell_compat)
        {
            blr_register_mxw_tables(router, buf);
            break;
        }
    case BLRM_LOWER_CASE_TABLES: // MAXWELL only
        /**
         * This branch could be reached as fallthrough from BLRM_MAP
         * with new state BLRM_REGISTER_READY.
         * Go ahead if maxwell_compat is not set
         */
        if (router->master_state == BLRM_LOWER_CASE_TABLES &&
            router->maxwell_compat)
        {
            blr_register_mxw_handlelowercase(router, buf);
            // Continue: ready for the registration, nothing to write/read
        }
    case BLRM_REGISTER_READY:
        // Prepare Slave registration request: COM_REGISTER_SLAVE
        buf = blr_make_registration(router);
        // Set new state
        router->master_state = BLRM_REGISTER;
        // Send the packet
        router->master->func.write(router->master, buf);
        break;
    case BLRM_REGISTER:
        /* discard master reply to COM_REGISTER_SLAVE */
        gwbuf_free(buf);

        /* if semisync option is set, check for master semi-sync availability */
        if (router->request_semi_sync)
        {
            blr_register_getsemisync(router, buf);
            break;
        }
        else
        {
            /* Continue */
            router->master_state = BLRM_REQUEST_BINLOGDUMP;
        }
    case BLRM_CHECK_SEMISYNC:
        /**
         * This branch could be reached as fallthrough from BLRM_REGISTER
         * if request_semi_sync option is false
         */
        if (router->master_state == BLRM_CHECK_SEMISYNC)
        {
            if (blr_register_setsemisync(router, buf))
            {
                break;
            }
        }
    case BLRM_REQUEST_SEMISYNC:
        /**
         * This branch could be reached as fallthrough from BLRM_REGISTER or
         * BLRM_CHECK_SEMISYNC if request_semi_sync option is false or
         * master doesn't support semisync or it's not enabled.
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
         * just after sending COM_REGISTER_SLAVE
         * if request_semi_sync option is false.
         */

        /**
         * End of registration process:
         *
         * Now request a dump of the binlog file: COM_BINLOG_DUMP
         */
        buf = blr_make_binlog_dump(router);
        router->master_state = BLRM_BINLOGDUMP;
        router->master->func.write(router->master, buf);

        if (router->binlog_name[0])
        {
            MXS_NOTICE("%s: Request binlog records from %s at "
                       "position %lu from master server [%s]:%d",
                       router->service->name, router->binlog_name,
                       router->current_pos,
                       router->service->dbref->server->address,
                       router->service->dbref->server->port);
        }

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
        if (router->heartbeat > 0)
        {
            char task_name[BLRM_TASK_NAME_LEN + 1] = "";
            snprintf(task_name,
                     BLRM_TASK_NAME_LEN,
                     "%s heartbeat",
                     router->service->name);
            hktask_add(task_name,
                       blr_check_last_master_event,
                       router,
                       router->heartbeat);
        }

        break;
    }
}

/**
 * Slave Protocol registration to Master (MariaDB 10 compatibility):
 *
 * Handles previous reply from MariaDB10 Master (GTID Domain ID) and
 * sends the SET @slave_connect_state='x-y-z' GTID registration.
 *
 * The next state is set to BLRM_MARIADB10_REQUEST_GTID
 *
 * @param router    Current router instance
 * @param buf       GWBUF with server reply to previous
 *                  registration command
 */
static void blr_register_mariadb_gtid_request(ROUTER_INSTANCE *router,
                                              GWBUF *buf)
{
    const char format_gtid_val[] = "SET @slave_connect_state='%s'";

    // SET the requested GTID
    char set_gtid[GTID_MAX_LEN + sizeof(format_gtid_val)];
    sprintf(set_gtid,
            format_gtid_val,
            router->last_mariadb_gtid);

    MXS_INFO("%s: Requesting GTID (%s) from master server.",
             router->service->name,
             router->last_mariadb_gtid);
    // Send the request
    blr_register_send_command(router,
                              set_gtid,
                              BLRM_MARIADB10_REQUEST_GTID);
}

/**
 * The routine hanldes a Fake ROTATE_EVENT
 *
 * It takes care of any missing file between
 * current file and the one in rotate event:
 * files with 4 bytes size colud be created.
 *
 * The filename specified in rotate event
 * is created/overwritten
 *
 * @param router    The router instance
 * @param hdr       The Replication event header
 * @param ptr       The packet data
 * @return          True for succesfull binlog file rotation,
 *                  False otherwise.
 */
static bool blr_handle_fake_rotate(ROUTER_INSTANCE *router,
                                   REP_HEADER *hdr,
                                   uint8_t *ptr)
{
    ss_dassert(hdr->event_type == ROTATE_EVENT);

    uint64_t pos;
    int len, slen;
    char file[BINLOG_FNAMELEN + 1];

    len = hdr->event_size - BINLOG_EVENT_HDR_LEN; // Event size minus header
    slen = len - (8 + BINLOG_EVENT_CRC_SIZE);     // Allow for position and CRC
    if (!router->master_chksum)
    {
        slen += BINLOG_EVENT_CRC_SIZE;
    }
    if (slen > BINLOG_FNAMELEN)
    {
        slen = BINLOG_FNAMELEN;
    }
    memcpy(file, ptr + BINLOG_EVENT_HDR_LEN + 8, slen);
    file[slen] = 0;

    pos = extract_field(ptr + BINLOG_EVENT_HDR_LEN + 4, 32);
    pos <<= 32;
    pos |= extract_field(ptr + BINLOG_EVENT_HDR_LEN, 32);

    MXS_DEBUG("Fake ROTATE_EVENT received: file %s, pos %" PRIu64
              ". Next event at pos %" PRIu32,
              file,
              pos,
              hdr->next_pos);
    /**
     * Detect any missing file in sequence.
     */
    if (!blr_handle_missing_files(router, file))
    {
        return false;
    }

    spinlock_acquire(&router->binlog_lock);

    /* Set writing pos to 4 if Master GTID */
    if (router->mariadb10_master_gtid && pos == 4)
    {
        /**
         * If a MariadB 10 Slave is connecting and reading the
         * events from this binlog file, the router->binlog_position check
         * might fail in blr_slave.c:blr_slave_binlog_dump()
         * and the slave connection will be closed.
         *
         * The slave will automatically try to re-connect.
         */
        router->last_written = BINLOG_MAGIC_SIZE;
        router->current_pos = BINLOG_MAGIC_SIZE;
        router->binlog_position = BINLOG_MAGIC_SIZE;
        router->last_event_pos = BINLOG_MAGIC_SIZE;
        router->current_safe_event = BINLOG_MAGIC_SIZE;
    }

    router->rotating = 1;

    spinlock_release(&router->binlog_lock);

    return blr_rotate_event(router, ptr, hdr);
}

/**
 * The routine hanldes a Fake GTID_LIST_EVENT, MariaDB 10 only.
 *
 * Fake MARIADB10_GTID_GTID_LIST_EVENT could be sent
 * when using GTID registration with MariaDB 10 server
 * The event header 'next_pos' tells where to write
 * the next event.
 * We set internal pointers to that position.
 *
 * @param router    The router instance
 * @param hdr       The Replication event header
 * @param ptr       The packet data
 */
static void blr_handle_fake_gtid_list(ROUTER_INSTANCE *router,
                                      REP_HEADER *hdr,
                                      uint8_t *ptr)
{
    ss_dassert(hdr->event_type == MARIADB10_GTID_GTID_LIST_EVENT);

    if (router->mariadb10_master_gtid)
    {
        uint64_t binlog_file_eof = lseek(router->binlog_fd, 0L, SEEK_END);

        MXS_INFO("Fake GTID_LIST received: file %s, pos %" PRIu64
                 ". Next event at pos %" PRIu32,
                 router->binlog_name,
                 router->current_pos,
                 hdr->next_pos);

        /**
         * We could write in any binlog file position:
         * fill any GAP with an ignorable event
         * if GTID_LIST next_pos is greter than current EOF
         */
        if (hdr->next_pos && (hdr->next_pos > binlog_file_eof))
        {
            uint64_t hole_size = hdr->next_pos - binlog_file_eof;

            MXS_INFO("Detected hole while processing"
                     " a Fake GTID_LIST Event: hole size will be %"
                     PRIu64 " bytes",
                     hole_size);

            /* Set the offet for the write routine */
            spinlock_acquire(&router->binlog_lock);

            router->last_written = binlog_file_eof;

            spinlock_release(&router->binlog_lock);

            // Write One Hole
            // TODO: write small holes
            blr_write_special_event(router,
                                    binlog_file_eof,
                                    hole_size,
                                    hdr,
                                    BLRM_IGNORABLE);
        }
        else
        {
            // Increment the internal offsets
            spinlock_acquire(&router->binlog_lock);

            router->last_written = hdr->next_pos;
            router->last_event_pos = router->current_pos;
            router->current_pos = hdr->next_pos;
            router->binlog_position = router->current_pos;
            router->current_safe_event = router->current_pos;

            spinlock_release(&router->binlog_lock);
        }
    }
}

/**
 * Detect any missing file in sequence between
 * current router->binlog_name and new_file
 * in fake ROTATE_EVENT.
 *
 * In case of missing files, new files with 4 bytes
 * will be created up to new_file.
 *
 * @param    router      The current router
 * @param    new_file    The filename in Fake ROTATE_EVENT
 * @return               true on success, false on errors
 */
static bool blr_handle_missing_files(ROUTER_INSTANCE *router,
                                     char *new_file)
{
    char *fptr;
    uint32_t new_fseqno;
    uint32_t curr_fseqno;
    char buf[BLRM_BINLOG_NAME_STR_LEN];
    char bigbuf[PATH_MAX + 1];

    if (*new_file &&
        (fptr = strrchr(new_file, '.')) == NULL)
    {
        return false;
    }
    if (router->fileroot)
    {
        MXS_FREE(router->fileroot);
    }
    /* set filestem */
    router->fileroot = MXS_STRNDUP_A(new_file, fptr - new_file);

    new_fseqno = atol(fptr + 1);

    if (!*router->binlog_name)
    {
        MXS_INFO("Fake ROTATE_EVENT comes with %s log file."
                 " Current router binlog file has not been set yet."
                 " Skipping creation of empty files"
                 " before sequence %" PRIu32 "",
                 new_file,
                 new_fseqno);
        return true;
    }

    if (*router->binlog_name &&
        (fptr = strrchr(router->binlog_name, '.')) == NULL)
    {
        return false;
    }
    curr_fseqno = atol(fptr + 1);
    int32_t delta_seq = new_fseqno - (curr_fseqno + 1);

    /**
     * Try creating delta_seq empty binlog files:
     *
     * Note: currenlty working for positive delta
     * and same filestem.
     */
    if (delta_seq > 0)
    {
        MXS_INFO("Fake ROTATE_EVENT comes with a %" PRIu32
                 " delta sequence in its name."
                 " Creating %" PRIi32 " empty files",
                 delta_seq,
                 delta_seq);

        // Create up to (delta_seq - 1) empty (with 4 bytes) binlog files
        for (int i = 1; i <= delta_seq; i++)
        {
            sprintf(buf, BINLOG_NAMEFMT, router->fileroot, curr_fseqno + i);
            if (!blr_file_new_binlog(router, buf))
            {
                return false;
            }
            else
            {
                MXS_INFO("Created empty binlog file [%d] '%s'"
                         " due to Fake ROTATE_EVENT file sequence delta.",
                         i,
                         buf);
            }
        }

        // Some files created, return true
        return true;
    }

    // Did nothing, just return true
    return true;
}

/**
 * Check the connection retry limit and increment
 * by BLR_MASTER_BACKOFF_TIME up to router->retry_interval.
 *
 * @param router    The current router instance
 * @return          The interval to use for next reconnect
 *                  or 0 if router->retry_limit has been hit.
 */
static int blr_check_connect_retry(ROUTER_INSTANCE *router)
{
    /* Stop reconnection to master */
    if (router->retry_count >= router->retry_limit)
    {
        return 0;
    }

    /* Return the interval for next reconnect */
    if (router->retry_count >= router->retry_interval / BLR_MASTER_BACKOFF_TIME)
    {
        return router->retry_interval;
    }
    else
    {
        return BLR_MASTER_BACKOFF_TIME * (1 + router->retry_count);
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
