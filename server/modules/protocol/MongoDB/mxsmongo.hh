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
#include <atomic>
#include <deque>
#include <sstream>
#include <stdexcept>
#include <bsoncxx/array/view.hpp>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongoc/mongoc.h>
#include <maxscale/buffer.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/target.hh>

class DCB;

class Config;
class ComERR;

namespace mxsmongo
{

class Command;

using DocumentBuilder = bsoncxx::builder::basic::document;
using ArrayBuilder = bsoncxx::builder::basic::array;
using bsoncxx::builder::basic::kvp;

namespace mongo
{

struct HEADER
{
    int32_t msg_len;
    int32_t request_id;
    int32_t response_to;
    int32_t opcode;
};

const int HEADER_LEN = sizeof(HEADER);

const int MAX_BSON_OBJECT_SIZE = 16 * 1024 * 1024;
const int MAX_MSG_SIZE         = 48 * 1000* 1000;
const int MAX_WRITE_BATCH_SIZE = 100000;

}

// The Mongo version we claim to be.
const int MONGO_VERSION_MAJOR = 4;
const int MONGO_VERSION_MINOR = 4;
const int MONGO_VERSION_PATCH = 1;

const char* const MONGO_ZVERSION = "4.4.1";

// See mongo: src/mongo/db/wire_version.h, 6 is the version that uses OP_MSG messages.
const int MIN_WIRE_VERSION = 6;
const int MAX_WIRE_VERSION = 6;

bsoncxx::document::value& topology_version();

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

inline int32_t set_byte1(uint8_t* pBuffer, uint8_t val)
{
    *pBuffer = val;
    return 1;
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

enum class Conversion
{
    STRICT,
    RELAXED
};

void append(DocumentBuilder& doc, const core::string_view& key, const bsoncxx::document::element& element);
inline void append(DocumentBuilder& doc, const std::string& key, const bsoncxx::document::element& element)
{
    append(doc, core::string_view(key.data(), key.length()), element);
}
inline void append(DocumentBuilder& doc, const char* zKey, const bsoncxx::document::element& element)
{
    append(doc, core::string_view(zKey), element);
}


template<class T>
T element_as(const std::string& command,
             const char* zKey,
             const bsoncxx::document::element& element,
             Conversion conversion = Conversion::STRICT);

template<>
bsoncxx::document::view element_as<bsoncxx::document::view>(const std::string& command,
                                                            const char* zKey,
                                                            const bsoncxx::document::element& element,
                                                            Conversion conversion);

template<>
bsoncxx::array::view element_as<bsoncxx::array::view>(const std::string& command,
                                                      const char* zKey,
                                                      const bsoncxx::document::element& element,
                                                      Conversion conversion);

template<>
std::string element_as<std::string>(const std::string& command,
                                    const char* zKey,
                                    const bsoncxx::document::element& element,
                                    Conversion conversion);

template<>
int64_t element_as<int64_t>(const std::string& command,
                            const char* zKey,
                            const bsoncxx::document::element& element,
                            Conversion conversion);

template<>
int32_t element_as<int32_t>(const std::string& command,
                            const char* zKey,
                            const bsoncxx::document::element& element,
                            Conversion conversion);
template<>
bool element_as<bool>(const std::string& command,
                      const char* zKey,
                      const bsoncxx::document::element& element,
                      Conversion conversion);


namespace error
{

#define MXSMONGO_ERROR(symbol, code, name) const int symbol = code;
#include "mxsmongoerror.hh"
#undef MXSMONGO_ERROR

int from_mariadb_code(int code);

const char* name(int code);

}

class Exception : public std::runtime_error
{
public:
    Exception(const std::string& message, int code)
        : std::runtime_error(message)
        , m_code(code)
    {
    }

    virtual GWBUF* create_response(const Command& command) const = 0;
    virtual void create_response(const Command& command, DocumentBuilder& doc) const = 0;

protected:
    int m_code;
};

class SoftError : public Exception
{
public:
    using Exception::Exception;

    GWBUF* create_response(const Command& command) const override final;
    void create_response(const Command& command, DocumentBuilder& doc) const override final;
};

class HardError : public Exception
{
public:
    using Exception::Exception;

    GWBUF* create_response(const Command& command) const override final;
    void create_response(const Command& command, DocumentBuilder& doc) const override final;
};

class MariaDBError : public Exception
{
public:
    MariaDBError(const ComERR& err);

    GWBUF* create_response(const Command& command) const override final;
    void create_response(const Command& command, DocumentBuilder& doc) const override final;

private:
    int         m_mariadb_code;
    std::string m_mariadb_message;
};

namespace key
{

// Mongo Commands
const char BATCHSIZE[]               = "batchSize";
const char BUILDINFO[]               = "buildInfo";
const char COUNT[]                   = "count";
const char CREATE[]                  = "create";
const char DELETE[]                  = "delete";
const char DELETES[]                 = "deletes";
const char DISTINCT[]                = "distinct";
const char DOCUMENTS[]               = "documents";
const char DROP[]                    = "drop";
const char DROPDATABASE[]            = "dropDatabase";
const char ENDSESSIONS[]             = "endSessions";
const char FILTER[]                  = "filter";
const char FIND[]                    = "find";
const char FIRSTBATCH[]              = "firstBatch";
const char GETCMDLINEOPTS[]          = "getCmdLineOpts";
const char GETFREEMONITORINGSTATUS[] = "getFreeMonitoringStatus";
const char GETLOG[]                  = "getLog";
const char INSERT[]                  = "insert";
const char ISMASTER[]                = "isMaster";
const char KEY[]                     = "key";
const char LIMIT[]                   = "limit";
const char LISTCOLLECTIONS[]         = "listCollections";
const char LISTDATABASES[]           = "listDatabases";
const char MULTI[]                   = "multi";
const char NAMEONLY[]                = "nameOnly";
const char NEXTBATCH[]               = "nextBatch";
const char ORDERED[]                 = "ordered";
const char PING[]                    = "ping";
const char PROJECTION[]              = "projection";
const char Q[]                       = "q";
const char QUERY[]                   = "query";
const char REPLSETGETSTATUS[]        = "replSetGetStatus";
const char SKIP[]                    = "skip";
const char SORT[]                    = "sort";
const char U[]                       = "u";
const char UPDATE[]                  = "update";
const char UPDATES[]                 = "updates";
const char WHATSMYURI[]              = "whatsmyuri";

// MaxScale Commands
const char MXSDIAGNOSE[]             = "mxsDiagnose";
const char MXSGETCONFIG[]            = "mxsGetConfig";
const char MXSSETCONFIG[]            = "mxsSetConfig";

};

bool get_integer(const bsoncxx::document::element& element, int64_t* pInt);
bool get_number_as_integer(const bsoncxx::document::element& element, int64_t* pInt);
bool get_number_as_double(const bsoncxx::document::element& element, double* pDouble);

/**
 * Converts an element to a value that can be used in comparisons.
 *
 * @param element  The element to be converted.
 *
 * @return A value expressed as a string; a number will just be the number, but a
 *         string will be enclosed in quotes.
 *
 * @throws SoftError(BAD_VALUE) if the element cannot be converted to a value.
 */
std::string to_value(const bsoncxx::document::element& element);

std::string to_string(const bsoncxx::document::element& element);

std::vector<std::string> projection_to_extractions(const bsoncxx::document::view& projection);

std::string query_to_where_condition(const bsoncxx::document::view& filter);
std::string query_to_where_clause(const bsoncxx::document::view& filter);

std::string sort_to_order_by(const bsoncxx::document::view& sort);

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

    Packet(const Packet&) = default;
    Packet& operator = (const Packet&) = default;

    Packet(const uint8_t* pData, const uint8_t* pEnd)
        : m_pEnd(pEnd)
        , m_pHeader(reinterpret_cast<const mongo::HEADER*>(pData))
    {
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
    const uint8_t*       m_pEnd;
    const mongo::HEADER* m_pHeader;
};

class Query final : public Packet
{
public:
    Query(const Packet& packet);

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

class Reply final : public Packet
{
public:
    Reply(const Packet& packet)
        : Packet(packet)
    {
        mxb_assert(opcode() == MONGOC_OPCODE_REPLY);

        const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(mongo::HEADER);

        pData += mxsmongo::get_byte4(pData, &m_flags);
        pData += mxsmongo::get_byte8(pData, &m_cursor_id);
        pData += mxsmongo::get_byte4(pData, &m_start_from);
        pData += mxsmongo::get_byte4(pData, &m_nReturned);

        while (pData < m_pEnd)
        {
            uint32_t size;
            mxsmongo::get_byte4(pData, &size);
            m_documents.push_back(bsoncxx::document::view { pData, size });
            pData += size;
        }

        mxb_assert(m_nReturned == (int)m_documents.size());
        mxb_assert(pData == m_pEnd);
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

class Msg final : public Packet
{
public:
    enum
    {
        NONE             = 0,
        CHECKSUM_PRESENT = 1 << 0,
        MORE_TO_COME     = 1 << 1,
        EXHAUST_ALLOWED  = 1 << 16
    };

    using DocumentVector = std::vector<bsoncxx::document::view>;
    using DocumentArguments = std::unordered_map<std::string, DocumentVector>;

    Msg(const Packet& packet);

    Msg(const Msg&) = default;
    Msg& operator = (const Msg&) = default;

    bool checksum_present() const
    {
        return (m_flags & CHECKSUM_PRESENT) ? true : false;
    }

    bool exhaust_allowed() const
    {
        return (m_flags & EXHAUST_ALLOWED) ? true : false;
    }

    bool more_to_come() const
    {
        return (m_flags & MORE_TO_COME) ? true : false;
    }

    const bsoncxx::document::view& document() const
    {
        return m_document;
    }

    const DocumentArguments& arguments() const
    {
        return m_arguments;
    }

    std::ostream& out(std::ostream& o) const override
    {
        Packet::out(o);
        o << "flags      : " << m_flags << "\n";
        o << "document   : " << bsoncxx::to_json(m_document) << "\n";
        o << "arguments  : " << "\n";

        for (const auto& rv : m_arguments)
        {
            o << rv.first << " ";

            bool first = true;
            for (const auto& doc  : rv.second)
            {
                if (!first)
                {
                    o << ", ";
                }
                else
                {
                    first = false;
                }

                o << bsoncxx::to_json(doc);
            }

            o << "\n";
        }

        return o;
    }

private:
    uint32_t                m_flags { 0 };
    bsoncxx::document::view m_document;
    DocumentArguments       m_arguments;
};

class Database;

class Mongo
{
public:
    class Context
    {
    public:
        Context(const Context&) = delete;
        Context& operator = (const Context&) = delete;

        Context(mxs::ClientConnection* pClient_connection,
                mxs::Component* pDownstream)
            : m_client_connection(*pClient_connection)
            , m_downstream(*pDownstream)
            , m_connection_id(++s_connection_id)
        {
        }

        mxs::ClientConnection& client_connection()
        {
            return m_client_connection;
        }

        mxs::Component& downstream()
        {
            return m_downstream;
        }

        int64_t connection_id() const
        {
            return m_connection_id;
        }

        int32_t current_request_id() const
        {
            return m_request_id;
        }

        int32_t next_request_id()
        {
            return ++m_request_id;
        }

    private:
        mxs::ClientConnection& m_client_connection;
        mxs::Component&        m_downstream;
        int32_t                m_request_id { 1 };
        int64_t                m_connection_id;

        static std::atomic_int64_t s_connection_id;
    };

    enum State
    {
        READY,  // Ready for a command.
        PENDING // A command is being executed.
    };

    Mongo(mxs::ClientConnection* pClient_connection,
          mxs::Component* pDownstream,
          Config* pConfig);
    ~Mongo();

    Mongo(const Mongo&) = delete;
    Mongo& operator = (const Mongo&) = delete;

    State state() const
    {
        return m_sDatabase ?  PENDING : READY;
    }

    bool is_pending() const
    {
        return state() == PENDING;
    }

    Context& context()
    {
        return m_context;
    }

    const Config& config() const
    {
        return m_config;
    }

    GWBUF* handle_request(GWBUF* pRequest);

    int32_t clientReply(GWBUF* sMariaDB_response, DCB* pDcb);

private:
    void kill_client();

    using SDatabase = std::unique_ptr<Database>;

    GWBUF* handle_query(GWBUF* pRequest, const mxsmongo::Query& req);
    GWBUF* handle_msg(GWBUF* pRequest, const mxsmongo::Msg& req);

    State              m_state { READY };
    Context            m_context;
    Config&            m_config;
    std::deque<GWBUF*> m_requests;
    SDatabase          m_sDatabase;
};

}

inline std::ostream& operator << (std::ostream& out, const mxsmongo::Packet& x)
{
    x.out(out);
    return out;
}
