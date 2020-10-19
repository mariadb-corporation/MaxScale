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
#include <endian.h>
#include <maxscale/target.hh>

#include <bsoncxx/json.hpp>
// Claim we are part of Mongo, so that we can include internal headers.
#define MONGOC_COMPILATION
// libmongoc is C and they use 'new' and 'delete' both as names of fields
// in structures and as arguments in function prototypes. So we redefine
// them temporarily.
#define new new_arg
#define delete delete_arg
#include <mongoc-flags.h>
#include <mongoc-flags-private.h>
#include <mongoc-rpc-private.h>
#include <mongoc-server-description-private.h>
#undef new
#undef delete

#include <mongoc/mongoc-opcode.h>
#include <maxscale/buffer.hh>

const int MXSMONGO_HEADER_LEN       = sizeof(mongoc_rpc_header_t);
const int MXSMONGO_QUERY_HEADER_LEN = sizeof(mongoc_rpc_query_t);

namespace mxsmongo
{

inline int32_t get_byte1(const uint8_t* pBuffer, uint8_t* pHost8)
{
    *pHost8 = *pBuffer;
    return 1;
}

inline int32_t get_byte4(const uint8_t* pBuffer, uint32_t* pHost32)
{
    uint32_t le32 = *(reinterpret_cast<const uint32_t*>(pBuffer));
    *pHost32 = le32toh(le32);
    return 4;
}

inline int32_t get_byte4(const uint8_t* pBuffer, int32_t* pHost32)
{
    uint32_t host32;
    auto rv = get_byte4(pBuffer, &host32);
    *pHost32 = host32;
    return rv;
}

inline uint32_t get_byte4(const uint8_t* pBuffer)
{
    uint32_t host32;
    get_byte4(pBuffer, &host32);
    return host32;
}

inline int32_t get_byte8(const uint8_t* pBuffer, uint64_t* pHost64)
{
    uint64_t le64 = *(reinterpret_cast<const uint64_t*>(pBuffer));
    *pHost64 = le64toh(le64);
    return 8;
}

inline int32_t get_byte8(const uint8_t* pBuffer, int64_t* pHost64)
{
    uint64_t host64;
    auto rv = get_byte8(pBuffer, &host64);
    *pHost64 = host64;
    return rv;
}

inline uint64_t get_byte8(const uint8_t* pBuffer)
{
    uint64_t host64;
    get_byte8(pBuffer, &host64);
    return host64;
}

inline int32_t get_zstring(const uint8_t* pBuffer, const char** pzString)
{
    const char* zString = reinterpret_cast<const char*>(pBuffer);
    *pzString = zString;
    return strlen(zString) + 1;
}

inline int32_t set_byte4(uint8_t* pBuffer, uint32_t val)
{
    uint32_t le32 = htole32(val);
    auto ple32 = reinterpret_cast<uint32_t*>(pBuffer);
    *ple32 = le32;
    return 4;
}

inline int32_t set_byte8(uint8_t* pBuffer, uint64_t val)
{
    uint64_t le64 = htole64(val);
    auto ple64 = reinterpret_cast<uint64_t*>(pBuffer);
    *ple64 = le64;
    return 8;
}

const char* opcode_to_string(int code);

namespace keys
{

const char ISMASTER[] = "ismaster";

};

enum class Command
{
    UNKNOWN,

    ISMASTER,
};

Command get_command(const bsoncxx::document::view& doc);

class Packet
{
public:
    enum OpCode
    {
        REPLY = MONGOC_OPCODE_REPLY,
        UPDATE = MONGOC_OPCODE_UPDATE,
        INSERT = MONGOC_OPCODE_INSERT,
        QUERY = MONGOC_OPCODE_QUERY,
        GET = MONGOC_OPCODE_GET_MORE,
        DELETE = MONGOC_OPCODE_DELETE,
        KILL = MONGOC_OPCODE_KILL_CURSORS,
        COMPRESSED = MONGOC_OPCODE_COMPRESSED,
        MSG = MONGOC_OPCODE_MSG,
    };

    Packet(const uint8_t* pData, const uint8_t* pEnd)
        : m_pData(pData)
        , m_pEnd(pEnd)
        , m_pHeader(reinterpret_cast<const mongoc_rpc_header_t*>(m_pData))
    {
        m_pData += sizeof(mongoc_rpc_header_t);
    }

