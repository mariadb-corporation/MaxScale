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
#include <variant>
#include <maxbase/json.hh>
#include <bsoncxx/types/value.hpp>
#include "nosqlbase.hh"

namespace nosql
{

namespace aggregation
{

/**
 * Operator
 */
class Operator
{
public:
    using Creator = std::unique_ptr<Operator>(*)(bsoncxx::types::value);

    virtual ~Operator();

    static void unsupported(string_view key);

    static std::unique_ptr<Operator> create(bsoncxx::types::value value);

    static std::unique_ptr<Operator> create_expression_operator(bsoncxx::types::value value);

    bool ready() const
    {
        return m_ready;
    }

    virtual bsoncxx::types::value process(bsoncxx::document::view doc) = 0;

    bsoncxx::types::value value() const
    {
        return m_value;
    }

    using Number = std::variant<int32_t, int64_t, double>;

    static Number mul(const Number& lhs, const Number& rhs);

protected:
    Operator()
    {
    }

    Operator(const bsoncxx::types::value& value)
        : m_value(value)
    {
    }

    void set_ready()
    {
        m_ready = true;
    }

    bsoncxx::types::value m_value;

private:
    bool m_ready { false };
};

template<class Derived>
class ConcreteOperator : public Operator
{
public:
    using Operator::Operator;

    static std::unique_ptr<Operator> create(bsoncxx::types::value value)
    {
        return std::make_unique<Derived>(value);
    }
};

/**
 * Accessor
 */
class Accessor : public ConcreteOperator<Accessor>
{
public:
    Accessor(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    std::vector<std::string> m_fields;
};

/**
 * Literal
 */
class Literal : public ConcreteOperator<Literal>
{
public:
    Literal(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;
};

/**
 * Cond
 */
class Cond : public ConcreteOperator<Cond>
{
public:
    static constexpr const char* const NAME = "$cond";

    Cond(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    std::vector<std::unique_ptr<Operator>> m_ops;
};

/**
 * Divide
 */
class Divide : public ConcreteOperator<Divide>
{
public:
    static constexpr const char* const NAME = "$divide";

    Divide(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    std::vector<std::unique_ptr<Operator>> m_ops;
    ArrayBuilder                           m_builder;
};

/**
 * First
 */
class First : public ConcreteOperator<First>
{
public:
    static constexpr const char* const NAME = "$first";

    First(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    Accessor m_field;
};

/**
 * Max
 */
class Max : public ConcreteOperator<Max>
{
public:
    static constexpr const char* const NAME = "$max";

    Max(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    static bool gt(bsoncxx::types::value lhs, bsoncxx::types::value rhs);

    std::unique_ptr<Operator> m_sOp;
};

/**
 * Multiply
 */
class Multiply : public ConcreteOperator<Multiply>
{
public:
    static constexpr const char* const NAME = "$multiply";

    Multiply(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    std::vector<std::unique_ptr<Operator>> m_ops;
    ArrayBuilder                           m_builder;
};

/**
 * Ne
 */
class Ne : public ConcreteOperator<Ne>
{
public:
    static constexpr const char* const NAME = "$ne";

    Ne(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    std::vector<std::unique_ptr<Operator>> m_ops;
    ArrayBuilder                           m_builder;
};

/**
 * Sum
 */
class Sum : public ConcreteOperator<Sum>
{
public:
    static constexpr const char* const NAME = "$sum";

    Sum(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    void add_int32(int32_t value);
    void add_int64(int64_t value);
    void add_double(double value);

    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

/**
 * ToDouble
 */
class ToDouble : public ConcreteOperator<ToDouble>
{
public:
    static constexpr const char* const NAME = "$toDouble";

    ToDouble(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

}
}
