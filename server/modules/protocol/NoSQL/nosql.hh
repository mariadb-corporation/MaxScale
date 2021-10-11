/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
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
#include <maxscale/routingworker.hh>
#include <maxscale/session.hh>
#include <maxscale/target.hh>
#include "nosqlcursor.hh"

class DCB;

class Config;
class ComERR;

namespace nosql
{

using bsoncxx::stdx::string_view;

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

int32_t get_document(const uint8_t* pData, const uint8_t* pEnd, bsoncxx::document::view* pView);

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

enum class State
{
    BUSY,
    READY
};

// The MongoDB version we claim to be.
const int NOSQL_VERSION_MAJOR = 4;
const int NOSQL_VERSION_MINOR = 4;
const int NOSQL_VERSION_PATCH = 1;

const char* const NOSQL_ZVERSION = "4.4.1";

// See MongoDB: src/mongo/db/wire_version.h, 6 is the version that uses OP_MSG messages.
// Minimum version reported as 0, even though the old protocol versions are not fully
// supported as the MongoDB Shell does not do the right thing if the minimum version is 6.
const int MIN_WIRE_VERSION = 0;
const int MAX_WIRE_VERSION = 6;

const int DEFAULT_CURSOR_RETURN = 101;  // Documented to be that.

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
bool element_as(const bsoncxx::document::element& element,
                Conversion conversion,
                T* pT);

template<class T>
inline bool element_as(const bsoncxx::document::element& element, T* pT)
{
    return element_as(element, Conversion::STRICT, pT);
}

template<>
bool element_as(const bsoncxx::document::element& element,
                Conversion conversion,
                double* pT);

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

class ConcreteLastError: public LastError
{
public:
    ConcreteLastError(const std::string& err, int32_t code)
        : m_err(err)
        , m_code(code)
    {
    }

    void populate(DocumentBuilder& doc) override;

private:
    std::string m_err;
    int32_t     m_code;
    std::string m_code_name;
};

class NoError : public LastError
{
public:
    class Id
    {
    public:
        virtual ~Id() {};

        virtual std::string to_string() const = 0;

        virtual void append(DocumentBuilder& doc, const std::string& key) const = 0;
    };

    const static bsoncxx::oid null_oid;

    NoError(int32_t n = 0);
    NoError(int32_t n, bool updated_existing);
    NoError(std::unique_ptr<Id>&& sUpserted);

    void populate(DocumentBuilder& doc) override;

private:
    int32_t             m_n { -1 };
    bool                m_updated_existing { false };
    std::unique_ptr<Id> m_sUpserted;
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

    int code() const
    {
        return m_mariadb_code;
    }

