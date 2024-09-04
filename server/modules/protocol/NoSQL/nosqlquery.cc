/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqlquery.hh"
#include <map>
#include <sstream>
#include "nosqlbase.hh"
#include "nosqlcommon.hh"
#include "nosqlfieldpath.hh"
#include "nosqlnobson.hh"

using namespace std;

namespace nosql
{

namespace condition
{

namespace
{

#define NOSQL_CONDITION(C) { C::NAME, C::create }

map<string, Query::Condition::Creator, less<>> top_level_conditions =
{
    NOSQL_CONDITION(AlwaysFalse),
    NOSQL_CONDITION(AlwaysTrue),
    NOSQL_CONDITION(And),
    NOSQL_CONDITION(Or),
    NOSQL_CONDITION(Nor)
};

class FieldCondition : public Query::Condition
{
public:
    FieldCondition(string_view field_path, const BsonView& view)
        : m_field_path(field_path, FieldPath::Mode::WITHOUT_DOLLAR)
        , m_view(view)
    {
    }

    string generate_sql() const override
    {
        string condition;

        const string& head = m_field_path.head();
        const auto* pTail = m_field_path.tail();
        auto type = m_view.type();

        if (head == "_id" && pTail == nullptr && type != bsoncxx::type::k_document)
        {
            condition = "( id = '";

            bool is_utf8 = (type == bsoncxx::type::k_utf8);

            if (is_utf8)
            {
                condition += "\"";
            }

            auto id = nosql::element_to_string(m_view);

            condition += id;

            if (is_utf8)
            {
                condition += "\"";
            }

            condition += "'";

            if (is_utf8 && id.length() == 24 && nosql::is_hex(id))
            {
                // This sure looks like an ObjectId. And this is the way it will appear
                // if a search is made using a DBPointer. So we'll cover that case as well.

                condition += " OR id = '{\"$oid\":\"" + id + "\"}'";
            }

            condition += ")";
        }
        else
        {
            Path path(m_field_path.path(), m_view);

            condition = path.get_comparison_condition();
        }

        return condition;
    }

    bool matches(bsoncxx::document::view doc) const override
    {
        if (!m_sEvaluator)
        {
            m_sEvaluator = Query::Evaluator::create(&m_field_path, m_view);
        }

        return m_sEvaluator->matches(doc);
    }

private:
    FieldPath                 m_field_path;
    BsonView                  m_view;
    mutable Query::SEvaluator m_sEvaluator;
};

}
}

namespace evaluator
{

namespace
{

#define NOSQL_EVALUATOR(C) { C::NAME, C::create }

map<string, Query::Evaluator::Creator, less<>> evaluators =
{
    NOSQL_EVALUATOR(All),
    NOSQL_EVALUATOR(ElemMatch),
    NOSQL_EVALUATOR(Eq),
    NOSQL_EVALUATOR(Exists),
    NOSQL_EVALUATOR(Gt),
    NOSQL_EVALUATOR(Gte),
    NOSQL_EVALUATOR(In),
    NOSQL_EVALUATOR(Lt),
    NOSQL_EVALUATOR(Lte),
    NOSQL_EVALUATOR(Ne),
    NOSQL_EVALUATOR(Size),
    NOSQL_EVALUATOR(Type),
};

}

}

}

namespace
{

using namespace nosql;

void require_1(const Query::Condition::BsonView& view, const char* zCondition)
{
    int32_t number = -1;

    switch (view.type())
    {
    case bsoncxx::type::k_int32:
        number = view.get_int32();
        break;

    case bsoncxx::type::k_int64:
        number = view.get_int64();
        break;

    case bsoncxx::type::k_double:
        {
            number = view.get_double();

            if (number != view.get_double())
            {
                number = -1;
            }
        }
        break;

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 d128 = view.get_decimal128().value;

            if (d128 == bsoncxx::decimal128("1"))
            {
                number = 1;
            }
        }
        break;

    default:
        {
            stringstream serr;
            serr << "Expected a number in: " << zCondition << ": " << nobson::to_bson_expression(view);

            throw SoftError(serr.str(), error::FAILED_TO_PARSE);
        }
    }

