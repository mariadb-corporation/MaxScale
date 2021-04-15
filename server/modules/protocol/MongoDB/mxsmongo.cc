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

#include "mxsmongo.hh"
#include <sstream>
#include <map>
#include <bsoncxx/json.hpp>
#include <maxscale/dcb.hh>
#include <maxscale/session.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include "../../filter/masking/mysql.hh"
#include "mxsmongodatabase.hh"
#include "crc32.h"

using namespace std;

namespace
{

uint32_t (*crc32_func)(const void *, size_t) = wiredtiger_crc32c_func();

}

namespace mxsmongo
{

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
bsoncxx::document::view element_as<bsoncxx::document::view>(const string& command,
                                                            const char* zKey,
                                                            const bsoncxx::document::element& element,
                                                            Conversion)
{
    if (element.type() != bsoncxx::type::k_document)
    {
        stringstream ss;
        ss << "BSON field '" << command << "." << zKey << "' is the wrong type '"
           << bsoncxx::to_string(element.type()) << "', expected type 'object'";

        throw SoftError(ss.str(), error::TYPE_MISMATCH);
    }

    return element.get_document();
}

template<>
bsoncxx::array::view element_as<bsoncxx::array::view>(const string& command,
                                                      const char* zKey,
                                                      const bsoncxx::document::element& element,
                                                      Conversion)
{
    if (element.type() != bsoncxx::type::k_array)
    {
        stringstream ss;
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
        stringstream ss;
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
        stringstream ss;
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
            stringstream ss;
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
        stringstream ss;
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
            stringstream ss;
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
        stringstream ss;
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

mxsmongo::Query::Query(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_QUERY);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(mongo::HEADER);

    pData += mxsmongo::get_byte4(pData, &m_flags);
    pData += mxsmongo::get_zstring(pData, &m_zCollection);
    pData += mxsmongo::get_byte4(pData, &m_nSkip);
    pData += mxsmongo::get_byte4(pData, &m_nReturn);

    uint32_t size;
    mxsmongo::get_byte4(pData, &size);
    m_query = bsoncxx::document::view { pData, size };
    pData += size;

    if (pData < m_pEnd)
    {
        mxsmongo::get_byte4(pData, &size);
        if (m_pEnd - pData != size)
        {
            mxb_assert(!true);
            std::stringstream ss;
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
        std::stringstream ss;
        ss << "Malformed packet, " << m_pEnd - pData << " trailing bytes found.";

        throw std::runtime_error(ss.str());
    }
}

mxsmongo::Msg::Msg(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_MSG);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(mongo::HEADER);

    pData += mxsmongo::get_byte4(pData, &m_flags);

    if (checksum_present())
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(m_pHeader);

        uint32_t checksum = crc32_func(p, m_pHeader->msg_len - sizeof(uint32_t));

        p += (m_pHeader->msg_len - sizeof(uint32_t));
        const uint32_t* pChecksum = reinterpret_cast<const uint32_t*>(p);

        if (checksum != *pChecksum)
        {
            std::stringstream ss;
            ss << "Invalid checksum, expected " << checksum << ", got " << *pChecksum << ".";
            throw std::runtime_error(ss.str());
        }
    }

    const uint8_t* pSections_end = m_pEnd - (checksum_present() ? sizeof(uint32_t) : 0);
    size_t sections_size = pSections_end - pData;

    while (pData < pSections_end)
    {
        uint8_t kind;
        pData += mxsmongo::get_byte1(pData, &kind);

        switch (kind)
        {
        case 0:
            // Body section encoded as a single BSON object.
            {
                mxb_assert(m_document.empty());
                uint32_t size;
                mxsmongo::get_byte4(pData, &size);

                if (pData + size > pSections_end)
                {
                    std::stringstream ss;
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
                mxsmongo::get_byte4(pData, &total_size);

                if (pData + total_size > pSections_end)
                {
                    std::stringstream ss;
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
                    MXB_NOTICE("zIdentifier: %s", zIdentifier);

                    ++pData; // NULL-terminator

                    auto& documents = m_arguments[zIdentifier];

                    // And now there are documents all the way down...
                    while (pData < pEnd)
                    {
                        uint32_t size;
                        mxsmongo::get_byte4(pData, &size);
                        if (pData + size <= pEnd)
                        {
                            bsoncxx::document::view doc { pData, size };
                            MXB_NOTICE("DOC: %s", bsoncxx::to_json(doc).c_str());
                            documents.push_back(doc);
                            pData += size;
                        }
                        else
                        {
                            mxb_assert(!true);
                            std::stringstream ss;
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
                std::stringstream ss;
                ss << "Malformed packet, expected a 'kind' of 0 or 1, received " << kind << ".";
                throw std::runtime_error(ss.str());
            }
        }
    }

    if (pData != pSections_end)
    {
        mxb_assert(!true);
        std::stringstream ss;
        ss << "Malformed packet, " << pSections_end - pData << " trailing bytes found.";
        throw std::runtime_error(ss.str());
    }
}

const char* mxsmongo::opcode_to_string(int code)
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
        return "OPCODE_GET_MORE";

    case MONGOC_OPCODE_DELETE:
        return "MONGOC_OPCODE_DELETE";

    case MONGOC_OPCODE_KILL_CURSORS:
        return "OPCODE_KILL_CURSORS";

    case MONGOC_OPCODE_COMPRESSED:
        return "MONGOC_OPCODE_COMPRESSED";

    case MONGOC_OPCODE_MSG:
        return "MONGOC_OPCODE_MSG";

    default:
        mxb_assert(!true);
        return "MONGOC_OPCODE_UKNOWN";
    }
}

int mxsmongo::error::from_mariadb_code(int code)
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

const char* mxsmongo::error::name(int mongo_code)
{
    switch (mongo_code)
    {
#define MXSMONGO_ERROR(symbol, code, name) case symbol: { return name; }
#include "mxsmongoerror.hh"
#undef MXSMONGO_ERROR

    default:
        mxb_assert(!true);
        return "";
    }
}

GWBUF* mxsmongo::SoftError::create_response(const Command& command) const
{
    DocumentBuilder doc;
    create_response(command, doc);

    return command.create_response(doc.extract());
}

void mxsmongo::SoftError::create_response(const Command&, DocumentBuilder& doc) const
{
    doc.append(kvp("ok", 0));
    doc.append(kvp("errmsg", what()));
    doc.append(kvp("code", m_code));
    doc.append(kvp("codeName", mxsmongo::error::name(m_code)));
}

GWBUF* mxsmongo::HardError::create_response(const mxsmongo::Command& command) const
{
    DocumentBuilder doc;
    create_response(command, doc);

    return command.create_response(doc.extract());
}

void mxsmongo::HardError::create_response(const Command&, DocumentBuilder& doc) const
{
    doc.append(kvp("$err", what()));
    doc.append(kvp("code", m_code));
}

mxsmongo::MariaDBError::MariaDBError(const ComERR& err)
    : Exception("Mongo request failed due to MariaDB error.", error::COMMAND_FAILED)
    , m_mariadb_code(err.code())
    , m_mariadb_message(err.message())
{
}

GWBUF* mxsmongo::MariaDBError::create_response(const Command& command) const
{
    DocumentBuilder doc;
    create_response(command, doc);

    return command.create_response(doc.extract());
}

void mxsmongo::MariaDBError::create_response(const Command& command, DocumentBuilder& doc) const
{
    string json = bsoncxx::to_json(command.doc());
    string sql = command.last_statement();

    DocumentBuilder mariadb;
    mariadb.append(kvp("code", m_mariadb_code));
    mariadb.append(kvp("message", m_mariadb_message));
    mariadb.append(kvp("command", json));
    mariadb.append(kvp("sql", sql));

    doc.append(kvp("$err", what()));
    doc.append(kvp("code", error::from_mariadb_code(m_mariadb_code)));
    doc.append(kvp("mariadb", mariadb.extract()));
}

vector<string> mxsmongo::projection_to_extractions(const bsoncxx::document::view& projection)
{
    vector<string> extractions;

    bool id_seen = false;

    for (auto it = projection.begin(); it != projection.end(); ++it)
    {
        const auto& element = *it;
        const auto& key = element.key();

        if (key.size() == 0)
        {
            continue;
        }

        if (key.compare("_id") != 0)
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
            default:
                include_id = static_cast<bool>(element.get_bool());
            }

            if (!include_id)
            {
                continue;
            }
        }

        extractions.push_back(static_cast<string>(key));
    }

    if (!id_seen)
    {
        extractions.push_back("_id");
    }

    return extractions;
}

namespace
{

using namespace mxsmongo;

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
            throw mxsmongo::SoftError("$or/$and/$nor entries need to be full objects",
                                      mxsmongo::error::BAD_VALUE);
        }
    }

    if (!condition.empty())
    {
        condition = "(" + condition + ")";
    }

    return condition;
}

