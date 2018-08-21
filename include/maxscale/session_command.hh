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

#include <maxscale/ccdefs.hh>

#include <memory>
#include <list>
#include <string>

#include <maxscale/buffer.hh>

namespace maxscale
{

class SessionCommand;
typedef std::shared_ptr<SessionCommand> SSessionCommand;
typedef std::list<SSessionCommand> SessionCommandList;

class SessionCommand
{
    SessionCommand(const SessionCommand&);
    SessionCommand& operator=(const SessionCommand&);
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
     * @brief Get the command type of the session command
     *
     * @return The type of the command
     */
    uint8_t get_command() const;

    /**
     * @brief Get the position of this session command
     *
     * @return The position of the session command
     */
    uint64_t get_position() const;

    /**
     * @brief Creates a deep copy of the internal buffer
     *
     * @return A deep copy of the internal buffer or NULL on error
     */
    GWBUF* deep_copy_buffer();

    /**
     * @brief Create a new session command
     *
     * @param buffer The buffer containing the command. Note that the ownership
     *               of @c buffer is transferred to this object.
     * @param id     A unique position identifier used to track replies
     */
    SessionCommand(GWBUF *buffer, uint64_t id);

    ~SessionCommand();

    /**
     * @brief Debug function for printing session commands
     *
     * @return String representation of the object
     */
    std::string to_string();

    /**
     * @brief Equality comparison
     *
     * @return True if @c rhs is equal
     */
    bool eq(const SessionCommand& rhs) const;

    /**
     * Mark the session command as a re-execution of another command
     *
     * This function makes the current command's buffer a reference to the other
     * command's buffer. The commands will still have separate positions and
     * reply statuses.
     *
     * @param rhs Session command whose data is used
     */
    void mark_as_duplicate(const SessionCommand& rhs);

private:
    mxs::Buffer m_buffer;    /**< The buffer containing the command */
    uint8_t     m_command;   /**< The command being executed */
    uint64_t    m_pos;       /**< Unique position identifier */
    bool        m_reply_sent; /**< Whether the session command reply has been sent */
};

inline bool operator ==(const SessionCommand& lhs, const SessionCommand& rhs)
{
    return lhs.eq(rhs);
}

inline bool operator !=(const SessionCommand& lhs, const SessionCommand& rhs)
{
    return !lhs.eq(rhs);
}

}
