/*
 * Copyright (c) 2024 MariaDB plc
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
#pragma once

#include "nosqlprotocol.hh"
#include <set>
#include <variant>
#include <bsoncxx/types/bson_value/value.hpp>
#include <bsoncxx/types/bson_value/view.hpp>
#include <maxbase/json.hh>
#include "nosqlbase.hh"
#include "nosqlnobson.hh"

namespace nosql
{

/**
 * Operator
 */
class Operator
{
public:
    using BsonValue = bsoncxx::types::bson_value::value;
    using BsonView = bsoncxx::types::bson_value::view;
    using TypeSet = std::set<bsoncxx::type>;

    static const TypeSet ALL_TYPES;
    static const TypeSet NUMBER_TYPES;

    static constexpr const TypeSet& ALLOWED_LITERALS = ALL_TYPES;

    virtual ~Operator();

    static void unsupported(string_view key);

protected:
    Operator()
    {
    }
};

template<class DerivedBy, class DerivedFrom>
class ConcreteOperator : public DerivedFrom
{
public:
    using DerivedFrom::DerivedFrom;

    static std::unique_ptr<DerivedFrom> create(const typename DerivedFrom::BsonView& value)
    {
        return std::make_unique<DerivedBy>(value);
    }
};

template<class DerivedBy, class DerivedFrom, class Op = DerivedFrom>
class SingleExpressionOperator : public ConcreteOperator<DerivedBy, DerivedFrom>
{
public:
    using Base = SingleExpressionOperator;

    SingleExpressionOperator(const typename DerivedFrom::BsonView& value)
        : m_sOp(Op::create(value, DerivedBy::ALLOWED_LITERALS))
    {
    }

protected:
    std::unique_ptr<Op> m_sOp;
};

template<class DerivedBy, class DerivedFrom, class Op = DerivedFrom>
class MultiExpressionOperator : public ConcreteOperator<DerivedBy, DerivedFrom>
{
public:
    using Base = MultiExpressionOperator;

    static constexpr size_t const NO_LIMIT = std::numeric_limits<size_t>::max();

    MultiExpressionOperator(const typename DerivedFrom::BsonView& value,
                            size_t nMin,
                            size_t nMax = NO_LIMIT)
        : m_ops(Op::create_operators(value, DerivedBy::NAME, nMin, nMax, DerivedBy::ALLOWED_LITERALS))
    {
    }

protected:
    std::vector<std::unique_ptr<Op>> m_ops;
};

}