    const std::string& message() const
    {
        return m_mariadb_message;
    }

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
const char ASSERTS[]                         = "asserts";
const char BATCH_SIZE[]                      = "batchSize";
const char BITS[]                            = "bits";
const char CLIENT[]                          = "client";
const char CODE_NAME[]                       = "codeName";
const char CODE[]                            = "code";
const char COLLECTION[]                      = "collection";
const char COMMANDS[]                        = "commands";
const char COMMAND[]                         = "command";
const char COMPILED[]                        = "compiled";
const char CONFIG[]                          = "config";
const char CONNECTION_ID[]                   = "connectionId";
const char CONNECTIONS[]                     = "connections";
const char CPU_ADDR_SIZE[]                   = "cpuAddrSize";
const char CPU_ARCH[]                        = "cpuArch";
const char CREATED_COLLECTION_AUTOMATICALLY[]= "createdCollectionAutomatically";
const char CURRENT_TIME[]                    = "currentTime";
const char CURSORS[]                         = "cursors";
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
const char ELECTION_METRICS[]                = "electionMetrics";
const char EMPTY[]                           = "empty";
const char ERRMSG[]                          = "errmsg";
const char ERROR[]                           = "error";
const char ERRORS[]                          = "errors";
const char ERR[]                             = "err";
const char EXTRA_INDEX_ENTRIES[]             = "extraIndexEntries";
const char EXTRA_INFO[]                      = "extraInfo";
const char EXTRA[]                           = "extra";
const char FILTER[]                          = "filter";
const char FIRST_BATCH[]                     = "firstBatch";
const char FLOW_CONTROL[]                    = "flowControl";
const char GIT_VERSION[]                     = "gitVersion";
const char HELP[]                            = "help";
const char HOSTNAME[]                        = "hostname";
const char ID[]                              = "id";
const char ID_INDEX[]                        = "idIndex";
const char INDEX_DETAILS[]                   = "indexDetails";
const char INDEX[]                           = "index";
const char INDEXES[]                         = "indexes";
const char INFO[]                            = "info";
const char INPROG[]                          = "inprog";
const char ISMASTER[]                        = "ismaster";
const char JAVASCRIPT_ENGINE[]               = "javascriptEngine";
const char KEY_PATTERN[]                     = "keyPattern";
const char KEY_VALUE[]                       = "keyValue";
const char KEY[]                             = "key";
const char KEYS_PER_INDEX[]                  = "keysPerIndex";
const char KIND[]                            = "kind";
const char LIMIT[]                           = "limit";
const char LOCAL_TIME[]                      = "localTime";
const char LOGICAL_SESSION_TIMEOUT_MINUTES[] = "logicalSessionTimeoutMinutes";
const char LOG[]                             = "log";
const char MARIADB[]                         = "mariadb";
const char MAX_BSON_OBJECT_SIZE[]            = "maxBsonObjectSize";
const char MAX_MESSAGE_SIZE_BYTES[]          = "maxMessageSizeBytes";
const char MAXSCALE[]                        = "maxscale";
const char MAX_WIRE_VERSION[]                = "maxWireVersion";
const char MAX_WRITE_BATCH_SIZE[]            = "maxWriteBatchSize";
const char MEM_LIMIT_MB[]                    = "memLimitMB";
const char MEM_SIZE_MB[]                     = "memSizeMB";
const char MESSAGE[]                         = "message";
const char MIN_WIRE_VERSION[]                = "minWireVersion";
const char MISSING_INDEX_ENTRIES[]           = "missingIndexEntries";
const char MODULES[]                         = "modules";
const char MULTI[]                           = "multi";
const char NAMES[]                           = "names";
const char NAME[]                            = "name";
const char NAME_ONLY[]                       = "nameOnly";
const char NEXT_BATCH[]                      = "nextBatch";
const char NRECORDS[]                        = "nrecords";
const char NS[]                              = "ns";
const char NUMA_ENABLED[]                    = "numaEnabled";
const char NUM_CORES[]                       = "numCores";
const char N[]                               = "n";
const char N_INDEXES[]                       = "nIndexes";
const char N_INDEXES_WAS[]                   = "nIndexesWas";
const char N_INVALID_DOCUMENTS[]             = "nInvalidDocuments";
const char N_MODIFIED[]                      = "nModified";
const char OK[]                              = "ok";
const char OPENSSL[]                         = "openssl";
const char OPTIONS[]                         = "options";
const char ORDERBY[]                         = "orderby";
const char ORDERED[]                         = "ordered";
const char OS[]                              = "os";
const char PARSED[]                          = "parsed";
const char PID[]                             = "pid";
const char PROJECTION[]                      = "projection";
const char QUERY[]                           = "query";
const char Q[]                               = "q";
const char READ_ONLY[]                       = "readOnly";
const char RESPONSE[]                        = "response";
const char REQUIRES_AUTH[]                   = "requiresAuth";
const char RUNNING[]                         = "running";
const char SINGLE_BATCH[]                    = "singleBatch";
const char SIZE_ON_DISK[]                    = "sizeOnDisk";
const char SKIP[]                            = "skip";
const char SLAVE_OK[]                        = "slaveOk";
const char SORT[]                            = "sort";
const char SQL[]                             = "sql";
const char STORAGE_ENGINE[]                  = "storageEngine";
const char STATE[]                           = "state";
const char STORAGE_ENGINES[]                 = "storageEngines";
const char SYNC_MILLIS[]                     = "syncMillis";
const char SYSTEM[]                          = "system";
const char TOPOLOGY_VERSION[]                = "topologyVersion";
const char TOTAL_LINES_WRITTEN[]             = "totalLinesWritten";
const char TOTAL_SIZE[]                      = "totalSize";
const char TYPE[]                            = "type";
const char UPDATED_EXISTING[]                = "updatedExisting";
const char UPDATES[]                         = "updates";
const char UPSERT[]                          = "upsert";
const char UPSERTED[]                        = "upserted";
const char UPTIME[]                          = "uptime";
const char UPTIME_ESTIMATE[]                 = "uptimeEstimate";
const char UPTIME_MILLIS[]                   = "uptimeMillis";
const char U[]                               = "u";
const char V[]                               = "v";
const char VALID[]                           = "valid";
const char VERSION_ARRAY[]                   = "versionArray";
const char VERSION[]                         = "version";
const char WARNINGS[]                        = "warnings";
const char WAS[]                             = "was";
const char WRITE_CONCERN[]                   = "writeConcern";
const char WRITE_ERRORS[]                    = "writeErrors";
const char WRITTEN_TO[]                      = "writtenTo";
const char WTIMEOUT[]                        = "wtimeout";
const char W[]                               = "w";
const char YOU[]                             = "you";
const char _ID[]                             = "_id";
const char _ID_[]                            = "_id_";

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
template<class bsoncxx_document_or_array_element>
bool get_number_as_integer(const bsoncxx_document_or_array_element& element, int64_t* pInt)
{
    bool rv = true;

    switch (element.type())
    {
    case bsoncxx::type::k_int32:
        *pInt = element.get_int32();
        break;

    case bsoncxx::type::k_int64:
        *pInt = element.get_int64();
        break;

    case bsoncxx::type::k_double:
        // Integers are often passed as double.
        *pInt = element.get_double();
        break;

    default:
        rv = false;
    }

    return rv;
}
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
std::string to_string(const bsoncxx::document::element& element);

std::vector<std::string> projection_to_extractions(const bsoncxx::document::view& projection);

std::string query_to_where_condition(const bsoncxx::document::view& filter);
std::string query_to_where_clause(const bsoncxx::document::view& filter);

std::string sort_to_order_by(const bsoncxx::document::view& sort);

std::string update_specification_to_set_value(const bsoncxx::document::view& update_command,
                                              const bsoncxx::document::element& update_specification);

std::string update_specification_to_set_value(const bsoncxx::document::view& update_specification);


class Packet
{
public:
    Packet(const Packet&) = default;
    Packet(Packet&& rhs) = default;
    Packet& operator = (const Packet&) = default;
    Packet& operator = (Packet&&) = default;

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

