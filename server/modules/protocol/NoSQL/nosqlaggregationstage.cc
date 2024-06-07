/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqlaggregationstage.hh"
#include <map>
#include <sstream>
#include "nosqlbsoncxx.hh"

using namespace std;

namespace nosql
{

namespace aggregation
{

namespace
{

using StageCreator = unique_ptr<Stage>(*)(bsoncxx::document::element, string_view, string_view, Stage*);
using Stages = map<string_view, StageCreator, less<>>;

#define NOSQL_STAGE(O) { O::NAME, O::create }

Stages stages =
{
    NOSQL_STAGE(AddFields),
    NOSQL_STAGE(Count),
    NOSQL_STAGE(Group),
    NOSQL_STAGE(Limit),
    NOSQL_STAGE(ListSearchIndexes),
    NOSQL_STAGE(Match),
};

}

/**
 * Stage
 */

Stage::~Stage()
{
}

string Stage::trailing_sql() const
{
    mxb_assert(!true);
    throw SoftError("A stage that must be part of the pipeline cannot be replaced by SQL.",
                    error::INTERNAL_ERROR);
}

//static
unique_ptr<Stage> Stage::get(bsoncxx::document::element element,
                             string_view database,
                             string_view table,
                             Stage* pPrevious)
{
    auto it = stages.find(element.key());

    if (it == stages.end())
    {
        stringstream ss;
        ss << "Unrecognized pipeline stage name: '" << element.key() << "'";

        throw SoftError(ss.str(), error::LOCATION40324);
    }

    return it->second(element, database, table, pPrevious);
}

std::vector<bsoncxx::document::value> Stage::post_process(GWBUF&& mariadb_response)
{
    mxb_assert(!true);

    stringstream ss;
    ss << "Invalid stage" << name() << ", cannot post-process a resultset.";

    throw SoftError(ss.str(), error::INTERNAL_ERROR);

    return std::vector<bsoncxx::document::value>();
}

/**
 * AddFields
 */
AddFields::AddFields(bsoncxx::document::element element,
                     std::string_view database,
                     std::string_view table,
                     Stage* pPrevious)
    : ConcreteStage(pPrevious)
{
    if (element.type() != bsoncxx::type::k_document)
    {
        stringstream ss;
        ss << "$addFields specification stage must be an object, got "
           << bsoncxx::to_string(element.type());

        throw SoftError(ss.str(), error::LOCATION40272);
    }

    bsoncxx::document::view add_field = element.get_document();

    try
    {
        for (auto it = add_field.begin(); it != add_field.end(); ++it)
        {
            auto def = *it;

            m_operators.emplace_back(NamedOperator { def.key(), Operator::create(def.get_value()) });
        }
    }
    catch (const SoftError& x)
    {
        stringstream ss;
        ss << "Invalid $addFields :: caused by :: " << x.what();

        throw SoftError(ss.str(), error::LOCATION16020);
    }
}

std::vector<bsoncxx::document::value> AddFields::process(std::vector<bsoncxx::document::value>& in)
{
    vector<bsoncxx::document::value> out;

    for (const bsoncxx::document::value& in_doc : in)
    {
        DocumentBuilder out_doc;

        for (auto element : in_doc)
        {
            out_doc.append(kvp(element.key(), element.get_value()));
        }

        for (const NamedOperator& nop : m_operators)
        {
            out_doc.append(kvp(nop.name, nop.sOperator->process(in_doc)));
        }

        out.emplace_back(out_doc.extract());
    }

    return out;
}

/**
 * Count
 */
Count::Count(bsoncxx::document::element element,
             std::string_view database,
             std::string_view table,
             Stage* pPrevious)
    : ConcreteStage(pPrevious)
{
    if (element.type() == bsoncxx::type::k_string)
    {
        m_field = element.get_string();
    }

    if (m_field.empty())
    {
        throw SoftError("the count field must be a non-empty string", error::LOCATION40156);
    }
}

std::vector<bsoncxx::document::value> Count::process(std::vector<bsoncxx::document::value>& in)
{
    vector<bsoncxx::document::value> rv;

    DocumentBuilder doc;
    doc.append(kvp(m_field, (int32_t)in.size()));

    rv.emplace_back(doc.extract());

    return rv;
}

/**
 * Group
 */
Stage::Operators Group::s_available_operators =
{
    { "$addToSet",     static_cast<OperatorCreator>(nullptr) },
    { "$avg",          static_cast<OperatorCreator>(nullptr) },
    { First::NAME,     First::create },
    { "$last",         static_cast<OperatorCreator>(nullptr) },
    { Max::NAME,       Max::create },
    { "$mergeObjects", static_cast<OperatorCreator>(nullptr) },
    { "$min",          static_cast<OperatorCreator>(nullptr) },
    { "$push",         static_cast<OperatorCreator>(nullptr) },
    { "$stdDevPop",    static_cast<OperatorCreator>(nullptr) },
    { Sum::NAME,       Sum::create },
};

Group::Group(bsoncxx::document::element element,
             std::string_view database,
             std::string_view table,
             Stage* pPrevious)
    : ConcreteStage(pPrevious)
{
    if (element.type() != bsoncxx::type::k_document)
    {
        throw SoftError("a group's fields must be specified in an object", error::LOCATION15947);
    }

    bsoncxx::document::view group = element.get_document();

    bool id_present = false;

    for (auto it = group.begin(); it != group.end(); ++it)
    {
        auto def = *it;

        if (def.key() == "_id")
        {
            if (def.type() != bsoncxx::type::k_null)
            {
                throw SoftError("Can only handle _id = null.", error::INTERNAL_ERROR);
            }

            id_present = true;
            continue;
        }

        add_operator(def);
    }

    if (!id_present)
    {
        throw SoftError("a group specification must include an _id", error::LOCATION15955);
    }
}

vector<bsoncxx::document::value> Group::process(vector<bsoncxx::document::value>& docs)
{
    for (const bsoncxx::document::value& doc : docs)
    {
        for (const NamedOperator& nop : m_operators)
        {
            nop.sOperator->process(doc);
        }
    }

    DocumentBuilder doc;
    doc.append(kvp("_id", bsoncxx::types::b_null()));

    for (const NamedOperator& nop : m_operators)
    {
        bsoncxx::types::value value = nop.sOperator->value();

        doc.append(kvp(nop.name, value));
    }

    vector<bsoncxx::document::value> rv;

    rv.emplace_back(doc.extract());

    return rv;
}

void Group::add_operator(bsoncxx::document::element operator_def)
{
    string_view name = operator_def.key();

    if (operator_def.type() != bsoncxx::type::k_document)
    {
        stringstream ss;
        ss << "The field '" << name << "' must be an accumulator object";

        SoftError(ss.str(), error::LOCATION40234);
    }

    add_operator(name, operator_def.get_document());
}

void Group::add_operator(std::string_view name, bsoncxx::document::view def)
{
    auto it = def.begin();

    if (it == def.end())
    {
        stringstream ss;
        ss << "The field '" << name << "' must specify one accumulator";

        throw SoftError(ss.str(), error::LOCATION40238);
    }

    auto element = *it;

    auto jt = s_available_operators.find(element.key());

    if (jt == s_available_operators.end())
    {
        stringstream ss;
        ss << "Unknown group operator '" << element.key() << "'";

        throw SoftError(ss.str(), error::LOCATION15952);
    }

    OperatorCreator create = jt->second;

    if (create)
    {
        auto sOperator = create(element.get_value());

        m_operators.emplace_back(NamedOperator { name, std::move(sOperator) });
    }
    else
    {
        Operator::unsupported(element.key());
    }
}

/**
 * Limit
 */
Limit::Limit(bsoncxx::document::element element,
             std::string_view database,
             std::string_view table,
             Stage* pPrevious)
    : ConcreteStage(pPrevious, Kind::DUAL)
{
    if (!is_integer(element))
    {
        // TODO: Append the element value.
        stringstream ss;
        ss << "invalid argument to $limit stage: Expected a number in: $limit: ";

        throw SoftError(ss.str(), error::LOCATION2107201);
    }

    m_nLimit = get_integer<int64_t>(element);

    if (m_nLimit < 0)
    {
        stringstream ss;
        ss << "invalid argument to $limit stage: Expected a non-negative number in: $limit: " << m_nLimit;

        throw SoftError(ss.str(), error::LOCATION5107201);
    }
    else if (m_nLimit == 0)
    {
        throw SoftError("the limit must be positive", error::LOCATION15958);
    }
}

string Limit::trailing_sql() const
{
    stringstream ss;
    ss << "LIMIT " << m_nLimit;

    return ss.str();
}

std::vector<bsoncxx::document::value> Limit::process(std::vector<bsoncxx::document::value>& in)
{
    if (in.size() > (size_t)m_nLimit)
    {
        in.erase(in.begin() + m_nLimit, in.end());
    }

    return std::move(in);
}

/**
 * Limit
 */
ListSearchIndexes::ListSearchIndexes(bsoncxx::document::element element,
                                     std::string_view database,
                                     std::string_view table,
                                     Stage* pPrevious)
    : ConcreteStage(pPrevious)
{
    throw SoftError("listSearchIndexes stage is only allowed on MongoDB Atlas", error::LOCATION6047401);
}

std::vector<bsoncxx::document::value> ListSearchIndexes::process(std::vector<bsoncxx::document::value>& in)
{
    mxb_assert(!true);
    return std::move(in);
}

/**
 * Match
 */
Match::Match(bsoncxx::document::element element,
             std::string_view database,
             std::string_view table,
             Stage* pPrevious)
    : ConcreteStage(pPrevious, Kind::DUAL)
{
    if (element.type() != bsoncxx::type::k_document)
    {
        throw SoftError("the match filter must be an expression in a object", error::LOCATION15959);
    }

    m_match = element.get_document();

    if (m_match.begin() != m_match.end())
    {
        // TODO: Handle $match criteria.
        throw SoftError("$match cannot yet handle any criteria", error::INTERNAL_ERROR);
    }
}

string Match::trailing_sql() const
{
    // TODO: Generate SQL if possible
    return string();
}

std::vector<bsoncxx::document::value> Match::process(std::vector<bsoncxx::document::value>& in)
{
    return std::move(in);
}

}
}