    if (number != 1)
    {
        stringstream serr;
        serr << zCondition << " must be an integer value of 1";

        throw SoftError(serr.str(), error::FAILED_TO_PARSE);
    }
}

}

namespace nosql
{

/*
 * Query
 */
Query::Query(bsoncxx::document::view match)
    : m_conditions(create(match))
{
}

std::string Query::sql() const
{
    if (m_sql.empty())
    {
        auto it = m_conditions.begin();
        auto end = m_conditions.end();

        while (it != end)
        {
            auto& sCondition = *it;

            string sql = sCondition->generate_sql();

            if (sql.empty())
            {
                m_sql.clear();
                break;
            }
            else if (!m_sql.empty())
            {
                m_sql += " AND ";
            }

            m_sql += sql;

            ++it;
        }

        if (m_sql.empty())
        {
            m_sql = "true";
        }
    }

    return m_sql;
}

bool Query::matches(bsoncxx::document::view doc) const
{
    bool rv = true;

    for (const auto& sCondition : m_conditions)
    {
        if (!sCondition->matches(doc))
        {
            rv = false;
            break;
        }
    }

    return rv;
}

//static
Query::SConditions Query::create(bsoncxx::document::view doc)
{
    SConditions conditions;

    for (const auto& element : doc)
    {
        conditions.emplace_back(Condition::create(element));
    }

    return conditions;
}

/*
 * Query::Condition
 */

//static
Query::SCondition Query::Condition::create(string_view name, const BsonView& view)
{
    SCondition sCondition;

    if (!name.empty() && name.front() == '$')
    {
        auto it = nosql::condition::top_level_conditions.find(name);

        if (it == nosql::condition::top_level_conditions.end())
        {
            ostringstream serr;
            serr << "unknown top level operator: " << name;

            throw SoftError(serr.str(), error::BAD_VALUE);
        }

        sCondition = it->second(view);
    }
    else
    {
        sCondition = std::make_unique<condition::FieldCondition>(name, view);
    }

    return sCondition;
}

//static
Query::SCondition Query::Condition::create(bsoncxx::document::view doc)
{
    SConditions conditions;

    for (const auto& element : doc)
    {
        conditions.emplace_back(create(element));
    }

    SCondition sCondition;

    switch (conditions.size())
    {
    case 0:
        sCondition = std::make_unique<condition::AlwaysTrue>();
        break;

    case 1:
        sCondition = std::move(conditions.front());
        break;

    default:
        sCondition = std::make_unique<condition::And>(std::move(conditions));
    }

    return sCondition;
}

Query::SConditions Query::Condition::logical_condition(const BsonView& view, const char* zOp)
{
    if (view.type() != bsoncxx::type::k_array)
    {
        ostringstream serr;
        serr << zOp << " must be an array";

        throw SoftError(serr.str(), error::BAD_VALUE);
    }

    bsoncxx::array::view array = view.get_array();

    auto begin = array.begin();
    auto end = array.end();

    if (begin == end)
    {
        throw SoftError("$and/$or/$nor must be a nonempty array", error::BAD_VALUE);
    }

    SConditions conditions;

    for (auto it = begin; it != end; ++it)
    {
        auto& element = *it;

        if (element.type() != bsoncxx::type::k_document)
        {
            throw SoftError("$or/$and/$nor entries need to be full objects", error::BAD_VALUE);
        }

        conditions.emplace_back(create(element.get_document()));
    }

    return conditions;
}

/**
 * Query::Evaluator
 */
bool Query::Evaluator::matches(bsoncxx::document::view doc) const
{
    return matches(m_field_path.get(doc));
}

bool Query::Evaluator::matches(const bsoncxx::document::element& element) const
{
    return matches(element.get_value());
}

//static
Query::SEvaluator Query::Evaluator::create(const FieldPath* pField_path,
                                           string_view name,
                                           const BsonView& view)
{
    SEvaluator sEvaluator;

    if (!name.empty() && name.front() == '$')
    {
        auto it = nosql::evaluator::evaluators.find(name);

        if (it == nosql::evaluator::evaluators.end())
        {            ostringstream serr;
            serr << "unknown operator: " << name;

            throw SoftError(serr.str(), error::BAD_VALUE);
        }

        sEvaluator = it->second(pField_path, view);
    }

    return sEvaluator;
}

//static
Query::SEvaluator Query::Evaluator::create(const FieldPath* pField_path,
                                           const bsoncxx::types::bson_value::view& view)
{
    SEvaluator sEvaluator;

    if (view.type() == bsoncxx::type::k_document)
    {
        sEvaluator = create(pField_path, view.get_document());
    }

    if (!sEvaluator)
    {
        sEvaluator = std::make_unique<evaluator::Eq>(pField_path, view);
    }

    return sEvaluator;
}

//static
Query::SEvaluator Query::Evaluator::create(const FieldPath* pField_path, bsoncxx::document::view doc)
{
    SEvaluator sEvaluator;

    // TODO: For now we ignore all elements but the last.
    for (const auto& element : doc)
    {
        sEvaluator = create(pField_path, element);
    }

    return sEvaluator;
}

namespace condition
{

/*
 * LogicalCondition
 */

/**
 * AlwaysFalse
 */
AlwaysFalse::AlwaysFalse(const BsonView& view)
{
    require_1(view, NAME);
}

string AlwaysFalse::generate_sql() const
{
    return "false";
}

bool AlwaysFalse::matches(bsoncxx::document::view doc) const
{
    return false;
}

/**
 * AlwaysTrue
 */
AlwaysTrue::AlwaysTrue()
{
}

AlwaysTrue::AlwaysTrue(const BsonView& view)
{
    require_1(view, NAME);
}

string AlwaysTrue::generate_sql() const
{
    return "true";
}

bool AlwaysTrue::matches(bsoncxx::document::view doc) const
{
    return true;
}

/**
 * And
 */
bool And::matches(bsoncxx::document::view doc) const
{
   return std::all_of(m_conditions.begin(), m_conditions.end(), [&doc] (const auto& sCondition) {
        return sCondition->matches(doc);
    });
}

void And::add_sql(string& sql, const string& condition) const
{
    if (!sql.empty())
    {
        sql += " AND ";
    }

    sql += condition;
}

/**
 * Or
 */
bool Or::matches(bsoncxx::document::view doc) const
{
   return std::any_of(m_conditions.begin(), m_conditions.end(), [&doc] (const auto& sCondition) {
        return sCondition->matches(doc);
   });
}

void Or::add_sql(string& sql, const string& condition) const
{
    if (!sql.empty())
    {
        sql += " OR ";
    }

    sql += condition;
}

/**
 * Nor
 */
bool Nor::matches(bsoncxx::document::view doc) const
{
   return std::none_of(m_conditions.begin(), m_conditions.end(), [&doc] (const auto& sCondition) {
        return sCondition->matches(doc);
   });
}

void Nor::add_sql(string& sql, const string& condition) const
{
    if (!sql.empty())
    {
        sql += " AND ";
    }

    sql += "NOT " + condition;
}

}

namespace evaluator
{

/**
 * All
 */
All::All(const FieldPath* pField_path, const BsonView& view)
    : Base(pField_path)
{
    if (view.type() != bsoncxx::type::k_array)
    {
        throw SoftError("$all needs an array", error::BAD_VALUE);
    }

    m_all = view.get_array();
}

bool All::matches(const bsoncxx::types::bson_value::view& view) const
{
    bool rv;

    if (view.type() == bsoncxx::type::k_array)
    {
        rv = true;

        bsoncxx::array::view a = view.get_array();
        set<bsoncxx::types::bson_value::view> s;

        for (const bsoncxx::array::element& e : a)
        {
            s.insert(e.get_value());
        }

        for (const bsoncxx::array::element& e : m_all)
        {
            if (s.find(e.get_value()) == s.end())
            {
                rv = false;
                break;
            }
        }
    }
    else
    {
        rv = (view == m_all);
    }

    return rv;
}

/**
 * ElemMatch
 */
ElemMatch::ElemMatch(const FieldPath* pField_path, const BsonView& view)
    : Base(pField_path)
{
    if (view.type() != bsoncxx::type::k_document)
    {
        throw SoftError("$elemMatch needs an Object", error::BAD_VALUE);
    }

    bsoncxx::document::view doc = view.get_document();

    for (const bsoncxx::document::element& element : doc)
    {
        m_elem_match.emplace_back(Evaluator::create(pField_path, element));
    }
}

bool ElemMatch::matches(const bsoncxx::types::bson_value::view& view) const
{
    bool rv = false;

    if (view.type() == bsoncxx::type::k_array)
    {
        rv = matches(view.get_array());
    }

    return rv;
}

bool ElemMatch::matches(const bsoncxx::array::view& array) const
{
    bool rv = false;

    auto it = m_elem_match.begin();
    auto end = m_elem_match.end();

    if (it != end)
    {
        for (const bsoncxx::array::element& element : array)
        {
            bsoncxx::types::bson_value::view view = element.get_value();

            rv = std::all_of(it, end, [&view](const std::unique_ptr<Evaluator>& sEvaluator) {
                return sEvaluator->matches(view);
            });

            if (rv)
            {
                break;
            }
        }
    }

    return rv;
}

/**
 * Eq
 */
bool Eq::matches(const bsoncxx::types::bson_value::view& view) const
{
    return m_view == view;
}

/**
 * Exists
 */

bool Exists::matches(const bsoncxx::document::element& element) const
{
    return element ? true : false;
}

bool Exists::matches(const bsoncxx::types::bson_value::view& view) const
{
    mxb_assert(!true);
    return false;
}

/**
 * Gt
 */
bool Gt::matches(const bsoncxx::types::bson_value::view& view) const
{
    return m_view > view;
}

/**
 * Gte
 */
bool Gte::matches(const bsoncxx::types::bson_value::view& view) const
{
    return m_view >= view;
}

/**
 * In
 */
In::In(const FieldPath* pField_path, const BsonView& view)
    : Base(pField_path)
    , m_in(get_array(view))
{
}

bool In::matches(const bsoncxx::types::bson_value::view& view) const
{
    bool rv = false;

    for (const bsoncxx::array::element& element : m_in)
    {
        mxb_assert(element.get_value().type() != bsoncxx::type::k_regex);

        if (element.get_value() == view)
        {
            rv = true;
            break;
        }
    }

    return rv;
}

//static
bsoncxx::array::view In::get_array(const BsonView& view)
{
    if (view.type() != bsoncxx::type::k_array)
    {
        throw SoftError("$in needs an array", error::BAD_VALUE);
    }

    bsoncxx::array::view in = view.get_array();

    for (const bsoncxx::array::element& element : in)
    {
        if (element.type() == bsoncxx::type::k_regex)
        {
            // TODO: Handle regular expression matches.
            throw SoftError("Currently regular expressions cannot be used in $in",
                            error::INTERNAL_ERROR);
        }
    }

    return in;
}

/**
 * Lt
 */
bool Lt::matches(const bsoncxx::types::bson_value::view& view) const
{
    return m_view < view;
}

/**
 * Lte
 */
bool Lte::matches(const bsoncxx::types::bson_value::view& view) const
{
    return m_view <= view;
}

/**
 * Ne
 */
bool Ne::matches(const bsoncxx::types::bson_value::view& view) const
{
    return m_view != view;
}

/**
 * Size
 */
Size::Size(const FieldPath* pField_path, const BsonView& view)
    : Base(pField_path)
{
    int64_t size = 0;

    switch (view.type())
    {
    case bsoncxx::type::k_int32:
        size = view.get_int32();
        break;

    case bsoncxx::type::k_int64:
        size = view.get_int64();
        break;

    case bsoncxx::type::k_double:
        size = view.get_double();
        if (size != view.get_double())
        {
            stringstream serr;
            serr << "Failed to parse $size. Expected an integer: $size: "
                 << nobson::to_bson_expression(view);

            throw SoftError(serr.str(), error::BAD_VALUE);
        }
        break;

    default:
        {
            stringstream serr;
            serr << "Failed to parse $size. Expected a number in: $size: "
                 << nobson::to_bson_expression(view);

            throw SoftError(serr.str(), error::BAD_VALUE);
        }
    }

    if (size < 0)
    {
        stringstream serr;
        serr << "Failed to parse $size. Expected a non-negative number in: $size: "
             << nobson::to_bson_expression(view);
        throw SoftError(serr.str(), error::BAD_VALUE);
    }

    m_size = size;
}

bool Size::matches(const bsoncxx::types::bson_value::view& view) const
{
    bool rv = false;

    if (view.type() == bsoncxx::type::k_array)
    {
        bsoncxx::array::view a = view.get_array();

        if (m_size == std::distance(a.begin(), a.end()))
        {
            rv = true;
        }
    }

    return rv;
}


/**
 * Type
 */
Type::Type(const FieldPath* pField_path, const BsonView& view)
    : Base(pField_path)
    , m_types(get_types(view))
{
}

bool Type::matches(const bsoncxx::types::bson_value::view& view) const
{
    auto end = m_types.end();
    auto it = std::find(m_types.begin(), end, view.type());

    return it != end;
}

vector<bsoncxx::type> Type::get_types(const BsonView& view)
{
    vector<bsoncxx::type> rv;

    if (view.type() == bsoncxx::type::k_array)
    {
        bsoncxx::array::view array = view.get_array();
        for (const auto& item : array)
        {
            get_types(rv, item.get_value());
        }
    }
    else
    {
        get_types(rv, view);
    }

    return rv;
}

void Type::get_types(vector<bsoncxx::type>& types, const BsonView& view)
{
    int32_t code = 0;

    switch (view.type())
    {
    case bsoncxx::type::k_double:
        {
            auto d = view.get_double();
            code = d;

            if (code != d)
            {
                stringstream serr;
                serr << "Invalid numerical type code: " << d;

                throw SoftError(serr.str(), error::BAD_VALUE);
            }
        }
        break;

    case bsoncxx::type::k_int32:
        code = view.get_int32();
        break;

    case bsoncxx::type::k_int64:
        code = view.get_int32();
        break;

    case bsoncxx::type::k_string:
        {
            string_view sv = view.get_string();

            if (sv == "number")
            {
                types.push_back(bsoncxx::type::k_double);
                types.push_back(bsoncxx::type::k_int32);
                types.push_back(bsoncxx::type::k_int64);
                types.push_back(bsoncxx::type::k_decimal128);
            }
            else
            {
                bsoncxx::type type;
                if (!nobson::from_string(sv, &type))
                {
                    stringstream serr;
                    serr << "Unknown type name alias: " << sv;

                    throw SoftError(serr.str(), error::BAD_VALUE);
                }

                types.push_back(type);
            }
        }
        break;

    default:
        throw SoftError("type must be represented as a number or a string", error::TYPE_MISMATCH);
    }

    bsoncxx::type type;
    if (!nobson::from_number(code, &type))
    {
        stringstream serr;
        serr << "Invalid numerical type code: " << code;

        throw SoftError(serr.str(), error::BAD_VALUE);
    }

    types.push_back(type);
}

}

}