    enum Details
    {
        LOW_LEVEL = 1,
        HIGH_LEVEL = 2,
        ALL = (LOW_LEVEL | HIGH_LEVEL)
    };

    std::string to_string(uint32_t details, const char* zSeparator) const
    {
        std::ostringstream ss;

        if (details & LOW_LEVEL)
        {
            ss << low_level_to_string(zSeparator);
        }

        if (details & HIGH_LEVEL)
        {
            if (details & LOW_LEVEL)
            {
                ss << zSeparator;
            }

            ss << high_level_to_string(zSeparator);
        }

        return ss.str();
    }

    std::string to_string(uint32_t details) const
    {
        return to_string(details, ", ");
    }

    std::string to_string(const char* zSeparator) const
    {
        return to_string(HIGH_LEVEL, zSeparator);
    }

    std::string to_string() const
    {
        return to_string(HIGH_LEVEL, ", ");
    }

    std::string low_level_to_string(const char* zSeparator) const
    {
        std::ostringstream ss;

        ss << "msg_len: " << msg_len() << zSeparator
           << "request_id: " << request_id() << zSeparator
           << "response_to: " << response_to() << zSeparator
           << "opcode: " << opcode_to_string(opcode());

        return ss.str();
    }

    virtual std::string high_level_to_string(const char* zSeparator) const
    {
        return std::string();
    }

protected:
    const uint8_t*          m_pEnd;
    const protocol::HEADER* m_pHeader;
};

class Insert final : public Packet
{
public:
    Insert(const Packet& packet);
    Insert(Insert&& rhs) = default;

    enum Flags
    {
        CONTINUE_ON_ERROR = 0x01
    };

    uint32_t flags() const
    {
        return m_flags;
    }

    bool is_continue_on_error() const
    {
        return m_flags & CONTINUE_ON_ERROR;
    }

    const char* zCollection() const
    {
        return m_zCollection;
    }

    std::string collection() const
    {
        return m_zCollection;
    }

    const std::vector<bsoncxx::document::view>& documents() const
    {
        return m_documents;
    }

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "collection: " << m_zCollection << zSeparator
           << "continue_on_error: " << (is_continue_on_error() ? "true" : "false") << zSeparator
           << "documents: ";

        auto it = m_documents.begin();

        while (it != m_documents.end())
        {
            ss << bsoncxx::to_json(*it);

            if (++it != m_documents.end())
            {
                ss << ", ";
            }
        }

