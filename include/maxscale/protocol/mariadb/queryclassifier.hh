/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <maxscale/hint.h>
#include <maxscale/protocol/mariadb/query_classifier.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/router.hh>
#include <maxscale/session.hh>

namespace mariadb
{

class QueryClassifier
{
    QueryClassifier(const QueryClassifier&) = delete;
    QueryClassifier& operator=(const QueryClassifier&) = delete;

public:

    /** States of a LOAD DATA LOCAL INFILE */
    enum load_data_state_t
    {
        LOAD_DATA_INACTIVE,         /**< Not active */
        LOAD_DATA_ACTIVE,           /**< Load is active */
        LOAD_DATA_END               /**< Current query contains an empty packet that ends the load */
    };

    class RouteInfo
    {
    public:
        RouteInfo(const RouteInfo&) = default;
        RouteInfo& operator=(const RouteInfo&) = default;

        RouteInfo(RouteInfo&&) = default;
        RouteInfo& operator=(RouteInfo&&) = default;

        RouteInfo() = default;

        /**
         * Get the current routing target
         */
        uint32_t target() const
        {
            return m_target;
        }

        /**
         * Get the MariaDB command
         */
        uint8_t command() const
        {
            return m_command;
        }

        /**
         * Get the query type mask
         */
        uint32_t type_mask() const
        {
            return m_type_mask;
        }

        /**
         * Get the prepared statement ID in the query
         */
        uint32_t stmt_id() const
        {
            return m_stmt_id;
        }

        /**
         * Check if this is a continuation of a previous multi-packet query
         */
        bool large_query() const
        {
            return m_large_query;
        }

        /**
         * Check if the packet after this will be a continuation of multi-packet query
         */
        bool expecting_large_query() const
        {
            return m_next_large_query;
        }

        /**
         * Check if the server will generate a response for this packet
         */
        bool expecting_response() const
        {
            return load_data_state() == LOAD_DATA_INACTIVE
                   && !large_query()
                   && mxs_mysql_command_will_respond(command());
        }

        /**
         * Get the state of the LOAD DATA LOCAL INFILE command
         */
        load_data_state_t load_data_state() const
        {
            return m_load_data_state;
        }

        /**
         * Check if a LOAD DATA LOCAL INFILE is in progress
         */
        bool loading_data() const
        {
            return m_load_data_state != LOAD_DATA_INACTIVE;
        }

        /**
         * Get number of bytes send in the LOAD DATA LOCAL INFILE
         */
        uint64_t load_data_sent() const
        {
            return m_load_data_sent;
        }

        /**
         * Check if current transaction is still a read-only transaction
         *
         * @return True if no statements have been executed that modify data
         */
        bool is_trx_still_read_only() const
        {
            return m_trx_is_read_only;
        }

        /**
         * Whether the current binary protocol statement is a continuation of a previously executed statement.
         *
         * All COM_STMT_FETCH are continuations of a previously executed COM_STMT_EXECUTE. A COM_STMT_EXECUTE
         * can
         * be a continuation if it has parameters but it doesn't provide the metadata for them.
         */
        bool is_ps_continuation() const
        {
            return m_ps_continuation;
        }

        /**
         * Check if temporary tables have been created
         *
         * @return True if temporary tables have been created
         */
        bool have_tmp_tables() const
        {
            return !m_tmp_tables.empty();
        }

        /**
         * Check if the table is a temporary table
         *
         * @return True if the table in question is a temporary table
         */
        bool is_tmp_table(const std::string& table)
        {
            return m_tmp_tables.find(table) != m_tmp_tables.end();
        }

        //
        // Setters
        //

        void set_command(uint8_t c)
        {
            m_command = c;
        }

        void set_target(uint32_t t)
        {
            m_target = t;
        }

        void or_target(uint32_t t)
        {
            m_target |= t;
        }

        void set_type_mask(uint32_t t)
        {
            m_type_mask = t;
        }

        void or_type_mask(uint32_t t)
        {
            m_type_mask |= t;
        }

        void set_stmt_id(uint32_t stmt_id)
        {
            m_stmt_id = stmt_id;
        }

