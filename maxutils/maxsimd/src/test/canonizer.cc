/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>

#include <string.h>
#include <fstream>
#include <iostream>
#include <string>

#include <maxsimd/canonical.hh>

using std::cout;
using std::endl;

int main(int argc, char** argv)
{
    int rc = EXIT_FAILURE;

    if (argc != 3)
    {
        cout << "Usage: canonizer <input file> <output file>" << endl;
        return rc;
    }

    std::ifstream infile(argv[1]);
    std::ofstream outfile(argv[2]);

    if (infile && outfile)
    {
        for (std::string line; getline(infile, line);)
        {
            while (*line.rbegin() == '\n')
            {
                line.resize(line.size() - 1);
            }

            if (!line.empty())
            {
                maxsimd::get_canonical(&line);
                outfile << line << endl;
            }
        }

        rc = EXIT_SUCCESS;
    }
    else
    {
        cout << "Opening files failed." << endl;
    }

    return rc;
}
