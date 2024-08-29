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

#include "nosqlbase.hh"
#include <bsoncxx/types/bson_value/view.hpp>

namespace nosql
{

class FieldPath
{
public:
    enum Mode
    {
        WITH_DOLLAR,
        WITHOUT_DOLLAR
    };

    FieldPath();
    FieldPath(std::string_view path, Mode mode = WITH_DOLLAR);

    void reset(std::string_view path, Mode mode = WITH_DOLLAR);

    const std::string& head() const
    {
        return m_head;
    }

    const FieldPath* tail() const
    {
        return m_sTail.get();
    }

    std::string path() const
    {
        std::string p = m_head;

        if (m_sTail)
        {
            p += ".";
            p += m_sTail->path();
        }

        return p;
    }

    bsoncxx::document::element get(const bsoncxx::document::view& doc) const;

private:
    FieldPath(std::string_view path, FieldPath* pParent);

    void construct(std::string_view path);

    FieldPath*                 m_pParent;
    std::string                m_head;
    std::shared_ptr<FieldPath> m_sTail;
};

}