        void set_large_query(bool large_query)
        {
            // The value returned from large_query() must lag by one classification result. This means that
            // the first packet returns false and the subsequent ones return true.
            m_large_query = m_next_large_query;
            m_next_large_query = large_query;
        }

        void set_load_data_state(load_data_state_t state)
        {
            if (state == LOAD_DATA_ACTIVE)
            {
                mxb_assert(m_load_data_state == LOAD_DATA_INACTIVE);
                reset_load_data_sent();
            }

            m_load_data_state = state;
        }

        void append_load_data_sent(GWBUF* pBuffer)
        {
            m_load_data_sent += gwbuf_length(pBuffer);
        }

        void reset_load_data_sent()
        {
            m_load_data_sent = 0;
        }

        void set_trx_still_read_only(bool value)
        {
            m_trx_is_read_only = value;
        }

        void set_ps_continuation(bool value)
        {
            m_ps_continuation = value;
        }

        void add_tmp_table(const std::string& table)
        {
            m_tmp_tables.insert(table);
        }

        void remove_tmp_table(const std::string& table)
        {
            m_tmp_tables.erase(table);
        }

        void clear_tmp_tables()
        {
            m_tmp_tables.clear();
        }

    private:

        using TableSet = std::unordered_set<std::string>;

        uint32_t          m_target = QueryClassifier::TARGET_UNDEFINED;
        uint8_t           m_command = 0xff;
        uint32_t          m_type_mask = QUERY_TYPE_UNKNOWN;
        uint32_t          m_stmt_id = 0;
        load_data_state_t m_load_data_state = LOAD_DATA_INACTIVE;
        uint64_t          m_load_data_sent = 0;
        bool              m_large_query = false;
        bool              m_next_large_query = false;
        bool              m_prev_large_query = false;
        bool              m_trx_is_read_only = true;
        bool              m_ps_continuation = false;
        TableSet          m_tmp_tables;
    };

    class Handler
    {
    public:
        virtual bool lock_to_master() = 0;
        virtual bool is_locked_to_master() const = 0;

        virtual bool supports_hint(HINT_TYPE hint_type) const = 0;
    };

    // NOTE: For the time being these must be exactly like the ones in readwritesplit.hh
    enum
    {
        TARGET_UNDEFINED    = 0x00,
        TARGET_MASTER       = 0x01,
        TARGET_SLAVE        = 0x02,
        TARGET_NAMED_SERVER = 0x04,
        TARGET_ALL          = 0x08,
        TARGET_RLAG_MAX     = 0x10,
        TARGET_LAST_USED    = 0x20
    };

    static bool target_is_master(uint32_t t)
    {
        return t & TARGET_MASTER;
    }

    static bool target_is_slave(uint32_t t)
    {
        return t & TARGET_SLAVE;
    }

    static bool target_is_named_server(uint32_t t)
    {
        return t & TARGET_NAMED_SERVER;
    }

    static bool target_is_all(uint32_t t)
    {
        return t & TARGET_ALL;
    }

    static bool target_is_rlag_max(uint32_t t)
    {
        return t & TARGET_RLAG_MAX;
    }

    static bool target_is_last_used(uint32_t t)
    {
        return t & TARGET_LAST_USED;
    }

    enum current_target_t
    {
        CURRENT_TARGET_UNDEFINED,   /**< Current target has not been set. */
        CURRENT_TARGET_MASTER,      /**< Current target is master */
        CURRENT_TARGET_SLAVE        /**< Current target is a slave */
    };

    QueryClassifier(Handler* pHandler,
                    MXS_SESSION* pSession,
                    mxs_target_t use_sql_variables_in);

    /**
     * @brief Return the current route info. A call to update_route_info()
     *        will change the values.
     *
     * @return The current RouteInfo.
     */
    const RouteInfo& current_route_info() const
    {
        return m_route_info;
    }

    void master_replaced()
    {
        m_route_info.clear_tmp_tables();
    }

