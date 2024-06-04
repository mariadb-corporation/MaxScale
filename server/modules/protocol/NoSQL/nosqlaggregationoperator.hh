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

/**
 * Accessor
 */
class Accessor : public Operator
{
public:
    Accessor(bsoncxx::types::value value);

    static std::unique_ptr<Operator> create(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    std::vector<std::string> m_fields;
};

/**
 * Literal
 */
class Literal : public Operator
{
public:
    Literal(bsoncxx::types::value value);

    static std::unique_ptr<Operator> create(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;
};

/**
 * First
 */
class First : public Operator
{
public:
    static constexpr const char* const NAME = "$first";

    First(bsoncxx::types::value value);

    static std::unique_ptr<Operator> create(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    Accessor m_field;
};

/**
 * Multiply
 */
class Multiply : public Operator
{
public:
    static constexpr const char* const NAME = "$multiply";

    Multiply(bsoncxx::types::value value);

    static std::unique_ptr<Operator> create(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    std::vector<std::unique_ptr<Operator>> m_sOps;
    ArrayBuilder                           m_builder;
};

/**
 * Sum
 */
class Sum : public Operator
{
public:
    static constexpr const char* const NAME = "$sum";

    Sum(bsoncxx::types::value value);

    static std::unique_ptr<Operator> create(bsoncxx::types::value value);

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
class ToDouble : public Operator
{
public:
    static constexpr const char* const NAME = "$toDouble";

    ToDouble(bsoncxx::types::value value);

    static std::unique_ptr<Operator> create(bsoncxx::types::value value);

    bsoncxx::types::value process(bsoncxx::document::view doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
    ArrayBuilder              m_builder;
};

}
}
