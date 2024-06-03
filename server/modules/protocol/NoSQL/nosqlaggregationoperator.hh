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
#include "nosqlbase.hh"
#include <maxbase/json.hh>

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
    using Creator = std::unique_ptr<Operator>(*)(bsoncxx::document::element);

    virtual ~Operator();

    static std::unique_ptr<Operator> unsupported(bsoncxx::document::element element);

    static std::unique_ptr<Operator> create(bsoncxx::document::element element);

    bool ready() const
    {
        return m_ready;
    }

    virtual mxb::Json process(const mxb::Json& doc) = 0;

    mxb::Json value() const
    {
        return m_value;
    }

protected:
    Operator()
        : m_value(mxb::Json::Type::UNDEFINED)
    {
    }

    Operator(const mxb::Json& value)
        : m_value(value)
    {
    }

    void set_ready()
    {
        m_ready = true;
    }

    mxb::Json m_value;

private:
    bool m_ready { false };
};

/**
 * Accessor
 */
class Accessor : public Operator
{
public:
    Accessor(bsoncxx::document::element element);

    static std::unique_ptr<Operator> create(bsoncxx::document::element element);

    mxb::Json process(const mxb::Json& doc) override;

private:
    std::vector<std::string> m_fields;
};

/**
 * Literal
 */
class Literal : public Operator
{
public:
    Literal(bsoncxx::document::element element);

    static std::unique_ptr<Operator> create(bsoncxx::document::element element);

    mxb::Json process(const mxb::Json& doc) override;
};

/**
 * First
 */
class First : public Operator
{
public:
    static constexpr const char* const NAME = "$first";

    First(bsoncxx::document::element element);

    static std::unique_ptr<Operator> create(bsoncxx::document::element element);

    mxb::Json process(const mxb::Json& doc) override;

private:
    Accessor m_field;
};

/**
 * Sum
 */
class Sum : public Operator
{
public:
    static constexpr const char* const NAME = "$sum";

    Sum(bsoncxx::document::element element);

    static std::unique_ptr<Operator> create(bsoncxx::document::element element);

    mxb::Json process(const mxb::Json& doc) override;

private:
    void add_integer(int64_t value);
    void add_real(double value);

    std::unique_ptr<Operator> m_sOp;
};

/**
 * ToDouble
 */
class ToDouble : public Operator
{
public:
    static constexpr const char* const NAME = "$toDouble";

    ToDouble(bsoncxx::document::element element);

    static std::unique_ptr<Operator> create(bsoncxx::document::element element);

    mxb::Json process(const mxb::Json& doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
};

}
}