string get_and_condition(const bsoncxx::document::element& element)
{
    mxb_assert(element.key().compare("$and") == 0);

    string condition;

    if (element.type() == bsoncxx::type::k_array)
    {
        condition = get_and_condition(element.get_array());
    }
    else
    {
        throw mxsmongo::SoftError("$and must be an array", mxsmongo::error::BAD_VALUE);
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
            throw mxsmongo::SoftError("$or/$and/$nor entries need to be full objects",
                                      mxsmongo::error::BAD_VALUE);
        }
    }

    if (!condition.empty())
    {
        condition = "(" + condition + ")";
    }

    return condition;
}

string get_nor_condition(const bsoncxx::document::element& element)
{
    mxb_assert(element.key().compare("$nor") == 0);

    string condition;

    if (element.type() == bsoncxx::type::k_array)
    {
        condition = get_nor_condition(element.get_array());
    }
    else
    {
        throw mxsmongo::SoftError("$nor must be an array", mxsmongo::error::BAD_VALUE);
    }

    return condition;
}

// https://docs.mongodb.com/manual/reference/operator/query/not/#op._S_not
string get_not_condition(const bsoncxx::document::element& element)
{
    mxb_assert(element.key().compare("$not") == 0);

    string condition;

    if (element.type() == bsoncxx::type::k_document)
    {
        string sub_condition = get_condition(element.get_document());

        if (!sub_condition.empty())
        {
            condition += "NOT " + sub_condition;
        }
    }
    else
    {
        MXS_ERROR("The value of a $not element is not a document.");
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
            throw mxsmongo::SoftError("$or/$and/$nor entries need to be full objects",
                                      mxsmongo::error::BAD_VALUE);
        }
    }

    if (!condition.empty())
    {
        condition = "(" + condition + ")";
    }

    return condition;
}

