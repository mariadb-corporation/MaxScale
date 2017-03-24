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

#pragma once

#include <list>
#include <string>

#include <maxscale/buffer.hh>

using namespace maxscale;

class SessionCommand;
typedef std::list<SessionCommand> SessionCommandList;

class SessionCommand
{
public:
    /**
     * @brief Mark reply as received
     */
    void mark_reply_received();

    /**
     * @brief Check if the session command has received a reply
     * @return True if the reply is already received
     */
    bool is_reply_received() const;

    /**
     * @brief Creates a copy of the internal buffer
     * @return A copy of the internal buffer
     */
    Buffer copy_buffer() const;

    /**
     * @brief Create a new session command
     *
     * @param buffer The buffer containing the command. Note that the ownership
     *               of @c buffer is transferred to this object.
     */
    SessionCommand(GWBUF *buffer);

    ~SessionCommand();

    /**
     * @brief Debug function for printing session commands
     *
     * @return String representation of the object
     */
    std::string to_string();

private:
    Buffer m_buffer; /**< The buffer containing the command */
    bool   m_replySent; /**< Whether the session command reply has been sent */

    SessionCommand();
    SessionCommand& operator = (const SessionCommand& command);
};
