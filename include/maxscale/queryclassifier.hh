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
#include <string>
#include <tr1/memory>
#include <tr1/unordered_map>
#include <tr1/unordered_set>
#include <maxscale/hint.h>
#include <maxscale/router.h>
#include <maxscale/session.h>

namespace maxscale
{

class QueryClassifier
{
    QueryClassifier(const QueryClassifier&) = delete;
    QueryClassifier& operator = (const QueryClassifier&) = delete;

public:
    class Handler
    {
    public:
        virtual bool lock_to_master() = 0;
        virtual bool is_locked_to_master() const = 0;

        virtual bool supports_hint(HINT_TYPE hint_type) const = 0;
    };

    typedef std::tr1::unordered_set<std::string> TableSet;

    // NOTE: For the time being these must be exactly like the ones in readwritesplit.hh
    enum
    {
        TARGET_UNDEFINED    = 0x00,
        TARGET_MASTER       = 0x01,
        TARGET_SLAVE        = 0x02,
        TARGET_NAMED_SERVER = 0x04,
        TARGET_ALL          = 0x08,
        TARGET_RLAG_MAX     = 0x10
    };

    static bool target_is_master(uint32_t t)
    {
        return (t & TARGET_MASTER);
    }

    static bool target_is_slave(uint32_t t)
    {
        return (t & TARGET_SLAVE);
    }

    static bool target_is_named_server(uint32_t t)
    {
        return (t & TARGET_NAMED_SERVER);
    }

    static bool target_is_all(uint32_t t)
    {
        return (t & TARGET_ALL);
    }

    static bool target_is_rlag_max(uint32_t t)
    {
        return (t & TARGET_RLAG_MAX);
    }

    enum current_target_t
    {
        CURRENT_TARGET_UNDEFINED, /**< Current target has not been set. */
        CURRENT_TARGET_MASTER,    /**< Current target is master */
        CURRENT_TARGET_SLAVE      /**< Current target is a slave */
    };

    /** States of a LOAD DATA LOCAL INFILE */
    enum load_data_state_t
    {
        LOAD_DATA_INACTIVE,         /**< Not active */
        LOAD_DATA_START,            /**< Current query starts a load */
        LOAD_DATA_ACTIVE,           /**< Load is active */
        LOAD_DATA_END               /**< Current query contains an empty packet that ends the load */
    };

    QueryClassifier(Handler* pHandler,
                    MXS_SESSION* pSession,
                    mxs_target_t use_sql_variables_in);

    void master_replaced()
    {
        // As the master has changed, we can reset the temporary table information
        set_have_tmp_tables(false);
        clear_tmp_tables();
    }

    bool large_query() const
    {
        return m_large_query;
    }

    void set_large_query(bool large_query)
    {
        m_large_query = large_query;
    }

    load_data_state_t load_data_state() const
    {
        return m_load_data_state;
    }

    void set_load_data_state(load_data_state_t state)
    {
        m_load_data_state = state;
    }

    /**
     * @brief Store and process a prepared statement
     *
     * @param buffer Buffer containing either a text or a binary protocol
     *               prepared statement
     * @param id     The unique ID for this statement
     */
    void ps_store(GWBUF* buffer, uint32_t id);

    /**
     * @brief Store a mapping from an external id to the corresponding internal id
     *
     * @param external_id  The external id as seen by the client.
     * @param internal_id  The corresponding internal id.
     */
    void ps_id_internal_put(uint32_t external_id, uint32_t internal_id);

    uint32_t get_target_type(QueryClassifier::current_target_t current_target,
                             GWBUF *buffer,
                             uint8_t* command,
                             uint32_t* type,
                             uint32_t* stmt_id);

private:
    bool multi_statements_allowed() const
    {
        return m_multi_statements_allowed;
    }

    uint64_t load_data_sent() const
    {
        return m_load_data_sent;
    }

    void append_load_data_sent(GWBUF* pBuffer)
    {
        m_load_data_sent += gwbuf_length(pBuffer);
    }

    void reset_load_data_sent()
    {
        m_load_data_sent = 0;
    }

    bool have_tmp_tables() const
    {
        return m_have_tmp_tables;
    }

    void set_have_tmp_tables(bool have_tmp_tables)
    {
        m_have_tmp_tables = have_tmp_tables;
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

    bool is_tmp_table(const std::string& table)
    {
        return m_tmp_tables.find(table) != m_tmp_tables.end();
    }

    /**
     * @brief Get the type of a stored prepared statement
     *
     * @param id The unique identifier for the prepared statement or the plaintext
     *           name of the prepared statement
     *
     * @return The type of the prepared statement
     */
    uint32_t ps_get_type(uint32_t id) const;
    uint32_t ps_get_type(std::string id) const;

    /**
     * @brief Remove a prepared statement
     *
     * @param id Statement identifier to remove
     */
    void ps_erase(std::string id);
    void ps_erase(uint32_t id);

    /**
     * @brief Get the internal ID for the given binary prepared statement
     *
     * @param buffer Buffer containing a binary protocol statement other than COM_STMT_PREPARE
     *
     * @return The internal ID of the prepared statement that the buffer contents refer to
     */
    uint32_t ps_id_internal_get(GWBUF* pBuffer);

    uint32_t get_route_target(uint8_t command, uint32_t qtype, HINT* pHints);

    MXS_SESSION* session() const
    {
        return m_pSession;
    }

    void log_transaction_status(GWBUF *querybuf, uint32_t qtype);

    static uint32_t determine_query_type(GWBUF *querybuf, int command);

    void check_create_tmp_table(GWBUF *querybuf, uint32_t type);

    bool is_read_tmp_table(GWBUF *querybuf, uint32_t qtype);

    void check_drop_tmp_table(GWBUF *querybuf);

    bool check_for_multi_stmt(GWBUF *buf, uint8_t packet_type);

    current_target_t
    handle_multi_temp_and_load(QueryClassifier::current_target_t current_target,
                               GWBUF *querybuf,
                               uint8_t packet_type,
                               uint32_t *qtype);

private:
    class PSManager;
    typedef std::shared_ptr<PSManager> SPSManager;

    typedef std::tr1::unordered_map<uint32_t, uint32_t> HandleMap;

    static bool find_table(QueryClassifier& qc, const std::string& table);
    static bool delete_table(QueryClassifier& qc, const std::string& table);


private:
    Handler*          m_pHandler;
    MXS_SESSION*      m_pSession;
    mxs_target_t      m_use_sql_variables_in;
    load_data_state_t m_load_data_state;          /**< The LOAD DATA state */
    uint64_t          m_load_data_sent;           /**< How much data has been sent */
    bool              m_have_tmp_tables;
    TableSet          m_tmp_tables;               /**< Set of temporary tables */
    bool              m_large_query;              /**< Set to true when processing payloads >= 2^24 bytes */
    bool              m_multi_statements_allowed; /**< Are multi-statements allowed */
    SPSManager        m_sPs_manager;
    HandleMap         m_ps_handles;               /** External ID to internal ID */
};

}