string get_or_condition(const bsoncxx::document::element& element)
{
    mxb_assert(element.key().compare("$or") == 0);

    string condition;

    if (element.type() == bsoncxx::type::k_array)
    {
        condition = get_or_condition(element.get_array());
    }
    else
    {
        throw mxsmongo::SoftError("$or must be an array", mxsmongo::error::BAD_VALUE);
    }

    return condition;
}

// https://docs.mongodb.com/manual/reference/operator/query/#logical
string get_logical_condition(const bsoncxx::document::element& element)
{
    string condition;

    const auto& key = element.key();

    if (key.compare("$and") == 0)
    {
        condition = get_and_condition(element);
    }
    else if (key.compare("$nor") == 0)
    {
        condition = get_nor_condition(element);
    }
    else if (key.compare("$not") == 0)
    {
        condition = get_not_condition(element);
    }
    else if (key.compare("$or") == 0)
    {
        condition = get_or_condition(element);
    }
    else
    {
        stringstream ss;
        ss << "unknown top level operator: " << key;

        throw mxsmongo::SoftError(ss.str(), mxsmongo::error::BAD_VALUE);
    }

    return condition;
}

using ElementValueToString = string (*)(const bsoncxx::document::element&);

struct ElementValueInfo
{
    const string         op;
    ElementValueToString converter;
};

