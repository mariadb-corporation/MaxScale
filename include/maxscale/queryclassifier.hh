/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
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
#include <maxscale/hint.hh>
#include <maxscale/parser.hh>
#include <maxscale/router.hh>
#include <maxscale/session.hh>

namespace mariadb
{
class QueryClassifier;

class TrxTracker
{
public:

    enum TrxState : uint8_t
    {
        TRX_INACTIVE  = 0,
        TRX_ACTIVE    = 1 << 0,
        TRX_READ_ONLY = 1 << 1,
        TRX_ENDING    = 1 << 2,
        TRX_STARTING  = 1 << 3,
    };

    bool is_autocommit() const
    {
        return m_autocommit;
    }

    bool is_trx_read_only() const
    {
        return m_trx_state & TRX_READ_ONLY;
    }

    bool is_trx_ending() const
    {
        return m_trx_state & TRX_ENDING;
    }

    bool is_trx_starting() const
    {
        return m_trx_state & TRX_STARTING;
    }

    bool is_trx_active() const
    {
        return m_trx_state & TRX_ACTIVE;
    }

    /**
     * Track the transaction state
     *
     * @tparam ParseType Whether to use the query classifier or the custom parser to track the transaction
     *                   state. By default tracking is done using the query classifier.
     *
     * @param packetbuf A query that is being executed
     * @param parser    The parser class to use
     */
    template<mxs::Parser::ParseTrxUsing ParseType = mxs::Parser::ParseTrxUsing::DEFAULT>
    void track_transaction_state(const GWBUF& packetbuf, const mxs::Parser& parser);

    /**
     * Use reply to fix the transaction state
     *
     * If the state reported by the server does not match the expected one, the internal state is fixed to
     * match the server state. The only case when this happens is when something hidden (e.g. a stored
     * procedure call) opens a transaction that's not seen by the parsing done by MaxScale.
     *
     * Currently this only supports fixing the transaction state based on the reply server status bits that
     * are specific to MariaDB protocol. All other protocols should emulate it by setting the corresponding
     * bits there.
     *
     * @param reply The reply from the server
     */
    void fix_trx_state(const mxs::Reply& reply);

    void set_autocommit(bool value)
    {
        m_autocommit = value;
    }

    void set_state(uint8_t state)
    {
        m_trx_state = state;
    }

private:
    // The QueryClassifier internally uses an internal TrxTracker to query the transaction state if the
    // caller-provided TrxTracker is not given to QueryClassifier::update_route_info().
    friend class QueryClassifier;

    /**
     * The default mode for transactions. Set with SET SESSION TRANSACTION with the access mode set to either
     * READ ONLY or READ WRITE. The default is READ WRITE.
     */
    uint8_t m_default_trx_mode {0};

    /**
     * The access mode for the next transaction. Set with SET TRANSACTION and it only affects the next one.
     * All transactions after it will use the default transaction access mode.
     */
    uint8_t m_next_trx_mode {0};

    /**
     * The transaction state of the session.
     *
     * This tells only the state of @e explicitly started transactions.
     * That is, if @e autocommit is OFF, which means that there is always an
     * active transaction that is ended with an explicit COMMIT or ROLLBACK,
     * at which point a new transaction is started, this variable will still
     * be TRX_INACTIVE, unless a transaction has explicitly been
     * started with START TRANSACTION.
     *
     * Likewise, if @e autocommit is ON, which means that every statement is
     * executed in a transaction of its own, this will return false, unless a
     * transaction has explicitly been started with START TRANSACTION.
     *
     * The value is valid only if either a router or a filter
     * has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     */
    uint8_t m_trx_state {TRX_INACTIVE};