        return ss.str();
    }

private:
    uint32_t                             m_flags;
    const char*                          m_zCollection;
    std::vector<bsoncxx::document::view> m_documents;
};

class Delete final : public Packet
{
public:
    Delete(const Packet& packet);
    Delete(Delete&& rhs) = default;

    enum Flags
    {
        SINGLE_REMOVE = 1
    };

    const char* zCollection() const
    {
        return m_zCollection;
    }

    std::string collection() const
    {
        return m_zCollection;
    }

    uint32_t flags() const
    {
        return m_flags;
    }

    bool is_single_remove() const
    {
        return m_flags & SINGLE_REMOVE;
    }

    const bsoncxx::document::view& selector() const
    {
        return m_selector;
    }

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "collection: " << m_zCollection << zSeparator
           << "single_remove: " << (is_single_remove() ? "true" : "false") << zSeparator
           << "selector: " << bsoncxx::to_json(m_selector);

        return ss.str();
    }

private:
    const char*             m_zCollection;
    uint32_t                m_flags;
    bsoncxx::document::view m_selector;
};

class Update final : public Packet
{
public:
    Update(const Packet& packet);
    Update(Update&& rhs) = default;

    enum Flags
    {
        UPSERT = 0x01,
        MULTI  = 0x02,
    };

    const char* zCollection() const
    {
        return m_zCollection;
    }

    std::string collection() const
    {
        return m_zCollection;
    }

    uint32_t flags() const
    {
        return m_flags;
    }

    bool is_upsert() const
    {
        return m_flags & UPSERT;
    }

    bool is_multi() const
    {
        return m_flags & MULTI;
    }

    const bsoncxx::document::view& selector() const
    {
        return m_selector;
    }

    const bsoncxx::document::view update() const
    {
        return m_update;
    }

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "collection: " << m_zCollection << zSeparator
           << "upsert: " << (is_upsert() ? "true" : "false") << zSeparator
           << "multi: " << (is_multi() ? "true" : "false") << zSeparator
           << "selector: " << bsoncxx::to_json(m_selector) << zSeparator
           << "update: " << bsoncxx::to_json(m_update);

        return ss.str();
    }

private:
    const char*             m_zCollection;
    uint32_t                m_flags;
    bsoncxx::document::view m_selector;
    bsoncxx::document::view m_update;
};

class Query final : public Packet
{
public:
    Query(const Packet& packet);
    Query(Query&& rhs) = default;

    enum Flags
    {
        TAILABLE_CURSOR   = (1 << 1),
        SLAVE_OK          = (1 << 2),
        OPLOG_REPLAY      = (1 << 3),
        NO_CURSOR_TIMEOUT = (1 << 4),
        AWAIT_DATA        = (1 << 5),
        EXHAUST           = (1 << 6),
        PARTIAL           = (1 << 7)
    };

    uint32_t flags() const
    {
        return m_flags;
    }

    bool is_tailable_cursor() const
    {
        return m_flags & TAILABLE_CURSOR;
    }

    bool is_slave_ok() const
    {
        return m_flags & SLAVE_OK;
    }

    bool is_oplog_replay() const
    {
        return m_flags & OPLOG_REPLAY;
    }

    bool is_no_cursor_timeout() const
    {
        return m_flags & NO_CURSOR_TIMEOUT;
    }

    bool is_await_data() const
    {
        return m_flags & AWAIT_DATA;
    }

    bool is_exhaust() const
    {
        return m_flags & EXHAUST;
    }

    bool is_partial() const
    {
        return m_flags & PARTIAL;
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

    int32_t nReturn() const
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

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "collection: " << m_zCollection << zSeparator
           << "flags: " << m_flags << zSeparator // TODO: Perhaps should be decoded,
           << "nSkip: " << m_nSkip << zSeparator
           << "nReturn: " << m_nReturn << zSeparator
           << "query: " << bsoncxx::to_json(m_query) << zSeparator
           << "fields: " << bsoncxx::to_json(m_fields);

        return ss.str();
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

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "flags: " << m_flags << zSeparator
           << "cursorId: " << m_cursor_id << zSeparator
           << "start_from: " << m_start_from << zSeparator
           << "nReturned: " << m_nReturned << zSeparator
           << "documents: ";

        auto it = m_documents.begin();

        while (it != m_documents.end())
        {
            ss << bsoncxx::to_json(*it);

            if (++it != m_documents.end())
            {
                ss << ", ";
            }
        }

        return ss.str();
    }

protected:
    int32_t                              m_flags;
    int64_t                              m_cursor_id;
    int32_t                              m_start_from;
    int32_t                              m_nReturned;
    std::vector<bsoncxx::document::view> m_documents;
};

