/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

#include "nosqlcommand.hh"
#include <maxbase/string.hh>
#include <maxbase/worker.hh>
#include "nosqldatabase.hh"
#include "crc32.h"

using mxb::Worker;

using namespace std;

namespace
{

using namespace nosql;

uint32_t (*crc32_func)(const void *, size_t) = wiredtiger_crc32c_func();

}


namespace nosql
{

//
// Command
//
Command::~Command()
{
    free_request();
}

namespace
{

static CacheKey empty_key;

}

const CacheKey& Command::cache_key() const
{
    return empty_key;
}

bool Command::is_admin() const
{
    return false;
}

string Command::to_json() const
{
    return "";
}

void Command::patch_response(GWBUF& response,
                             int32_t request_id,
                             int32_t response_to,
                             ResponseChecksum response_checksum)
{
    uint8_t* pData = response.data();
    protocol::HEADER* pHeader = reinterpret_cast<protocol::HEADER*>(pData);

    pHeader->request_id = request_id;
    pHeader->response_to = response_to;

    size_t nData = response.length() - sizeof(uint32_t);

    uint32_t checksum = 0;

    if (response_checksum == ResponseChecksum::UPDATE)
    {
        checksum = crc32_func(pData, nData);
    }

    pData += nData;

    protocol::set_byte4(pData, checksum);
}

void Command::free_request()
{
    m_request.clear();
}

void Command::send_downstream(const string& sql)
{
    if (m_database.config().should_log_out())
    {
        MXB_NOTICE("SQL: %s", sql.c_str());
    }

    uint8_t seq_no = 1;

    const char* zSql = sql.data();
    const char* end = zSql + sql.length();

    size_t nRemaining = end - zSql;
    size_t payload_len = (nRemaining + 1 > MAX_PAYLOAD_LEN ? MAX_PAYLOAD_LEN : nRemaining + 1);
    size_t sql_len = payload_len - 1; // First packet, 1 byte for the command byte.

    m_database.context().downstream().routeQuery(mariadb::create_query(std::string_view(zSql, sql_len)));

    zSql += sql_len;
    nRemaining -= sql_len;

    while (nRemaining != 0 || payload_len == MAX_PAYLOAD_LEN)
    {
        payload_len = (nRemaining > MAX_PAYLOAD_LEN ? MAX_PAYLOAD_LEN : nRemaining);
        sql_len = payload_len; // NOT first packet, no command byte is present.

        m_database.context().downstream().routeQuery(mariadb::create_packet(seq_no++, zSql, sql_len));

        zSql += sql_len;
        nRemaining -= sql_len;
    }

    m_last_statement = sql;
}

void Command::send_downstream_via_loop(const string& sql)
{
    m_database.context().worker().lcall([this, sql]() {
            send_downstream(sql);
        });
}

MXS_SESSION& Command::session()
{
    return m_database.context().session();
}

namespace
{

string unexpected_message(const std::string& who, const std::string& statement)
{
    ostringstream ss;
    ss << "Unexpected response received by " << who << " from backend for: " << statement;

    return ss.str();
}

}

void Command::throw_unexpected_packet()
{
    throw HardError(unexpected_message(description(), m_last_statement), error::INTERNAL_ERROR);
}

mxs::RoutingWorker& Command::worker() const
{
    return m_database.context().worker();
}

GWBUF* Command::create_response(const bsoncxx::document::value& doc, IsError is_error) const
{
    GWBUF* pResponse = nullptr;

    if (!is_silent())
    {
        switch (m_response_kind)
        {
        case ResponseKind::REPLY:
            pResponse = create_reply_response(doc, is_error);
            break;

        case ResponseKind::MSG:
        case ResponseKind::MSG_WITH_CHECKSUM:
            pResponse = create_msg_response(doc);
            break;

        case ResponseKind::NONE:
            mxb_assert(!true);
        }
    }

    return pResponse;
}

void Command::log_back(const char* zContext, const bsoncxx::document::value& doc) const
{
    if (m_database.config().should_log_back())
    {
        MXB_NOTICE("%s: %s", zContext, bsoncxx::to_json(doc).c_str());
    }
}

//static
pair<GWBUF*, uint8_t*> Command::create_reply_response_buffer(int32_t request_id,
                                                             int32_t response_to,
                                                             int64_t cursor_id,
                                                             int32_t starting_from,
                                                             size_t size_of_documents,
                                                             size_t nDocuments,
                                                             IsError is_error)
{
    // TODO: In the following is assumed that whatever is returned will
    // TODO: fit into a MongoDB packet.

    int32_t response_flags = 0;
    if (is_error == IsError::YES)
    {
        response_flags |= MONGOC_REPLY_QUERY_FAILURE;
    }
    int32_t number_returned = nDocuments;

    size_t response_size = protocol::HEADER_LEN
        + sizeof(response_flags) + sizeof(cursor_id) + sizeof(starting_from) + sizeof(number_returned)
        + size_of_documents;

    GWBUF response(response_size);

    auto* pRes_hdr = reinterpret_cast<protocol::HEADER*>(response.data());
    pRes_hdr->msg_len = response_size;
    pRes_hdr->request_id = request_id;
    pRes_hdr->response_to = response_to;
    pRes_hdr->opcode = MONGOC_OPCODE_REPLY;

    uint8_t* pData = response.data() + protocol::HEADER_LEN;

    pData += protocol::set_byte4(pData, response_flags);
    pData += protocol::set_byte8(pData, cursor_id);
    pData += protocol::set_byte4(pData, starting_from);
    pData += protocol::set_byte4(pData, number_returned);

    return make_pair(nosql::gwbuf_to_gwbufptr(std::move(response)), pData);
}

//static
GWBUF* Command::create_reply_response(int32_t request_id,
                                      int32_t response_to,
                                      int64_t cursor_id,
                                      int32_t position,
                                      size_t size_of_documents,
                                      const vector<bsoncxx::document::value>& documents)
{
    GWBUF* pResponse;
    uint8_t* pData;

    tie(pResponse, pData) = create_reply_response_buffer(request_id,
                                                         response_to,
                                                         cursor_id,
                                                         position,
                                                         size_of_documents,
                                                         documents.size(),
                                                         IsError::NO);

    for (const auto& doc : documents)
    {
        auto view = doc.view();
        size_t size = view.length();

        memcpy(pData, view.data(), view.length());
        pData += view.length();
    }

    return pResponse;
}

GWBUF* Command::create_reply_response(int64_t cursor_id,
                                      int32_t position,
                                      size_t size_of_documents,
                                      const vector<bsoncxx::document::value>& documents) const
{
    return create_reply_response(m_database.context().next_request_id(),
                                 m_request_id,
                                 cursor_id,
                                 position,
                                 size_of_documents,
                                 documents);
}

GWBUF* Command::create_reply_response(const bsoncxx::document::value& doc, IsError is_error) const
{
    log_back("Response(Reply)", doc);

    auto doc_view = doc.view();
    size_t doc_len = doc_view.length();

    GWBUF* pResponse;
    uint8_t* pData;

    tie(pResponse, pData) = create_reply_response_buffer(m_database.context().next_request_id(), m_request_id,
                                                         0, 0, doc_len, 1, is_error);

    memcpy(pData, doc_view.data(), doc_view.length());

    return pResponse;
}

GWBUF* Command::create_msg_response(const bsoncxx::document::value& doc) const
{
    log_back("Response(Msg)", doc);

    uint32_t flag_bits = 0;
    uint8_t kind = 0;
    uint32_t doc_length = doc.view().length();

    size_t response_size = protocol::HEADER_LEN + sizeof(flag_bits) + sizeof(kind) + doc_length;

    bool append_checksum = (m_response_kind == ResponseKind::MSG_WITH_CHECKSUM);

    if (append_checksum)
    {
        flag_bits |= packet::Msg::CHECKSUM_PRESENT;
        response_size += sizeof(uint32_t); // sizeof checksum
    }

    GWBUF response(response_size);

    auto* pRes_hdr = reinterpret_cast<protocol::HEADER*>(response.data());
    pRes_hdr->msg_len = response_size;
    pRes_hdr->request_id = m_database.context().next_request_id();
    pRes_hdr->response_to = m_request_id;
    pRes_hdr->opcode = MONGOC_OPCODE_MSG;

    uint8_t* pData = response.data() + protocol::HEADER_LEN;

    pData += protocol::set_byte4(pData, flag_bits);

    pData += protocol::set_byte1(pData, kind);
    memcpy(pData, doc.view().data(), doc_length);
    pData += doc_length;

    if (append_checksum)
    {
        uint32_t checksum = crc32_func(response.data(), response_size - sizeof(uint32_t));
        pData += protocol::set_byte4(pData, checksum);
    }

    return nosql::gwbuf_to_gwbufptr(std::move(response));
}

}
