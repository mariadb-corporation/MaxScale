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

#include "mongodbclient.hh"
#include <string>
#include <vector>
#include <bsoncxx/builder/basic/array.hpp>
#include <mysql.h>
#include <maxscale/buffer.hh>

namespace mxsmongo
{

class Command;

class MongoCursor
{
public:
    MongoCursor();
    MongoCursor(const std::vector<std::string>& extractions,
                mxs::Buffer&& mariadb_response);

    GWBUF* create_first_batch(const Command& command, int32_t nBatch);
    GWBUF* create_next_batch(const Command& command, int32_t nBatch);

private:
    void initialize();

    enum class Result
    {
        PARTIAL,
        COMPLETE
    };

    GWBUF* create_batch(const Command& command, const std::string& which_batch, int32_t nBatch);
    Result create_batch(bsoncxx::builder::basic::array& batch, int32_t nBatch);

    int64_t                       m_id;
    std::vector<std::string>      m_extractions;
    mxs::Buffer                   m_mariadb_response;
    uint8_t*                      m_pBuffer { nullptr };
    std::vector<std::string>      m_names;
    std::vector<enum_field_types> m_types;
};

}