class GetMore final : public Packet
{
public:
    GetMore(const Packet& packet);
    GetMore(const GetMore& that) = default;
    GetMore(GetMore&& that) = default;

    const char* zCollection() const
    {
        return m_zCollection;
    }

    std::string collection() const
    {
        return m_zCollection;
    }

    int32_t nReturn() const
    {
        return m_nReturn;
    }

    int64_t cursor_id() const
    {
        return m_cursor_id;
    }

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "collection: " << m_zCollection << zSeparator
           << "nReturn: " << m_nReturn << zSeparator
           << "cursor_id: " << m_cursor_id;

        return ss.str();
    }

private:
    const char* m_zCollection;
    int32_t     m_nReturn;
    int64_t     m_cursor_id;
};

class KillCursors final : public Packet
{
public:
    KillCursors(const Packet& packet);
    KillCursors(const KillCursors& that) = default;
    KillCursors(KillCursors&& that) = default;

    const std::vector<int64_t> cursor_ids() const
    {
        return m_cursor_ids;
    };

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        auto it = m_cursor_ids.begin();

        while (it != m_cursor_ids.end())
        {
            ss << *it;

            if (++it != m_cursor_ids.end())
            {
                ss << ", ";
            }
        }

        return ss.str();
    }

private:
    std::vector<int64_t> m_cursor_ids;
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
    Msg(const Msg& rhs) = default;
    Msg(Msg&& rhs) = default;

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

    std::string high_level_to_string(const char* zSeparator) const override
    {
        std::ostringstream ss;

        ss << "flags: " << m_flags << zSeparator
           << "document: " << bsoncxx::to_json(m_document) << zSeparator
           << "arguments: ";

        auto it = m_arguments.begin();

        while (it != m_arguments.end())
        {
            ss << "(" << it->first << ": ";

            auto jt = it->second.begin();

            while (jt != it->second.end())
            {
                ss << bsoncxx::to_json(*jt);

                if (++jt != it->second.end())
                {
                    ss << ", ";
                }
            }

            ss << ")";

            if (++it != m_arguments.end())
            {
                ss << ", ";
            }
        }

        return ss.str();
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

        Context(MXS_SESSION* pSession,
                mxs::ClientConnection* pClient_connection,
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

        mxs::RoutingWorker& worker() const
        {
            mxb_assert(m_session.worker());
            return *m_session.worker();
        }

        void set_metadata_sent(bool metadata_sent)
        {
            m_metadata_sent = metadata_sent;
        }

        bool metadata_sent() const
        {
            return m_metadata_sent;
        }

    private:
        MXS_SESSION&               m_session;
        mxs::ClientConnection&     m_client_connection;
        mxs::Component&            m_downstream;
        int32_t                    m_request_id { 1 };
        int64_t                    m_connection_id;
        std::unique_ptr<LastError> m_sLast_error;
        bool                       m_metadata_sent { false };

        static std::atomic<int64_t> s_connection_id;
    };

    NoSQL(MXS_SESSION*           pSession,
          mxs::ClientConnection* pClient_connection,
          mxs::Component* pDownstream,
          Config* pConfig);
    ~NoSQL();

    NoSQL(const NoSQL&) = delete;
    NoSQL& operator = (const NoSQL&) = delete;

    State state() const
    {
        return m_sDatabase ?  State::BUSY : State::READY;
    }

    bool is_busy() const
    {
        return state() == State::BUSY;
    }

    Context& context()
    {
        return m_context;
    }

    const Config& config() const
    {
        return m_config;
    }

    State handle_request(GWBUF* pRequest, GWBUF** ppResponse);

    GWBUF* handle_request(GWBUF* pRequest)
    {
        GWBUF* pResponse = nullptr;
        handle_request(pRequest, &pResponse);

        return pResponse;
    }

    int32_t clientReply(GWBUF* sMariaDB_response, DCB* pDcb);

private:
    void kill_client();

    using SDatabase = std::unique_ptr<Database>;

    State handle_delete(GWBUF* pRequest, nosql::Delete&& req, GWBUF** ppResponse);
    State handle_insert(GWBUF* pRequest, nosql::Insert&& req, GWBUF** ppResponse);
    State handle_update(GWBUF* pRequest, nosql::Update&& req, GWBUF** ppResponse);
    State handle_query(GWBUF* pRequest, nosql::Query&& req, GWBUF** ppResponse);
    State handle_get_more(GWBUF* pRequest, nosql::GetMore&& req, GWBUF** ppResponse);
    State handle_kill_cursors(GWBUF* pRequest, nosql::KillCursors&& req, GWBUF** ppResponse);
    State handle_msg(GWBUF* pRequest, nosql::Msg&& req, GWBUF** ppResponse);

    State              m_state { State::READY };
    Context            m_context;
    Config&            m_config;
    std::deque<GWBUF*> m_requests;
    SDatabase          m_sDatabase;
};

