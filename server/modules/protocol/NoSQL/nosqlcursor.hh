/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include <string>
#include <vector>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <mysql.h>
#include <maxbase/stopwatch.hh>
#include <maxscale/buffer.hh>

namespace mxsmongo
{

class MongoCursor
{
public:
    MongoCursor(const std::string& ns);
    MongoCursor(MongoCursor&& rhs) = default;
    MongoCursor(const std::string& ns,
                const std::vector<std::string>& extractions,
                mxs::Buffer&& mariadb_response);

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
