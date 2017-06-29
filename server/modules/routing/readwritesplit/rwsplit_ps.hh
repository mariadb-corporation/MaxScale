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

#include "readwritesplit.hh"

#include <tr1/unordered_map>
#include <string>

/** Prepared statement ID to type maps for text protocols */
typedef std::tr1::unordered_map<uint32_t, uint32_t>    BinaryPSMap;
typedef std::tr1::unordered_map<std::string, uint32_t> TextPSMap;

/** Class for tracking prepared statement types by PS statement ID */
class PSManager
{
    PSManager(const PSManager&);
    PSManager& operator =(const PSManager&);

public:
    PSManager();
    ~PSManager();

    /**
     * @brief Store and process a prepared statement
     *
     * @param buffer Buffer containing either a text or a binary protocol
     *               prepared statement
     * @param id     The unique ID for this statement
     */
    void store(GWBUF* buffer, uint32_t id);

    /**
     * @brief Get the type of a stored prepared statement
     *
     * @param id The unique identifier for the prepared statement or the plaintext
     *           name of the prepared statement
     *
     * @return The type of the prepared statement
     */
    uint32_t get_type(uint32_t id) const;
    uint32_t get_type(std::string id) const;

    /**
     * @brief Remove a prepared statement
     *
     * @param id Statement identifier to remove
     */
    void erase(std::string id);
    void erase(uint32_t id);

private:
    BinaryPSMap m_binary_ps;
    TextPSMap   m_text_ps;
};

/**
 * @brief Get the type of a prepared statement
 *
 * @param buffer Buffer containing either a text or a binary prepared statement
 *
 * @return The type of the prepared statement
 */
uint32_t get_prepare_type(GWBUF* buffer);

/**
 * @brief Extract text identifier of a PREPARE or EXECUTE statement
 *
 * @param buffer Buffer containing a PREPARE or EXECUTE command
 *
 * @return The string identifier of the statement
 */
std::string get_text_ps_id(GWBUF* buffer);

/**
 * @brief Replace the ID of a binary protocol statement
 *
 * @param buffer Buffer containing a binary protocol statement with an ID
 * @param id     ID to insert into the buffer
 */
void replace_binary_ps_id(GWBUF* buffer, uint32_t id);
