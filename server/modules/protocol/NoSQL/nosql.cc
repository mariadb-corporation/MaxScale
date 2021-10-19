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

#include "nosql.hh"
#include <sstream>
#include <set>
#include <map>
#include <bsoncxx/json.hpp>
#include <maxscale/dcb.hh>
#include <maxscale/session.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include "../../filter/masking/mysql.hh"
#include "nosqldatabase.hh"
#include "crc32.h"

using namespace std;

namespace
{

uint32_t (*crc32_func)(const void *, size_t) = wiredtiger_crc32c_func();

}

namespace nosql
{

namespace protocol
{

namespace
{

const std::unordered_map<string, int32_t> alias_type_mapping =
{
    { alias::DOUBLE,           type::DOUBLE },
    { alias::STRING,           type::STRING },
    { alias::OBJECT,           type::OBJECT },
    { alias::ARRAY,            type::ARRAY },
    { alias::BIN_DATA,         type::BIN_DATA },
    { alias::UNDEFINED,        type::UNDEFINED },
    { alias::OBJECT_ID,        type::OBJECT_ID },
    { alias::BOOL,             type::BOOL },
    { alias::DATE,             type::DATE },
    { alias::NULL_ALIAS,       type::NULL_TYPE },
    { alias::REGEX,            type::REGEX },
    { alias::DB_POINTER,       type::DB_POINTER },
    { alias::JAVASCRIPT,       type::JAVASCRIPT },
    { alias::SYMBOL,           type::SYMBOL },
    { alias::JAVASCRIPT_SCOPE, type::JAVASCRIPT },
    { alias::INT32,            type::INT32 },
    { alias::TIMESTAMP,        type::TIMESTAMP },
    { alias::INT64,            type::INT64 },
    { alias::DECIMAL128,       type::DECIMAL128 },
    { alias::MIN_KEY,          type::MIN_KEY },
    { alias::MAX_KEY,          type::MAX_KEY },
};

}

namespace alias
{

const char* DOUBLE           = "double";
const char* STRING           = "string";
const char* OBJECT           = "object";
const char* ARRAY            = "array";
const char* BIN_DATA         = "binData";
const char* UNDEFINED        = "undefined";
const char* OBJECT_ID        = "objectId";
const char* BOOL             = "bool";
const char* DATE             = "date";
const char* NULL_ALIAS       = "date";
const char* REGEX            = "regex";
const char* DB_POINTER       = "dbPointer";
const char* JAVASCRIPT       = "javacript";
const char* SYMBOL           = "symbol";
const char* JAVASCRIPT_SCOPE = "javacriptWithScope";
const char* INT32            = "int";
const char* TIMESTAMP        = "timestamp";
const char* INT64            = "long";
const char* DECIMAL128       = "decimal";
const char* MIN_KEY          = "minKey";
const char* MAX_KEY          = "maxKey";

}

string type::to_alias(int32_t type)
{
    // Slow, but only needed during error reporting.

    for (const auto& kv : alias_type_mapping)
    {
        if (kv.second == type)
        {
            return kv.first;
        }
    }

    mxb_assert(!true);
    return "unknown";
}

int32_t alias::to_type(const string& alias)
{
    auto it = alias_type_mapping.find(alias);

    if (it == alias_type_mapping.end())
    {
        ostringstream ss;
        ss << "Unknown type name alias: " << alias;

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    return it->second;
}

int32_t get_document(const uint8_t* pData, const uint8_t* pEnd, bsoncxx::document::view* pView)
{
    if (pEnd - pData < 4)
    {
        mxb_assert(!true);
        std::ostringstream ss;
        ss << "Malformed packet, expecting document, but not even document length received.";

        throw std::runtime_error(ss.str());
    }

    uint32_t size;
    get_byte4(pData, &size);

    if (pData + size > pEnd)
    {
        mxb_assert(!true);
        std::ostringstream ss;
        ss << "Malformed packet, document claimed to be " << size << " bytes, but only "
           << pEnd - pData << " available.";

        throw std::runtime_error(ss.str());
    }

    *pView = bsoncxx::document::view(pData, size);

    return size;
}

}

void append(DocumentBuilder& doc, const core::string_view& key, const bsoncxx::document::element& element)
{
    // bsoncxx should simply allow the addition of an element, and do this internally.
    switch (element.type())
    {
    case bsoncxx::type::k_array:
        doc.append(kvp(key, element.get_array()));
        break;

    case bsoncxx::type::k_binary:
        doc.append(kvp(key, element.get_binary()));
        break;

    case bsoncxx::type::k_bool:
        doc.append(kvp(key, element.get_bool()));
        break;

    case bsoncxx::type::k_code:
        doc.append(kvp(key, element.get_code()));
        break;

    case bsoncxx::type::k_codewscope:
        doc.append(kvp(key, element.get_codewscope()));
        break;

    case bsoncxx::type::k_date:
        doc.append(kvp(key, element.get_date()));
        break;

    case bsoncxx::type::k_dbpointer:
        doc.append(kvp(key, element.get_dbpointer()));
        break;

    case bsoncxx::type::k_decimal128:
        doc.append(kvp(key, element.get_decimal128()));
        break;

    case bsoncxx::type::k_document:
        doc.append(kvp(key, element.get_document()));
        break;

    case bsoncxx::type::k_double:
        doc.append(kvp(key, element.get_double()));
        break;

    case bsoncxx::type::k_int32:
        doc.append(kvp(key, element.get_int32()));
        break;

    case bsoncxx::type::k_int64:
        doc.append(kvp(key, element.get_int64()));
        break;

    case bsoncxx::type::k_maxkey:
        doc.append(kvp(key, element.get_maxkey()));
        break;

    case bsoncxx::type::k_minkey:
        doc.append(kvp(key, element.get_minkey()));
        break;

    case bsoncxx::type::k_null:
        doc.append(kvp(key, element.get_null()));
        break;

    case bsoncxx::type::k_oid:
        doc.append(kvp(key, element.get_oid()));
        break;

    case bsoncxx::type::k_regex:
        doc.append(kvp(key, element.get_regex()));
        break;

    case bsoncxx::type::k_symbol:
        doc.append(kvp(key, element.get_symbol()));
        break;

    case bsoncxx::type::k_timestamp:
        doc.append(kvp(key, element.get_timestamp()));
        break;

    case bsoncxx::type::k_undefined:
        doc.append(kvp(key, element.get_undefined()));
        break;

    case bsoncxx::type::k_utf8:
        doc.append(kvp(key, element.get_utf8()));
        break;
    }
}

template<>
bool element_as(const bsoncxx::document::element& element,
                Conversion conversion,
                double* pT)
{
    bool rv = true;

    auto type = element.type();

    if (conversion == Conversion::STRICT && type != bsoncxx::type::k_double)
    {
        rv = false;
    }
    else
    {
        switch (type)
        {
        case bsoncxx::type::k_int32:
            *pT = element.get_int32();
            break;

        case bsoncxx::type::k_int64:
            *pT = element.get_int64();
            break;

        case bsoncxx::type::k_double:
            *pT = element.get_double();
            break;

        default:
            rv = false;
        }
    }

    return rv;
}

template<>
bsoncxx::document::view element_as<bsoncxx::document::view>(const string& command,
                                                            const char* zKey,
                                                            const bsoncxx::document::element& element,
                                                            Conversion conversion)
{
    if (conversion == Conversion::STRICT && element.type() != bsoncxx::type::k_document)
    {
        ostringstream ss;
        ss << "BSON field '" << command << "." << zKey << "' is the wrong type '"
           << bsoncxx::to_string(element.type()) << "', expected type 'object'";

        throw SoftError(ss.str(), error::TYPE_MISMATCH);
    }

    bsoncxx::document::view doc;

    switch (element.type())
    {
    case bsoncxx::type::k_document:
        doc = element.get_document();
        break;

    case bsoncxx::type::k_null:
        break;

    default:
        {
            ostringstream ss;
            ss << "BSON field '" << command << "." << zKey << "' is the wrong type '"
               << bsoncxx::to_string(element.type()) << "', expected type 'object' or 'null'";

            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }
    }

    return doc;
}

template<>
bsoncxx::array::view element_as<bsoncxx::array::view>(const string& command,
                                                      const char* zKey,
                                                      const bsoncxx::document::element& element,
                                                      Conversion)
{
    if (element.type() != bsoncxx::type::k_array)
    {
        ostringstream ss;
        ss << "BSON field '" << command << "." << zKey << "' is the wrong type '"
           << bsoncxx::to_string(element.type()) << "', expected type 'array'";

        throw SoftError(ss.str(), error::TYPE_MISMATCH);
    }

    return element.get_array();
}

template<>
string element_as<string>(const string& command,
                          const char* zKey,
                          const bsoncxx::document::element& element,
                          Conversion)
{
    if (element.type() != bsoncxx::type::k_utf8)
    {
        ostringstream ss;
        ss << "BSON field '" << command << "." << zKey << "' is the wrong type '"
           << bsoncxx::to_string(element.type()) << "', expected type 'string'";

        throw SoftError(ss.str(), error::TYPE_MISMATCH);
    }

    const auto& utf8 = element.get_utf8();
    return string(utf8.value.data(), utf8.value.size());
}

template<>
int64_t element_as<int64_t>(const string& command,
                            const char* zKey,
                            const bsoncxx::document::element& element,
                            Conversion conversion)
{
    int64_t rv;

    if (conversion == Conversion::STRICT && element.type() != bsoncxx::type::k_int64)
    {
        ostringstream ss;
        ss << "BSON field '" << command << "." << zKey << "' is the wrong type '"
           << bsoncxx::to_string(element.type()) << "', expected type 'int64'";

        throw SoftError(ss.str(), error::TYPE_MISMATCH);
    }

    switch (element.type())
    {
    case bsoncxx::type::k_int32:
        rv = element.get_int32();
        break;

    case bsoncxx::type::k_int64:
        rv = element.get_int64();
        break;

    case bsoncxx::type::k_double:
        rv = element.get_double();
        break;

    default:
        {
            ostringstream ss;
            ss << "BSON field '" << command << "." << zKey << "' is the wrong type '"
               << bsoncxx::to_string(element.type()) << "', expected a number";

            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }
    }

    return rv;
}

template<>
int32_t element_as<int32_t>(const string& command,
                            const char* zKey,
                            const bsoncxx::document::element& element,
                            Conversion conversion)
{
    int32_t rv;

    if (conversion == Conversion::STRICT && element.type() != bsoncxx::type::k_int32)
    {
        ostringstream ss;
        ss << "BSON field '" << command << "." << zKey << "' is the wrong type '"
           << bsoncxx::to_string(element.type()) << "', expected type 'int32'";

        throw SoftError(ss.str(), error::TYPE_MISMATCH);
    }

    switch (element.type())
    {
    case bsoncxx::type::k_int32:
        rv = element.get_int32();
        break;

    case bsoncxx::type::k_int64:
        rv = element.get_int64();
        break;

    case bsoncxx::type::k_double:
        rv = element.get_double();
        break;

    default:
        {
            ostringstream ss;
            ss << "BSON field '" << command << "." << zKey << "' is the wrong type '"
               << bsoncxx::to_string(element.type()) << "', expected a number";

            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }
    }

    return rv;
}

template<>
bool element_as<bool>(const string& command,
                      const char* zKey,
                      const bsoncxx::document::element& element,
                      Conversion conversion)
{
    bool rv = true;

    if (conversion == Conversion::STRICT && element.type() != bsoncxx::type::k_bool)
    {
        ostringstream ss;
        ss << "BSON field '" << command << "." << zKey << "' is the wrong type '"
           << bsoncxx::to_string(element.type()) << "', expected type 'bool'";

        throw SoftError(ss.str(), error::TYPE_MISMATCH);
    }

    switch (element.type())
    {
    case bsoncxx::type::k_bool:
        rv = element.get_bool();
        break;

    case bsoncxx::type::k_int32:
        rv = element.get_int32() != 0;
        break;

    case bsoncxx::type::k_int64:
        rv = element.get_int64() != 0;
        break;

    case bsoncxx::type::k_double:
        rv = element.get_double() != 0;
        break;

    case bsoncxx::type::k_null:
        rv = false;
        break;

    default:
        rv = true;
    }

    return rv;
}

}

nosql::Insert::Insert(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_INSERT);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    pData += protocol::get_byte4(pData, &m_flags);
    pData += protocol::get_zstring(pData, &m_zCollection);

