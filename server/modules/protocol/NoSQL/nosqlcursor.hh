/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include <set>
#include <string>
#include <vector>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <mysql.h>
#include <maxbase/stopwatch.hh>
#include <maxscale/buffer.hh>

namespace nosql
{

class NoSQLCursor
{
public:
    NoSQLCursor(const NoSQLCursor& rhs) = delete;

    static std::unique_ptr<NoSQLCursor> create(const std::string& ns);

    static std::unique_ptr<NoSQLCursor> create(const std::string& ns,
                                               const std::vector<std::string>& extractions,
                                               mxs::Buffer&& mariadb_response);

    static std::unique_ptr<NoSQLCursor> get(const std::string& collection, int64_t id);
    static void put(std::unique_ptr<NoSQLCursor> sCursor);
    static std::set<int64_t> kill(const std::string& collection, const std::vector<int64_t>& ids);
    static void kill_idle(const mxb::TimePoint& now, const std::chrono::seconds& timeout);

    static void start_purging_idle_cursors(const std::chrono::seconds& cursor_timeout);

    const std::string& ns() const
    {
        return m_ns;
    }

    int64_t id() const
    {
        return m_id;
    }

    bool exhausted() const
    {
        return m_exhausted;
    }

    void create_first_batch(bsoncxx::builder::basic::document& doc, int32_t nBatch, bool single_batch);
    void create_next_batch(bsoncxx::builder::basic::document& doc, int32_t nBatch);

    static void create_first_batch(bsoncxx::builder::basic::document& doc,
                                   const std::string& ns);

    const mxb::TimePoint& last_use() const
    {
        return m_used;
    }

private:
    NoSQLCursor(const std::string& ns);

    NoSQLCursor(const std::string& ns,
                const std::vector<std::string>& extractions,
                mxs::Buffer&& mariadb_response);

    void initialize();

    void touch();

    enum class Result
    {
        PARTIAL,
        COMPLETE
    };

    void create_batch(bsoncxx::builder::basic::document& doc,
                      const std::string& which_batch,
                      int32_t nBatch,
                      bool single_batch);
    Result create_batch(bsoncxx::builder::basic::array& batch, int32_t nBatch);

    std::string                   m_ns;
    int64_t                       m_id;
    bool                          m_exhausted;
    std::vector<std::string>      m_extractions;
    mxs::Buffer                   m_mariadb_response;
    uint8_t*                      m_pBuffer { nullptr };
    std::vector<std::string>      m_names;
    std::vector<enum_field_types> m_types;
    mxb::TimePoint                m_used;
};

}
