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
#pragma once

#include "nosqlprotocol.hh"
#include <map>
#include <vector>
#include <bsoncxx/document/view.hpp>
#include <maxbase/json.hh>
#include "nosqlaggregationoperator.hh"

namespace nosql
{

namespace aggregation
{

/**
 * Stage
 */
class Stage
{
public:
    class Query
    {
    public:
        enum class Kind
        {
            MALLEABLE,
            FROZEN
        };

        Query()
        {
        }

        void reset(std::string_view database, std::string_view table);;
        void reset();

        void freeze()
        {
            mxb_assert(m_kind == Kind::MALLEABLE);
            m_kind = Kind::FROZEN;
        }

        bool is_modified() const
        {
            return m_is_modified;
        }

        std::string sql() const;

        bool is_frozen() const
        {
            return m_kind == Kind::FROZEN;
        }

        bool is_malleable() const
        {
            return m_kind == Kind::MALLEABLE;
        }

        const std::string& database() const
        {
            return m_database;
        }

        const std::string& table() const
        {
            return m_table;
        }

        const std::string& column() const
        {
            return m_column;
        }

        const std::string& from() const
        {
            if (m_from.empty())
            {
                m_from = "`" + m_database + "`.`" + m_table + "`";
            }

            return m_from;
        }

        const std::string& where() const
        {
            return m_where;
        }

        const std::string& order_by() const
        {
            return m_order_by;
        }

        int limit() const
        {
            return m_limit;
        }

        void set_column(std::string_view column)
        {
            mxb_assert(m_kind == Kind::MALLEABLE);
            m_column = column;
            m_is_modified = true;
        }

        void set_from(std::string_view from)
        {
            mxb_assert(m_kind == Kind::MALLEABLE);
            m_from = from;
            m_is_modified = true;
        }

        void set_where(const std::string& where)
        {
            mxb_assert(m_kind == Kind::MALLEABLE);
            m_where = where;
            m_is_modified = true;
        }

        void set_order_by(const std::string& order_by)
        {
            mxb_assert(m_kind == Kind::MALLEABLE);
            m_order_by = order_by;
            m_is_modified = true;
        }

        void set_limit(int limit)
        {
            mxb_assert(m_kind == Kind::MALLEABLE);
            mxb_assert(limit > 0);
            m_limit = limit;
            m_is_modified = true;
        }

    private:
        static constexpr int MAX_LIMIT = std::numeric_limits<int>::max();

        std::string         m_database;
        std::string         m_table;
        Kind                m_kind { Kind::MALLEABLE };
        bool                m_is_modified { false };
        std::string         m_column { "doc" };
        mutable std::string m_from;
        std::string         m_where;
        std::string         m_order_by;
        int                 m_limit { MAX_LIMIT };
    };

    using OperatorCreator = std::function<std::unique_ptr<Operator>(bsoncxx::types::value)>;
    using Operators = std::map<std::string_view, OperatorCreator, std::less<>>;

    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    virtual const char* name() const = 0;

    enum class Kind
    {
        PIPELINE, // Pipeline stage, must be part of the processing pipeline.
        SQL,      // SQL stage, provides or modifies SQL and must be excluded from the pipeline.
    };

    Stage* previous() const
    {
        return m_pPrevious;
    }

    virtual Kind kind() const;

    bool is_sql() const
    {
        return kind() == Kind::SQL;
    }

    bool is_pipeline() const
    {
        return kind() == Kind::PIPELINE;
    }

    virtual void update(Query& query) const;

    virtual ~Stage();

    static std::unique_ptr<Stage> get(bsoncxx::document::element element, Stage* pPrevious);

    /**
     * Perform the stage on the provided documents.
     *
     * @param in  An array of documents.
     *
     * @return An array of documents.
     */
    virtual std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) = 0;

