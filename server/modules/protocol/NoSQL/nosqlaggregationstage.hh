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
    using OperatorCreator = std::function<std::unique_ptr<Operator>(bsoncxx::types::value)>;
    using Operators = std::map<std::string_view, OperatorCreator, std::less<>>;

    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    enum class Kind
    {
        PURE, // Must be part of the pipeline.
        DUAL  // Can be part of the pipeline or if first stage, can modify the SQL.
    };

    Kind kind() const
    {
        return m_kind;
    }

    virtual std::string trailing_sql() const;

    virtual ~Stage();

    static std::unique_ptr<Stage> get(bsoncxx::document::element element);

    /**
     * Perform the stage on the provided documents.
     *
     * @param in  An array of documents.
     *
     * @return An array of documents.
     */
    virtual std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) = 0;

protected:
    Stage(Kind kind = Kind::PURE)
        : m_kind(kind)
    {
    }

private:
    const Kind m_kind;
};

/**
 * AddFields
 */
class AddFields : public Stage
{
public:
    static constexpr const char* const NAME = "$addFields";

    static std::unique_ptr<Stage> create(bsoncxx::document::element element);

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;

private:
    AddFields(bsoncxx::document::view group);

    struct NamedOperator
    {
        std::string_view          name;
        std::unique_ptr<Operator> sOperator;
    };

    std::vector<NamedOperator> m_operators;
};

/**
 * Count
 */
class Count : public Stage
{
public:
    static constexpr const char* const NAME = "$count";

    Count(bsoncxx::document::element element);

    static std::unique_ptr<Stage> create(bsoncxx::document::element element);

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;

private:
    std::string_view m_field;
};

/**
 * Group
 */
class Group : public Stage
{
public:
    static constexpr const char* const NAME = "$group";

    static std::unique_ptr<Stage> create(bsoncxx::document::element element);

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;

private:
    Group(bsoncxx::document::view group);

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
class Limit : public Stage
{
public:
    static constexpr const char* const NAME = "$limit";

    Limit(bsoncxx::document::element element);

    std::string trailing_sql() const override;

    static std::unique_ptr<Stage> create(bsoncxx::document::element element);

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;

private:
    int64_t m_nLimit;
};

/**
 * ListSearchIndexes
 */
class ListSearchIndexes : public Stage
{
public:
    static constexpr const char* const NAME = "$listSearchIndexes";

    ListSearchIndexes(bsoncxx::document::element element);

    static std::unique_ptr<Stage> create(bsoncxx::document::element element);

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;
};

/**
 * Match
 */
class Match : public Stage
{
public:
    static constexpr const char* const NAME = "$match";

    Match(bsoncxx::document::element element);

    std::string trailing_sql() const override;

    static std::unique_ptr<Stage> create(bsoncxx::document::element element);

    std::vector<bsoncxx::document::value> process(std::vector<bsoncxx::document::value>& in) override;

private:
    bsoncxx::document::view m_match;
};

}
}