    Packet(const uint8_t* pData, int32_t size)
        : Packet(pData, pData + size)
    {
    }

    Packet(const std::vector<uint8_t>& buffer)
        : Packet(buffer.data(), buffer.data() + buffer.size())
    {
    }

    Packet(const GWBUF* pBuffer)
        : Packet(gwbuf_link_data(pBuffer), gwbuf_link_data(pBuffer) + gwbuf_link_length(pBuffer))
    {
        mxb_assert(gwbuf_is_contiguous(pBuffer));
    }

    int32_t msg_len() const
    {
        return m_pHeader->msg_len;
    }

    int32_t request_id() const
    {
        return m_pHeader->request_id;
    }

    int32_t response_to() const
    {
        return m_pHeader->response_to;
    }

    int32_t opcode() const
    {
        return m_pHeader->opcode;
    }

    virtual std::ostream& out(std::ostream& o) const
    {
        o << "msg_len    : " << msg_len() << "\n";
        o << "request_id : " << request_id() << "\n";
        o << "response_to: " << response_to() << "\n";
        o << "opcode     : " << opcode_to_string(opcode()) << "\n";

        return o;
    }

    std::string to_string() const
    {
        std::stringstream ss;
        out(ss);
        return ss.str();
    }

protected:
    Packet(const Packet&) = default;
    Packet& operator = (const Packet&) = default;

protected:
    const uint8_t*             m_pData;
    const uint8_t*             m_pEnd;
    const mongoc_rpc_header_t* m_pHeader;
};

class Query : public Packet
{
public:
    Query(const Packet& packet)
        : Packet(packet)
    {
        mxb_assert(opcode() == MONGOC_OPCODE_QUERY);

        m_pData += mxsmongo::get_byte4(m_pData, &m_flags);
        m_pData += mxsmongo::get_zstring(m_pData, &m_zCollection);
        m_pData += mxsmongo::get_byte4(m_pData, &m_nSkip);
        m_pData += mxsmongo::get_byte4(m_pData, &m_nReturn);

        uint32_t size;
        mxsmongo::get_byte4(m_pData, &size);
        m_query = bsoncxx::document::view { m_pData, size };
        m_pData += size;

        if (m_pData < m_pEnd)
        {
            mxsmongo::get_byte4(m_pData, &size);
            mxb_assert(m_pEnd - m_pData == size);
            m_fields = bsoncxx::document::view { m_pData, size };
            m_pData += size;
        }

        mxb_assert(m_pData == m_pEnd);
    }

    uint32_t flags() const
    {
        return m_flags;
    }

    const char* zCollection() const
    {
        return m_zCollection;
    }

    std::string collection() const
    {
        return m_zCollection;
    }

    uint32_t nSkip() const
    {
        return m_nSkip;
    }

    uint32_t nReturn() const
    {
        return m_nReturn;
    }

    const bsoncxx::document::view& query() const
    {
        return m_query;
    }

    const bsoncxx::document::view& fields() const
    {
        return m_fields;
    }

    Query(const Query&) = default;
    Query& operator = (const Query&) = default;

    std::ostream& out(std::ostream& o) const override
    {
        Packet::out(o);
        o << "flags      : " << m_flags << "\n";
        o << "collection : " << m_zCollection << "\n";
        o << "nSkip      : " << m_nSkip << "\n";
        o << "nReturn    : " << m_nReturn << "\n";
        o << "query      : " << bsoncxx::to_json(m_query) << "\n";
        o << "fields     : " << bsoncxx::to_json(m_fields);
        return o;
    }

protected:
    uint32_t                m_flags;
    const char*             m_zCollection;
    uint32_t                m_nSkip;
    uint32_t                m_nReturn;
    bsoncxx::document::view m_query;
    bsoncxx::document::view m_fields;
};

class Reply : public Packet
{
public:
    Reply(const Packet& packet)
        : Packet(packet)
    {
        mxb_assert(opcode() == MONGOC_OPCODE_REPLY);

        m_pData += mxsmongo::get_byte4(m_pData, &m_flags);
        m_pData += mxsmongo::get_byte8(m_pData, &m_cursor_id);
        m_pData += mxsmongo::get_byte4(m_pData, &m_start_from);
        m_pData += mxsmongo::get_byte4(m_pData, &m_nReturned);

        while (m_pData < m_pEnd)
        {
            uint32_t size;
            mxsmongo::get_byte4(m_pData, &size);
            m_documents.push_back(bsoncxx::document::view { m_pData, size });
            m_pData += size;
        }

        mxb_assert(m_nReturned == (int)m_documents.size());
        mxb_assert(m_pData == m_pEnd);
    }

