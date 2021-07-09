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
#include <endian.h>
#include <atomic>
#include <deque>
#include <set>
#include <sstream>
#include <stdexcept>
#include <bsoncxx/array/view.hpp>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongoc/mongoc.h>
#include <maxbase/stopwatch.hh>
#include <maxscale/buffer.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/target.hh>
#include "nosqlcursor.hh"

class DCB;

class Config;
class ComERR;

namespace nosql
{

class Command;

using DocumentBuilder = bsoncxx::builder::basic::document;
using ArrayBuilder = bsoncxx::builder::basic::array;
using bsoncxx::builder::basic::kvp;

namespace protocol
{

namespace type
{
// These are the one we recognize, but there are more.
const int32_t DOUBLE = 1;
const int32_t STRING = 2;
const int32_t OBJECT = 3;
const int32_t ARRAY = 4;
const int32_t BOOL = 8;
const int32_t INT32 = 16;
};

namespace alias
{
extern const char* DOUBLE;
extern const char* STRING;
extern const char* OBJECT;
extern const char* ARRAY;
extern const char* BOOL;
extern const char* INT32;

int32_t to_type(const std::string& alias);

inline int32_t to_type(const char* zAlias)
{
    return to_type(std::string(zAlias));
}

inline int32_t to_type(const bsoncxx::stdx::string_view& alias)
{
    return to_type(std::string(alias.data(), alias.length()));
}

}

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

}

// The MongoDB version we claim to be.
const int NOSQL_VERSION_MAJOR = 4;
const int NOSQL_VERSION_MINOR = 4;
const int NOSQL_VERSION_PATCH = 1;

const char* const NOSQL_ZVERSION = "4.4.1";

// See MongoDB: src/mongo/db/wire_version.h, 6 is the version that uses OP_MSG messages.
const int MIN_WIRE_VERSION = 6;
const int MAX_WIRE_VERSION = 6;

bsoncxx::document::value& topology_version();

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

#define NOSQL_ERROR(symbol, code, name) const int symbol = code;
#include "nosqlerror.hh"
#undef NOSQL_ERROR

int from_mariadb_code(int code);

const char* name(int code);

}

class LastError
{
public:
    virtual ~LastError() {}

    virtual void populate(DocumentBuilder& doc) = 0;
};

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

    virtual std::unique_ptr<LastError> create_last_error() const = 0;

protected:
    int m_code;
};

class SoftError : public Exception
{
public:
    using Exception::Exception;

    GWBUF* create_response(const Command& command) const override final;
    void create_response(const Command& command, DocumentBuilder& doc) const override final;

    std::unique_ptr<LastError> create_last_error() const override final;
};

class HardError : public Exception
{
public:
    using Exception::Exception;

    GWBUF* create_response(const Command& command) const override final;
    void create_response(const Command& command, DocumentBuilder& doc) const override final;

    std::unique_ptr<LastError> create_last_error() const override final;
};

class MariaDBError : public Exception
{
public:
    MariaDBError(const ComERR& err);

    GWBUF* create_response(const Command& command) const override final;
    void create_response(const Command& command, DocumentBuilder& doc) const override final;

    std::unique_ptr<LastError> create_last_error() const override final;

private:
    int         m_mariadb_code;
    std::string m_mariadb_message;
};

