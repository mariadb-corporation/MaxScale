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

class Stage
{
public:
    using OperatorCreator = std::function<std::unique_ptr<Operator>(bsoncxx::types::value)>;
    using Operators = std::map<std::string_view, OperatorCreator, std::less<>>;

    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

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
    Stage() {};
};

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

    bsoncxx::document::view    m_group;
    std::vector<NamedOperator> m_operators;

    static Operators           s_available_operators;
};

}
}


