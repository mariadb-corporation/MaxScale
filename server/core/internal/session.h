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

/**
 * @file core/maxscale/session.h - The private session interface
 */

#include <maxscale/session.h>

MXS_BEGIN_DECLS

#define SESSION_STATS_INIT {0}
#define MXS_DOWNSTREAM_INIT {0}
#define MXS_UPSTREAM_INIT {0}
#define SESSION_FILTER_INIT {0}

#define SESSION_INIT {.ses_chk_top = CHK_NUM_SESSION, \
    .stats = SESSION_STATS_INIT, .head = MXS_DOWNSTREAM_INIT, .tail = MXS_UPSTREAM_INIT, \
    .state = SESSION_STATE_ALLOC, .client_protocol_data = 0, .ses_chk_tail = CHK_NUM_SESSION}

#define SESSION_PROTOCOL(x, type)       DCB_PROTOCOL((x)->client_dcb, type)

/**
 * Filter type for the sessionGetList call
 */
typedef enum
{
    SESSION_LIST_ALL,
    SESSION_LIST_CONNECTION
} SESSIONLISTFILTER;

int session_isvalid(MXS_SESSION *);
const char *session_state(mxs_session_state_t);

/**
 * Link a session to a backend DCB.
 *
 * @param session  The session to link with the dcb
 * @param dcb      The backend DCB to be linked
 */
void session_link_backend_dcb(MXS_SESSION *session, struct dcb *dcb);

/**
 * Unlink a backend DCB to a session.
 *
 * @param session  The session to link with the dcb
 * @param dcb      The backend DCB to be linked
 */
void session_unlink_backend_dcb(MXS_SESSION *session, struct dcb *dcb);

RESULTSET *sessionGetList(SESSIONLISTFILTER);

void printAllSessions();
void printSession(MXS_SESSION *);

void dprintSessionList(DCB *pdcb);
void dprintAllSessions(struct dcb *);
void dprintSession(struct dcb *, MXS_SESSION *);
void dListSessions(struct dcb *);

/**
 * @brief Get a session reference
 *
 * This creates an additional reference to a session which allows it to live
 * as long as it is needed.
 *
 * @param session Session reference to get
 * @return Reference to a MXS_SESSION
 *
 * @note The caller must free the session reference by calling session_put_ref
 */
MXS_SESSION* session_get_ref(MXS_SESSION *sessoin);

MXS_END_DECLS