    /**
     * Tells whether autocommit is ON or not. The value effectively only tells the last value
     * of the statement "set autocommit=...".
     *
     * That is, if the statement "set autocommit=1" has been executed, then even if a transaction has
     * been started, which implicitly will cause autocommit to be set to 0 for the duration of the
     * transaction, this value will be true.
     *
     * By default autocommit is ON.
     */
    bool m_autocommit {true};
};

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
    };

    class RouteInfo
    {
    public:
        RouteInfo(const RouteInfo&) = default;
        RouteInfo& operator=(const RouteInfo&) = default;

        RouteInfo(RouteInfo&&) = default;
        RouteInfo& operator=(RouteInfo&&) = default;

        RouteInfo(const mxs::Parser* pParser)
            : m_pParser(pParser)
        {
        }

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
        bool multi_part_packet() const
        {
            return m_multi_part_packet;
        }

        /**
         * Check if the packet after this will be a continuation of multi-packet query
         */
        bool expecting_multi_part_packet() const
        {
            return m_next_multi_part_packet;
        }

        /**
         * Check if the server will generate a response for this packet
         */
        bool expecting_response() const
        {
            return load_data_state() == LOAD_DATA_INACTIVE
                   && !multi_part_packet()
                   && m_pParser->command_will_respond(command());
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

        void set_multi_part_packet(bool multi_part_packet)
        {
            // The value returned from multi_part_packet() must lag by one classification result.
            // This means that the first packet returns false and the subsequent ones return true.
            m_multi_part_packet = m_next_multi_part_packet;
            m_next_multi_part_packet = multi_part_packet;
        }

        void set_load_data_state(load_data_state_t state)
        {
            m_load_data_state = state;
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

        const TrxTracker& trx() const
        {
            return m_trx_tracker;
        }

    private:
        friend class QueryClassifier;
        using TableSet = std::unordered_set<std::string>;

        TrxTracker         m_trx_tracker;
        const mxs::Parser* m_pParser;
        uint32_t           m_target = QueryClassifier::TARGET_UNDEFINED;
        uint8_t            m_command = 0xff;
        uint32_t           m_type_mask = mxs::sql::TYPE_UNKNOWN;
        uint32_t           m_stmt_id = 0;
        load_data_state_t  m_load_data_state = LOAD_DATA_INACTIVE;
        bool               m_multi_part_packet = false;
        bool               m_next_multi_part_packet = false;
        bool               m_trx_is_read_only = true;
        bool               m_ps_continuation = false;
        TableSet           m_tmp_tables;
    };

    class Handler
    {
    public:
        virtual bool lock_to_master() = 0;
        virtual bool is_locked_to_master() const = 0;

        virtual bool supports_hint(Hint::Type hint_type) const = 0;
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

    enum class Log
    {
        ALL,    // Log all warnings and errors
        NONE,   // Log nothing
    };

    /**
     * Constructs a simple QueryClassifier
     *
     * This version does not log errors or warnings, doesn't support routing hints and doesn't implement the
     * "master locking" that is possible when a custom Handler class is used. This also disables the verbose
     * mode so that any info level logging is omitted.
     *
     * @param parser   Parser to use
     * @param pSession Session that uses this QueryClassifier
     */
    QueryClassifier(mxs::Parser& parser, MXS_SESSION* pSession);

    /**
     * Construct a QueryClassifier
     *
     * @param parser               Parser to use
     * @param pHandler             The handler instance
     * @param pSession             Session that uses this QueryClassifier
     * @param use_sql_variables_in How to classify user variables (MASTER makes them behave like writes)
     * @param log                  Whether to log errors and warnings
     */
    QueryClassifier(mxs::Parser& parser,
                    Handler* pHandler,
                    MXS_SESSION* pSession,
                    mxs_target_t use_sql_variables_in,
                    Log log = Log::ALL);

    mxs::Parser& parser()
    {
        return m_parser;
    }

    const mxs::Parser& parser() const
    {
        return m_parser;
    }

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
        return mxs::Parser::type_mask_contains(m_route_info.type_mask(), mxs::sql::TYPE_BEGIN_TRX);
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
     * @param buffer A request buffer.
     *
     * @return A const reference to the current route info.
     */
    const RouteInfo& update_route_info(GWBUF& buffer);          // TODO: const correct

    /**
     * Update the RouteInfo state based on the reply from the downstream component
     *
     * Currently this only updates the LOAD DATA state.
     *
     * @param reply The reply from the downstream component
     */
    void update_from_reply(const mxs::Reply& reply);

    /**
     * Reverts the effects of the latest update_route_info call
     *
     * @note Can only be called after a call to update_route_info() and must only be called once.
     */
    void revert_update()
    {
        m_route_info = m_prev_route_info;
    }

    /**
     * Set verbose mode
     *
     * @param value If true (the default), query classification is logged on the INFO level.
     */
    void set_verbose(bool value)
    {
        m_verbose = value;
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

    void     process_routing_hints(const GWBUF::HintVector& hints, uint32_t* target);
    uint32_t get_route_target(uint32_t qtype, const TrxTracker& trx_tracker);

    MXS_SESSION* session() const
    {
        return m_pSession;
    }

    void log_transaction_status(GWBUF* querybuf, uint32_t qtype, const TrxTracker& trx_tracker);

    uint32_t determine_query_type(const GWBUF& packet) const;

    void check_create_tmp_table(GWBUF* querybuf, uint32_t type);

    bool is_read_tmp_table(GWBUF* querybuf, uint32_t qtype);

    void check_drop_tmp_table(GWBUF* querybuf);

    current_target_t handle_multi_temp_and_load(QueryClassifier::current_target_t current_target,
                                                GWBUF* querybuf,
                                                uint32_t* qtype);

    bool query_continues_ps(const GWBUF& buffer);

private:
    class PSManager;
    typedef std::shared_ptr<PSManager> SPSManager;

    typedef std::unordered_map<uint32_t, uint32_t> HandleMap;

    static bool find_table(QueryClassifier& qc, const std::string& table);
    static bool delete_table(QueryClassifier& qc, const std::string& table);


private:
    mxs::Parser& m_parser;
    Handler*     m_pHandler;
    MXS_SESSION* m_pSession;
    mxs_target_t m_use_sql_variables_in;
    bool         m_multi_statements_allowed;        /**< Are multi-statements allowed */
    SPSManager   m_sPs_manager;
    RouteInfo    m_route_info;
    RouteInfo    m_prev_route_info; // Previous state, used for rollback of state
    bool         m_verbose = true;  // Whether to log info level messages for classified queries

    uint32_t m_prev_ps_id = 0;      /**< For direct PS execution, storest latest prepared PS ID.
                                     * https://mariadb.com/kb/en/library/com_stmt_execute/#statement-id **/
};

//
// Template and inline implementations
//

template<mxs::Parser::ParseTrxUsing ParseType>
void TrxTracker::track_transaction_state(const GWBUF& packetbuf, const mxs::Parser& parser)
{
    const auto trx_starting_active = TRX_ACTIVE | TRX_STARTING;

    mxb_assert((m_trx_state & (TRX_STARTING | TRX_ENDING)) != (TRX_STARTING | TRX_ENDING));

    if (m_trx_state & TRX_ENDING)
    {
        if (m_autocommit)
        {
            // Transaction ended, go into inactive state
            m_trx_state = TRX_INACTIVE;
        }
        else
        {
            // Without autocommit the end of a transaction starts a new one
            m_trx_state = trx_starting_active | m_next_trx_mode;
            m_next_trx_mode = m_default_trx_mode;
        }
    }
    else if (m_trx_state & TRX_STARTING)
    {
        m_trx_state &= ~TRX_STARTING;
    }
    else if (!m_autocommit && m_trx_state == TRX_INACTIVE)
    {
        // This state is entered when autocommit was disabled
        m_trx_state = trx_starting_active | m_next_trx_mode;
        m_next_trx_mode = m_default_trx_mode;
    }

    if (parser.is_query(packetbuf))
    {
        uint32_t type = parser.get_trx_type_mask_using(packetbuf, ParseType);

        mxb_assert_message(ParseType == mxs::Parser::ParseTrxUsing::CUSTOM
                           || parser.get_trx_type_mask_using(packetbuf, mxs::Parser::ParseTrxUsing::DEFAULT)
                           == parser.get_trx_type_mask_using(packetbuf, mxs::Parser::ParseTrxUsing::CUSTOM),
                           "Parser and query classifier should parse transactions identically: %s",
                           std::string(parser.get_sql(packetbuf)).c_str());

        if (type & mxs::sql::TYPE_BEGIN_TRX)
        {
            if (type & mxs::sql::TYPE_DISABLE_AUTOCOMMIT)
            {
                // This disables autocommit and the next statement starts a new transaction
                m_autocommit = false;
                m_trx_state = TRX_INACTIVE;
            }
            else
            {
                auto new_trx_state = trx_starting_active | m_next_trx_mode;
                m_next_trx_mode = m_default_trx_mode;
                if (type & mxs::sql::TYPE_READ)
                {
                    new_trx_state |= TRX_READ_ONLY;
                }
                else if (type & mxs::sql::TYPE_WRITE)
                {
                    new_trx_state &= ~TRX_READ_ONLY;
                }
                m_trx_state = new_trx_state;
            }
        }
        else if (type & (mxs::sql::TYPE_COMMIT | mxs::sql::TYPE_ROLLBACK))
        {
            auto new_trx_state = m_trx_state | TRX_ENDING;
            // A commit never starts a new transaction. This would happen with: SET AUTOCOMMIT=0; COMMIT;
            new_trx_state &= ~TRX_STARTING;
            m_trx_state = new_trx_state;

            if (type & mxs::sql::TYPE_ENABLE_AUTOCOMMIT)
            {
                m_autocommit = true;
            }
        }
        else if (type & (mxs::sql::TYPE_READWRITE | mxs::sql::TYPE_READONLY))
        {
            // Currently only pp_sqlite should return these types
            mxb_assert(ParseType == mxs::Parser::ParseTrxUsing::DEFAULT
                       && parser.get_operation(packetbuf) == mxs::sql::OP_SET_TRANSACTION);
            uint32_t mode = type & mxs::sql::TYPE_READONLY ? TRX_READ_ONLY : 0;
            m_next_trx_mode = mode;

            if (!(type & mxs::sql::TYPE_NEXT_TRX))
            {
                // All future transactions will use this access mode
                m_default_trx_mode = mode;
            }
        }
    }
}

inline void TrxTracker::fix_trx_state(const mxs::Reply& reply)
{
    // These are defined somewhere in the connector-c headers but including the header directly doesn't work.
    // For the sake of simplicity, just declare them here.
    constexpr uint16_t STATUS_IN_TRX = 1;
    constexpr uint16_t STATUS_AUTOCOMMIT = 2;
    constexpr uint16_t STATUS_IN_RO_TRX = 8192;

    uint32_t status = reply.server_status();

    if (status == mxs::Reply::NO_SERVER_STATUS)
    {
        // The response from the server did not contain the status bits. A handful of response types, for
        // example the one to COM_STMT_PREPARE, will not contain the server status bits.
        return;
    }

    bool is_autocommit = status & STATUS_AUTOCOMMIT;
    m_autocommit = is_autocommit;

    bool in_trx = status & (STATUS_IN_TRX | STATUS_IN_RO_TRX);

    if (!is_trx_active() && in_trx)
    {
        m_trx_state = TrxState::TRX_ACTIVE | TrxState::TRX_STARTING;

        if (status & STATUS_IN_RO_TRX)
        {
            m_trx_state |= TrxState::TRX_READ_ONLY;
        }
    }
    else if (is_trx_active() && !is_trx_ending() && !is_trx_starting() && !in_trx)
    {
        m_trx_state |= TrxState::TRX_ENDING;
    }

#ifdef SS_DEBUG
    if (auto autocommit = reply.get_variable("autocommit"); !autocommit.empty())
    {
        mxb_assert(is_autocommit == mxb::sv_case_eq(autocommit, "ON"));
    }

    if (auto trx_state = reply.get_variable("trx_state"); !trx_state.empty())
    {
        if (trx_state.find_first_of("TI") != std::string_view::npos)
        {
            mxb_assert(in_trx);
        }
        else if (trx_state.find_first_of("rRwWsSL") == std::string_view::npos)
        {
            mxb_assert(!in_trx);
        }
    }

    if (auto trx_characteristics = reply.get_variable("trx_characteristics"); !trx_characteristics.empty())
    {
        if (trx_characteristics == "START TRANSACTION READ ONLY;")
        {
            mxb_assert(status & STATUS_IN_RO_TRX);
        }
        else if (trx_characteristics == "START TRANSACTION READ WRITE;")
        {
            mxb_assert((status & STATUS_IN_RO_TRX) == 0);
        }
    }
#endif
}
}