    /**
     * Check if current transaction is still a read-only transaction
     *
     * @return True if no statements have been executed that modify data
     */
    bool is_trx_starting() const
    {
        return qc_query_is_type(m_route_info.type_mask(), QUERY_TYPE_BEGIN_TRX);
    }

    /**
     * Get number of parameters for a prepared statement
     *
     * @param id The statement ID
     *
     * @return Number of parameters
     */
    uint16_t get_param_count(uint32_t id);

    /**
     * @brief Store and process a prepared statement
     *
     * @param buffer Buffer containing either a text or a binary protocol
     *               prepared statement
     * @param id     The unique ID for this statement
     */
    void ps_store(GWBUF* buffer, uint32_t id);

    /**
     * @brief Remove a prepared statement
     *
     * @param buffer Buffer containing a DEALLOCATE statement or a binary protocol command
     */
    void ps_erase(GWBUF* buffer);

    /**
     * @brief Store a prepared statement response
     *
     * @param id          The ID of the prepared statement
     * @param param_count The number of parameters it takes
     */
    void ps_store_response(uint32_t id, uint16_t param_count);

    /**
     * @brief Update the current RouteInfo.
     *
     * @param current_target  What the current target is.
     * @param pBuffer         A request buffer.
     *
     * @return A copy of the current route info.
     */
    RouteInfo update_route_info(QueryClassifier::current_target_t current_target, GWBUF* pBuffer);

    /**
     * Reverts the effects of the latest update_route_info call
     *
     * @note Can only be called after a call to update_route_info() and must only be called once.
     */
    void revert_update()
    {
        m_route_info = m_prev_route_info;
    }

private:
    bool multi_statements_allowed() const
    {
        return m_multi_statements_allowed;
    }


    /**
     * @brief Get the internal ID for the given binary prepared statement
     *
     * @param buffer Buffer containing a binary protocol statement other than COM_STMT_PREPARE
     *
     * @return The internal ID of the prepared statement that the buffer contents refer to
     */
    uint32_t ps_id_internal_get(GWBUF* pBuffer);

    /**
     * Check if the query type is that of a read-only query
     *
     * @param qtype Query type mask
     *
     * @return True if the query type is that of a read-only query
     */
    bool query_type_is_read_only(uint32_t qtype) const;

    void     process_routing_hints(HINT* pHints, uint32_t* target);
    uint32_t get_route_target(uint8_t command, uint32_t qtype);

    MXS_SESSION* session() const
    {
        return m_pSession;
    }

    void log_transaction_status(GWBUF* querybuf, uint32_t qtype);

    static uint32_t determine_query_type(GWBUF* querybuf, int command);

    void check_create_tmp_table(GWBUF* querybuf, uint32_t type);

    bool is_read_tmp_table(GWBUF* querybuf, uint32_t qtype);

    void check_drop_tmp_table(GWBUF* querybuf);

    bool check_for_multi_stmt(GWBUF* buf, uint8_t packet_type);

    current_target_t handle_multi_temp_and_load(QueryClassifier::current_target_t current_target,
                                                GWBUF* querybuf,
                                                uint8_t packet_type,
                                                uint32_t* qtype);

    bool query_continues_ps(uint8_t cmd, uint32_t stmt_id, GWBUF* buffer);

private:
    class PSManager;
    typedef std::shared_ptr<PSManager> SPSManager;

    typedef std::unordered_map<uint32_t, uint32_t> HandleMap;

    static bool find_table(QueryClassifier& qc, const std::string& table);
    static bool delete_table(QueryClassifier& qc, const std::string& table);


private:
    Handler*     m_pHandler;
    MXS_SESSION* m_pSession;
    mxs_target_t m_use_sql_variables_in;
    bool         m_multi_statements_allowed;        /**< Are multi-statements allowed */
    SPSManager   m_sPs_manager;
    RouteInfo    m_route_info;
    RouteInfo    m_prev_route_info; // Previous state, used for rollback of state


    uint32_t m_prev_ps_id = 0;      /**< For direct PS execution, storest latest prepared PS ID.
                                     * https://mariadb.com/kb/en/library/com_stmt_execute/#statement-id **/
};
}
