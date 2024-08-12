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

#include "nosqlbase.hh"
#include <bsoncxx/types/bson_value/view.hpp>

namespace nosql
{

class FieldPath
{
public:
    FieldPath();
    FieldPath(std::string_view path);

    void reset(std::string_view path);

    const std::string& head() const
    {
        return m_head;
    }

    const FieldPath* tail() const
    {
        return m_sTail.get();
    }

    bsoncxx::document::element get(const bsoncxx::document::view& doc);

private:
    FieldPath(std::string_view path, FieldPath* pParent);

    void construct(std::string_view path);

    FieldPath*                 m_pParent;
    std::string                m_head;
    std::shared_ptr<FieldPath> m_sTail;
};

}
