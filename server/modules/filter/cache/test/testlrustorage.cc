/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <iostream>
#include <maxscale/alloc.h>
#include <maxscale/paths.h>
#include "teststorage.hh"
#include "testerlrustorage.hh"

using namespace std;

namespace
{

class TestLRUStorage : public TestStorage
{
public:
    TestLRUStorage(std::ostream* pOut)
        : TestStorage(pOut)
    {}

private:
    int execute(StorageFactory& factory,
                size_t threads,
                size_t seconds,
                size_t items,
                size_t min_size,
                size_t max_size)
    {
        TesterLRUStorage tester(&out(), &factory);

        return tester.run(threads, seconds, items, min_size, max_size);
    }
};

}

int main(int argc, char* argv[])
{
    char* libdir = MXS_STRDUP("../../../../../query_classifier/qc_sqlite/");
    set_libdir(libdir);

    TestLRUStorage test(&cout);
    int rv = test.run(argc, argv);

    // TODO: Remove this once globally allocated memory is freed
    MXS_FREE(libdir);
    return rv;
}
