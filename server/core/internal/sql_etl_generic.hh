/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxbase/ccdefs.hh>

#include "sql_etl.hh"

namespace sql_etl
{

// Generic extractor that uses the ODBC catalog functions to determine the table layout
class GenericExtractor : public Extractor
{
public:
    /**
     * Creates a new generic extractor
     *
     * As the ODBC catalog functions require a catalog, a schema and a table, we need to pass it as an option
     * during construction.
     *
     * @param catalog The catalog used for the ODBC functions
     */
    GenericExtractor(std::string catalog)
        : m_catalog(std::move(catalog))
    {
    }

    void init_connection(mxq::ODBC& source) override
    {
    }

    void start(mxq::ODBC& source, const std::deque<Table>& tables) override
    {
    }

    void start_thread(mxq::ODBC& source, const std::deque<Table>& tables) override
    {
    }

    void threads_started(mxq::ODBC& source, const std::deque<Table>& tables) override
    {
    }

    std::string create_table(mxq::ODBC& source, const Table& table) override;
    std::string select(mxq::ODBC& source, const Table& table) override;
    std::string insert(mxq::ODBC& source, const Table& table) override;


private:
    std::string m_catalog;
};
}
