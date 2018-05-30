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
#include <maxscale/protocol/mysql.h>
namespace maxscale
{

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

    /**
     * Write a query to the backend
     *
     * This function handles the replacement of the prepared statement IDs from
     * the internal ID to the server specific one. Trailing parts of large
     * packets should use RWBackend::continue_write.
     *
     * @param buffer Buffer to write
     * @param type   Whether a response is expected
     *
     * @return True if writing was successful
     */
    bool write(GWBUF* buffer, response_type type = EXPECT_RESPONSE);

    /**
     * Continue a previously started write
     *
     * This should only be used when RWBackend::write has been called to start
     * a new query.
     *
     * @param buffer Buffer to write
     *
     * @return True if writing was successful
     */
    bool continue_write(GWBUF* buffer)
    {
        return mxs::Backend::write(buffer);
    }

    void close(close_type type = CLOSE_NORMAL);

    inline uint8_t current_command() const
    {
        return m_command;
    }

    bool local_infile_requested() const
    {
        return m_local_infile_requested;
    }

    bool reply_is_complete(GWBUF *buffer);

private:
    reply_state_t    m_reply_state;
    BackendHandleMap m_ps_handles; /**< Internal ID to backend PS handle mapping */
    modutil_state    m_modutil_state; /**< @see modutil_count_signal_packets */
    uint8_t          m_command;
    bool             m_local_infile_requested; /**< Whether a LOCAL INFILE was requested */

};

}