    Reply(const Reply&) = default;
    Reply& operator = (const Reply&) = default;

    std::ostream& out(std::ostream& o) const override
    {
        Packet::out(o);
        o << "flags      : " << m_flags << "\n";
        o << "cursorId   : " << m_cursor_id << "\n";
        o << "start_from : " << m_start_from << "\n";
        o << "nReturned  : " << m_nReturned << "\n";
        o << "documents  : \n";

        for (const auto& doc : m_documents)
        {
            o << bsoncxx::to_json(doc) << "\n";
        }

        return o;
    }

protected:
    int32_t                              m_flags;
    int64_t                              m_cursor_id;
    int32_t                              m_start_from;
    int32_t                              m_nReturned;
    std::vector<bsoncxx::document::view> m_documents;
};

class Msg : public Packet
{
public:
    Msg(const Packet& packet)
        : Packet(packet)
    {
        mxb_assert(opcode() == MONGOC_OPCODE_MSG);

        m_pData += mxsmongo::get_byte4(m_pData, &m_flags);

        if (checksum_present())
        {
            // TODO: Check checksum.
        }

        const uint8_t* pSections_end = m_pEnd - (checksum_present() ? sizeof(uint32_t) : 0);
        size_t sections_size = pSections_end - m_pData;

        while (m_pData < pSections_end)
        {
            uint8_t kind;
            m_pData += mxsmongo::get_byte1(m_pData, &kind);

            switch (kind)
            {
            case 0:
                // Body section encoded as a single BSON object.
                {
                    uint32_t size;
                    mxsmongo::get_byte4(m_pData, &size);
                    m_documents.push_back(bsoncxx::document::view { m_pData, size });
                    m_pData += size;
                }
                break;

            case 1:
                // TODO
                mxb_assert(!true);
                break;

            default:
                mxb_assert(!true);
            }
        }

        mxb_assert(m_pData == pSections_end);
    }

    Msg(const Msg&) = default;
    Msg& operator = (const Msg&) = default;

    bool checksum_present() const
    {
        return (m_flags & MONGOC_MSG_CHECKSUM_PRESENT) ? true : false;
    }

    bool exhaust_allowed() const
    {
        return (m_flags & MONGOC_MSG_EXHAUST_ALLOWED) ? true : false;
    }

    bool more_to_come() const
    {
        return (m_flags & MONGOC_MSG_MORE_TO_COME) ? true : false;
    }

    const std::vector<bsoncxx::document::view>& documents() const
    {
        return m_documents;
    }

    std::ostream& out(std::ostream& o) const override
    {
        Packet::out(o);
        o << "flags      : " << m_flags << "\n";
        o << "sections   : \n";

        for (const auto& doc : m_documents)
        {
            o << bsoncxx::to_json(doc) << "\n";
        }

        return o;
    }

protected:
    uint32_t                             m_flags;
    std::vector<bsoncxx::document::view> m_documents;
};

class Database;

class Mongo
{
public:
    enum State
    {
        READY,  // Ready for a command.
        PENDING // A command is being executed.
    };

    Mongo();
    ~Mongo();

    Mongo(const Mongo&) = delete;
    Mongo& operator = (const Mongo&) = delete;

    GWBUF* handle_request(const mxsmongo::Packet& req, mxs::Component& downstream);

    GWBUF* translate(GWBUF* pMariaDB_response);

private:
    using SDatabase = std::unique_ptr<Database>;

    GWBUF* handle_query(const mxsmongo::Query& req, mxs::Component& downstream);
    GWBUF* handle_msg(const mxsmongo::Msg& req, mxs::Component& downstream);

    GWBUF* create_ismaster_response(const mxsmongo::Packet& request);

    State     m_state { READY };
    int32_t   m_request_id { 1 };
    SDatabase m_sDatabase;

};

}

inline std::ostream& operator << (std::ostream& out, const mxsmongo::Packet& x)
{
    x.out(out);
    return out;
}
