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
#include "../../filter/masking/mysql.hh"
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
    NOSQL_STAGE(CollStats),
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

Stage::Kind Stage::kind() const
{
    return Kind::PIPELINE;
}

Stage::Processor Stage::update_sql(string&) const
{
    mxb_assert(!true);
    throw SoftError("A stage that must be part of the pipeline cannot be replaced by SQL.",
                    error::INTERNAL_ERROR);

    return Processor::RETAIN;
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
 * CollStats
 */
CollStats::CollStats(bsoncxx::document::element element,
                     std::string_view database,
                     std::string_view table,
                     Stage* pPrevious)
    : ConcreteStage(pPrevious)
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

    // TODO: Check document.

    stringstream ss;

    ss << "SELECT table_rows, avg_row_length, data_length, index_length "
       << "FROM information_schema.tables "
       << "WHERE information_schema.tables.table_schema = '" << database << "' "
       << "AND information_schema.tables.table_name = '" << table << "'";

    m_sql = ss.str();
}

Stage::Kind CollStats::kind() const
{
    return Kind::SQL;
}

Stage::Processor CollStats::update_sql(string& sql) const
{
    mxb_assert(sql.empty());

    sql = m_sql;

    return Processor::REPLACE;
}

std::vector<bsoncxx::document::value> CollStats::process(std::vector<bsoncxx::document::value>& in)
{
    mxb_assert(!true);

    throw SoftError("$collStats can only post-process a resultset.", error::INTERNAL_ERROR);

    return std::vector<bsoncxx::document::value>();
}

std::vector<bsoncxx::document::value> CollStats::post_process(GWBUF&& mariadb_response)
{
    uint8_t* pBuffer = mariadb_response.data();

    ComQueryResponse cqr(&pBuffer);
    auto nFields = cqr.nFields();
    mxb_assert(nFields == 4);

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

        int64_t nTable_rows = std::stoll((*it++).as_string().to_string());
        int64_t nAvg_row_length = std::stoll((*it++).as_string().to_string());
        int64_t nData_length = std::stoll((*it++).as_string().to_string());
        int64_t nIndex_length = std::stoll((*it++).as_string().to_string());

        DocumentBuilder storage_stats;
        storage_stats.append(kvp("size", nData_length + nIndex_length));
        storage_stats.append(kvp("count", nTable_rows));
        storage_stats.append(kvp("avgObjSize", nAvg_row_length));
        storage_stats.append(kvp("numOrphanDocs", 0));
        storage_stats.append(kvp("storageSize", nData_length + nIndex_length));
        storage_stats.append(kvp("totalIndexSize", nIndex_length));
        storage_stats.append(kvp("freeStorageSize", 0));
        storage_stats.append(kvp("nindexes", 1));
        storage_stats.append(kvp("capped", false));

        DocumentBuilder doc;
        doc.append(kvp("storageStats", storage_stats.extract()));

        docs.emplace_back(doc.extract());
    }

    return docs;
}

/**
 * Count
 */
Count::Count(bsoncxx::document::element element,
             std::string_view database,
             std::string_view table,
             Stage* pPrevious)
    : ConcreteStage(pPrevious)
    , m_database(database)
    , m_table(table)
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

Stage::Kind Count::kind() const
{
    Stage::Kind kind;

    if (m_pPrevious)
    {
        if (m_pPrevious->kind() == Kind::SQL)
        {
            // If the previous stage was SQL, then we can simply replace it
            // with "SELECT COUNT(*) ..." as the only output of the $count
            // stage is the number of input documents.
            kind = Kind::SQL;
        }
        else
        {
            kind = Kind::PIPELINE;
        }
    }
    else
    {
        kind = Kind::SQL;
    }

    return kind;
}

Stage::Processor Count::update_sql(string& sql) const
{
    mxb_assert(kind() == Kind::SQL);

    stringstream ss;
    ss << "SELECT COUNT(*) FROM `" << m_database << "`.`" << m_table << "`";

    sql = ss.str();

    return Processor::REPLACE;
}

std::vector<bsoncxx::document::value> Count::process(std::vector<bsoncxx::document::value>& in)
{
    return create_out(in.size());
}

std::vector<bsoncxx::document::value> Count::post_process(GWBUF&& mariadb_response)
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

    mxb_assert(ComResponse(pBuffer).type() != ComResponse::EOF_PACKET);

    CQRTextResultsetRow row(&pBuffer, types); // Advances pBuffer
    auto it = row.begin();

    int nCount = std::stoi((*it).as_string().to_string());

    return create_out(nCount);
}

std::vector<bsoncxx::document::value> Count::create_out(int32_t nCount)
{
    vector<bsoncxx::document::value> rv;

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
    : ConcreteStage(pPrevious)
    , m_database(database)
    , m_table(table)
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

Stage::Kind Limit::kind() const
{
    Stage::Kind kind;

    if (m_pPrevious)
    {
        kind = m_pPrevious->kind() == Kind::SQL ? Kind::SQL : Kind::PIPELINE;
    }
    else
    {
        kind = Kind::SQL;
    }

    return kind;
}

Stage::Processor Limit::update_sql(string& sql) const
{
    mxb_assert(kind() == Kind::SQL);

    if (!m_pPrevious)
    {
        mxb_assert(sql.empty());

        stringstream ss;
        ss << "SELECT doc FROM `" << m_database << "`.`" << m_table << "`";

        sql = ss.str();
    }

    mxb_assert(!sql.empty());

    stringstream ss;
    ss << " LIMIT " << m_nLimit;

    sql += ss.str();

    return Processor::RETAIN;
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

std::vector<bsoncxx::document::value> Limit::post_process(GWBUF&& mariadb_response)
{
    return Match::process_resultset(std::move(mariadb_response));
}


/**
 * ListSearchIndexes
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
    : ConcreteStage(pPrevious)
    , m_database(database)
    , m_table(table)
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

Stage::Kind Match::kind() const
{
    return m_pPrevious ? Kind::PIPELINE : Kind::SQL;
}

Stage::Processor Match::update_sql(string& sql) const
{
    mxb_assert(kind() == Kind::SQL);
    mxb_assert(sql.empty());

    stringstream ss;
    ss << "SELECT doc FROM `" << m_database << "`.`" << m_table << "`";

    sql = ss.str();

    return Processor::REPLACE;
}

std::vector<bsoncxx::document::value> Match::process(std::vector<bsoncxx::document::value>& in)
{
    mxb_assert(kind() == Kind::PIPELINE);

    return std::move(in);
}

std::vector<bsoncxx::document::value> Match::post_process(GWBUF&& mariadb_response)
{
    return process_resultset(std::move(mariadb_response));
}

//static
std::vector<bsoncxx::document::value> Match::process_resultset(GWBUF&& mariadb_response)
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

}
}

