/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxbase/exception.hh>
#include <maxbase/string.hh>
#include <maxsql/ccdefs.hh>
#include <mysql.h>

struct st_mysql;
struct st_mysql_res;
class Connection;

namespace maxsql
{
// The DatabaseError::code() is the mysql error code, or -1
// if it was a higher level error (like
DEFINE_EXCEPTION(DatabaseError);

class ResultSet
{
public:
    struct Row
    {
        explicit Row(int num_columns)
            : columns(num_columns)
        {
        }
        std::vector<std::string> columns;
        template<typename T>
        T get(int col_num) const
        {
            return maxbase::StringToTHelper<T>::convert(columns[col_num]);
        }
    };

    class Iterator
    {
    public:
        Iterator   operator++();
        Iterator   operator++(int);
        bool       operator==(const Iterator&) const;
        bool       operator!=(const Iterator&) const;
        const Row& operator*() const;
        const Row* operator->();
        const Row* operator->() const;
    private:
        st_mysql_res* m_result;
        Row           m_current_row;
        int           m_row_nr; // this is for operator==
        friend class ResultSet;

        Iterator();     // end Iterator
        explicit Iterator(st_mysql_res* res);
        void _read_one();
    };

    void                     discard_result();
    std::vector<std::string> column_names() const;
    Iterator                 begin();
    Iterator                 end();

    ~ResultSet();
private:
    friend class Connection;
    ResultSet(st_mysql* conn);
    st_mysql_res*            m_result;
    std::vector<std::string> m_column_names;
    long                     m_num_rows;
};
}