    while (pData < m_pEnd)
    {
        if (m_pEnd - pData < 4)
        {
            mxb_assert(!true);
            std::ostringstream ss;
            ss << "Malformed packet, expecting document, but not even document length received.";

            throw std::runtime_error(ss.str());
        }

        uint32_t size;
        protocol::get_byte4(pData, &size);

        if (pData + size > m_pEnd)
        {
            mxb_assert(!true);
            std::ostringstream ss;
            ss << "Malformed packet, document claimed to be " << size << " bytes, but only "
               << m_pEnd - pData << " available.";

            throw std::runtime_error(ss.str());
        }

        auto view = bsoncxx::document::view { pData, size };
        m_documents.push_back(view);

        pData += size;
    }
}

nosql::Delete::Delete(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_DELETE);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    pData += 4; // ZERO int32
    pData += protocol::get_zstring(pData, &m_zCollection);
    pData += protocol::get_byte4(pData, &m_flags);
    pData += protocol::get_document(pData, m_pEnd, &m_selector);

    mxb_assert(pData == m_pEnd);
}

nosql::Update::Update(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_UPDATE);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    pData += 4; // ZERO int32
    pData += protocol::get_zstring(pData, &m_zCollection);
    pData += protocol::get_byte4(pData, &m_flags);
    pData += protocol::get_document(pData, m_pEnd, &m_selector);
    pData += protocol::get_document(pData, m_pEnd, &m_update);

    mxb_assert(pData == m_pEnd);
}

nosql::Query::Query(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_QUERY);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    pData += protocol::get_byte4(pData, &m_flags);
    pData += protocol::get_zstring(pData, &m_zCollection);
    pData += protocol::get_byte4(pData, &m_nSkip);
    pData += protocol::get_byte4(pData, &m_nReturn);

    uint32_t size;
    protocol::get_byte4(pData, &size);
    m_query = bsoncxx::document::view { pData, size };
    pData += size;

    if (pData < m_pEnd)
    {
        protocol::get_byte4(pData, &size);
        if (m_pEnd - pData != size)
        {
            mxb_assert(!true);
            std::ostringstream ss;
            ss << "Malformed packet, expected " << size << " bytes for document, "
               << m_pEnd - pData << " found.";

            throw std::runtime_error(ss.str());
        }
        m_fields = bsoncxx::document::view { pData, size };
        pData += size;
    }

    if (pData != m_pEnd)
    {
        mxb_assert(!true);
        std::ostringstream ss;
        ss << "Malformed packet, " << m_pEnd - pData << " trailing bytes found.";

        throw std::runtime_error(ss.str());
    }
}

nosql::GetMore::GetMore(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_GET_MORE);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    int32_t zero;
    pData += protocol::get_byte4(pData, &zero);
    pData += protocol::get_zstring(pData, &m_zCollection);
    pData += protocol::get_byte4(pData, &m_nReturn);
    pData += protocol::get_byte8(pData, &m_cursor_id);

    if (m_nReturn == 0)
    {
        m_nReturn = DEFAULT_CURSOR_RETURN;
    }
}

nosql::KillCursors::KillCursors(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_KILL_CURSORS);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    int32_t zero;
    pData += protocol::get_byte4(pData, &zero);
    int32_t nCursors;
    pData += protocol::get_byte4(pData, &nCursors);

    for (int32_t i = 0; i < nCursors; ++i)
    {
        int64_t cursor_id;
        pData += protocol::get_byte8(pData, &cursor_id);
        m_cursor_ids.push_back(cursor_id);
    }
}

nosql::Msg::Msg(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_MSG);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    pData += protocol::get_byte4(pData, &m_flags);

    if (checksum_present())
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(m_pHeader);

        uint32_t checksum = crc32_func(p, m_pHeader->msg_len - sizeof(uint32_t));

        p += (m_pHeader->msg_len - sizeof(uint32_t));
        const uint32_t* pChecksum = reinterpret_cast<const uint32_t*>(p);

        if (checksum != *pChecksum)
        {
            std::ostringstream ss;
            ss << "Invalid checksum, expected " << checksum << ", got " << *pChecksum << ".";
            throw std::runtime_error(ss.str());
        }
    }

    const uint8_t* pSections_end = m_pEnd - (checksum_present() ? sizeof(uint32_t) : 0);
    size_t sections_size = pSections_end - pData;

    while (pData < pSections_end)
    {
        uint8_t kind;
        pData += protocol::get_byte1(pData, &kind);

        switch (kind)
        {
        case 0:
            // Body section encoded as a single BSON object.
            {
                mxb_assert(m_document.empty());
                uint32_t size;
                protocol::get_byte4(pData, &size);

                if (pData + size > pSections_end)
                {
                    std::ostringstream ss;
                    ss << "Malformed packet, section(0) size " << size << " larger "
                       << "than available amount " << pSections_end - pData << " of data.";
                    throw std::runtime_error(ss.str());
                }

                m_document = bsoncxx::document::view { pData, size };
                pData += size;
            }
            break;

        case 1:
            {
                uint32_t total_size;
                protocol::get_byte4(pData, &total_size);

                if (pData + total_size > pSections_end)
                {
                    std::ostringstream ss;
                    ss << "Malformed packet, section(1) size " << total_size << " larger "
                       << "than available amount " << pSections_end - pData << " of data.";
                    throw std::runtime_error(ss.str());
                }

                auto* pEnd = pData + total_size;
                pData += 4;

                const char* zIdentifier = reinterpret_cast<const char*>(pData); // NULL-terminated
                while (*pData && pData != pEnd)
                {
                    ++pData;
                }

                if (pData != pEnd)
                {
                    ++pData; // NULL-terminator

                    auto& documents = m_arguments[zIdentifier];

                    // And now there are documents all the way down...
                    while (pData < pEnd)
                    {
                        uint32_t size;
                        protocol::get_byte4(pData, &size);
                        if (pData + size <= pEnd)
                        {
                            bsoncxx::document::view doc { pData, size };
                            MXB_INFO("DOC: %s", bsoncxx::to_json(doc).c_str());
                            documents.push_back(doc);
                            pData += size;
                        }
                        else
                        {
                            mxb_assert(!true);
                            std::ostringstream ss;
                            ss << "Malformed packet, expected " << size << " bytes for document, "
                               << pEnd - pData << " found.";
                            throw std::runtime_error(ss.str());
                        }
                    }
                }
                else
                {
                    mxb_assert(!true);
                    throw std::runtime_error("Malformed packet, 'identifier' not NULL-terminated.");
                }
            }
            break;

        default:
            {
                mxb_assert(!true);
                std::ostringstream ss;
                ss << "Malformed packet, expected a 'kind' of 0 or 1, received " << kind << ".";
                throw std::runtime_error(ss.str());
            }
        }
    }

    if (pData != pSections_end)
    {
        mxb_assert(!true);
        std::ostringstream ss;
        ss << "Malformed packet, " << pSections_end - pData << " trailing bytes found.";
        throw std::runtime_error(ss.str());
    }
}

const char* nosql::opcode_to_string(int code)
{
    switch (code)
    {
    case MONGOC_OPCODE_REPLY:
        return "MONGOC_OPCODE_REPLY";

    case MONGOC_OPCODE_UPDATE:
        return "MONGOC_OPCODE_UPDATE";

    case MONGOC_OPCODE_INSERT:
        return "MONGOC_OPCODE_INSERT";

    case MONGOC_OPCODE_QUERY:
        return "MONGOC_OPCODE_QUERY";

    case MONGOC_OPCODE_GET_MORE:
        return "MONGOC_OPCODE_GET_MORE";

    case MONGOC_OPCODE_DELETE:
        return "MONGOC_OPCODE_DELETE";

    case MONGOC_OPCODE_KILL_CURSORS:
        return "MONGOC_OPCODE_KILL_CURSORS";

    case MONGOC_OPCODE_COMPRESSED:
        return "MONGOC_OPCODE_COMPRESSED";

    case MONGOC_OPCODE_MSG:
        return "MONGOC_OPCODE_MSG";

    default:
        mxb_assert(!true);
        return "MONGOC_OPCODE_UNKNOWN";
    }
}

int nosql::error::from_mariadb_code(int code)
{
    // TODO: Expand the range of used codes.

    switch (code)
    {
    case 0:
        return OK;

    default:
        return COMMAND_FAILED;
    }
}

const char* nosql::error::name(int protocol_code)
{
    switch (protocol_code)
    {
#define NOSQL_ERROR(symbol, code, name) case symbol: { return name; }
#include "nosqlerror.hh"
#undef NOSQL_ERROR

    default:
        mxb_assert(!true);
        return "";
    }
}

GWBUF* nosql::SoftError::create_response(const Command& command) const
{
    DocumentBuilder doc;
    create_response(command, doc);

    return command.create_response(doc.extract(), Command::IsError::YES);
}

void nosql::SoftError::create_response(const Command& command, DocumentBuilder& doc) const
{
    doc.append(kvp(key::OK, 0));
    if (command.response_kind() == Command::ResponseKind::REPLY)
    {
        // TODO: Turning on the error bit in the OP_REPLY is not sufficient, but "$err"
        // TODO: must be set as well. Figure out why, because it should not be needed.
        doc.append(kvp("$err", what()));
    }
    doc.append(kvp(key::ERRMSG, what()));
    doc.append(kvp(key::CODE, m_code));
    doc.append(kvp(key::CODE_NAME, nosql::error::name(m_code)));
}

void nosql::ConcreteLastError::populate(DocumentBuilder& doc)
{
    doc.append(nosql::kvp(nosql::key::ERR, m_err));
    doc.append(nosql::kvp(nosql::key::CODE, m_code));
    doc.append(nosql::kvp(nosql::key::CODE_NAME, nosql::error::name(m_code)));
}

unique_ptr<nosql::LastError> nosql::SoftError::create_last_error() const
{
    return std::make_unique<ConcreteLastError>(what(), m_code);
}

GWBUF* nosql::HardError::create_response(const nosql::Command& command) const
{
    DocumentBuilder doc;
    create_response(command, doc);

    return command.create_response(doc.extract(), Command::IsError::YES);
}

void nosql::HardError::create_response(const Command&, DocumentBuilder& doc) const
{
    doc.append(kvp("$err", what()));
    doc.append(kvp(key::CODE, m_code));
}

unique_ptr<nosql::LastError> nosql::HardError::create_last_error() const
{
    return std::make_unique<ConcreteLastError>(what(), m_code);
}

nosql::MariaDBError::MariaDBError(const ComERR& err)
    : Exception("Protocol command failed due to MariaDB error.", error::COMMAND_FAILED)
    , m_mariadb_code(err.code())
    , m_mariadb_message(err.message())
{
}

GWBUF* nosql::MariaDBError::create_response(const Command& command) const
{
    DocumentBuilder doc;
    create_response(command, doc);

    return command.create_response(doc.extract(), Command::IsError::YES);
}

void nosql::MariaDBError::create_response(const Command& command, DocumentBuilder& doc) const
{
    string json = command.to_json();
    string sql = command.last_statement();

    DocumentBuilder mariadb;
    mariadb.append(kvp(key::CODE, m_mariadb_code));
    mariadb.append(kvp(key::MESSAGE, m_mariadb_message));
    mariadb.append(kvp(key::COMMAND, json));
    mariadb.append(kvp(key::SQL, sql));

    doc.append(kvp("$err", what()));
    auto protocol_code = error::from_mariadb_code(m_mariadb_code);;
    doc.append(kvp(key::CODE, protocol_code));
    doc.append(kvp(key::CODE_NAME, nosql::error::name(protocol_code)));
    doc.append(kvp(key::MARIADB, mariadb.extract()));

    MXS_ERROR("Protocol command failed due to MariaDB error: "
              "json = \"%s\", code = %d, message = \"%s\", sql = \"%s\"",
              json.c_str(), m_mariadb_code, m_mariadb_message.c_str(), sql.c_str());}

unique_ptr<nosql::LastError> nosql::MariaDBError::create_last_error() const
{
    class MariaDBLastError : public ConcreteLastError
    {
    public:
        MariaDBLastError(const string& err,
                         int32_t mariadb_code,
                         const string& mariadb_message)
            : ConcreteLastError(err, error::from_mariadb_code(mariadb_code))
            , m_mariadb_code(mariadb_code)
            , m_mariadb_message(mariadb_message)
        {
        }

        void populate(DocumentBuilder& doc) override
        {
            ConcreteLastError::populate(doc);

            DocumentBuilder mariadb;
            mariadb.append(kvp(key::CODE, m_mariadb_code));
            mariadb.append(kvp(key::MESSAGE, m_mariadb_message));

            doc.append(kvp(key::MARIADB, mariadb.extract()));
        }

    private:
        int32_t m_mariadb_code;
        string  m_mariadb_message;
    };

    return std::make_unique<ConcreteLastError>(what(), m_code);
}


