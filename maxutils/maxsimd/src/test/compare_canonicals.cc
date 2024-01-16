/*
 * Copyright (c) 2024 MariaDB plc
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

#include <maxbase/ccdefs.hh>

#include <fstream>
#include <iostream>

#include <maxbase/string.hh>
#include <maxsimd/canonical.hh>

/**
 * Reads the files given as the program arguments and generates canonical versions of each of the lines.
 *
 * @return 0 if all files produce identical results with both specialized and generic functions. 1 if at least
 *         one difference was found.
 */
int main(int argc, char** argv)
{
    int rc = EXIT_SUCCESS;

    if (argc < 2)
    {
        std::cout << "USAGE: " << argv[0] << " FILE\n";
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; i++)
    {
        std::ifstream infile(argv[i]);

        if (!infile)
        {
            std::cout << "Error opening file '" << argv[i] << "': " << mxb_strerror(errno) << "\n";
            rc = EXIT_FAILURE;
            break;
        }

        int lineno = 0;

        for (std::string line; std::getline(infile, line);)
        {
            ++lineno;
            std::string specialized = line;
            std::string generic = line;
            maxsimd::get_canonical(&specialized);
            maxsimd::generic::get_canonical(&generic);

            if (specialized != generic)
            {
                std::cout << "Error at " << argv[i] << ":" << lineno << "\n"
                          << "Generic:       '" << generic << "'\n"
                          << "Specialized:   '" << specialized << "'\n"
                          << "\n";
                rc = EXIT_FAILURE;
            }
        }
    }

    return rc;
}
