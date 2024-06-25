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
#include <random>
#include <sstream>
#include "../../filter/masking/mysql.hh"
#include "nosqlcommon.hh"
#include "nosqlnobson.hh"

using namespace std;

namespace nosql
{

namespace aggregation
{

namespace
{

using StageCreator = unique_ptr<Stage>(*)(bsoncxx::document::element, Stage*);
using Stages = map<string_view, StageCreator, less<>>;

#define NOSQL_STAGE(O) { O::NAME, O::create }

Stages stages =
{
    NOSQL_STAGE(AddFields),
    NOSQL_STAGE(CollStats),
    NOSQL_STAGE(Count),
    NOSQL_STAGE(Group),
    NOSQL_STAGE(Limit),
    NOSQL_STAGE(ListSearchIndexes),
    NOSQL_STAGE(Match),
    NOSQL_STAGE(Sample),
    NOSQL_STAGE(Sort),
};

}

/**
 * Stage
 */

void Stage::Query::reset(std::string_view database, std::string_view table)
{
    mxb_assert(is_malleable());

    m_database = database;
    m_table = table;

    reset();
}

void Stage::Query::reset()
{
    mxb_assert(is_malleable());

    m_is_modified = false;
    m_column = "doc";
    m_from.clear();
    m_where.clear();
    m_order_by.clear();
    m_limit = MAX_LIMIT;
}

string Stage::Query::sql() const
{
    stringstream ss;
    ss << "SELECT " << column() << " FROM " << from();

    auto w = where();

    if (!w.empty())
    {
        ss << " WHERE " << w;
    }

    auto o = order_by();

    if (!o.empty())
    {
        ss << " ORDER BY " << o;
    }

    auto l = limit();

    if (l != MAX_LIMIT)
    {
        ss << " LIMIT " << l;
    }

    return ss.str();
}

Stage::~Stage()
{
}

//static
unique_ptr<Stage> Stage::get(bsoncxx::document::element element, Stage* pPrevious)
{
    auto it = stages.find(element.key());

    if (it == stages.end())
    {
        stringstream ss;
        ss << "Unrecognized pipeline stage name: '" << element.key() << "'";

        throw SoftError(ss.str(), error::LOCATION40324);
    }

    return it->second(element, pPrevious);
}

//static
std::vector<bsoncxx::document::value> Stage::process_resultset(GWBUF&& mariadb_response)
{
    uint8_t* pBuffer = mariadb_response.data();

    ComQueryResponse cqr(&pBuffer);
    auto nFields = cqr.nFields();
    mxb_assert(nFields == 1);

    vector<enum_field_types> types;

    for (size_t i = 0; i < nFields; ++i)
    {
        ComQueryResponse::ColumnDef column_def(&pBuffer);

        types.push_back(column_def.type());
    }

    ComResponse eof(&pBuffer);
    mxb_assert(eof.type() == ComResponse::EOF_PACKET);

    vector<bsoncxx::document::value> docs;

    while (ComResponse(pBuffer).type() != ComResponse::EOF_PACKET)
    {
        CQRTextResultsetRow row(&pBuffer, types); // Advances pBuffer
        auto it = row.begin();

        bsoncxx::document::value doc = bsoncxx::from_json((*it++).as_string().to_string());
        docs.emplace_back(std::move(doc));
    }

    return docs;
}


/**
 * AddFields
 */
AddFields::AddFields(bsoncxx::document::element element, Stage* pPrevious)
    : PipelineStage(pPrevious)
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
 * CollStats
 */
CollStats::CollStats(bsoncxx::document::element element, Stage* pPrevious)
    : SQLStage(pPrevious)
{
    if (pPrevious)
    {
        throw SoftError("$collStats is only valid as the first stage in a pipeline",
                        error::LOCATION40602);
    }

    if (element.type() != bsoncxx::type::k_document)
    {
        stringstream ss;
        ss << "$collStats must take a nested object but found a "
           << bsoncxx::to_string(element.type());

        throw SoftError(ss.str(), error::LOCATION5447000);
    }

    bsoncxx::document::view coll_stats = element.get_document();

    // TODO: Check document. The presence of "storageStats" should affects the output.
}

void CollStats::update(Query& query) const
{
    mxb_assert(query.is_malleable());

    stringstream column;
    column <<
        "JSON_OBJECT('storageStats', "
        "JSON_OBJECT('size', data_length + index_length, "
        "'count', table_rows, "
        "'avgObjSize', avg_row_length, "
        "'numOrphanDocs', 0, "
        "'storageSize', data_length + index_length, "
        "'totalIndexSize', index_length, "
        "'freeStorageSize', 0, "
        "'nindexes', 1, "
        "'capped', false)"
        ") as doc";

    stringstream where;
    where <<
        "information_schema.tables.table_schema = '" << query.database() << "' "
        "AND information_schema.tables.table_name = '" << query.table() << "'";

    query.set_column(column.str());
    query.set_from("information_schema.tables");
    query.set_where(where.str());

    query.freeze();
}

/**
 * Count
 */
Count::Count(bsoncxx::document::element element, Stage* pPrevious)
    : DualStage(pPrevious)
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

void Count::update(Query& query) const
{
    mxb_assert(is_sql() && query.is_malleable());

    if (query.is_modified())
    {
        string from = "(" + query.sql() + ") AS count_input";
        query.reset();
        query.set_from(from);
    }

    query.set_column("JSON_OBJECT('count', COUNT(*)) AS doc");
}

std::vector<bsoncxx::document::value> Count::process(std::vector<bsoncxx::document::value>& in)
{
    vector<bsoncxx::document::value> rv;

    int32_t nCount = in.size();

    DocumentBuilder doc;
    doc.append(kvp(m_field, nCount));

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

Group::Group(bsoncxx::document::element element, Stage* pPrevious)
    : PipelineStage(pPrevious)
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
Limit::Limit(bsoncxx::document::element element, Stage* pPrevious)
    : DualStage(pPrevious)
{
    if (!nobson::get_number(element, &m_nLimit))
    {
        // TODO: Append the element value.
        stringstream ss;
        ss << "invalid argument to $limit stage: Expected a number in: $limit: ";

        throw SoftError(ss.str(), error::LOCATION2107201);
    }

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

void Limit::update(Query& query) const
{
    mxb_assert(is_sql() && query.is_malleable());

    auto limit = query.limit();

    if (m_nLimit < limit)
    {
        query.set_limit(m_nLimit);
    }
}

std::vector<bsoncxx::document::value> Limit::process(std::vector<bsoncxx::document::value>& in)
{
    mxb_assert(kind() == Kind::PIPELINE);

    if (in.size() > (size_t)m_nLimit)
    {
        in.erase(in.begin() + m_nLimit, in.end());
    }

    return std::move(in);
}

/**
 * ListSearchIndexes
 */
ListSearchIndexes::ListSearchIndexes(bsoncxx::document::element element, Stage* pPrevious)
    : UnsupportedStage("listSearchIndexes stage is only allowed on MongoDB Atlas", error::LOCATION6047401)
{
}

/**
 * Match
 */
Match::Match(bsoncxx::document::element element, Stage* pPrevious)
    : DualStage(pPrevious)
{
    if (element.type() != bsoncxx::type::k_document)
    {
        throw SoftError("the match filter must be an expression in a object", error::LOCATION15959);
    }

    m_match = element.get_document();

    if (!m_match.empty())
    {
        m_where_condition = where_condition_from_query(m_match);
    }
}

void Match::update(Query& query) const
{
    mxb_assert(is_sql() && query.is_malleable());

    if (!m_where_condition.empty())
    {
        string where = query.where();

        if (!where.empty())
        {
            where += " AND ";
        }

        where += m_where_condition;

        query.set_where(where);
    }
}

std::vector<bsoncxx::document::value> Match::process(std::vector<bsoncxx::document::value>& in)
{
    mxb_assert(kind() == Kind::PIPELINE);

    // TODO: Match query.
    return std::move(in);
}

/**
 * Sample
 */
Sample::Sample(bsoncxx::document::element element, Stage* pPrevious)
    : DualStage(pPrevious)
{
    if (element.type() != bsoncxx::type::k_document)
    {
        throw SoftError("the $sample stage specification must be an object", error::LOCATION28745);
    }

    bsoncxx::document::view sample = element.get_document();

    auto it = sample.begin();

    if (it == sample.end())
    {
        throw SoftError("$sample stage must specify a size", error::LOCATION28749);
    }

    int64_t nSamples;
    while (it != sample.end())
    {
        bsoncxx::document::element e = *it;

        if (e.key() != "size")
        {
            stringstream ss;
            ss << "unrecognized option to $sample: " << e.key();

            throw SoftError(ss.str(), error::LOCATION28748);
        }

        if (!nobson::get_number(e, &nSamples))
        {
            throw SoftError("size argument to $sample must be a number", error::LOCATION28746);
        }

        ++it;
    }

    if (nSamples < 0)
    {
        throw SoftError("size argument to $sample must not be negative", error::LOCATION28747);
    }

    m_nSamples = nSamples;
}

void Sample::update(Query& query) const
{
    mxb_assert(is_sql() && query.is_malleable());

    if (query.is_modified())
    {
        string from = "(" + query.sql() + ") AS sample_input";
        query.reset();
        query.set_from(from);
    }

    query.set_order_by("RAND()");
    query.set_limit(m_nSamples);
}

std::vector<bsoncxx::document::value> Sample::process(std::vector<bsoncxx::document::value>& in)
{
    mxb_assert(kind() == Kind::PIPELINE);

    std::vector<bsoncxx::document::value> out;

    if (in.size() <= m_nSamples)
    {
        out = std::move(in);
    }
    else
    {
        std::default_random_engine gen;

        std::sample(in.begin(), in.end(), std::back_inserter(out), m_nSamples, gen);
    }

    return out;
}

/**
 * Sort
 */
Sort::Sort(bsoncxx::document::element element, Stage* pPrevious)
    : DualStage(pPrevious)
{
    if (element.type() != bsoncxx::type::k_document)
    {
        throw SoftError("the $sort key specification must be an object", error::LOCATION15973);
    }

    bsoncxx::document::view sort = element.get_document();

    if (sort.empty())
    {
        throw SoftError("$sort stage must have at least one sort key", error::LOCATION15976);
    }

    m_order_by = order_by_value_from_sort(sort);

    // TODO: Implement manual sorting.
    if (m_pPrevious && m_pPrevious->kind() == Kind::PIPELINE)
    {
        throw SoftError("Currently $sort can only appear first or directly after a stage "
                        "implemented using SQL.", error::INTERNAL_ERROR);
    }
}

void Sort::update(Query& query) const
{
    mxb_assert(is_sql() && query.is_malleable());

    if (query.is_modified())
    {
        string from = "(" + query.sql() + ") AS sort_input";
        query.reset();
        query.set_from(from);
    }

    query.set_order_by(m_order_by);
}

std::vector<bsoncxx::document::value> Sort::process(std::vector<bsoncxx::document::value>& in)
{
    mxb_assert(!true);
    return std::move(in);
}

}
}

