/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "blr.hh"

#include <inttypes.h>

#include <maxscale/alloc.h>

bool blr_handle_one_event(MXS_ROUTER* instance, REP_HEADER& hdr, uint8_t* ptr, uint32_t len, int semisync)
{
    ROUTER_INSTANCE* router = static_cast<ROUTER_INSTANCE*>(instance);
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
    if (router->trx_safe == 0
        || (router->trx_safe
            && router->pending_transaction.state == BLRM_NO_TRANSACTION))
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
        if (router->mariadb10_compat
            && hdr.event_type == MARIADB10_GTID_EVENT)
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
                snprintf(mariadb_gtid,
                         GTID_MAX_LEN,
                         "%u-%u-%lu",
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
            char* statement_sql;
            int db_name_len, var_block_len, statement_len;
            db_name_len = ptr[MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4];
            var_block_len = ptr[MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4 + 1 + 2];

            statement_len = len - (MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4 + 1 + 2 + 2 \
                                   + var_block_len + 1 + db_name_len);
            statement_sql =
                static_cast<char*>(MXS_CALLOC(1, statement_len + 1));
            MXS_ABORT_IF_NULL(statement_sql);
            memcpy(statement_sql,
                   (char*)ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4 + 1 + 2 + 2 \
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
            if (router->pending_transaction.state > BLRM_NO_TRANSACTION
                && router->pending_transaction.standalone)
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
        return false;
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

            uint32_t offset = MYSQL_HEADER_LEN + 1;     // Skip header and OK byte

            /**
             * Write the raw event data to disk without the network
             * header or the OK byte
             */
            if (blr_write_binlog_record(router, &hdr, len - offset, ptr + offset) == 0)
            {
                blr_master_close(router);
                blr_start_master_in_main(router);
                return false;
            }

            /* Check for rotate event */
            if (hdr.event_type == ROTATE_EVENT)
            {
                if (!blr_rotate_event(router, ptr + offset, &hdr))
                {
                    blr_master_close(router);
                    blr_start_master_in_main(router);
                    return false;
                }
            }

            /* Handle semi-sync request from master */
            if (router->master_semi_sync != MASTER_SEMISYNC_NOT_AVAILABLE
                && semisync == BLR_MASTER_SEMI_SYNC_ACK_REQ)
            {

                MXS_DEBUG("%s: binlog record in file %s, pos %lu has "
                          "SEMI_SYNC_ACK_REQ and needs a Semi-Sync ACK packet to "
                          "be sent to the master server [%s]:%d",
                          router->service->name,
                          router->binlog_name,
                          router->current_pos,
                          router->service->dbref->server->address,
                          router->service->dbref->server->port);

                /* Send Semi-Sync ACK packet to master server */
                blr_send_semisync_ack(router, hdr.next_pos);

                /* Reset ACK sending */
                semisync = 0;
            }

            /**
             * Distributing binlog events to slaves
             * may depend on pending transaction
             */

            spinlock_acquire(&router->binlog_lock);

            if (router->trx_safe == 0
                || (router->trx_safe
                    && router->pending_transaction.state == BLRM_NO_TRANSACTION))
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

                        if (router->mariadb10_compat
                            && router->mariadb10_gtid)
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
                    blr_master_close(router);
                    blr_start_master_in_main(router);
                    return false;
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

    return true;
}
