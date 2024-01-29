/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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
#include <functional>

namespace maxscale
{

/**
 * Class for storing a history of SQL commands that modify data
 */
class History
{
public:
    class Subscriber
    {
    public:
        /**
         * Create a new Subscriber
         *
         * @param history The history that this Subscriber is a part of
         * @param cb      The callback that's called when a mismatch is found
         */
        Subscriber(History& history, std::function<void ()> cb);

        ~Subscriber();

        /**
         * Set the ID of the current command
         *
         * The ID is used to uniquely identify
         * @param id The ID to set
         */
        void set_current_id(uint32_t id);

        /**
         * Get the ID of the current command
         *
         * @return The ID of the current command being executed
         */
        uint32_t current_id() const;

        /**
         * Add a response and compare it to the one stored in the history
         *
         * @param success Whether the command succeeded or not
         *
         * @return True if the command matched the one in the history or false if it didn't. If the validity
         *         could not yet be verified, due to the recorded response not having arrived yet, the
         *         function returns true. If at a later point it turns out that the result was a mismatch, the
         *         callback given to the constructor is called.
         */
        bool add_response(bool success);

        /**
         * Get the current history
         *
         * @return The history that has been recorded so far
         */
        const std::deque<GWBUF>& history() const;

        /**
         * Get the result of a command
         *
         * @param id The ID of the command
         *
         * @return The result of the command if it was found
         */
        std::optional<bool> get(uint32_t id) const;

    private:
        friend class History;

        bool compare_responses(uint32_t id, bool success);

        History& m_history;

        // The callback that is called when the history needs to be checked
        std::function<void ()> m_cb;

        // The internal ID of the current query.
        uint32_t m_current_id {0};

        // The ID and response to the command that will be added to the history. This is stored in a separate
        // variable in case the correct response that is delivered to the client isn't available when this
        // subscriber receive the response.
        std::map<uint32_t, bool> m_ids_to_check;
    };

    /**
     * Construct a History
     *
     * @param limit           How many commands to keep in the history
     * @param allow_pruning   Whether history pruning is allowed
     * @param disable_history If true, recovery is disabled but consistency checks are still done
     */
    History(size_t limit, bool allow_pruning, bool disable_history);

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
     * known that no subscriber needs it anymore.
     */
    void clear()
    {
        m_history.clear();
    }

    /**
     * Create a new subscriber for this history
     *
     * @note The subscriber must be reset before the History that it is a part of is destroyed. For backend
     *       connections, this means it must be released in finish_connect() and not in the destructor. The
     *       reason for this is that the lifetime of the protocol data that usually contains the History is
     *       linked to the session and once the final DCB is closed, the session is freed moments before the
     *       last DCB is freed.
     *
     * @param cb The callback that is called when a history response mismatch is encountered
     *
     * @return The new subscriber
     */
    std::unique_ptr<Subscriber> subscribe(std::function<void ()> cb)
    {
        return std::make_unique<Subscriber>(*this, std::move(cb));
    }

    /**
     * Compare history responses that arrived before the accepted reply arrived
     *
     * This should be called by the client protocol module after a command was added to the history.
     *
     * @param id      The ID of the command that just completed
     * @param success Whether the command succeeded or not
     */
    void check_early_responses(uint32_t id, bool success);

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
     * Whether the state can still be recovered from the history
     *
     * The recovery may only be partial if the configuration allows it.
     *
     * @return True if the session state can be recovered from history
     */
    bool can_recover_state() const;

    /**
     * Size of the history
     */
    size_t size() const
    {
        return m_history.size();
    }

    size_t runtime_size() const;

    /**
     * Fill the given JSON object with history statistics
     *
     * @param obj The JSON object to fill
     */
    void fill_json(json_t* obj) const;

private:

    /**
     * Pin the history responses
     *
     * This prevents the history from being erased while the history is being replayed.
     *
     * @param subscriber The subscriber that requested the pinning to be done
     */
    void pin_responses(Subscriber* subscriber);

    /**
     * Set the history position of a subscriber
     *
     * @param subscriber The subscriber in question
     * @param position   The position it's at
     */
    void set_position(Subscriber* subscriber, uint32_t position);

    /**
     * Mark that a subscriber is waiting for a response
     *
     * @param subscriber The subscriber that needs the response
     */
    void need_response(Subscriber* subscriber);

    /**
     * Remove a subscriber from the history
     *
     * @param subscriber The subscriber to remove
     */
    void remove(Subscriber* subscriber);

    void prune_responses();

    bool still_in_history(uint32_t id) const;

    // The struct used to communicate information from the subscriber protocol to the client protocol.
    struct HistoryInfo
    {
        // Flag that is set to true whenever a subscriber executes a command before the response which is
        // delivered to the client has arrived.
        bool waiting_for_response {false};

        // Current position in history. Used to track the responses that are still needed.
        uint32_t position {0};
    };

    // History of all commands that modify the session state
    std::deque<GWBUF> m_history;

    // The responses to the executed commands, contains the ID and the result
    std::map<uint32_t, bool> m_history_responses;

    // Whether the history has been pruned of old commands. If true, reconnection should only take place if it
    // is acceptable to lose some state history (i.e. prune_sescmd_history is enabled).
    bool m_history_pruned {false};

    // History information for all open subscriptions
    std::map<Subscriber*, HistoryInfo> m_history_info;

    // Number of stored session commands
    size_t m_max_sescmd_history;

    // Whether history pruning is allowed
    bool m_allow_pruning;
};
}
