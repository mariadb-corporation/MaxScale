/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/cdefs.h>
#include <maxbase/worker.h>
#include <maxscale/session.hh>

MXS_BEGIN_DECLS

// The worker ID of the "main" thread
#define MXS_RWORKER_MAIN -1

/**
 * Return the routing worker associated with the provided worker id.
 *
 * @param worker_id  A worker id. If MXS_RWORKER_MAIN is used, the
 *                   routing worker running in the main thread will
 *                   be returned.
 *
 * @return The corresponding routing worker instance, or NULL if the
 *         id does not correspond to a routing worker.
 */
MXB_WORKER* mxs_rworker_get(int worker_id);

/**
 * Return the current routing worker.
 *
 * @return A routing worker, or NULL if there is no current routing worker.
 */
MXB_WORKER* mxs_rworker_get_current();

/**
 * Return the id of the current routing worker.
 *
 * @return The id of the routing worker, or -1 if there is no current
 *         routing worker.
 */
int mxs_rworker_get_current_id();

/**
 * Broadcast a message to all routing workers.
 *
 * @param msg_id  The message id.
 * @param arg1    Message specific first argument.
 * @param arg2    Message specific second argument.
 *
 * @return The number of messages posted; if less that ne number of workers
 *         then some postings failed.
 *
 * @attention The return value tells *only* whether message could be posted,
 *            *not* that it has reached the worker.
 *
 * @attentsion Exactly the same arguments are passed to all workers. Take that
 *             into account if the passed data must be freed.
 *
 * @attention This function is signal safe.
 */
size_t mxs_rworker_broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2);

/**
 * Call a function on all workers
 *
 * A convenience function for executing simple tasks on all workers. The task
 * will be executed immediately on the current worker and thus recursive calls
 * into functions should not be done.
 *
 * @param cb Callback to call
 * @param data Data passed to the callback
 *
 * @return The number of messages posted; if less that ne number of workers
 *         then some postings failed.
 */
size_t mxs_rworker_broadcast(void (* cb)(void* data), void* data);

/**
 * Add a session to the current routing worker's session container. Currently
 * only required for some special commands e.g. "KILL <process_id>" to work.
 *
 * @param session Session to add.
 * @return true if successful, false if id already existed in map.
 */
bool mxs_rworker_register_session(MXS_SESSION* session);

/**
 * Remove a session from the current routing worker's session container. Does
 * not actually remove anything from an epoll-set or affect the session in any
 * way.
 *
 * @param id Which id to remove.
 * @return The removed session or NULL if not found.
 */
bool mxs_rworker_deregister_session(uint64_t id);

/**
 * Find a session in the current routing worker's session container.
 *
 * @param id Which id to find.
 * @return The found session or NULL if not found.
 */
MXS_SESSION* mxs_rworker_find_session(uint64_t id);

/**
 * Worker local storage
 */

/**
 * Initialize a globally unique data identifier
 *
 * The value returned by this function is used with the other data commands.
 * The value is a unique handle to thread-local storage.
 *
 * @return The data identifier usable for worker local data storage
 */
uint64_t mxs_rworker_create_key();

/**
 * Set local worker data on current worker
 *
 * @param key      Key acquired with create_data
 * @param data     Data to store
 * @param callback Callback used to delete the data, NULL if no deletion is
 *                 required. This function is called by mxs_rworker_delete_data
 *                 when the data is deleted.
 */
void mxs_rworker_set_data(uint64_t key, void* data, void (* callback)(void*));

/**
 * Get local data from current worker
 *
 * @param key    Key to use
 *
 * @return Data previously stored or NULL if no data was previously stored
 */
void* mxs_rworker_get_data(uint64_t key);

/**
 * Deletes local data from all workers
 *
 * The key must not be used again after deletion.
 *
 * @param key      Key to remove
 */
void mxs_rworker_delete_data(uint64_t key);

MXS_END_DECLS
