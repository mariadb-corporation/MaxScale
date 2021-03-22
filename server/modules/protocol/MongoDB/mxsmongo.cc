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
#include <bsoncxx/builder/stream/document.hpp>
#include <maxscale/dcb.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include "mxsmongodatabase.hh"

using namespace std;

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

GWBUF* mxsmongo::SoftError::create_response(const mxsmongo::Command& command) const
{
    return command.create_soft_error(what(), m_code);
}

GWBUF* mxsmongo::HardError::create_response(const mxsmongo::Command& command) const
{
    return command.create_hard_error(what(), m_code);
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

const unordered_map<string, ElementValueInfo> converters =
{
    { "$eq",  { "=",      &element_to_value } },
    { "$gt",  { ">",      &element_to_value } },
    { "$gte", { ">=",     &element_to_value } },
    { "$lt",  { "<",      &element_to_value } },
    { "$in",  { "IN",     &element_to_array } },
    { "$lte", { "<=",     &element_to_value } },
    { "$ne",  { "!=",     &element_to_value } },
    { "$nin", { "NOT IN", &element_to_array } },
};

string get_comparison_op_and_value(const bsoncxx::document::view& doc)
{
    string rv;

    for (auto it = doc.begin(); it != doc.end(); ++it)
    {
        const auto& element = *it;
        const auto op = static_cast<string>(element.key());

        auto jt = converters.find(op);

        if (jt != converters.end())
        {
            if (!rv.empty())
            {
                MXS_WARNING("Comparison object '%s' has more fields than one, only "
                            "the last one will be applied.", bsoncxx::to_json(doc).c_str());
            }

            rv = " " + jt->second.op + " " + jt->second.converter(element);
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
        string op_and_value = get_comparison_op_and_value(element.get_document());

        if (!op_and_value.empty())
        {
            condition = "( JSON_EXTRACT(doc, '$." + field + "')" + op_and_value + ")";
        }
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

string mxsmongo::to_string(const bsoncxx::document::element& element)
{
    return element_to_value(element);
}

string mxsmongo::filter_to_where_clause(const bsoncxx::document::view& filter)
{
    return get_condition(filter);
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

        if (!mxsmongo::get_integer(element, &value))
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

    case bsoncxx::type::k_double:
        // Integers are often passed as double.
        *pInt = element.get_double();
        break;

    default:
        rv = false;
    }

    return rv;
}

std::atomic_int64_t mxsmongo::Mongo::Context::s_connection_id;

mxsmongo::Mongo::Mongo(mxs::ClientConnection* pClient_connection,
                       mxs::Component* pDownstream,
                       const Config* pConfig)
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
            MXS_ERROR("Packet %s not handled.", mxsmongo::opcode_to_string(req.opcode()));
            mxb_assert(!true);
            break;

        case MONGOC_OPCODE_MSG:
            pResponse = handle_msg(pRequest, mxsmongo::Msg(req));
            break;

        case MONGOC_OPCODE_QUERY:
            pResponse = handle_query(pRequest, mxsmongo::Query(req));
            break;

        default:
            MXS_ERROR("Unknown opcode %d.", req.opcode());
            mxb_assert(!true);
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

int32_t mxsmongo::Mongo::clientReply(GWBUF* pMariaDB_response, DCB* pDcb)
{
    mxb_assert(m_sDatabase.get());

    // TODO: Remove need for making resultset contiguous and adda
    // TODO: capability for dealing with resultsets larger than 16MB
    pMariaDB_response = gwbuf_make_contiguous(pMariaDB_response);
    mxb_assert(gwbuf_length(pMariaDB_response) < MYSQL_PACKET_LENGTH_MAX);

    GWBUF* pMongoDB_response = m_sDatabase->translate(*pMariaDB_response);
    gwbuf_free(pMariaDB_response);

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
