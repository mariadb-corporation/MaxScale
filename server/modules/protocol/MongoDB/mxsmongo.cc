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
#include "mxsmongodatabase.hh"

using namespace std;

namespace
{

struct ThisUnit
{
    const map<const char*, mxsmongo::Command> commands_by_key =
    {
        { mxsmongo::keys::FIND,      mxsmongo::Command::FIND },
        { mxsmongo::keys::INSERT,    mxsmongo::Command::INSERT },
        { mxsmongo::keys::ISMASTER,  mxsmongo::Command::ISMASTER }
    };
} this_unit;

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

mxsmongo::Command mxsmongo::get_command(const bsoncxx::document::view& doc)
{
    mxsmongo::Command command = mxsmongo::Command::UNKNOWN;

    // TODO: At some point it might be good to apply some kind of heuristic for
    // TODO: deciding whether to loop over the keys of the document or over
    // TODO: the keys in the map. Or, can we be certain that e.g. the first
    // TODO: field in the document is the command?

    for (const auto& kv : this_unit.commands_by_key)
    {
        if (doc.find(kv.first) != doc.cend())
        {
            command = kv.second;
            break;
        }
    }

    return command;
}

string mxsmongo::projection_to_columns(const bsoncxx::document::view& projection)
{
    vector<string> columns;

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
            // TODO: Could something meaningful be returned for _id?
            columns.push_back(static_cast<string>(key));
        }
    }

    if (columns.empty())
    {
        columns.push_back("*");
    }

    return mxb::join(columns);
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
            MXS_ERROR("An element of an $and array is not a document.");
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
        MXS_ERROR("The value of an $and element is not an array.");
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
            MXS_ERROR("An element of a $nor array is not a document.");
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
        MXS_ERROR("The value of a $nor element is not an array.");
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
            MXS_ERROR("An element of an $or array is not a document.");
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
        MXS_ERROR("The value of an $or element is not an array.");
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
        MXS_ERROR("Operator is not recognized: '%s'.",
                  static_cast<string>(key).c_str());
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
        MXS_ERROR("Cannot convert a '%s' to a value.", bsoncxx::to_string(x.type()).c_str());
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

            if (!value.empty())
            {
                values.push_back(value);
            }
            else
            {
                MXS_ERROR("All values of an array cannot be converted.");
                values.clear();
                break;
            }
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
            MXS_ERROR("No converter found for '%s'. Invalid operator?", op.c_str());
            rv.clear();
            break;
        }
    }

    return rv;
}

// https://docs.mongodb.com/manual/reference/operator/query/#comparison
string get_comparison_condition(const bsoncxx::document::element& element)
{
    string condition;

    string field = static_cast<string>(element.key());

    if (element.type() == bsoncxx::type::k_utf8)
    {
        auto utf8 = element.get_utf8();
        string value(utf8.value.data(), utf8.value.size());

        condition = "(" + field + " = " + value + ")";
    }
    else if (element.type() == bsoncxx::type::k_document)
    {
        string op_and_value = get_comparison_op_and_value(element.get_document());

        if (!op_and_value.empty())
        {
            condition = "(" + field + op_and_value + ")";
        }
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
            MXS_ERROR("Fieldname in sort object is empty.");
            order_by.clear();
            break;
        }

        bool ok = true;
        int value = 0;

        switch (element.type())
        {
        case bsoncxx::type::k_int32:
            value = element.get_int32();
            break;

        case bsoncxx::type::k_int64:
            value = element.get_int64();
            break;

        default:
            MXS_ERROR("Only integer value ('%s' provided) can be used with sorting fields.",
                      bsoncxx::to_string(element.type()).c_str());
            ok = false;
        }

        if (!ok)
        {
            order_by.clear();
            break;
        }

        if (value > 1)
        {
            MXS_WARNING("Sorting value %d > 1, assuming 1 is meant.", value);
            value = 1;
        }
        else if (value < -1)
        {
            MXS_WARNING("Sorting value %d < -1, assuming -1 is meant.", value);
            value = 1;
        }

        if (value != 0)
        {
            if (!order_by.empty())
            {
                order_by += ", ";
            }

            order_by += static_cast<string>(element.key());

            if (value == -1)
            {
                order_by += " DESC";
            }
        }
    }

    return order_by;
}

namespace
{

bool get_integer(const bsoncxx::document::element& element, int64_t* pInt)
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

}

std::string mxsmongo::skip_and_limit_to_limit(const bsoncxx::document::element& skip,
                                              const bsoncxx::document::element& limit)
{
    mxb_assert(skip || limit);

    string rv;

    bool ok = true;

    int64_t nSkip;
    if (skip && (!get_integer(skip, &nSkip) || nSkip < 0))
    {
        ok = false;
    }

    int64_t nLimit;
    if (ok && limit && (!get_integer(limit, &nLimit) || nLimit < 0))
    {
        ok = false;
    }

    if (ok)
    {
        if (skip && !limit)
        {
            nLimit = std::numeric_limits<int64_t>::max();
        }

        stringstream ss;
        ss << " LIMIT ";

        if (nSkip != 0)
        {
            ss << nSkip << ", ";
        }

        ss << nLimit;

        rv = ss.str();
    }
    else
    {
        MXS_ERROR("The value of 'skip' and/or 'limit' is not a valid integer.");
    }

    return rv;
}

mxsmongo::Mongo::Mongo(mxs::Component* pDownstream, const Config* pConfig)
    : m_context(pDownstream)
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
            MXS_ERROR("Packet %s not handled (yet).", mxsmongo::opcode_to_string(req.opcode()));
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

    GWBUF* pMongoDB_response = m_sDatabase->translate(*pMariaDB_response);
    gwbuf_free(pMariaDB_response);

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

    return 0;
}

GWBUF* mxsmongo::Mongo::handle_query(GWBUF* pRequest, const mxsmongo::Query& req)
{
    MXS_NOTICE("\n%s\n", req.to_string().c_str());

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
    MXS_NOTICE("\n%s\n", req.to_string().c_str());

    mxb_assert(req.documents().size() == 1); // TODO

    GWBUF* pResponse = nullptr;

    for (const auto& doc : req.documents())
    {
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
    }

    return pResponse;
}
