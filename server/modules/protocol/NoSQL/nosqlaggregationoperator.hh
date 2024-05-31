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
    virtual ~Operator();

    bool ready() const
    {
        return m_ready;
    }

    virtual void process(const mxb::Json& doc) = 0;

    virtual void update(mxb::Json& doc, const std::string& field) = 0;

protected:
    class Field
    {
    public:
        enum class Kind
        {
            LITERAL,
            ACCESSOR
        };

        Field(std::string_view field);

        Kind kind() const
        {
            return m_kind;
        }

        mxb::Json access(const mxb::Json& doc);

    private:
        Kind                     m_kind;
        std::vector<std::string> m_fields;
    };

    Operator()
    {
    }

    void set_ready()
    {
        m_ready = true;
    }

private:
    bool m_ready { false };
};

class First : public Operator
{
public:
    First(bsoncxx::document::element field);

    void process(const mxb::Json& doc) override;

    void update(mxb::Json& doc, const std::string& field) override;

private:
    Field     m_field;
    mxb::Json m_value;
};

}
}