/**
 * A Path represents all incarnations of a particular JSON path.
 */
class Path
{
public:
    /**
     * An Incarnation represents a single JSON path.
     */
    class Incarnation
    {
    public:
        Incarnation(std::string&& path,
                    std::string&& parent_path,
                    std::string&& array_path)
            : m_path(std::move(path))
            , m_parent_path(std::move(parent_path))
            , m_array_path(std::move(array_path))
        {
        }

        /**
         * @return A complete JSON path.
         */
        const std::string& path() const
        {
            return m_path;
        }

        /**
         * @return The JSON path of the parent element or an empty string if there is no parent.
         *
         * @note The path does *not* contain any suffixes like "[*]" and is intended to be used
         *       e.g. for ensuring that the parent is an OBJECT.
         */
        const std::string& parent_path() const
        {
            return m_parent_path;
        }

        /**
         * @return The JSON path of the nearest ancestor element that is expected to be an array,
         *         or an empty string if no such ancestor exists.
         *
         * @note The path does *not* contain any suffixes like "[*]" and is intended to be used
         *       e.g. for ensuring that the ancestor is an ARRAY.
         */
        const std::string& array_path() const
        {
            return m_array_path;
        }

        bool has_parent() const
        {
            return !m_parent_path.empty();
        }

        bool has_array_demand() const
        {
            return !m_array_path.empty();
        }

        std::string get_comparison_condition(const bsoncxx::document::element& element) const;
        std::string get_comparison_condition(const bsoncxx::document::view& doc) const;

    private:
        std::string m_path;
        std::string m_parent_path;
        std::string m_array_path;
    };

    class Part
    {
    public:
        enum Kind
        {
            ELEMENT,
            ARRAY,
            INDEXED_ELEMENT
        };

        Part(Kind kind, const std::string& name, Part* pParent = 0)
            : m_kind(kind)
            , m_name(name)
            , m_pParent(pParent)
        {
            if (m_pParent)
            {
                m_pParent->add_child(this);
            }
        }

        Kind kind() const
        {
            return m_kind;
        }

        bool is_element() const
        {
            return m_kind == ELEMENT;
        }

        bool is_array() const
        {
            return m_kind == ARRAY;
        }

        bool is_indexed_element() const
        {
            return m_kind == INDEXED_ELEMENT;
        }

        Part* parent() const
        {
            return m_pParent;
        }

        std::string name() const;

        std::string path() const;

        static std::vector<Part*> get_leafs(const std::string& path,
                                            std::vector<std::unique_ptr<Part>>& parts);

    private:
        void add_child(Part* pChild)
        {
            m_children.push_back(pChild);
        }

        static void add_leaf(const std::string& part,
                             bool last,
                             bool is_number,
                             Part* pParent,
                             std::vector<Part*>& leafs,
                             std::vector<std::unique_ptr<Part>>& parts);

        static void add_part(const std::string& part,
                             bool last,
                             std::vector<Part*>& leafs,
                             std::vector<std::unique_ptr<Part>>& parts);

        Kind               m_kind;
        std::string        m_name;
        Part*              m_pParent { nullptr };
        std::vector<Part*> m_children;
    };

    Path(const bsoncxx::document::element& element);

    std::string get_comparison_condition() const;

    static std::vector<Incarnation> get_incarnations(const std::string& key);

private:
    std::string get_element_condition(const bsoncxx::document::element& element) const;
    std::string get_document_condition(const bsoncxx::document::view& doc) const;

    static void add_part(std::vector<Incarnation>& rv, const std::string& part);

    bsoncxx::document::element m_element;
    std::vector<Incarnation>   m_paths;
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


/**
 * Escape the characters \ and '.
 *
 * @param from  The string to escape.
 *
 * @return The same string with \ and ' escaped.
 */
std::string escape_essential_chars(std::string&& from);

}