    /**
     * Process a resultset. Assumes rows with a single column containg a JSON object.
     *
     * @param mariadb_response
     *
     * @return An array of corresponding documents.
     */
    static std::vector<bsoncxx::document::value> process_resultset(GWBUF&& mariadb_response);

protected:
    Stage(Stage* pPrevious)
        : m_pPrevious(pPrevious)
    {
    }

    Stage* const m_pPrevious;
};

template<class Derived>
class ConcreteStage : public Stage
{
public:
    using Stage::Stage;

    const char* name() const override
    {
        return Derived::NAME;
    }

    static std::unique_ptr<Stage> create(bsoncxx::document::element element, Stage* pPrevious)
    {
        mxb_assert(element.key() == Derived::NAME);
        return std::make_unique<Derived>(element, pPrevious);
    }
};

/**
 * AddFields
 */
class AddFields : public ConcreteStage<AddFields>
{
public:
    static constexpr const char* const NAME = "$addFields";

    AddFields(bsoncxx::document::element element, Stage* pPrevious);

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;

private:
    struct NamedOperator
    {
        std::string_view          name;
        std::unique_ptr<Operator> sOperator;
    };

    std::vector<NamedOperator> m_operators;
};

/**
 * CollStats
 */
class CollStats : public ConcreteStage<CollStats>
{
public:
    static constexpr const char* const NAME = "$collStats";

    CollStats(bsoncxx::document::element element, Stage* pPrevious);

    Stage::Kind kind() const override;

    void update(Query& query) const override;

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;
};

/**
 * Count
 */
class Count : public ConcreteStage<Count>
{
public:
    static constexpr const char* const NAME = "$count";

    Count(bsoncxx::document::element element, Stage* pPrevious);

    Stage::Kind kind() const override;

    void update(Query& query) const override;

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;

private:
    std::string_view m_field;
};

/**
 * Group
 */
class Group : public ConcreteStage<Group>
{
public:
    static constexpr const char* const NAME = "$group";

    Group(bsoncxx::document::element element, Stage* pPrevious);

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;

private:
    void add_operator(bsoncxx::document::element operator_def);
    void add_operator(std::string_view name, bsoncxx::document::view def);

    struct NamedOperator
    {
        std::string_view          name;
        std::unique_ptr<Operator> sOperator;
    };

    std::vector<NamedOperator> m_operators;

    static Operators           s_available_operators;
};

/**
 * Limit
 */
class Limit : public ConcreteStage<Limit>
{
public:
    static constexpr const char* const NAME = "$limit";

    Limit(bsoncxx::document::element element, Stage* pPrevious);

    Kind kind() const override;

    void update(Query& query) const override;

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;

private:
    int64_t m_nLimit;
};

/**
 * ListSearchIndexes
 */
class ListSearchIndexes : public ConcreteStage<ListSearchIndexes>
{
public:
    static constexpr const char* const NAME = "$listSearchIndexes";

    ListSearchIndexes(bsoncxx::document::element element, Stage* pPrevious);

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;
};

/**
 * Match
 */
class Match : public ConcreteStage<Match>
{
public:
    static constexpr const char* const NAME = "$match";

    Match(bsoncxx::document::element element, Stage* pPrevious);

    const std::string& where_condition() const
    {
        return m_where_condition;
    }

    Kind kind() const override;

    void update(Query& query) const override;

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;

private:
    bsoncxx::document::view m_match;
    std::string             m_where_condition;
};

/**
 * Sample
 */
class Sample : public ConcreteStage<Sample>
{
public:
    static constexpr const char* const NAME = "$sample";

    Sample(bsoncxx::document::element element, Stage* pPrevious);

    Kind kind() const override;

    void update(Query& query) const override;

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;

private:
    uint64_t m_nSamples;
};

/**
 * Sort
 */
class Sort : public ConcreteStage<Sort>
{
public:
    static constexpr const char* const NAME = "$sort";

    Sort(bsoncxx::document::element element, Stage* pPrevious);

    Kind kind() const override;

    void update(Query& query) const override;

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;

private:
    std::string m_order_by;
};

}
}