vector<string> nosql::projection_to_extractions(const bsoncxx::document::view& projection)
{
    vector<string> extractions;

    auto it = projection.begin();
    auto end = projection.end();

    if (it != end)
    {
        bool id_seen = false;

        for (; it != end; ++it)
        {
            const auto& element = *it;
            const auto& key = element.key();

            if (key.size() == 0)
            {
                continue;
            }

            if (key.compare("_id") == 0)
            {
                id_seen = true;

                bool include_id = false;

                switch (element.type())
                {
                case bsoncxx::type::k_int32:
                    include_id = static_cast<int32_t>(element.get_int32());
                    break;

                case bsoncxx::type::k_int64:
                    include_id = static_cast<int64_t>(element.get_int64());
                    break;

                case bsoncxx::type::k_bool:
                    include_id = static_cast<bool>(element.get_bool());
                    break;

                case bsoncxx::type::k_double:
                    include_id = static_cast<double>(element.get_double());
                    break;

                default:
                    ;
                }

                if (!include_id)
                {
                    continue;
                }
            }

            auto extraction = escape_essential_chars(static_cast<string>(key));

            extractions.push_back(static_cast<string>(key));
        }

        if (!id_seen)
        {
            extractions.push_back("_id");
        }
    }

    return extractions;
}

namespace
{

using namespace nosql;

string get_condition(const bsoncxx::document::view& doc);

// https://docs.mongodb.com/manual/reference/operator/query/and/#op._S_and
string get_and_condition(const bsoncxx::array::view& array)
{
    string condition;

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        const auto& item = *it;

        if (item.type() == bsoncxx::type::k_document)
        {
            string sub_condition = get_condition(item.get_document().view());

            if (sub_condition.empty())
            {
                condition.clear();
                break;
            }
            else
            {
                if (!condition.empty())
                {
                    condition += " AND ";
                }

                condition += sub_condition;
            }
        }
        else
        {
            throw nosql::SoftError("$or/$and/$nor entries need to be full objects",
                                   nosql::error::BAD_VALUE);
        }
    }

    if (!condition.empty())
    {
        condition = "(" + condition + ")";
    }

    return condition;
}

// https://docs.mongodb.com/manual/reference/operator/query/nor/#op._S_nor
string get_nor_condition(const bsoncxx::array::view& array)
{
    string condition;

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        const auto& element = *it;

        if (element.type() == bsoncxx::type::k_document)
        {
            string sub_condition = get_condition(element.get_document().view());

            if (sub_condition.empty())
            {
                condition.clear();
                break;
            }
            else
            {
                if (!condition.empty())
                {
                    condition += " AND ";
                }

                condition += "NOT " + sub_condition;
            }
        }
        else
        {
            throw nosql::SoftError("$or/$and/$nor entries need to be full objects",
                                   nosql::error::BAD_VALUE);
        }
    }

    if (!condition.empty())
    {
        condition = "(" + condition + ")";
    }

    return condition;
}

// https://docs.mongodb.com/manual/reference/operator/query/or/#op._S_or
string get_or_condition(const bsoncxx::array::view& array)
{
    string condition;

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        const auto& element = *it;

        if (element.type() == bsoncxx::type::k_document)
        {
            string sub_condition = get_condition(element.get_document().view());

            if (sub_condition.empty())
            {
                condition.clear();
                break;
            }
            else
            {
                if (!condition.empty())
                {
                    condition += " OR ";
                }

                condition += sub_condition;
            }
        }
        else
        {
            throw nosql::SoftError("$or/$and/$nor entries need to be full objects",
                                   nosql::error::BAD_VALUE);
        }
    }

    if (!condition.empty())
    {
        condition = "(" + condition + ")";
    }

    return condition;
}

// https://docs.mongodb.com/manual/reference/operator/query/#logical
string get_logical_condition(const bsoncxx::document::element& element)
{
    string condition;

    const auto& key = element.key();

    auto get_array = [](const char* zOp, const bsoncxx::document::element& element)
    {
        if (element.type() != bsoncxx::type::k_array)
        {
            ostringstream ss;
            ss << zOp << " must be an array";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        auto array = static_cast<bsoncxx::array::view>(element.get_array());

        auto begin = array.begin();
        auto end = array.end();

        if (begin == end)
        {
            throw SoftError("$and/$or/$nor must be a nonempty array", error::BAD_VALUE);
        }

        return array;
    };

    if (key.compare("$and") == 0)
    {
        condition = get_and_condition(get_array("$and", element));
    }
    else if (key.compare("$nor") == 0)
    {
        condition = get_nor_condition(get_array("$nor", element));
    }
    else if (key.compare("$or") == 0)
    {
        condition = get_or_condition(get_array("$or", element));
    }
    else
    {
        ostringstream ss;
        ss << "unknown top level operator: " << key;

        throw nosql::SoftError(ss.str(), nosql::error::BAD_VALUE);
    }

    return condition;
}

enum class ValueFor
{
    JSON,
    JSON_NESTED,
    SQL
};

using ElementValueToString = string (*)(const bsoncxx::document::element& element,
                                        ValueFor,
                                        const string& op);
using FieldAndElementValueToComparison = string (*) (const Path::Incarnation& p,
                                                     const bsoncxx::document::element& element,
                                                     const string& mariadb_op,
                                                     const string& nosql_op,
                                                     ElementValueToString value_to_string);

struct ElementValueInfo
{
    const string                     mariadb_op;
    ElementValueToString             value_to_string;
    FieldAndElementValueToComparison field_and_value_to_comparison;
};

void double_to_string(double d, ostream& os)
{
    // printf("%.20g\n", -std::numeric_limits<double>::max()) => "-1.7976931348623157081e+308"
    char buffer[28];

    sprintf(buffer, "%.20g", d);

    os << buffer;

    if (strpbrk(buffer, ".e") == nullptr)
    {
        // No decimal point, add ".0" to prevent this number from being an integer.
        os << ".0";
    }
}

string double_to_string(double d)
{
    ostringstream ss;
    double_to_string(d, ss);
    return ss.str();
}

template<class document_element_or_array_item>
string element_to_value(const document_element_or_array_item& x, ValueFor value_for, const string& op = "")
{
    ostringstream ss;

    switch (x.type())
    {
    case bsoncxx::type::k_double:
        double_to_string(x.get_double(), ss);
        break;

    case bsoncxx::type::k_utf8:
        {
            const auto& utf8 = x.get_utf8();
            string_view s(utf8.value.data(), utf8.value.size());

            switch (value_for)
            {
            case ValueFor::JSON:
                ss << "'\"" << s << "\"'";
                break;

            case ValueFor::JSON_NESTED:
            case ValueFor::SQL:
                ss << "\"" << s << "\"";
            }
        }
        break;

    case bsoncxx::type::k_int32:
        ss << x.get_int32();
        break;

    case bsoncxx::type::k_int64:
        ss << x.get_int64();
        break;

    case bsoncxx::type::k_bool:
        ss << x.get_bool();
        break;

    case bsoncxx::type::k_date:
        ss << x.get_date();
        break;

    case bsoncxx::type::k_array:
        {
            ss << "JSON_ARRAY(";

            bsoncxx::array::view a = x.get_array();

            bool first = true;
            for (auto element : a)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    ss << ", ";
                }

                ss << element_to_value(element, ValueFor::JSON_NESTED, op);
            }

            ss << ")";
        }
        break;

    case bsoncxx::type::k_document:
        {
            ss << "JSON_OBJECT(";

            bsoncxx::document::view d = x.get_document();

            bool first = true;
            for (auto element : d)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    ss << ", ";
                }

                ss << "\"" << element.key() << "\", " << element_to_value(element, ValueFor::JSON, op);
            }

            ss << ")";
        }
        break;

    case bsoncxx::type::k_null:
        switch (value_for)
        {
        case ValueFor::JSON:
        case ValueFor::JSON_NESTED:
            ss << "null";
            break;

        case ValueFor::SQL:
            ss << "'null'";
        }
        break;

    case bsoncxx::type::k_regex:
        {
            ostringstream ss2;

            auto r = x.get_regex();
            if (r.options.length() != 0)
            {
                ss2 << "(?" << r.options << ")";
            }

            ss2 << r.regex;

            ss << "REGEXP '" << escape_essential_chars(ss2.str()) << "'";
        }
        break;

    case bsoncxx::type::k_minkey:
        ss << std::numeric_limits<int64_t>::min();
        break;

    case bsoncxx::type::k_maxkey:
        ss << std::numeric_limits<int64_t>::max();
        break;

    default:
        {
            ss << "cannot convert a " << bsoncxx::to_string(x.type()) << " to a value for comparison";

            throw nosql::SoftError(ss.str(), nosql::error::BAD_VALUE);
        }
    }

    return ss.str();
}