template<class document_element_or_array_item>
string element_to_value(const document_element_or_array_item& x)
{
    stringstream ss;

    switch (x.type())
    {
    case bsoncxx::type::k_double:
        ss << x.get_double();
        break;

    case bsoncxx::type::k_utf8:
        {
            const auto& utf8 = x.get_utf8();
            ss << "'" << string(utf8.value.data(), utf8.value.size()) << "'";
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

    default:
        {
            // TODO: Mongo deals gracefully with this.
            ss << "cannot convert a " << bsoncxx::to_string(x.type()) << " to a value for comparison";

            throw mxsmongo::SoftError(ss.str(), mxsmongo::error::BAD_VALUE);
        }
    }

    return ss.str();
}

string element_to_array(const bsoncxx::document::element& element)
{
    vector<string> values;

    if (element.type() == bsoncxx::type::k_array)
    {
        bsoncxx::array::view array = element.get_array();

        for (auto it = array.begin(); it != array.end(); ++it)
        {
            const auto& item = *it;

            string value = element_to_value(item);
            mxb_assert(!value.empty());

            values.push_back(value);
        }
    }
    else
    {
        MXS_ERROR("The value of an $in/$nin element is not an array.");
    }

    string rv;

    if (!values.empty())
    {
        rv = "(" + mxb::join(values) + ")";
    }

    return rv;
}

string element_to_null(const bsoncxx::document::element& element)
{
    bool b = mxsmongo::element_as<bool>("maxscale", "internal", element, mxsmongo::Conversion::RELAXED);

    if (b)
    {
        return "NOT NULL";
    }
    else
    {
        return "NULL";
    }
}

string elemMatch_to_condition(const string& field, const bsoncxx::document::element& element)
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
        auto elemMatch = *doc.begin();
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

        condition = "(JSON_CONTAINS(doc, "
            + element_to_value(elemMatch) + ", '$." + field + "') = " + value
            + ")";
    }

    return condition;
}

const unordered_map<string, ElementValueInfo> converters =
{
    { "$eq",     { "=",      &element_to_value } },
    { "$gt",     { ">",      &element_to_value } },
    { "$gte",    { ">=",     &element_to_value } },
    { "$lt",     { "<",      &element_to_value } },
    { "$in",     { "IN",     &element_to_array } },
    { "$lte",    { "<=",     &element_to_value } },
    { "$ne",     { "!=",     &element_to_value } },
    { "$nin",    { "NOT IN", &element_to_array } },
    { "$exists", { "IS",     &element_to_null } }
};

string get_comparison_condition(const string& field, const bsoncxx::document::view& doc)
{
    string rv;

    for (auto it = doc.begin(); it != doc.end(); ++it)
    {
        if (!rv.empty())
        {
            MXS_WARNING("Comparison object '%s' has more fields than one, only "
                        "the last one will be applied.", bsoncxx::to_json(doc).c_str());
        }

        const auto& element = *it;
        const auto op = static_cast<string>(element.key());

        auto jt = converters.find(op);

        if (jt != converters.end())
        {
            rv = "( JSON_EXTRACT(doc, '$." + field + "') "
                + jt->second.op + " " + jt->second.converter(element) + ")";
        }
        else if (op == "$elemMatch")
        {
            rv = elemMatch_to_condition(field, element);
        }
        else
        {
            stringstream ss;
            ss << "unknown operator: " << op;
            throw mxsmongo::SoftError(ss.str(), mxsmongo::error::BAD_VALUE);
        }
    }

    return rv;
}