namespace key
{

const char ADMIN_ONLY[]                      = "adminOnly";
const char ARGV[]                            = "argv";
const char BATCH_SIZE[]                      = "batchSize";
const char BITS[]                            = "bits";
const char CODE_NAME[]                       = "codeName";
const char CODE[]                            = "code";
const char COLLECTION[]                      = "collection";
const char COMMANDS[]                        = "commands";
const char COMMAND[]                         = "command";
const char COMPILED[]                        = "compiled";
const char CONFIG[]                          = "config";
const char CONNECTION_ID[]                   = "connectionId";
const char CURSORS_ALIVE[]                   = "cursorsAlive";
const char CURSORS_KILLED[]                  = "cursorsKilled";
const char CURSORS_NOT_FOUND[]               = "cursorsNotFound";
const char CURSORS_UNKNOWN[]                 = "cursorsUnknown";
const char CURSOR[]                          = "cursor";
const char DATABASES[]                       = "databases";
const char DEBUG[]                           = "debug";
const char DELETES[]                         = "deletes";
const char DOCUMENTS[]                       = "documents";
const char DROPPED[]                         = "dropped";
const char EMPTY[]                           = "empty";
const char ERRMSG[]                          = "errmsg";
const char ERROR[]                           = "error";
const char ERR[]                             = "err";
const char FILTER[]                          = "filter";
const char FIRST_BATCH[]                     = "firstBatch";
const char GIT_VERSION[]                     = "gitVersion";
const char HELP[]                            = "help";
const char ID[]                              = "id";
const char INDEX[]                           = "index";
const char INFO[]                            = "info";
const char ISMASTER[]                        = "ismaster";
const char JAVASCRIPT_ENGINE[]               = "javascriptEngine";
const char KEY_PATTERN[]                     = "keyPattern";
const char KEY_VALUE[]                       = "keyValue";
const char KEY[]                             = "key";
const char KIND[]                            = "kind";
const char LIMIT[]                           = "limit";
const char LOCALTIME[]                       = "localTime";
const char LOGICAL_SESSION_TIMEOUT_MINUTES[] = "logicalSessionTimeoutMinutes";
const char LOG[]                             = "log";
const char MARIADB[]                         = "mariadb";
const char MAX_BSON_OBJECT_SIZE[]            = "maxBsonObjectSize";
const char MAX_MESSAGE_SIZE_BYTES[]          = "maxMessageSizeBytes";
const char MAXSCALE[]                        = "maxscale";
const char MAX_WIRE_VERSION[]                = "maxWireVersion";
const char MAX_WRITE_BATCH_SIZE[]            = "maxWriteBatchSize";
const char MESSAGE[]                         = "message";
const char MIN_WIRE_VERSION[]                = "minWireVersion";
const char MODULES[]                         = "modules";
const char MULTI[]                           = "multi";
const char NAME_ONLY[]                       = "nameOnly";
const char NAMES[]                           = "names";
const char NAME[]                            = "name";
const char NEXT_BATCH[]                      = "nextBatch";
const char N_INDEXES_WAS[]                   = "nIndexesWas";
const char N_MODIFIED[]                      = "nModified";
const char NS[]                              = "ns";
const char N[]                               = "n";
const char OK[]                              = "ok";
const char OPENSSL[]                         = "openssl";
const char OPTIONS[]                         = "options";
const char ORDERED[]                         = "ordered";
const char PARSED[]                          = "parsed";
const char PROJECTION[]                      = "projection";
const char QUERY[]                           = "query";
const char Q[]                               = "q";
const char READ_ONLY[]                       = "readOnly";
const char RESPONSE[]                        = "response";
const char RUNNING[]                         = "running";
const char SINGLE_BATCH[]                    = "singleBatch";
const char SIZE_ON_DISK[]                    = "sizeOnDisk";
const char SKIP[]                            = "skip";
const char SORT[]                            = "sort";
const char SQL[]                             = "sql";
const char STATE[]                           = "state";
const char STORAGE_ENGINES[]                 = "storageEngines";
const char SYNC_MILLIS[]                     = "syncMillis";
const char TOPOLOGY_VERSION[]                = "topologyVersion";
const char TOTAL_LINES_WRITTEN[]             = "totalLinesWritten";
const char TOTAL_SIZE[]                      = "totalSize";
const char TYPE[]                            = "type";
const char UPDATES[]                         = "updates";
const char UPSERT[]                          = "upsert";
const char U[]                               = "u";
const char VERSION_ARRAY[]                   = "versionArray";
const char VERSION[]                         = "version";
const char WRITE_CONCERN[]                   = "writeConcern";
const char WRITE_ERRORS[]                    = "writeErrors";
const char WRITTEN_TO[]                      = "writtenTo";
const char WTIMEOUT[]                        = "wtimeout";
const char W[]                               = "w";
const char YOU[]                             = "you";
const char _ID[]                             = "_id";

}

namespace value
{

const char COLLECTION[] = "collection";
const char IMMEDIATE[]  = "immediate";
const char MOZJS[]      = "mozjs";
const char MULTI[]      = "multi";
const char SINGLE[]     = "single";
const char UNDECIDED[]  = "undecided";

}

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
    Packet(const Packet&) = default;
    Packet& operator = (const Packet&) = default;

    Packet(const uint8_t* pData, const uint8_t* pEnd)
        : m_pEnd(pEnd)
        , m_pHeader(reinterpret_cast<const protocol::HEADER*>(pData))
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
        std::ostringstream ss;
        out(ss);
        return ss.str();
    }

protected:
    const uint8_t*       m_pEnd;
    const protocol::HEADER* m_pHeader;
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

        const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

        pData += protocol::get_byte4(pData, &m_flags);
        pData += protocol::get_byte8(pData, &m_cursor_id);
        pData += protocol::get_byte4(pData, &m_start_from);
        pData += protocol::get_byte4(pData, &m_nReturned);

        while (pData < m_pEnd)
        {
            uint32_t size;
            protocol::get_byte4(pData, &size);
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

class NoSQL
{
public:
    class Context
    {
    public:
        Context(const Context&) = delete;
        Context& operator = (const Context&) = delete;

        Context(mxs::ClientConnection* pClient_connection,
                mxs::Component* pDownstream);

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

        void set_last_error(std::unique_ptr<LastError>&& sLast_error)
        {
            m_sLast_error = std::move(sLast_error);
        }

        void get_last_error(DocumentBuilder& doc);
        void reset_error(int32_t n = 0);

    private:
        mxs::ClientConnection&     m_client_connection;
        mxs::Component&            m_downstream;
        int32_t                    m_request_id { 1 };
        int64_t                    m_connection_id;
        std::unique_ptr<LastError> m_sLast_error;

        static std::atomic<int64_t> s_connection_id;
    };

    enum State
    {
        READY,  // Ready for a command.
        PENDING // A command is being executed.
    };

    NoSQL(mxs::ClientConnection* pClient_connection,
          mxs::Component* pDownstream,
          Config* pConfig);
    ~NoSQL();

    NoSQL(const NoSQL&) = delete;
    NoSQL& operator = (const NoSQL&) = delete;

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

    GWBUF* handle_query(GWBUF* pRequest, const nosql::Query& req);
    GWBUF* handle_msg(GWBUF* pRequest, const nosql::Msg& req);

    State              m_state { READY };
    Context            m_context;
    Config&            m_config;
    std::deque<GWBUF*> m_requests;
    SDatabase          m_sDatabase;
};

/**
 * Get SQL statement for creating a document table.
 *
 * @param table_name  The name of the table. Will be used verbatim,
 *                    so all necessary quotes should be provided.
 * @param id_length   The VARCHAR length of the id column.
 *
 * @return An SQL statement for creating the table.
 */
std::string table_create_statement(const std::string& table_name,
                                   int64_t id_length);

}

inline std::ostream& operator << (std::ostream& out, const nosql::Packet& x)
{
    x.out(out);
    return out;
}
