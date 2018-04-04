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
#include <maxscale/router.h>
#include <maxscale/session.h>

namespace maxscale
{

class QueryClassifier
{
    QueryClassifier(const QueryClassifier&) = delete;
    QueryClassifier& operator = (const QueryClassifier&) = delete;

public:
    // NOTE: For the time being these must be exactly like the ones in readwritesplit.hh
    enum
    {
        TARGET_UNDEFINED = 0x00,
        TARGET_MASTER    = 0x01,
        TARGET_SLAVE     = 0x02,
        TARGET_ALL       = 0x08
    };

    /** States of a LOAD DATA LOCAL INFILE */
    enum load_data_state_t
    {
        LOAD_DATA_INACTIVE,         /**< Not active */
        LOAD_DATA_START,            /**< Current query starts a load */
        LOAD_DATA_ACTIVE,           /**< Load is active */
        LOAD_DATA_END               /**< Current query contains an empty packet that ends the load */
    };

    QueryClassifier(MXS_SESSION* pSession,
                    mxs_target_t use_sql_variables_in);

    load_data_state_t load_data_state() const
    {
        return m_load_data_state;
    }

    void set_load_data_state(load_data_state_t state)
    {
        m_load_data_state = state;
    }

    bool have_tmp_tables() const
    {
        return m_have_tmp_tables;
    }

    void set_have_tmp_tables(bool have_tmp_tables)
    {
        m_have_tmp_tables = have_tmp_tables;
    }

    bool large_query() const
    {
        return m_large_query;
    }

    void set_large_query(bool large_query)
    {
        m_large_query = large_query;
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

    uint32_t get_route_target(uint8_t command, uint32_t qtype);

private:
    class PSManager;
    typedef std::shared_ptr<PSManager> SPSManager;

private:
    MXS_SESSION*      m_pSession;
    mxs_target_t      m_use_sql_variables_in;
    load_data_state_t m_load_data_state;
    bool              m_have_tmp_tables;
    bool              m_large_query;          /**< Set to true when processing payloads >= 2^24 bytes */
    SPSManager        m_sPs_manager;
};

}