string element_to_array(const bsoncxx::document::element& element,
                        ValueFor,
                        const string& op = "")
{
    vector<string> values;

    if (element.type() == bsoncxx::type::k_array)
    {
        bsoncxx::array::view array = element.get_array();

        for (auto it = array.begin(); it != array.end(); ++it)
        {
            const auto& item = *it;

            string value = element_to_value(item, ValueFor::SQL, op);
            mxb_assert(!value.empty());

            values.push_back(value);
        }
    }
    else
    {
        ostringstream ss;
        ss << op << " needs an array";

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    string rv;

    if (!values.empty())
    {
        rv = "(" + mxb::join(values) + ")";
    }

    return rv;
}

string elemMatch_to_json_contain(const string& subfield,
                                 const Path::Incarnation& p,
                                 const bsoncxx::document::element& elemMatch)
{
    auto key = elemMatch.key();

    string value;
    if (key.compare("$eq") == 0)
    {
        value = "1";
    }
    else if (key.compare("$ne") == 0)
    {
        value = "0";
    }
    else
    {
        throw SoftError("$elemMatch supports only operators $eq and $ne (MaxScale)",
                        error::BAD_VALUE);
    }

    return "(JSON_CONTAINS(doc, JSON_OBJECT(\"" + subfield + "\", "
        + element_to_value(elemMatch, ValueFor::JSON_NESTED, "$elemMatch") + "), '$."
        + p.path() + "') = " + value + ")";
}

string elemMatch_to_json_contain(const string& subfield,
                                 const Path::Incarnation& p,
                                 const bsoncxx::document::view& elemMatch)
{
    string rv;

    if (elemMatch.empty())
    {
        rv = "false";
    }
    else
    {
        for (const auto& element : elemMatch)
        {
            rv = elemMatch_to_json_contain(subfield, p, element);
        }
    }

    return rv;
}

string elemMatch_to_json_contain(const Path::Incarnation& p, const bsoncxx::document::element& elemMatch)
{
    string rv;

    auto key = elemMatch.key();

    if (key.find("$") == 0)
    {
        string value;

        if (key.compare("$eq") == 0)
        {
            value = "1";
        }
        else if (key.compare("$ne") == 0)
        {
            value = "0";
        }
        else
        {
            throw SoftError("$elemMatch supports only operators $eq and $ne (MaxScale)",
                            error::BAD_VALUE);
        }

        rv = "(JSON_CONTAINS(doc, "
            + element_to_value(elemMatch, ValueFor::JSON, "$elemMatch") + ", '$." + p.path() + "') = " + value
            + ")";
    }
    else
    {
        if (elemMatch.type() == bsoncxx::type::k_document)
        {
            bsoncxx::document::view doc = elemMatch.get_document();
            rv = elemMatch_to_json_contain((string)key, p, doc);
        }
        else
        {
            rv = "(JSON_CONTAINS(doc, JSON_OBJECT(\"" + (string)key + "\", "
                + element_to_value(elemMatch, ValueFor::JSON_NESTED, "$elemMatch") + "), '$."
                + p.path() + "') = 1)";

            if (elemMatch.type() == bsoncxx::type::k_null)
            {
                rv += " OR (JSON_EXTRACT(doc, '$." + p.path() + "." + (string)key + "') IS NULL)";
            }
        }
    }

    return rv;
}

string elemMatch_to_json_contains(const Path::Incarnation& p, const bsoncxx::document::view& doc)
{
    string condition;

    for (const auto& elemMatch : doc)
    {
        if (!condition.empty())
        {
            condition += " AND ";
        }

        condition += elemMatch_to_json_contain(p, elemMatch);
    }

    if (!condition.empty())
    {
        condition = "(" + condition + ")";
    }

    return condition;
}

string elemMatch_to_condition(const Path::Incarnation& p, const bsoncxx::document::element& element)
{
    string condition;

    if (element.type() != bsoncxx::type::k_document)
    {
        throw SoftError("$elemMatch needs an Object", error::BAD_VALUE);
    }

    bsoncxx::document::view doc = element.get_document();

    if (doc.empty())
    {
        condition = "true";
    }
    else
    {
        condition = elemMatch_to_json_contains(p, doc);
    }

    return condition;
}

string exists_to_condition(const Path::Incarnation& p, const bsoncxx::document::element& element)
{
    string rv("(");

    bool b = nosql::element_as<bool>("?", "$exists", element, nosql::Conversion::RELAXED);

    if (b)
    {
        rv += "JSON_EXTRACT(doc, '$." + p.path() + "') IS NOT NULL";
    }
    else
    {
        bool close = false;
        if (!p.has_array_demand())
        {
            if (p.has_parent())
            {
                rv += "JSON_QUERY(doc, '$." + p.parent_path() + "') IS NULL OR "
                    "(JSON_TYPE(JSON_EXTRACT(doc, '$." + p.parent_path() + "')) = 'OBJECT'"
                    " AND ";
                close = true;
            }
        }
        else
        {
            rv += "JSON_TYPE(JSON_QUERY(doc, '$." + p.array_path() + "')) = 'ARRAY' AND ";
        }

        rv += "JSON_EXTRACT(doc, '$." + p.path() + "') IS NULL";

        if (close)
        {
            rv += ")";
        }
    }

    rv += ")";

    return rv;
}

namespace
{

bool is_scalar_value(const bsoncxx::document::element& element)
{
    bool rv;

    switch (element.type())
    {
    case bsoncxx::type::k_array:
    case bsoncxx::type::k_document:
        rv = false;
        break;

    default:
        rv = true;
        break;
    }

    return rv;
}

}

string default_field_and_value_to_comparison(const Path::Incarnation& p,
                                             const bsoncxx::document::element& element,
                                             const string& mariadb_op,
                                             const string& nosql_op,
                                             ElementValueToString value_to_string)
{
    auto type = element.type();

    if (type == bsoncxx::type::k_regex && nosql_op != "$eq")
    {
        ostringstream ss;
        ss << "Can't have regex as arg to " << nosql_op;

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

     // TODO: This is true with array anywhere, so p.is_parent_array() is probably needed.
    auto expects_array = p.has_array_demand();

    const char* zGet = expects_array || !is_scalar_value(element) ? "JSON_EXTRACT" : "JSON_VALUE";

    bool is_date = (type == bsoncxx::type::k_date);

    // A date is stored as a document containing a field "$date" with the value.
    string f = is_date ? p.path() + ".$date" : p.path();

    ostringstream ss;

    ss << "(" << zGet << "(doc, '$." << f << "') IS NOT NULL "
       << "AND (" << zGet << "(doc, '$." + f + "') " << mariadb_op << " ";

    bool is_array = type == bsoncxx::type::k_array;

    if (expects_array && !is_array)
    {
        ss << "JSON_ARRAY("
           << value_to_string(element, ValueFor::JSON_NESTED, nosql_op)
           << ")";
    }
    else
    {
        ss << value_to_string(element, ValueFor::SQL, nosql_op);
    }

    ss << "))";

    return ss.str();
}

string field_and_value_to_nin_comparison(const Path::Incarnation& p,
                                         const bsoncxx::document::element& element,
                                         const string& mariadb_op,
                                         const string& nosql_op,
                                         ElementValueToString value_to_string)
{
    string rv;
    string s = value_to_string(element, ValueFor::SQL, nosql_op);

    if (!s.empty())
    {
        rv = "(JSON_EXTRACT(doc, '$." + p.path() + "') " + mariadb_op + " " + s + ")";
    }
    else
    {
        rv = "(true)";
    }

    return rv;
}

string field_and_value_to_eq_comparison(const Path::Incarnation& p,
                                        const bsoncxx::document::element& element,
                                        const string& mariadb_op,
                                        const string& nosql_op,
                                        ElementValueToString value_to_string)
{
    string rv;

    if (element.type() == bsoncxx::type::k_null)
    {
        if (nosql_op == "$eq")
        {
            rv = "(JSON_EXTRACT(doc, '$." + p.path() + "') IS NULL "
                + "OR (JSON_CONTAINS(JSON_QUERY(doc, '$." + p.path() + "'), null) = 1) "
                + "OR (JSON_VALUE(doc, '$." + p.path() + "') = 'null'))";
        }
        else if (nosql_op == "$ne")
        {
            rv = "(JSON_EXTRACT(doc, '$." + p.path() + "') IS NOT NULL "
                + "AND (JSON_CONTAINS(JSON_QUERY(doc, '$." + p.path() + "'), 'null') = 0) "
                + "OR (JSON_VALUE(doc, '$." + p.path() + "') != 'null'))";
        }
    }
    else
    {
        rv = default_field_and_value_to_comparison(p, element, mariadb_op, nosql_op, value_to_string);
    }

    return rv;
}

const unordered_map<string, ElementValueInfo> converters =
{
    { "$eq",     { "=",      &element_to_value, field_and_value_to_eq_comparison } },
    { "$gt",     { ">",      &element_to_value, default_field_and_value_to_comparison } },
    { "$gte",    { ">=",     &element_to_value, default_field_and_value_to_comparison } },
    { "$lt",     { "<",      &element_to_value, default_field_and_value_to_comparison } },
    { "$lte",    { "<=",     &element_to_value, default_field_and_value_to_comparison } },
    { "$ne",     { "!=",     &element_to_value, field_and_value_to_eq_comparison } },
    { "$nin",    { "NOT IN", &element_to_array, field_and_value_to_nin_comparison } },
};

enum class ArrayOp
{
    AND,
    OR
};

inline const char* to_description(ArrayOp op)
{
    switch (op)
    {
    case ArrayOp::AND:
        return "$and";

    case ArrayOp::OR:
        return "$or";
    }

    mxb_assert(!true);
    return nullptr;
}

inline const char* to_logical_operator(ArrayOp op)
{
    switch (op)
    {
    case ArrayOp::AND:
        return " AND ";

    case ArrayOp::OR:
        return " OR ";
    }

    mxb_assert(!true);
    return nullptr;
}


void add_element_array(ostream& ss,
                       bool is_scoped,
                       const string& field,
                       const char* zDescription,
                       const bsoncxx::array::view& all_elements)
{
    vector<bsoncxx::document::view> elem_matches;

    ss << "(JSON_CONTAINS(";

    if (is_scoped)
    {
        // JSON_EXTRACT has to be used here, because, given a
        // document like '{"a" : [ { "x" : 1.0 }, { "x" : 2.0 } ] }'}
        // and a query like 'c.find({ "a.x" : { "$all" : [ 1, 2 ] } }',
        // the JSON_EXTRACT below will with the path '$.a[*].x' return
        // for that document the array '[1.0, 2.0]', which will match
        // the array, which is what we want.
        ss << "JSON_EXTRACT(doc, '$." << field << "'), JSON_ARRAY(";
    }
    else
    {
        ss << "doc, JSON_ARRAY(";
    }

    bool is_single =
        all_elements.begin() != all_elements.end() &&
        ++all_elements.begin() == all_elements.end();
    bool is_null = false;
    bool first_element = true;
    for (const auto& one_element : all_elements)
    {
        string value;

        switch (one_element.type())
        {
        case bsoncxx::type::k_null:
            is_null = true;
            break;

        case bsoncxx::type::k_regex:
            // Regexes cannot be added, as they are not values to be compared.
            break;

        case bsoncxx::type::k_document:
            {
                auto doc = static_cast<bsoncxx::document::view>(one_element.get_document());

                auto it = doc.begin();
                auto end = doc.end();

                if (it != end && it->key().compare("$elemMatch") == 0)
                {
                    auto element = *it;

                    if (element.type() != bsoncxx::type::k_document)
                    {
                        throw SoftError("$elemMatch needs an Object", error::BAD_VALUE);
                    }

                    elem_matches.push_back(element.get_document());
                }
                else
                {
                    value = element_to_value(one_element, ValueFor::JSON_NESTED, zDescription);
                }
            }
            break;

        default:
            value = element_to_value(one_element, ValueFor::JSON_NESTED, zDescription);
        }

        if (!value.empty())
        {
            if (first_element)
            {
                first_element = false;
            }
            else
            {
                ss << ", ";
            }

            ss << value;
        }
    }

    if (is_scoped)
    {
        ss << ")) = 1";
    }
    else
    {
        ss << "), '$." << field << "') = 1";
    }

    // With [*][*] we e.g. exclude [[2]] when looking for [2].
    ss << " AND JSON_EXTRACT(doc, '$." << field << "[*][*]') IS NULL";


    for(const auto& elem_match : elem_matches)
    {
        for (auto it = elem_match.begin(); it != elem_match.end(); ++it)
        {
            ss << " AND ";

            auto element = *it;

            ss << "(JSON_TYPE(JSON_EXTRACT(doc, '$." << field << "')) = 'ARRAY' AND "
               << "((JSON_CONTAINS(JSON_EXTRACT(doc, '$." << field << "[*]'), "
               << "JSON_OBJECT(\"" << element.key() << "\", "
               << element_to_value(*it, ValueFor::JSON_NESTED, zDescription)
               << ")) = 1) OR "
               << "(JSON_QUERY(doc, '$." << field << "[*]') IS NOT NULL AND "
               << "JSON_EXTRACT(doc, '$." << field << "[*]." << element.key() << "') IS NULL)))";
        }
    }

    ss << ")";

    if (is_single)
    {
        auto element = *all_elements.begin();
        if (element.type() != bsoncxx::type::k_document)
        {
            ss << " OR (JSON_VALUE(doc, '$." << field << "') = "
               << element_to_value(element, ValueFor::SQL, zDescription)
               << ")";
        }
    }

    if (is_null)
    {
        ss << " OR (JSON_EXTRACT(doc, '$." << field << "') IS NULL)";
    }
}

string array_op_to_condition(const Path::Incarnation& p,
                             const bsoncxx::document::element& element,
                             ArrayOp array_op)
{
    const char* zDescription = to_description(array_op);

    if (element.type() != bsoncxx::type::k_array)
    {
        ostringstream ss;
        ss << zDescription << " needs an array";

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    ostringstream ss;

    bsoncxx::array::view all_elements = element.get_array();

    if (all_elements.empty())
    {
        ss << "(true = false)";
    }
    else
    {
        // TODO: We have this information higher up already.
        string field = p.path();
        auto i = field.find_last_of('.');
        bool is_scoped = (i != string::npos);

        ss << "(";

        if (array_op == ArrayOp::AND)
        {
            if (is_scoped)
            {
                string path;
                path = field.substr(0, i);
                path += "[*].";
                path += field.substr(i + 1);

                ss << "(";
                bool add_or = false;
                for (auto f : { field, path })
                {
                    if (add_or)
                    {
                        ss << " OR ";
                    }
                    else
                    {
                        add_or = true;
                    }

                    add_element_array(ss, is_scoped, f, zDescription, all_elements);
                };
                ss << ")";
            }
            else
            {
                add_element_array(ss, is_scoped, field, zDescription, all_elements);
            }
        }
        else
        {
            mxb_assert(array_op == ArrayOp::OR);

            ss << "(";

            bool first_element = true;
            for (const auto& one_element : all_elements)
            {
                if (first_element)
                {
                    first_element = false;
                }
                else
                {
                    ss << " OR ";
                }

                auto type = one_element.type();

                switch (type)
                {
                case bsoncxx::type::k_null:
                    ss << "(JSON_EXTRACT(doc, '$." << field << "') IS NULL)";
                    break;

                case bsoncxx::type::k_regex:
                    ss << "(false)";
                    break;

                default:
                    {
                        if (is_scoped)
                        {
                            string path;
                            path = field.substr(0, i);
                            path += "[*].";
                            path += field.substr(i + 1);

                            ss << "(";
                            bool add_or = false;
                            for (auto p : { field, path })
                            {
                                if (add_or)
                                {
                                    ss << " OR ";
                                }
                                else
                                {
                                    add_or = true;
                                }

                                if (one_element.type() != bsoncxx::type::k_regex)
                                {
                                    ss << "(JSON_CONTAINS(";
                                    ss << "JSON_EXTRACT(doc, '$." << p << "'), JSON_ARRAY("
                                       << element_to_value(one_element, ValueFor::JSON, zDescription)
                                       << ")) = 1)";
                                }
                                else
                                {
                                    ss << "false";
                                }

                                if (one_element.type() != bsoncxx::type::k_document)
                                {
                                    ss << " OR (JSON_VALUE(doc, '$." << p << "') = "
                                       << element_to_value(one_element, ValueFor::SQL, zDescription)
                                       << ")";
                                }
                            }
                            ss << ")";
                        }
                        else
                        {
                            ss << "(JSON_CONTAINS(doc, JSON_ARRAY("
                               << element_to_value(one_element, ValueFor::JSON, zDescription)
                               << "), '$." << field << "') = 1)";

                            if (one_element.type() != bsoncxx::type::k_document)
                            {
                                ss << " OR (JSON_VALUE(doc, '$." << field << "') = "
                                   << element_to_value(one_element, ValueFor::SQL, zDescription)
                                   << ")";
                            }
                        }
                    }
                }
            }

            ss << ")";
        }

        ss << ")";
    }

    return ss.str();
}

string protocol_type_to_mariadb_type(int32_t number)
{
    switch (number)
    {
    case protocol::type::DOUBLE:
        return "'DOUBLE'";

    case protocol::type::STRING:
        return "'STRING'";

    case protocol::type::OBJECT:
        return "'OBJECT'";

    case protocol::type::ARRAY:
        return "'ARRAY'";

    case protocol::type::BOOL:
        return "'BOOLEAN'";

    case protocol::type::NULL_TYPE:
        return "'NULL'";

    case protocol::type::INT32:
    case protocol::type::INT64:
        return "'INTEGER'";

    case protocol::type::BIN_DATA:
    case protocol::type::UNDEFINED:
    case protocol::type::OBJECT_ID:
    case protocol::type::DATE:
    case protocol::type::REGEX:
    case protocol::type::DB_POINTER:
    case protocol::type::JAVASCRIPT:
    case protocol::type::SYMBOL:
    case protocol::type::JAVASCRIPT_SCOPE:
    case protocol::type::TIMESTAMP:
    case protocol::type::DECIMAL128:
    case protocol::type::MIN_KEY:
    case protocol::type::MAX_KEY:
        break;

    default:
        {
            ostringstream ss;
            ss << "Invalid numerical type code: " << number;
            throw SoftError(ss.str(), error::BAD_VALUE);
        };
    }

    ostringstream ss;
    ss << "Unsupported type code: " << number << " (\"" << protocol::type::to_alias(number) << "\")";
    throw SoftError(ss.str(), error::BAD_VALUE);

    return nullptr;
}

string type_to_condition_from_value(const Path::Incarnation& p, int32_t number)
{
    ostringstream ss;

    ss << "(JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << "')) = "
       << protocol_type_to_mariadb_type(number)
       << ")";

    return ss.str();
}

string type_to_condition_from_value(const Path::Incarnation& p, const bsoncxx::stdx::string_view& alias)
{
    string rv;

    if (alias.compare("number") == 0)
    {
        ostringstream ss;

        ss << "(JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << "')) = 'DOUBLE' OR "
           << "JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << "')) = 'INTEGER')";

        rv = ss.str();
    }
    else
    {
        rv = type_to_condition_from_value(p, protocol::alias::to_type(alias));
    }

    return rv;
}

template<class document_or_array_element>
string type_to_condition_from_value(const Path::Incarnation& p, const document_or_array_element& element)
{
    string rv;

    switch (element.type())
    {
    case bsoncxx::type::k_utf8:
        rv = type_to_condition_from_value(p, (bsoncxx::stdx::string_view)element.get_utf8());
        break;

    case bsoncxx::type::k_double:
        {
            double d = element.get_double();
            int32_t i = d;

            if (d != (double)i)
            {
                ostringstream ss;
                ss << "Invalid numerical type code: " << d;
                throw SoftError(ss.str(), error::BAD_VALUE);
            }

            rv = type_to_condition_from_value(p, i);
        };
        break;

    case bsoncxx::type::k_int32:
        rv = type_to_condition_from_value(p, (int32_t)element.get_int32());
        break;

    case bsoncxx::type::k_int64:
        rv = type_to_condition_from_value(p, (int32_t)(int64_t)element.get_int64());
        break;

    default:
        throw SoftError("type must be represented as a number or a string", error::TYPE_MISMATCH);
    }

    return rv;
}

string type_to_condition(const Path::Incarnation& p, const bsoncxx::document::element& element)
{
    string rv;

    if (element.type() == bsoncxx::type::k_array)
    {
        bsoncxx::array::view all_elements = element.get_array();

        if (all_elements.empty())
        {
            // Yes, this is what MongoDB returns.
            throw SoftError("a must match at least one type", error::FAILED_TO_PARSE);
        }

        ostringstream ss;
        ss << "(";

        bool first = true;
        for (const auto& one_element : all_elements)
        {
            if (first)
            {
                first = false;
            }
            else
            {
                ss << " OR ";
            }

            ss << type_to_condition_from_value(p, one_element);
        }

        ss << ")";

        rv = ss.str();
    }
    else
    {
        rv = type_to_condition_from_value(p, element);
    }

    return rv;
}

string mod_to_condition(const Path::Incarnation& p, const bsoncxx::document::element& element)
{
    if (element.type() != bsoncxx::type::k_array)
    {
        throw SoftError("malformed mod, needs to be an array", error::BAD_VALUE);
    }

    bsoncxx::array::view arguments = element.get_array();

    auto n = std::distance(arguments.begin(), arguments.end());

    const char* zMessage = nullptr;
    switch (n)
    {
    case 0:
    case 1:
        zMessage = "malformed mod, not enough elements";
        break;

    case 2:
        break;

    default:
        zMessage = "malformed mod, too many elements";
    }

    if (zMessage)
    {
        throw SoftError(zMessage, error::BAD_VALUE);
    }

    int64_t divisor;
    if (!get_number_as_integer(arguments[0], &divisor))
    {
        throw SoftError("malformed mod, divisor is not a number", error::BAD_VALUE);
    }

    if (divisor == 0)
    {
        throw SoftError("divisor cannot be 0", error::BAD_VALUE);
    }

    int64_t remainder;
    if (!get_number_as_integer(arguments[1], &remainder))
    {
        throw SoftError("malformed mod, remainder is not a number", error::BAD_VALUE);
    }

    ostringstream ss;
    ss << "((JSON_TYPE(JSON_VALUE(doc, '$." << p.path() << "')) = 'INTEGER' || "
       << "JSON_TYPE(JSON_VALUE(doc, '$." << p.path() << "')) = 'DOUBLE') AND "
       << "(MOD(JSON_VALUE(doc, '$." << p.path() << "'), " << divisor << ") = " << remainder << "))";

    return ss.str();
}

string timestamp_to_condition(const Path::Incarnation& p, const bsoncxx::types::b_timestamp& timestamp)
{
    ostringstream ss;

    string field = "$." + p.path();

    ss << "(JSON_QUERY(doc, '" << field << ".$timestamp') IS NOT NULL AND "
       << "JSON_VALUE(doc, '" << field << ".$timestamp.t') = " << timestamp.timestamp << " AND "
       << "JSON_VALUE(doc, '" << field << ".$timestamp.i') = " << timestamp.increment << ")";

    return ss.str();
}

string regex_to_condition(const Path::Incarnation& p,
                          const string_view& regex,
                          const string_view& options)
{
    ostringstream ss1;

    ss1 << "(JSON_VALUE(doc, '$." << p.path() << "') ";

    ostringstream ss2;
    if (options.length() != 0)
    {
        ss2 << "(?" << options << ")";
    }

    ss2 << regex;

    ss1 << "REGEXP '" << escape_essential_chars(ss2.str()) << "' OR ";

    ss1 << "JSON_COMPACT(JSON_QUERY(doc, '$." << p.path() << "')) = "
        << "JSON_COMPACT(JSON_OBJECT(\"$regex\", \"" << regex << "\", \"$options\", \"" << options <<"\")))";

    return ss1.str();
}

string regex_to_condition(const Path::Incarnation& p, const bsoncxx::types::b_regex& regex)
{
    return regex_to_condition(p, regex.regex, regex.options);
}

string regex_to_condition(const Path::Incarnation& p,
                          const bsoncxx::document::element& regex,
                          const bsoncxx::document::element& options)
{
    if (options && !regex)
    {
        throw SoftError("$options needs a $regex", error::BAD_VALUE);
    }

    if (regex.type() != bsoncxx::type::k_utf8)
    {
        throw SoftError("$regex has to be a string", error::BAD_VALUE);
    }

    string_view o;
    if (options)
    {
        if (options.type() != bsoncxx::type::k_utf8)
        {
            throw SoftError("$options has to be a string", error::BAD_VALUE);
        }

        o = options.get_utf8();
    }

    return regex_to_condition(p, regex.get_utf8(), o);
}

namespace
{

bool is_hex(const string& s)
{
    auto isxdigit = [](char c)
    {
        return std::isxdigit(c);
    };

    return std::all_of(s.begin(), s.end(), isxdigit);
}

}

// https://docs.mongodb.com/manual/reference/operator/query/#comparison
string get_comparison_condition(const bsoncxx::document::element& element)
{
    string condition;

    string field = static_cast<string>(element.key());
    auto type = element.type();

    if (field == "_id" && type != bsoncxx::type::k_document)
    {
        condition = "( id = '";

        bool is_utf8 = (type == bsoncxx::type::k_utf8);

        if (is_utf8)
        {
            condition += "\"";
        }

        auto id = to_string(element);

        condition += id;

        if (is_utf8)
        {
            condition += "\"";
        }

        condition += "'";

        if (is_utf8 && id.length() == 24 && is_hex(id))
        {
            // This sure looks like an ObjectId. And this is the way it will appear
            // if a search is made using a DBPointer. So we'll cover that case as well.

            condition += " OR id = '{\"$oid\":\"" + id + "\"}'";
        }

        condition += ")";
    }
    else
    {
        Path path(element);

        condition = path.get_comparison_condition();
    }

    return condition;
}

string get_condition(const bsoncxx::document::element& element)
{
    string condition;

    const auto& key = element.key();

    if (key.size() == 0)
    {
        return condition;
    }

    if (key.front() == '$')
    {
        condition = get_logical_condition(element);
    }
    else
    {
        condition = get_comparison_condition(element);
    }

    return condition;
}

string get_condition(const bsoncxx::document::view& doc)
{
    string where;

    for (auto it = doc.begin(); it != doc.end(); ++it)
    {
        const auto& element = *it;

        string condition = get_condition(element);

        if (condition.empty())
        {
            where.clear();
            break;
        }
        else
        {
            if (!where.empty())
            {
                where += " AND ";
            }

            where += condition;
        }
    }

    return where;
}

}

template<class document_element_or_array_item>
string element_to_string(const document_element_or_array_item& x)
{
    ostringstream ss;

    switch (x.type())
    {
    case bsoncxx::type::k_array:
        {
            bool first = true;
            ss << "[";
            bsoncxx::array::view array = x.get_array();
            for (const auto& item : array)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    ss << ", ";
                }

                ss << element_to_string(item);
            }
            ss << "]";
        }
        break;

    case bsoncxx::type::k_bool:
        ss << x.get_bool();
        break;

    case bsoncxx::type::k_code:
        ss << x.get_code().code;
        break;

    case bsoncxx::type::k_date:
        ss << x.get_date();
        break;

    case bsoncxx::type::k_decimal128:
        ss << x.get_decimal128().value.to_string();
        break;

    case bsoncxx::type::k_document:
        ss << escape_essential_chars(std::move(bsoncxx::to_json(x.get_document())));
        break;

    case bsoncxx::type::k_double:
        ss << element_to_value(x, ValueFor::JSON);
        break;

    case bsoncxx::type::k_int32:
        ss << x.get_int32();
        break;

    case bsoncxx::type::k_int64:
        ss << x.get_int64();
        break;

    case bsoncxx::type::k_null:
        ss << "null";
        break;

    case bsoncxx::type::k_oid:
        ss << "{\"$oid\":\"" << x.get_oid().value.to_string() << "\"}";
        break;

    case bsoncxx::type::k_regex:
        ss << x.get_regex().regex;
        break;

    case bsoncxx::type::k_symbol:
        ss << x.get_symbol().symbol;
        break;

    case bsoncxx::type::k_utf8:
        {
            const auto& view = x.get_utf8().value;
            string value(view.data(), view.length());
            ss << escape_essential_chars(std::move(value));
        }
        break;

    case bsoncxx::type::k_binary:
    case bsoncxx::type::k_codewscope:
    case bsoncxx::type::k_dbpointer:
    case bsoncxx::type::k_maxkey:
    case bsoncxx::type::k_minkey:
    case bsoncxx::type::k_timestamp:
    case bsoncxx::type::k_undefined:
        {
            ss << "A " << bsoncxx::to_string(x.type()) << " cannot be coverted to a string.";
            throw SoftError(ss.str(), error::BAD_VALUE);
        }
        break;
    }

    return ss.str();
}

