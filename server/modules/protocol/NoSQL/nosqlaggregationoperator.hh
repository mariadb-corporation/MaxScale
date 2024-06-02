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

class Operator
{
public:
    using Creator = std::unique_ptr<Operator>(*)(bsoncxx::document::element);

    virtual ~Operator();

    static std::unique_ptr<Operator> unsupported(bsoncxx::document::element element);

    static std::unique_ptr<Operator> get(bsoncxx::document::element element);

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

class Field : public Operator
{
public:
    enum class Kind
    {
        LITERAL,
        ACCESSOR
    };

    Field(bsoncxx::document::element element);

    static std::unique_ptr<Operator> create(bsoncxx::document::element element);

    Kind kind() const
    {
        return m_kind;
    }

    mxb::Json process(const mxb::Json& doc) override;

private:
    Kind                     m_kind;
    std::vector<std::string> m_fields;
};

class First : public Operator
{
public:
    static constexpr const char* const NAME = "$first";

    First(bsoncxx::document::element element);

    static std::unique_ptr<Operator> create(bsoncxx::document::element element);

    mxb::Json process(const mxb::Json& doc) override;

private:
    Field m_field;
};

class Sum : public Operator
{
public:
    static constexpr const char* const NAME = "$sum";

    Sum(bsoncxx::document::element element);

    static std::unique_ptr<Operator> create(bsoncxx::document::element element);

    mxb::Json process(const mxb::Json& doc) override;

private:
    std::unique_ptr<Operator> m_sOp;
};

}
}
