#pragma once
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

#include <maxscale/cdefs.h>
#include <maxscale/session.h>
#include <maxscale/worker.h>

MXS_BEGIN_DECLS

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
MXS_WORKER* mxs_rworker_get(int worker_id);

/**
 * Return the current routing worker.
 *
 * @return A routing worker, or NULL if there is no current routing worker.
 */
MXS_WORKER* mxs_rworker_get_current();

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

MXS_END_DECLS