string nosql::to_string(const bsoncxx::document::element& element)
{
    return element_to_string(element);
}

string nosql::query_to_where_condition(const bsoncxx::document::view& query)
{
    return get_condition(query);
}

string nosql::query_to_where_clause(const bsoncxx::document::view& query)
{
    string clause;
    string condition = query_to_where_condition(query);

    if (!condition.empty())
    {
        clause += "WHERE ";
        clause += condition;
        clause += " ";
    }

    return clause;
}


// https://docs.mongodb.com/manual/reference/method/cursor.sort/
string nosql::sort_to_order_by(const bsoncxx::document::view& sort)
{
    string order_by;

    for (auto it = sort.begin(); it != sort.end(); ++it)
    {
        const auto& element = *it;
        const auto& key = element.key();

        if (key.size() == 0)
        {
            throw nosql::SoftError("FieldPath cannot be constructed with empty string",
                                   nosql::error::LOCATION40352);
        }

        int64_t value = 0;

        if (!nosql::get_number_as_integer(element, &value))
        {
            ostringstream ss;
            // TODO: Should actually be the value itself, and not its type.
            ss << "Illegal key in $sort specification: "
               << element.key() << ": " << bsoncxx::to_string(element.type());

            throw nosql::SoftError(ss.str(), nosql::error::LOCATION15974);
        }

        if (value != 1 && value != -1)
        {
            throw nosql::SoftError("$sort key ordering must be 1 (for ascending) or -1 (for descending)",
                                   nosql::error::LOCATION15975);
        }

        if (!order_by.empty())
        {
            order_by += ", ";
        }

        order_by += "JSON_EXTRACT(doc, '$." + static_cast<string>(element.key()) + "')";

        if (value == -1)
        {
            order_by += " DESC";
        }
    }

    return order_by;
}