// https://docs.mongodb.com/manual/reference/operator/query/#comparison
string get_comparison_condition(const bsoncxx::document::element& element)
{
    string condition;

    string field = static_cast<string>(element.key());
    auto type = element.type();

    if (type == bsoncxx::type::k_document)
    {
        condition = get_comparison_condition(field, element.get_document());
    }
    else
    {
        string value;

        if (type == bsoncxx::type::k_oid && field == "_id")
        {
            // If the value is an Oid and the field is _id, then we assume that
            // _id is an ObjectID like '{ $oid: "..." }'.
            field += ".$oid";
            value = "'" + element.get_oid().value.to_string() + "'";
        }
        else
        {
            value = element_to_value(element);
        }

        condition = "( JSON_EXTRACT(doc, '$." + field + "') = " + value + ")";
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

string mxsmongo::to_value(const bsoncxx::document::element& element)
{
    return element_to_value(element);
}

template<class document_element_or_array_item>
string element_to_string(const document_element_or_array_item& x)
{
    stringstream ss;

    switch (x.type())
    {
    case bsoncxx::type::k_array:
        {
            ss << "[";
            bsoncxx::array::view array = x.get_array();
            for (const auto& item : array)
            {
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
        ss << bsoncxx::to_json(x.get_document());
        break;

    case bsoncxx::type::k_double:
        ss << x.get_double();
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
        ss << x.get_oid().value.to_string();
        break;

    case bsoncxx::type::k_regex:
        ss << x.get_regex().regex;
        break;

    case bsoncxx::type::k_symbol:
        ss << x.get_symbol().symbol;
        break;

    case bsoncxx::type::k_utf8:
        ss << x.get_utf8().value;
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

string mxsmongo::to_string(const bsoncxx::document::element& element)
{
    return element_to_string(element);
}

string mxsmongo::query_to_where_condition(const bsoncxx::document::view& query)
{
    return get_condition(query);
}

string mxsmongo::query_to_where_clause(const bsoncxx::document::view& query)
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
string mxsmongo::sort_to_order_by(const bsoncxx::document::view& sort)
{
    string order_by;

    for (auto it = sort.begin(); it != sort.end(); ++it)
    {
        const auto& element = *it;
        const auto& key = element.key();

        if (key.size() == 0)
        {
            throw mxsmongo::SoftError("FieldPath cannot be constructed with empty string",
                                      mxsmongo::error::LOCATION40352);
        }

        int64_t value = 0;

        if (!mxsmongo::get_number_as_integer(element, &value))
        {
            stringstream ss;
            // TODO: Should actually be the value itself, and not its type.
            ss << "Illegal key in $sort specification: "
               << element.key() << ": " << bsoncxx::to_string(element.type());

            throw mxsmongo::SoftError(ss.str(), mxsmongo::error::LOCATION15974);
        }

        if (value != 1 && value != -1)
        {
            throw mxsmongo::SoftError("$sort key ordering must be 1 (for ascending) or -1 (for descending)",
                                      mxsmongo::error::LOCATION15975);
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

bool mxsmongo::get_integer(const bsoncxx::document::element& element, int64_t* pInt)
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

bool mxsmongo::get_number_as_integer(const bsoncxx::document::element& element, int64_t* pInt)
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

bool mxsmongo::get_number_as_double(const bsoncxx::document::element& element, double_t* pDouble)
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

std::atomic_int64_t mxsmongo::Mongo::Context::s_connection_id;

namespace
{

void throw_cursor_not_found(int64_t id)
{
    stringstream ss;
    ss << "cursor id " << id << " not found";
    throw mxsmongo::SoftError(ss.str(), mxsmongo::error::CURSOR_NOT_FOUND);
}

}

mxsmongo::MongoCursor& mxsmongo::Mongo::Context::get_cursor(const std::string& collection, int64_t id)
{
    auto it = m_collection_cursors.find(collection);

    if (it == m_collection_cursors.end())
    {
        throw_cursor_not_found(id);
    }

    CursorsById& cursors = it->second;

    auto jt = cursors.find(id);

    if (jt == cursors.end())
    {
        throw_cursor_not_found(id);
    }

    return jt->second;
}

void mxsmongo::Mongo::Context::remove_cursor(const MongoCursor& cursor)
{
    auto it = m_collection_cursors.find(cursor.collection());
    mxb_assert(it != m_collection_cursors.end());

    if (it != m_collection_cursors.end())
    {
        CursorsById& cursors = it->second;

        auto jt = cursors.find(cursor.id());
        mxb_assert(jt != cursors.end());

        if (jt != cursors.end())
        {
            cursors.erase(jt);
        }
    }
}

void mxsmongo::Mongo::Context::store_cursor(MongoCursor&& cursor)
{
    CursorsById& cursors = m_collection_cursors[cursor.collection()];

    mxb_assert(cursors.find(cursor.id()) == cursors.end());

    cursors.emplace(std::make_pair(cursor.id(), std::move(cursor)));
}

mxsmongo::Mongo::Mongo(mxs::ClientConnection* pClient_connection,
                       mxs::Component* pDownstream,
                       Config* pConfig)
    : m_context(pClient_connection, pDownstream)
    , m_config(*pConfig)
{
}

mxsmongo::Mongo::~Mongo()
{
}

GWBUF* mxsmongo::Mongo::handle_request(GWBUF* pRequest)
{
    GWBUF* pResponse = nullptr;

    if (!m_sDatabase)
    {
        try
        {
            // If no database operation is in progress, we proceed.
            mxsmongo::Packet req(pRequest);

            mxb_assert(req.msg_len() == (int)gwbuf_length(pRequest));

            switch (req.opcode())
            {
            case MONGOC_OPCODE_COMPRESSED:
            case MONGOC_OPCODE_DELETE:
            case MONGOC_OPCODE_GET_MORE:
            case MONGOC_OPCODE_INSERT:
            case MONGOC_OPCODE_KILL_CURSORS:
            case MONGOC_OPCODE_REPLY:
            case MONGOC_OPCODE_UPDATE:
                {
                    mxb_assert(!true);
                    stringstream ss;
                    ss << "Unsupported packet " << mxsmongo::opcode_to_string(req.opcode()) << " received.";
                    throw std::runtime_error(ss.str());
                }
                break;

            case MONGOC_OPCODE_MSG:
                pResponse = handle_msg(pRequest, mxsmongo::Msg(req));
                break;

            case MONGOC_OPCODE_QUERY:
                pResponse = handle_query(pRequest, mxsmongo::Query(req));
                break;

            default:
                {
                    mxb_assert(!true);
                    stringstream ss;
                    ss << "Unknown packet " << req.opcode() << " received.";
                    throw std::runtime_error(ss.str());
                }
            }
        }
        catch (const std::exception& x)
        {
            MXS_ERROR("%s. Closing client connection.", x.what());
            kill_client();
        }

        gwbuf_free(pRequest);
    }
    else
    {
        // Otherwise we push it on the request queue.
        m_requests.push_back(pRequest);
    }

    return pResponse;
}

int32_t mxsmongo::Mongo::clientReply(GWBUF* pMariadb_response, DCB* pDcb)
{
    mxb_assert(m_sDatabase.get());

    // TODO: Remove need for making resultset contiguous and adda
    // TODO: capability for dealing with resultsets larger than 16MB
    pMariadb_response = gwbuf_make_contiguous(pMariadb_response);
    mxb_assert(gwbuf_length(pMariadb_response) < MYSQL_PACKET_LENGTH_MAX);

    mxs::Buffer mariadb_response(pMariadb_response);
    GWBUF* pMongoDB_response = m_sDatabase->translate(std::move(mariadb_response));

    if (m_sDatabase->is_ready())
    {
        m_sDatabase.reset();

        if (pMongoDB_response)
        {
            pDcb->writeq_append(pMongoDB_response);
        }

        if (!m_requests.empty())
        {
            // Loop as long as responses to requests can be generated immediately.
            // If it can't then we'll continue once clientReply() is called anew.
            do
            {
                mxb_assert(!m_sDatabase.get());

                GWBUF* pRequest = m_requests.front();
                m_requests.pop_front();

                pMongoDB_response = handle_request(pRequest);

                if (pMongoDB_response)
                {
                    // The response could be generated immediately, just send it.
                    pDcb->writeq_append(pMongoDB_response);
                }
            }
            while (pMongoDB_response && !m_requests.empty());
        }
    }
    else
    {
        // If the database is not ready, there cannot be a response.
        mxb_assert(pMongoDB_response == nullptr);
    }

    return 0;
}

void mxsmongo::Mongo::kill_client()
{
    m_context.client_connection().dcb()->session()->kill();
}

GWBUF* mxsmongo::Mongo::handle_query(GWBUF* pRequest, const mxsmongo::Query& req)
{
    MXB_NOTICE("Request(QUERY): %s, %s", req.zCollection(), bsoncxx::to_json(req.query()).c_str());

    auto sDatabase = Database::create(req.collection(), &m_context, &m_config);

    GWBUF* pResponse = sDatabase->handle_query(pRequest, req);

    if (!pResponse)
    {
        mxb_assert(!m_sDatabase.get());
        m_sDatabase = std::move(sDatabase);
    }

    return pResponse;
}

GWBUF* mxsmongo::Mongo::handle_msg(GWBUF* pRequest, const mxsmongo::Msg& req)
{
    MXB_NOTICE("Request(MSG): %s", bsoncxx::to_json(req.document()).c_str());

    GWBUF* pResponse = nullptr;

    const auto& doc = req.document();

    auto element = doc["$db"];

    if (element)
    {
        if (element.type() == bsoncxx::type::k_utf8)
        {
            auto utf8 = element.get_utf8();

            string name(utf8.value.data(), utf8.value.size());
            auto sDatabase = Database::create(name, &m_context, &m_config);

            pResponse = sDatabase->handle_command(pRequest, req, doc);

            if (!pResponse)
            {
                // TODO: See handle_query()
                m_sDatabase = std::move(sDatabase);
            }
        }
        else
        {
            MXS_ERROR("Key '$db' found, but value is not utf8.");
            mxb_assert(!true);
        }
    }
    else
    {
        MXS_ERROR("Document did not contain the expected key '$db': %s",
                  req.to_string().c_str());
        mxb_assert(!true);
    }

    return pResponse;
}
