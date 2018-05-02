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

#include <maxscale/cppdefs.hh>

#include <map>
#include <tr1/memory>

#include <maxscale/backend.hh>
#include <maxscale/modutil.h>

namespace maxscale
{

/** Enum for tracking client reply state */
enum reply_state_t
{
    REPLY_STATE_START,          /**< Query sent to backend */
    REPLY_STATE_DONE,           /**< Complete reply received */
    REPLY_STATE_RSET_COLDEF,    /**< Resultset response, waiting for column definitions */
    REPLY_STATE_RSET_ROWS       /**< Resultset response, waiting for rows */
};

typedef std::map<uint32_t, uint32_t> BackendHandleMap; /** Internal ID to external ID */

class RWBackend;
typedef std::tr1::shared_ptr<RWBackend> SRWBackend;
typedef std::list<SRWBackend> SRWBackendList;

class RWBackend: public mxs::Backend
{
    RWBackend(const RWBackend&);
    RWBackend& operator=(const RWBackend&);

public:

    static SRWBackendList from_servers(SERVER_REF* servers);

    RWBackend(SERVER_REF* ref);
    ~RWBackend();

    inline reply_state_t get_reply_state() const
    {
        return m_reply_state;
    }

    inline void set_reply_state(reply_state_t state)
    {
        m_reply_state = state;
    }

    void add_ps_handle(uint32_t id, uint32_t handle);
    uint32_t get_ps_handle(uint32_t id) const;

    bool execute_session_command();
    bool continue_session_command(GWBUF* buffer);
    bool write(GWBUF* buffer, response_type type = EXPECT_RESPONSE);
    void close(close_type type = CLOSE_NORMAL);

    // For COM_STMT_FETCH processing
    bool consume_fetched_rows(GWBUF* buffer);

    inline uint8_t current_command() const
    {
        return m_command;
    }

    bool reply_is_complete(GWBUF *buffer);

private:
    reply_state_t    m_reply_state;
    BackendHandleMap m_ps_handles; /**< Internal ID to backend PS handle mapping */
    modutil_state    m_modutil_state; /**< @see modutil_count_signal_packets */
    uint8_t          m_command;
    bool             m_opening_cursor; /**< Whether we are opening a cursor */
    uint32_t         m_expected_rows; /**< Number of rows a COM_STMT_FETCH is retrieving */

    inline bool is_opening_cursor() const
    {
        return m_opening_cursor;
    }

    inline void set_cursor_opened()
    {
        m_opening_cursor = false;
    }
};

}
