/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
 #pragma once

#include <maxscale/ccdefs.hh>

class StorageFactory;

class TestStorage
{
public:
    virtual ~TestStorage();

    enum
    {
        DEFAULT_THREADS  = 4,
        DEFAULT_SECONDS  = 10,
        DEFAULT_ITEMS    = 400,
        DEFAULT_MIN_SIZE = 1024,
        DEFAULT_MAX_SIZE = 1024 * 1024
    };

    int run(int argc, char** argv);

protected:
    TestStorage(std::ostream* pOut,
                size_t threads  = DEFAULT_THREADS,
                size_t seconds  = DEFAULT_SECONDS,
                size_t items    = DEFAULT_ITEMS,
                size_t min_size = DEFAULT_MIN_SIZE,
                size_t max_size = DEFAULT_MAX_SIZE);

    virtual int execute(StorageFactory& factory,
                        size_t threads,
                        size_t seconds,
                        size_t items,
                        size_t min_size,
                        size_t max_size) = 0;

    virtual void print_usage(const char* zProgram);

    std::ostream& out() const
    {
        return m_out;
    }

private:
    TestStorage(const TestStorage&);
    TestStorage& operator = (const TestStorage&);

private:
    std::ostream& m_out;
    size_t m_threads;
    size_t m_seconds;
    size_t m_items;
    size_t m_min_size;
    size_t m_max_size;
};
