/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/buffer.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/session.hh>
#include <deque>
#include <map>
#include <optional>

namespace maxscale
{

/**
 * Class for storing a history of SQL commands that modify data
 */
class History
{
public:
    /**
     * Construct a History
     *
     * @param limit How many commands to keep in the history
     */
    History(size_t limit);

    /**
     * Adds a command to the history
     *
     * @param buffer The buffer containing the command to add
     * @param ok     Whether the command was successful or not
     */
    void add(GWBUF&& buffer, bool ok);

    /**
     * Erase a command from the history
     *
     * @param id The ID of the command to erase
     *
     * @return True if a command was erased from the history
     */
    bool erase(uint32_t id);

    /**
     * Clear the whole history
     *
     * This does not clear the responses, they get cleared once the history is filled up again and when it is
     * known that no backend connection needs it anymore.
     */
    void clear()
    {
        m_history.clear();
    }

    /**
     * Get the current history
     *
     * @return The history that has been recorded
     */
    const std::deque<GWBUF>& history() const
    {
        return m_history;
    }

    /**
     * Pin the history responses
     *
     * This prevents the history from being erased while the history is being replayed.
     *
     * @param backend The backend that requested the pinning to be done
     */
    void pin_responses(mxs::BackendConnection* backend);

    /**
     * Set the history position of a backend
     *
     * @param backend  The backend in question
     * @param position The position it's at
     */
    void set_position(mxs::BackendConnection* backend, uint32_t position);

    /**
     * Mark that a backend is waiting for a response
     *
     * @param backend The backend that needs the response
     * @param cb      The callback that is called
     */
    void need_response(mxs::BackendConnection* backend, std::function<void ()> cb);

    /**
     * Remove a backend from the history
     *
     * @param backend The backend to remove
     */
    void remove(mxs::BackendConnection* backend);

    /**
     * Get the result of a command
     *
     * @param id The ID of the command
     *
     * @return The result of the command if it was found
     */
    std::optional<bool> get(uint32_t id) const;

    /**
     * Compare history responses that arrived before the accepted reply arrived
     *
     * This should be called by the client protocol module after a command was added to the history.
     */
    void check_early_responses();

    /**
     * Check if the history has been pruned
     *
     * @return True if the history has been pruned and some information has been lost
     */
    bool pruned() const
    {
        return m_history_pruned;
    }

    /**
     * Whether the history is empty
     */
    bool empty() const
    {
        return m_history.empty();
    }

    /**
     * Size of the history
     */
    size_t size() const
    {
        return m_history.size();
    }

    size_t runtime_size() const;

private:

    // The struct used to communicate information from the backend protocol to the client protocol.
    struct HistoryInfo
    {
        // Flag that is set to true whenever a backend executes a command before the response which is
        // delivered to the client has arrived.
        bool waiting_for_response {false};

        // The callback that is called
        std::function<void ()> response_cb;

        // Current position in history. Used to track the responses that are still needed.
        uint32_t position {0};
    };

    void prune_history();

    // History of all commands that modify the session state
    std::deque<GWBUF> m_history;

    // The responses to the executed commands, contains the ID and the result
    std::map<uint32_t, bool> m_history_responses;

    // Whether the history has been pruned of old commands. If true, reconnection should only take place if it
    // is acceptable to lose some state history (i.e. prune_sescmd_history is enabled).
    bool m_history_pruned {false};

    // History information for all open backend connections
    std::map<mxs::BackendConnection*, HistoryInfo> m_history_info;

    // Number of stored session commands
    size_t m_max_sescmd_history;
};
}
