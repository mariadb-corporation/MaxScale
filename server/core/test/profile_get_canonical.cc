/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/ccdefs.hh>
#include <maxbase/stopwatch.hh>
#include <maxsimd/canonical.hh>
#include <iostream>

using std::chrono::duration_cast;
using std::chrono::milliseconds;

int main(int argc, char* argv[])
{
    int ITERATIONS = 10000000;
    maxsimd::Markers markers;

    for (std::string line; std::getline(std::cin, line);)
    {
        auto start = maxbase::Clock::now();

        for (int i = 0; i < ITERATIONS; i++)
        {
            std::string line_cp(line);
            maxsimd::get_canonical(&line_cp, &markers);
        }

        auto end = maxbase::Clock::now();

        std::cout << line << "\n"
                  << duration_cast<milliseconds>(end - start).count() << "ms\n\n";
    }

    return 0;
}