namespace
{

enum class UpdateKind
{
    AGGREGATION_PIPELINE, // Element is an array
    REPLACEMENT_DOCUMENT, // Element is a document
    UPDATE_OPERATORS,     // Element is a document
    INVALID
};

UpdateKind get_update_kind(const bsoncxx::document::view& update_specification)
{
    UpdateKind kind = UpdateKind::INVALID;

    if (update_specification.empty())
    {
        kind = UpdateKind::REPLACEMENT_DOCUMENT;
    }
    else
    {
        for (auto field : update_specification)
        {
            const char* pData = field.key().data(); // Not necessarily null-terminated.

            string_view name(field.key().data(), field.key().length());

            if (*pData == '$')
            {
                if (kind == UpdateKind::INVALID || kind == UpdateKind::UPDATE_OPERATORS)
                {
                    // TODO: Change this into operator->function map.
                    if (name.compare("$set") != 0
                        && name.compare("$unset") != 0
                        && name.compare("$inc") != 0
                        && name.compare("$mul") != 0)
                    {
                        // TODO: This will now terminate the whole processing,
                        // TODO: but this should actually be returned as a write
                        // TODO: error for the particular update object.
                        ostringstream ss;
                        ss << "Unknown modifier: " << name
                           << ". Expected a valid update modifier or "
                           << "pipeline-style update specified as an array. "
                           << "Currently the only supported update operators are "
                           << "$inc, $mul, $set and $unset.";

                        throw SoftError(ss.str(), error::COMMAND_FAILED);
                    }

                    kind = UpdateKind::UPDATE_OPERATORS;
                }
                else
                {
                    // TODO: See above.
                    ostringstream ss;
                    ss << "The dollar ($) prefixed field '" << name << "' in '" << name << "' "
                       << "is not valid for storage.";

                    throw SoftError(ss.str(), error::DOLLAR_PREFIXED_FIELD_NAME);
                }
            }
            else
            {
                if (kind == UpdateKind::INVALID)
                {
                    kind = UpdateKind::REPLACEMENT_DOCUMENT;
                }
                else if (kind != UpdateKind::REPLACEMENT_DOCUMENT)
                {
                    // TODO: See above.
                    ostringstream ss;
                    ss << "Unknown modifier: " << name
                       << ". Expected  a valid update modifier or "
                       << "pipeline-style update specified as an array";

                    throw SoftError(ss.str(), error::FAILED_TO_PARSE);
                }
            }
        }
    }

    mxb_assert(kind != UpdateKind::INVALID);

    return kind;
}

UpdateKind get_update_kind(const bsoncxx::document::element& update_specification)
{
    UpdateKind kind = UpdateKind::INVALID;

    switch (update_specification.type())
    {
    case bsoncxx::type::k_array:
        kind = UpdateKind::AGGREGATION_PIPELINE;
        break;

    default:
        kind = get_update_kind(update_specification.get_document());
    }

    mxb_assert(kind != UpdateKind::INVALID);

    return kind;
}

string convert_update_operations(const bsoncxx::document::view& update_operations)
{
    string rv;

    for (auto element : update_operations)
    {
        if (!rv.empty())
        {
            rv += ", ";
        }

        bool add_value = true;
        string op;

        if (element.key().compare("$set") == 0)
        {
            rv += "JSON_SET(doc, ";
        }
        else if (element.key().compare("$unset") == 0)
        {
            rv += "JSON_REMOVE(doc, ";
            add_value = false;
        }
        else if (element.key().compare("$inc") == 0)
        {
            rv += "JSON_SET(doc, ";
            op = " + ";
        }
        else if (element.key().compare("$mul") == 0)
        {
            rv += "JSON_SET(doc, ";
            op = " * ";
        }
        else
        {
            // In get_update_kind() it is established that it is either $set or $unset.
            // This is to catch a changed there without a change here.
            mxb_assert(!true);
        }

        auto fields = static_cast<bsoncxx::document::view>(element.get_document());

        string s;
        for (auto field : fields)
        {
            if (!s.empty())
            {
                s += ", ";
            }

            string key = field.key().data();
            key = escape_essential_chars(std::move(key));

            s += "'$.";
            s += key;
            s += "'";

            if (add_value)
            {
                s += ", ";
                if (!op.empty())
                {
                    double inc;
                    if (element_as(field, Conversion::RELAXED, &inc))
                    {
                        auto d = double_to_string(inc);

                        s += "IF(JSON_EXTRACT(doc, '$." + key + "') IS NOT NULL, ";
                        s += "JSON_VALUE(doc, '$." + key + "')" + op + d + ", ";
                        s += d;
                        s += ")";
                    }
                    else
                    {
                        DocumentBuilder value;
                        append(value, key, field);

                        ostringstream ss;
                        ss << "Cannot increment with non-numeric argument: "
                           << bsoncxx::to_json(value.view());

                        throw SoftError(ss.str(), error::TYPE_MISMATCH);
                    }
                }
                else
                {
                    s += element_to_value(field, ValueFor::JSON_NESTED);
                }
            }
        }

        rv += s;

        rv += ")";
    }

    rv += " ";

    return rv;
}

void update_specification_to_set_value(UpdateKind kind,
                                       const bsoncxx::document::view& update_specification,
                                       ostream& sql)
{
    switch (kind)
    {
    case UpdateKind::REPLACEMENT_DOCUMENT:
        {
            if (update_specification.length() > protocol::MAX_BSON_OBJECT_SIZE)
            {
                ostringstream ss;
                ss << "Document to upsert is larger than " << protocol::MAX_BSON_OBJECT_SIZE;
                throw SoftError(ss.str(), error::LOCATION17420);
            }

            auto json = bsoncxx::to_json(update_specification);
            json = escape_essential_chars(std::move(json));

            sql << "JSON_SET('" << json << "', '$._id', JSON_EXTRACT(id, '$'))";
        }
        break;

    case UpdateKind::UPDATE_OPERATORS:
        {
            // TODO: With update operators the correct behavior is not
            // TODO: obtained with protocol::MAX_BSON_OBJECT_SIZE, but
            // TODO: with slightly less.
            const int max_bson_object_size = 16777210;

            if (update_specification.length() > max_bson_object_size)
            {
                ostringstream ss;
                ss << "Document to upsert is larger than " << protocol::MAX_BSON_OBJECT_SIZE;
                throw SoftError(ss.str(), error::LOCATION17419);
            }

            sql << convert_update_operations(update_specification);
        }
        break;

    default:
        mxb_assert(!true);
    }
}

}

string nosql::update_specification_to_set_value(const bsoncxx::document::view& update_command,
                                                const bsoncxx::document::element& update_specification)
{
    ostringstream sql;

    auto kind = get_update_kind(update_specification);

    switch (kind)
    {
    case UpdateKind::AGGREGATION_PIPELINE:
        {
            string message("Aggregation pipeline not supported: '");
            message += bsoncxx::to_json(update_command);
            message += "'.";

            MXB_ERROR("%s", message.c_str());
            throw HardError(message, error::COMMAND_FAILED);
        }
        break;

    default:
        update_specification_to_set_value(kind, update_specification.get_document(), sql);
    }

    return sql.str();
}

string nosql::update_specification_to_set_value(const bsoncxx::document::view& update_specification)
{
    ostringstream sql;

    auto kind = get_update_kind(update_specification);

    update_specification_to_set_value(kind, update_specification, sql);

    return sql.str();
}

bool nosql::get_integer(const bsoncxx::document::element& element, int64_t* pInt)
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

    default:
        rv = false;
    }

    return rv;
}

bool nosql::get_number_as_double(const bsoncxx::document::element& element, double_t* pDouble)
{
    bool rv = true;

    switch (element.type())
    {
    case bsoncxx::type::k_int32:
        *pDouble = element.get_int32();
        break;

    case bsoncxx::type::k_int64:
        *pDouble = element.get_int64();
        break;

    case bsoncxx::type::k_double:
        *pDouble = element.get_double();
        break;

    default:
        rv = false;
    }

    return rv;
}

std::atomic<int64_t> nosql::NoSQL::Context::s_connection_id;

nosql::NoError::NoError(int32_t n)
    : m_n(n)
{
}

nosql::NoError::NoError(int32_t n, bool updated_existing)
    : m_n(n)
    , m_updated_existing(updated_existing)
{
}

nosql::NoError::NoError(unique_ptr<Id>&& sUpserted)
    : m_n(1)
    , m_updated_existing(false)
    , m_sUpserted(std::move(sUpserted))
{
}

void nosql::NoError::populate(nosql::DocumentBuilder& doc)
{
    nosql::DocumentBuilder writeConcern;
    writeConcern.append(kvp(key::W, 1));
    writeConcern.append(kvp(key::WTIMEOUT, 0));

    if (m_n != -1)
    {
        doc.append(kvp(key::N, m_n));
    }

    if (m_updated_existing)
    {
        doc.append(kvp(key::UPDATED_EXISTING, m_updated_existing));
    }

    if (m_sUpserted)
    {
        m_sUpserted->append(doc, key::UPSERTED);
    }

    doc.append(kvp(key::SYNC_MILLIS, 0));
    doc.append(kvp(key::WRITTEN_TO, bsoncxx::types::b_null()));
    doc.append(kvp(key::WRITE_CONCERN, writeConcern.extract()));
    doc.append(kvp(key::ERR, bsoncxx::types::b_null()));
}

nosql::NoSQL::Context::Context(MXS_SESSION* pSession,
                               mxs::ClientConnection* pClient_connection,
                               mxs::Component* pDownstream)
    : m_session(*pSession)
    , m_client_connection(*pClient_connection)
    , m_downstream(*pDownstream)
    , m_connection_id(++s_connection_id)
    , m_sLast_error(std::make_unique<NoError>())
{
}

void nosql::NoSQL::Context::get_last_error(DocumentBuilder& doc)
{
    int32_t connection_id = m_connection_id; // MongoDB returns this as a 32-bit integer.

    doc.append(kvp(key::CONNECTION_ID, connection_id));
    m_sLast_error->populate(doc);
    doc.append(kvp(key::OK, 1));
}

void nosql::NoSQL::Context::reset_error(int32_t n)
{
    m_sLast_error = std::make_unique<NoError>(n);
}

nosql::NoSQL::NoSQL(MXS_SESSION* pSession,
                    mxs::ClientConnection* pClient_connection,
                    mxs::Component* pDownstream,
                    Config* pConfig)
    : m_context(pSession, pClient_connection, pDownstream)
    , m_config(*pConfig)
{
}

nosql::NoSQL::~NoSQL()
{
}

