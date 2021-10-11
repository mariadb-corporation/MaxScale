/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/modutil.hh>
#include <maxbase/stopwatch.hh>
#include <iostream>
#include <sstream>
#include <chrono>

using std::chrono::duration_cast;
using std::chrono::milliseconds;

int main(int argc, char* argv[])
{
    int ITERATIONS = 10000000;

    for (std::string line; std::getline(std::cin, line);)
    {
        GWBUF* buf = modutil_create_query(line.c_str());
        auto start = maxbase::Clock::now();

        for (int i = 0; i < ITERATIONS; i++)
        {
            auto str = mxs::get_canonical(buf);
        }

        auto end = maxbase::Clock::now();
        gwbuf_free(buf);

        std::cout << line << "\n"
                  << duration_cast<milliseconds>(end - start).count() << "ms\n\n";
    }

    return 0;
}
