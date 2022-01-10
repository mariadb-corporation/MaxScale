/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "resultset.hh"
#include "strings.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <unistd.h>

namespace maxsql
{

ResultSet::ResultSet(st_mysql* conn)
    : m_result(nullptr)
    , m_num_rows(0)
{
    int ncolls = mysql_field_count(conn);
    if (ncolls)
    {
        if (!(m_result = mysql_use_result(conn)))
        {
            MXB_THROWCode(DatabaseError, mysql_errno(conn),
                          "Failed to get result set. " << " : mysql_error " << mysql_error(conn));
        }

        st_mysql_field* fields = mysql_fetch_fields(m_result);
        for (int i = 0; i < ncolls; ++i)
        {
            m_column_names.push_back(fields[i].name);
        }
    }
}

ResultSet::~ResultSet()
{
    mysql_free_result(m_result);
}

void ResultSet::discard_result()
{
    // TODO. There must be a fast way, mariadb_cancel?
    Iterator ite = begin();
    while (ite != end())
    {
        ++ite;
    }
}

std::vector<std::string> ResultSet::column_names() const
{
    return m_column_names;
}

ResultSet::Iterator ResultSet::begin()
{
    return Iterator(m_result);
}

ResultSet::Iterator ResultSet::end()
{
    return Iterator();
}

ResultSet::Iterator::Iterator()
    : m_current_row(0)
    , m_row_nr(-1)
{
}

ResultSet::Iterator::Iterator(st_mysql_res* res)
    : m_result{res}
    , m_current_row(m_result ? mysql_num_fields(m_result) : 0)
    , m_row_nr{m_result ? 0 : -1}
{
    if (m_row_nr != -1)
    {
        _read_one();
    }
}

void ResultSet::Iterator::_read_one()
{
    auto db_row = mysql_fetch_row(m_result);
    if (!db_row)
    {
        m_row_nr = -1;      // end Iterator
    }
    else
    {
        int sz = m_current_row.columns.size();
        for (int i = 0; i < sz; ++i)
        {
            if (db_row[i])
            {
                m_current_row.columns[i] = db_row[i];
            }
            else
            {
                m_current_row.columns.clear();
            }
        }
        ++m_row_nr;
    }
}

ResultSet::Iterator ResultSet::Iterator::operator++()
{
    _read_one();
    return *this;
}

ResultSet::Iterator ResultSet::Iterator::operator++(int)
{
    auto ret = *this;
    return ++(*this);
}

bool ResultSet::Iterator::operator==(const ResultSet::Iterator& rhs) const
{
    return m_row_nr == rhs.m_row_nr;
}

bool ResultSet::Iterator::operator!=(const ResultSet::Iterator& rhs) const
{
    return m_row_nr != rhs.m_row_nr;
}

const ResultSet::Row& ResultSet::Iterator::operator*() const
{
    return m_current_row;
}

const ResultSet::Row* ResultSet::Iterator::operator->()
{
    return &m_current_row;
}

const ResultSet::Row* ResultSet::Iterator::operator->() const
{
    return &m_current_row;
}
}