State nosql::NoSQL::handle_request(GWBUF* pRequest, GWBUF** ppResponse)
{
    State state = State::READY;
    GWBUF* pResponse = nullptr;

    if (!m_sDatabase)
    {
        try
        {
            // If no database operation is in progress, we proceed.
            nosql::Packet req(pRequest);

            mxb_assert(req.msg_len() == (int)gwbuf_length(pRequest));

            switch (req.opcode())
            {
            case MONGOC_OPCODE_COMPRESSED:
            case MONGOC_OPCODE_REPLY:
                {
                    ostringstream ss;
                    ss << "Unsupported packet " << nosql::opcode_to_string(req.opcode()) << " received.";
                    throw std::runtime_error(ss.str());
                }
                break;

            case MONGOC_OPCODE_GET_MORE:
                state = handle_get_more(pRequest, nosql::GetMore(req), &pResponse);
                break;

            case MONGOC_OPCODE_KILL_CURSORS:
                state = handle_kill_cursors(pRequest, nosql::KillCursors(req), &pResponse);
                break;

            case MONGOC_OPCODE_DELETE:
                state = handle_delete(pRequest, nosql::Delete(req), &pResponse);
                break;

            case MONGOC_OPCODE_INSERT:
                state = handle_insert(pRequest, nosql::Insert(req), &pResponse);
                break;

            case MONGOC_OPCODE_MSG:
                state = handle_msg(pRequest, nosql::Msg(req), &pResponse);
                break;

            case MONGOC_OPCODE_QUERY:
                state = handle_query(pRequest, nosql::Query(req), &pResponse);
                break;

            case MONGOC_OPCODE_UPDATE:
                state = handle_update(pRequest, nosql::Update(req), &pResponse);
                break;

            default:
                {
                    mxb_assert(!true);
                    ostringstream ss;
                    ss << "Unknown packet " << req.opcode() << " received.";
                    throw std::runtime_error(ss.str());
                }
            }
        }
        catch (const std::exception& x)
        {
            MXB_ERROR("Closing client connection: %s", x.what());
            kill_client();
        }

        gwbuf_free(pRequest);
    }
    else
    {
        // Otherwise we push it on the request queue.
        m_requests.push_back(pRequest);
    }

    *ppResponse = pResponse;
    return state;
}

int32_t nosql::NoSQL::clientReply(GWBUF* pMariadb_response, DCB* pDcb)
{
    mxb_assert(m_sDatabase.get());

    // TODO: Remove need for making resultset contiguous.
    pMariadb_response = gwbuf_make_contiguous(pMariadb_response);

    mxs::Buffer mariadb_response(pMariadb_response);
    GWBUF* pProtocol_response = m_sDatabase->translate(std::move(mariadb_response));

    if (m_sDatabase->is_ready())
    {
        m_sDatabase.reset();

        if (pProtocol_response)
        {
            pDcb->writeq_append(pProtocol_response);
        }

        if (!m_requests.empty())
        {
            // Loop as long as responses to requests can be generated immediately.
            // If it can't then we'll continue once clientReply() is called anew.
            State state = State::READY;
            do
            {
                mxb_assert(!m_sDatabase.get());

                GWBUF* pRequest = m_requests.front();
                m_requests.pop_front();

                state = handle_request(pRequest, &pProtocol_response);

                if (pProtocol_response)
                {
                    // The response could be generated immediately, just send it.
                    pDcb->writeq_append(pProtocol_response);
                }
            }
            while (state == State::READY && !m_requests.empty());
        }
    }
    else
    {
        // If the database is not ready, there cannot be a response.
        mxb_assert(pProtocol_response == nullptr);
    }

    return 0;
}

void nosql::NoSQL::kill_client()
{
    m_context.client_connection().dcb()->session()->kill();
}

namespace
{

string extract_database(const string& collection)
{
    auto i = collection.find('.');

    if (i == string::npos)
    {
        return collection;
    }

    return collection.substr(0, i);
}

}

