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
#include "rwsplit_ps.hh"

#include <maxscale/alloc.h>
#include <maxscale/query_classifier.h>
#include <maxscale/protocol/mysql.h>

uint32_t get_prepare_type(GWBUF* buffer)
{
    uint32_t type;

    if (mxs_mysql_get_command(buffer) == MXS_COM_STMT_PREPARE)
    {
        // TODO: This could be done inside the query classifier
        size_t packet_len = gwbuf_length(buffer);
        size_t payload_len = packet_len - MYSQL_HEADER_LEN;
        GWBUF* stmt = gwbuf_alloc(packet_len);
        uint8_t* ptr = GWBUF_DATA(stmt);

        // Payload length
        *ptr++ = payload_len;
        *ptr++ = (payload_len >> 8);
        *ptr++ = (payload_len >> 16);
        // Sequence id
        *ptr++ = 0x00;
        // Command
        *ptr++ = MXS_COM_QUERY;

        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN + 1, payload_len - 1, ptr);
        type = qc_get_type_mask(stmt);

        gwbuf_free(stmt);
    }
    else
    {
        GWBUF* stmt = qc_get_preparable_stmt(buffer);
        ss_dassert(stmt);

        type = qc_get_type_mask(stmt);
    }

    ss_dassert((type & (QUERY_TYPE_PREPARE_STMT | QUERY_TYPE_PREPARE_NAMED_STMT)) == 0);

    return type;
}

std::string get_text_ps_id(GWBUF* buffer)
{
    std::string rval;
    char* name = qc_get_prepare_name(buffer);

    if (name)
    {
        rval = name;
        MXS_FREE(name);
    }

    return rval;
}

void replace_binary_ps_id(GWBUF* buffer, uint32_t id)
{
    uint8_t* ptr = GWBUF_DATA(buffer) + MYSQL_PS_ID_OFFSET;
    gw_mysql_set_byte4(ptr, id);
}

PSManager::PSManager()
{
}

PSManager::~PSManager()
{
}

void PSManager::erase(uint32_t id)
{
    if (m_binary_ps.erase(id) == 0)
    {
        MXS_WARNING("Closing unknown prepared statement with ID %u", id);
    }
}

void PSManager::erase(std::string id)
{
    if (m_text_ps.erase(id) == 0)
    {
        MXS_WARNING("Closing unknown prepared statement with ID '%s'", id.c_str());
    }
}

uint32_t PSManager::get_type(std::string id) const
{
    uint32_t rval = QUERY_TYPE_UNKNOWN;
    TextPSMap::const_iterator it = m_text_ps.find(id);

    if (it != m_text_ps.end())
    {
        rval = it->second;
    }
    else
    {
        MXS_WARNING("Using unknown prepared statement with ID '%s'", id.c_str());
    }

    return rval;
}


uint32_t PSManager::get_type(uint32_t id) const
{
    uint32_t rval = QUERY_TYPE_UNKNOWN;
    BinaryPSMap::const_iterator it = m_binary_ps.find(id);

    if (it != m_binary_ps.end())
    {
        rval = it->second;
    }
    else
    {
        MXS_WARNING("Using unknown prepared statement with ID %u", id);
    }

    return rval;
}

void PSManager::store(GWBUF* buffer, uint32_t id)
{
    ss_dassert(mxs_mysql_get_command(buffer) == MXS_COM_STMT_PREPARE ||
               qc_query_is_type(qc_get_type_mask(buffer),
                                QUERY_TYPE_PREPARE_NAMED_STMT));

    switch (mxs_mysql_get_command(buffer))
    {
    case MXS_COM_QUERY:
        m_text_ps[get_text_ps_id(buffer)] = get_prepare_type(buffer);
        break;

    case MXS_COM_STMT_PREPARE:
        m_binary_ps[id] = get_prepare_type(buffer);
        break;

    default:
        ss_dassert(!true);
        break;
    }
}
