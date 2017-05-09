/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <istream>

namespace maxscale
{

/**
 * @class TestReader
 *
 * The TestReader class is capable of reading a MySQL/MariaDB test file,
 * such like the ones in [MySQL|MariaDB]/server/mysql-test/t, and return
 * the SQL statements one by one. It does this by skipping test commands
 * and by coalescing lines to obtain a full statement.
 */
class TestReader
{
public:
    enum result_t
    {
        RESULT_ERROR, /*< The input is probably not a test file. */
        RESULT_EOF,   /*< End of file was reached. */
        RESULT_STMT,  /*< A statement was returned. */
    };

    /**
     * Initialize internal shared tables. This will automatically be called
     * by the TestReader constructor, but if multiple threads are used it
     * is adviseable to call this function explicitly from the main thread.
     */
    static void init();


    /**
     * Creates a TestReader instance.
     *
     * @param in    An input stream.
     * @param line  Optionally specify the initial line number.
     */
    TestReader(std::istream& in,
               size_t        line = 0);

    /**
     * @return The current line number.
     */
    size_t line() const { return m_line; }

    /**
     * Get next full SQL statement.
     *
     * @param stmt  String where statement will be stored.
     *
     * @return RESULT_STMT if a statement was returned in @c stmt.
     */
    result_t get_statement(std::string& stmt);

private:
    void skip_block();

private:
    TestReader(const TestReader&);
    TestReader& operator = (const TestReader&);

private:
    std::istream& m_in;        /*< The stream we are using. */
    size_t        m_line;      /*< The current line. */
    char          m_delimiter; /*< The current delimiter. */
};

};