State nosql::NoSQL::handle_delete(GWBUF* pRequest, nosql::Delete&& req, GWBUF** ppResponse)
{
    MXB_INFO("Request(DELETE): %s", req.to_string().c_str());

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = std::move(Database::create(extract_database(req.collection()), &m_context, &m_config));

    State state = m_sDatabase->handle_delete(pRequest, std::move(req), ppResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State nosql::NoSQL::handle_insert(GWBUF* pRequest, nosql::Insert&& req, GWBUF** ppResponse)
{
    MXB_INFO("Request(INSERT): %s", req.to_string().c_str());

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = std::move(Database::create(extract_database(req.collection()), &m_context, &m_config));

    State state = m_sDatabase->handle_insert(pRequest, std::move(req), ppResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State nosql::NoSQL::handle_update(GWBUF* pRequest, nosql::Update&& req, GWBUF** ppResponse)
{
    MXB_INFO("Request(UPDATE): %s", req.to_string().c_str());

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = std::move(Database::create(extract_database(req.collection()), &m_context, &m_config));

    State state = m_sDatabase->handle_update(pRequest, std::move(req), ppResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State nosql::NoSQL::handle_query(GWBUF* pRequest, nosql::Query&& req, GWBUF** ppResponse)
{
    MXB_INFO("Request(QUERY): %s", req.to_string().c_str());

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = std::move(Database::create(extract_database(req.collection()), &m_context, &m_config));

    State state = m_sDatabase->handle_query(pRequest, std::move(req), ppResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State nosql::NoSQL::handle_get_more(GWBUF* pRequest, nosql::GetMore&& req, GWBUF** ppResponse)
{
    MXB_INFO("Request(GetMore): %s", req.to_string().c_str());

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = std::move(Database::create(extract_database(req.collection()), &m_context, &m_config));

    State state = m_sDatabase->handle_get_more(pRequest, std::move(req), ppResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State nosql::NoSQL::handle_kill_cursors(GWBUF* pRequest, nosql::KillCursors&& req, GWBUF** ppResponse)
{
    MXB_INFO("Request(KillCursors): %s", req.to_string().c_str());

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = std::move(Database::create("admin", &m_context, &m_config));

    State state = m_sDatabase->handle_kill_cursors(pRequest, std::move(req), ppResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

string Path::Incarnation::get_comparison_condition(const bsoncxx::document::element& element) const
{
    string field = path();
    string condition;

    switch (element.type())
    {
    case bsoncxx::type::k_document:
        condition = get_comparison_condition(element.get_document());
        break;

    case bsoncxx::type::k_regex:
        condition = regex_to_condition(*this, element.get_regex());
        break;

    case bsoncxx::type::k_null:
        {
            if (has_array_demand())
            {
                condition = "(JSON_TYPE(JSON_QUERY(doc, '$." + m_array_path + "')) = 'ARRAY' AND ";
            }

            condition += "(JSON_EXTRACT(doc, '$." + field + "') IS NULL " +
                "OR (JSON_CONTAINS(JSON_QUERY(doc, '$." + field + "'), null) = 1) " +
                "OR (JSON_VALUE(doc, '$." + field + "') = 'null'))";

            if (has_array_demand())
            {
                condition += ")";
            }
        }
        break;

    case bsoncxx::type::k_date:
        condition = "(JSON_VALUE(doc, '$." + field + ".$date') = "
            + element_to_value(element, ValueFor::SQL) + ")";
        break;

    case bsoncxx::type::k_timestamp:
        condition = timestamp_to_condition(*this, element.get_timestamp());
        break;

    case bsoncxx::type::k_array:
        // TODO: This probably needs to be dealt with explicitly.
    default:
        {
            condition
                // Without the explicit check for NULL, this does not work when NOT due to $nor
                // is stashed in front of the whole thing.
                = "((JSON_QUERY(doc, '$." + field + "') IS NOT NULL"
                + " AND JSON_CONTAINS(JSON_QUERY(doc, '$." + field + "'), "
                + element_to_value(element, ValueFor::JSON) + ") = 1)"
                + " OR "
                + "(JSON_VALUE(doc, '$." + field + "') = "
                + element_to_value(element, ValueFor::SQL) + "))";
        }
    }

    return condition;
}

string Path::Incarnation::get_comparison_condition(const bsoncxx::document::view& doc) const
{
    string rv;

    // TODO: The fact that $regex and $options are not independent but used together,
    // TODO: means that, although that is handled here, it will, due to how things are
    // TODO: handled at an upper level lead to the same condition being generated twice.
    // TODO: It seems that all arguments should be investigated first, and only then should
    // TODO: SQL be generated.
    bool ignore_options = false;
    bool ignore_regex = false;

    auto it = doc.begin();
    auto end = doc.end();
    for (; it != end; ++it)
    {
        string condition;
        string separator;

        if (rv.empty())
        {
            rv += "(";
        }
        else
        {
            separator = " AND ";
        }

        const auto& element = *it;
        const auto nosql_op = static_cast<string>(element.key());

        auto jt = converters.find(nosql_op);

        if (jt != converters.end())
        {
            const auto& mariadb_op = jt->second.mariadb_op;
            const auto& value_to_string = jt->second.value_to_string;

            condition = jt->second.field_and_value_to_comparison(*this, element, mariadb_op,
                                                                 nosql_op, value_to_string);
        }
        else if (nosql_op == "$not")
        {
            if (element.type() != bsoncxx::type::k_document)
            {
                ostringstream ss;
                ss << "$not needs a document (regex not yet supported)";

                throw SoftError(ss.str(), error::BAD_VALUE);
            }

            auto doc = element.get_document();

            condition = "(NOT " + get_comparison_condition(doc) + ")";
        }
        else if (nosql_op == "$elemMatch")
        {
            condition = elemMatch_to_condition(*this, element);
        }
        else if (nosql_op == "$exists")
        {
            condition = exists_to_condition(*this, element);
        }
        else if (nosql_op == "$size")
        {
            condition = "(JSON_LENGTH(doc, '$." + path() + "') = " +
                element_to_value(element, ValueFor::SQL, nosql_op) + ")";
        }
        else if (nosql_op == "$all")
        {
            condition = array_op_to_condition(*this, element, ArrayOp::AND);
        }
        else if (nosql_op == "$in")
        {
            condition = array_op_to_condition(*this, element, ArrayOp::OR);
        }
        else if (nosql_op == "$type")
        {
            condition = type_to_condition(*this, element);
        }
        else if (nosql_op == "$mod")
        {
            condition = mod_to_condition(*this, element);
        }
        else if (nosql_op == "$regex")
        {
            if (!ignore_regex)
            {
                bsoncxx::document::element options;

                auto jt = it;
                ++jt;
                while (jt != end)
                {
                    if (jt->key().compare("$options") == 0)
                    {
                        ignore_options = true;
                        options = *jt;
                        break;
                    }
                    ++jt;
                }

                condition = regex_to_condition(*this, element, options);
            }
        }
        else if (nosql_op == "$options")
        {
            if (!ignore_options)
            {
                bsoncxx::document::element regex;

                auto jt = it;
                ++jt;
                while (jt != end)
                {
                    if (jt->key().compare("$regex") == 0)
                    {
                        ignore_regex = true;
                        regex = *jt;
                        break;
                    }
                    ++jt;
                }

                condition = regex_to_condition(*this, regex, element);
            }
        }
        else if (nosql_op.front() == '$')
        {
            ostringstream ss;
            ss << "unknown operator: " << nosql_op;

            throw SoftError(ss.str(), error::BAD_VALUE);
        }
        else
        {
            break;
        }

        if (!condition.empty())
        {
            rv += separator + condition;
        }
    }

    if (it == end)
    {
        rv += ")";
    }
    else
    {
        // We are simply looking for an object.
        // TODO: Given two objects '{"a": [{"x": 1}]}' and '{"a": [{"x": 1, "y": 2}]}'
        // TODO: a query like '{"a": {x: 1}}' will return them both, although MongoDB
        // TODO: returns just the former.

        ostringstream ss;
        ss << "JSON_CONTAINS(JSON_QUERY(doc, '$." << path() << "'), JSON_OBJECT(";

        while (it != end)
        {
            auto element = *it;

            ss << "\"" << element.key() << "\", ";
            ss << element_to_value(element, ValueFor::JSON_NESTED);

            if (++it != end)
            {
                ss << ", ";
            }
        }

        ss << "))";

        rv = ss.str();
    }

    return rv;
}

Path::Path(const bsoncxx::document::element& element)
    : m_element(element)
    , m_paths(get_incarnations(static_cast<string>(element.key())))
{
}

// https://docs.mongodb.com/manual/reference/operator/query/#comparison
string Path::get_comparison_condition() const
{
    string condition;

    if (m_element.type() == bsoncxx::type::k_document)
    {
        condition = get_document_condition(m_element.get_document());
    }
    else
    {
        condition = get_element_condition(m_element);
    }

    return condition;
}

string Path::Part::name() const
{
    string rv;

    switch (m_kind)
    {
    case Part::ELEMENT:
        if (m_pParent)
        {
            rv = m_pParent->path() + ".";
        }
        rv += m_name;
        break;

    case Part::ARRAY:
        if (m_pParent)
        {
            rv = m_pParent->path() + ".";
        }
        rv += m_name;
        break;

    case INDEXED_ELEMENT:
        if (m_pParent)
        {
            rv = m_pParent->path();
        }
        rv += "[" + m_name + "]";
        break;
    }

    return rv;
}

string Path::Part::path() const
{
    string rv;

    switch (m_kind)
    {
    case Part::ELEMENT:
        if (m_pParent)
        {
            rv = m_pParent->path() + ".";
        }
        rv += m_name;
        break;

    case Part::ARRAY:
        if (m_pParent)
        {
            rv = m_pParent->path() + ".";
        }
        rv += m_name + "[*]";
        break;

    case INDEXED_ELEMENT:
        if (m_pParent)
        {
            rv = m_pParent->path();
        }
        rv += "[" + m_name + "]";
        break;
    }

    return rv;
}

//static
vector<Path::Part*> Path::Part::get_leafs(const string& path, vector<std::unique_ptr<Part>>& parts)
{
    string::size_type i = 0;
    string::size_type j = path.find_first_of('.', i);

    vector<Part*> leafs;

    while (j != string::npos)
    {
        string part = path.substr(i, j - i);

        i = j + 1;
        j = path.find_first_of('.', i);

        add_part(part, false, leafs, parts);
    }

    add_part(path.substr(i, j), true, leafs, parts);

    return leafs;
}

//static
void Path::Part::add_leaf(const string& part,
                          bool last,
                          bool is_number,
                          Part* pParent,
                          vector<Part*>& leafs,
                          vector<std::unique_ptr<Part>>& parts)
{
    parts.push_back(std::make_unique<Part>(Part::ELEMENT, part, pParent));
    leafs.push_back(parts.back().get());

    if (!last)
    {
        parts.push_back(std::make_unique<Part>(Part::ARRAY, part, pParent));
        leafs.push_back(parts.back().get());
    }

    if (is_number && pParent && pParent->is_element())
    {
        parts.push_back(std::make_unique<Part>(Part::INDEXED_ELEMENT, part, pParent));
        leafs.push_back(parts.back().get());
    }
}

//static
void Path::Part::add_part(const string& part,
                          bool last,
                          vector<Part*>& leafs,
                          vector<std::unique_ptr<Part>>& parts)
{
    bool is_number = false;

    char* zEnd;
    auto l = strtol(part.c_str(), &zEnd, 10);

    // Is the part a number?
    if (*zEnd == 0 && l >= 0 && l != LONG_MAX)
    {
        // Yes, so this may refer to a field whose name is a number (e.g. { a.2: 42 })
        // or the n'th element (e.g. { a: [ ... ] }).
        is_number = true;
    }

    vector<Part*> tmp;

    if (leafs.empty())
    {
        add_leaf(part, last, is_number, nullptr, tmp, parts);
    }
    else
    {
        for (auto& pLeaf : leafs)
        {
            add_leaf(part, last, is_number, pLeaf, tmp, parts);
        }
    }

    tmp.swap(leafs);
}

//static
std::vector<Path::Incarnation> Path::get_incarnations(const std::string& path)
{
    vector<Incarnation> rv;
    vector<std::unique_ptr<Part>> parts;
    vector<Part*> leafs = Part::get_leafs(path, parts);

    for (const Part* pLeaf : leafs)
    {
        string path = pLeaf->path();
        Part* pParent = pLeaf->parent();

        string parent_path;
        string array_path;

        if (pParent)
        {
            parent_path = pParent->name();

            while (pLeaf && array_path.empty())
            {
                if (pLeaf->is_indexed_element() || (pParent && pParent->is_array()))
                {
                    array_path = pParent->name();
                }
                else if (pLeaf->is_element() && (pParent && pParent->is_indexed_element()))
                {
                    auto* pGramps = pParent->parent();
                    if (pGramps)
                    {
                        array_path = pGramps->name();
                    }
                }

                pLeaf = pParent;
                if (pParent)
                {
                    pParent = pParent->parent();
                }
            }
        }

        rv.push_back(Incarnation(std::move(path), std::move(parent_path), std::move(array_path)));
    }

    return rv;
}

string Path::get_element_condition(const bsoncxx::document::element& element) const
{
    string condition;

    if (m_paths.size() > 1)
    {
        condition += "(";
    }

    bool first = true;
    for (const auto& p : m_paths)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            condition += " OR ";
        }

        condition += "(" + p.get_comparison_condition(m_element) + ")";
    }

    if (m_paths.size() > 1)
    {
        condition += ")";
    }

    return condition;
}

string Path::get_document_condition(const bsoncxx::document::view& doc) const
{
    string condition;

    auto it = doc.begin();
    auto end = doc.end();

    if (it == end)
    {
        bool first = true;
        for (const auto& p : m_paths)
        {
            if (first)
            {
                first = false;
            }
            else
            {
                condition += " OR ";
            }

            condition += "(JSON_EXTRACT(doc, '$." + p.path() + "') = JSON_OBJECT() OR ";
            condition += "(JSON_TYPE(JSON_EXTRACT(doc, '$." + p.path() + "')) = 'ARRAY' AND ";
            condition += "JSON_CONTAINS(JSON_EXTRACT(doc, '$." + p.path() + "'), JSON_OBJECT())))";
        }
    }
    else
    {
        for (; it != end; ++it)
        {
            auto element = *it;

            if (!condition.empty())
            {
                condition += " AND ";
            }

            const auto nosql_op = static_cast<string>(element.key());

            if (nosql_op == "$not")
            {
                if (element.type() != bsoncxx::type::k_document)
                {
                    ostringstream ss;
                    ss << "$not needs a document (regex not yet supported)";

                    throw SoftError(ss.str(), error::BAD_VALUE);
                }

                bsoncxx::document::view doc = element.get_document();

                if (doc.begin() == doc.end())
                {
                    throw SoftError("$not cannot be empty", error::BAD_VALUE);
                }

                condition += "(NOT ";

                if (m_paths.size() > 1)
                {
                    condition += "(";
                }

                bool first = true;
                for (const auto& p : m_paths)
                {
                    if (first)
                    {
                        first = false;
                    }
                    else
                    {
                        condition += " OR ";
                    }

                    condition += "(" + p.get_comparison_condition(doc) + ")";
                }

                if (m_paths.size() > 1)
                {
                    condition += ")";
                }

                condition += ")";
            }
            else
            {
                condition += get_element_condition(element);
            }
        }
    }

    return "(" + condition + ")";
}

State nosql::NoSQL::handle_msg(GWBUF* pRequest, nosql::Msg&& req, GWBUF** ppResponse)
{
    MXB_INFO("Request(MSG): %s", req.to_string().c_str());

    State state = State::READY;

    const auto& doc = req.document();

    auto element = doc["$db"];

    if (element)
    {
        if (element.type() == bsoncxx::type::k_utf8)
        {
            auto utf8 = element.get_utf8();

            string name(utf8.value.data(), utf8.value.size());

            mxb_assert(!m_sDatabase.get());
            m_sDatabase = std::move(Database::create(name, &m_context, &m_config));

            state = m_sDatabase->handle_msg(pRequest, std::move(req), ppResponse);

            if (state == State::READY)
            {
                m_sDatabase.reset();
            }
        }
        else
        {
            MXB_ERROR("Closing client connection; key '$db' found, but value is not utf8.");
            kill_client();
        }
    }
    else
    {
        MXB_ERROR("Closing client connection; document did not "
                  "contain the expected key '$db': %s",
                  req.to_string().c_str());
        kill_client();
    }

    return state;
}

string nosql::table_create_statement(const std::string& table_name, int64_t id_length)
{
    ostringstream ss;
    ss << "CREATE TABLE " << table_name << " ("
       << "id VARCHAR(" << id_length << ") "
       << "AS (JSON_COMPACT(JSON_EXTRACT(doc, \"$._id\"))) UNIQUE KEY, "
       << "doc JSON, "
       << "CONSTRAINT id_not_null CHECK(id IS NOT NULL))";

    return ss.str();
}

std::string nosql::escape_essential_chars(std::string&& from)
{
    auto it = from.begin();
    auto end = from.end();

    while (it != end && *it != '\'' && *it != '\\')
    {
        ++it;
    }

    if (it == end)
    {
        return from;
    }

    string to(from.begin(), it);

    if (*it == '\'')
    {
        to.push_back('\'');
    }
    else
    {
        to.push_back('\\');
    }

    to.push_back(*it++);

    while (it != end)
    {
        auto c = *it;

        switch (c)
        {
        case '\\':
            to.push_back('\\');
            break;

        case '\'':
            to.push_back('\'');
            break;

        default:
            break;
        }

        to.push_back(c);

        ++it;
    }

    return to;
}

namespace
{

bool get_object_id(json_t* pObject, const char** pzOid, size_t* pLen)
{
    mxb_assert(json_typeof(pObject) == JSON_OBJECT);

    bool rv = false;

    if (json_object_size(pObject) == 1)
    {
        json_t* pOid = json_object_get(pObject, "$oid");

        if (pOid && json_typeof(pOid) == JSON_STRING)
        {
            *pzOid = json_string_value(pOid);
            *pLen = strlen(*pzOid);

            rv = true;
        }
    }

    return rv;
}

bool append_objectid(ArrayBuilder& array, json_t* pObject)
{
    bool rv = false;

    const char* zOid;
    size_t len;

    if (get_object_id(pObject, &zOid, &len))
    {
        try
        {
            // bxoncxx::oid would also take directly "zOid, len", but
            // with that constructor the conversion fails.
            array.append(bsoncxx::oid(string_view(zOid, len)));
            rv = true;
        }
        catch (const std::exception&)
        {
            // Ignored, this just means that the value of $oid was not valid.
        }
    }

    return rv;
}

bool append_objectid(DocumentBuilder& doc, const string_view& key, json_t* pObject)
{
    bool rv = false;

    const char* zOid;
    size_t len;

    if (get_object_id(pObject, &zOid, &len))
    {
        try
        {
            doc.append(kvp(key, bsoncxx::oid(string_view(zOid, len))));
            rv = true;
        }
        catch (const std::exception&)
        {
            // Ignored, this just means that the value of $oid was not valid.
        }
    }

    return rv;
}

}

bsoncxx::array::value nosql::bson_from_json_array(json_t* pArray)
{
    mxb_assert(json_typeof(pArray) == JSON_ARRAY);

    ArrayBuilder array;

    size_t index;
    json_t* pValue;
    json_array_foreach(pArray, index, pValue)
    {
        switch (json_typeof(pValue))
        {
        case JSON_OBJECT:
            if (!append_objectid(array, pValue))
            {
                array.append(bson_from_json(pValue));
            }
            break;

        case JSON_ARRAY:
            array.append(bson_from_json_array(pValue));
            break;

        case JSON_STRING:
            array.append(json_string_value(pValue));
            break;

        case JSON_INTEGER:
            array.append((int64_t)json_integer_value(pValue));
            break;

        case JSON_REAL:
            array.append(json_number_value(pValue));
            break;

        case JSON_TRUE:
            array.append(true);
            break;

        case JSON_FALSE:
            array.append(false);
            break;

        case JSON_NULL:
            array.append(bsoncxx::types::b_null());
            break;
        }
    }

    return array.extract();
}

bsoncxx::document::value nosql::bson_from_json(json_t* pObject)
{
    mxb_assert(json_typeof(pObject) == JSON_OBJECT);

    DocumentBuilder doc;

    const char* zKey;
    json_t* pValue;
    json_object_foreach(pObject, zKey, pValue)
    {
        string_view key(zKey);

        switch (json_typeof(pValue))
        {
        case JSON_OBJECT:
            if (!append_objectid(doc, key, pValue))
            {
                doc.append(kvp(key, bson_from_json(pValue)));
            }
            break;

        case JSON_ARRAY:
            doc.append(kvp(key, bson_from_json_array(pValue)));
            break;

        case JSON_STRING:
            doc.append(kvp(key, json_string_value(pValue)));
            break;

        case JSON_INTEGER:
            doc.append(kvp(key, (int64_t)json_integer_value(pValue)));
            break;

        case JSON_REAL:
            doc.append(kvp(key, json_number_value(pValue)));
            break;

        case JSON_TRUE:
            doc.append(kvp(key, true));
            break;

        case JSON_FALSE:
            doc.append(kvp(key, false));
            break;

        case JSON_NULL:
            doc.append(kvp(key, bsoncxx::types::b_null()));
            break;
        }
    }

    return doc.extract();
}

bsoncxx::document::value nosql::bson_from_json(const string& json)
{
    // A bsoncxx::document::value cannot be default constructed, so we just have
    // to return from many places.

    try
    {
        return bsoncxx::from_json(json);
    }
    catch (const std::exception& x)
    {
        MXB_WARNING("Could not default convert JSON to BSON: %s. JSON: %s",
                    x.what(), json.c_str());
    }

    // Ok, so the default JSON->BSON conversion failed. Probably due to there being JSON
    // sub-object that "by convention" should be converted into a particular BSON object,
    // but cannot be due to it not containing everything that is needed.

    mxb::Json j;

    if (j.load_string(json))
    {
        return bson_from_json(j.get_json());
    }
    else
    {
        MXB_ERROR("Could not load JSON data, returning empty document: %s. JSON: %s",
                  j.error_msg().c_str(), json.c_str());
    }

    DocumentBuilder doc;
    return doc.extract();
}

