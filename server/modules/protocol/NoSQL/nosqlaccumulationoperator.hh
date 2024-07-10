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
#include "nosqloperator.hh"
#include "nosqlaggregationoperator.hh"

namespace nosql
{

namespace accumulation
{

/**
 * Operator
 */
class Operator : public nosql::Operator
{
public:
    using Creator = std::unique_ptr<Operator>(*)(const BsonView& value);

    virtual ~Operator();

    bool ready() const
    {
        return m_ready;
    }

    virtual void accumulate(bsoncxx::document::view doc) = 0;
    virtual const BsonValue& finish();

    const BsonValue& value() const
    {
        return m_value;
    }

protected:
    Operator()
        : m_value(nullptr)
    {
    }

    Operator(const BsonView& value)
        : m_value(value)
    {
    }

    void set_ready()
    {
        m_ready = true;
    }

    BsonValue m_value;

private:
    bool m_ready { false };
};

template<class DerivedBy>
using ConcreteOperator = nosql::ConcreteOperator<DerivedBy, Operator>;

template<class DerivedBy>
using SingleExpressionOperator = nosql::SingleExpressionOperator<DerivedBy, Operator, aggregation::Operator>;

template<class DerivedBy>
using MultiExpressionOperator = nosql::MultiExpressionOperator<DerivedBy, Operator, aggregation::Operator>;

/**
 * Avg
 */
class Avg : public SingleExpressionOperator<Avg>
{
public:
    static constexpr const char* const NAME = "$avg";

    using Base::Base;

    void accumulate(bsoncxx::document::view doc) override;

private:
    int32_t m_count {0};
};

/**
 * First
 */
class First : public SingleExpressionOperator<First>
{
public:
    static constexpr const char* const NAME = "$first";

    using Base::Base;

    void accumulate(bsoncxx::document::view doc) override;
};

/**
 * Last
 */
class Last : public SingleExpressionOperator<Last>
{
public:
    static constexpr const char* const NAME = "$last";

    using Base::Base;

    void accumulate(bsoncxx::document::view doc) override;
};

/**
 * Max
 */
class Max : public SingleExpressionOperator<Max>
{
public:
    static constexpr const char* const NAME = "$max";

    using Base::Base;

    void accumulate(bsoncxx::document::view doc) override;

private:
    bool m_first { true };
};

/**
 * Min
 */
class Min : public SingleExpressionOperator<Min>
{
public:
    static constexpr const char* const NAME = "$min";

    using Base::Base;

    void accumulate(bsoncxx::document::view doc) override;

private:
    bool m_first { true };
};

/**
 * Push
 */
class Push : public SingleExpressionOperator<Push>
{
public:
    static constexpr const char* const NAME = "$push";

    using Base::Base;

    void accumulate(bsoncxx::document::view doc) override;
    const BsonValue& finish() override;

private:
    ArrayBuilder m_builder;
};

/**
 * Sum
 */
class Sum : public SingleExpressionOperator<Sum>
{
public:
    static constexpr const char* const NAME = "$sum";

    using Base::Base;

    void accumulate(bsoncxx::document::view doc) override;
};

}
}
