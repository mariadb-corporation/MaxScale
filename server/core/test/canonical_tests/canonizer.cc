/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/ccdefs.hh>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <fstream>
#include <iostream>
#include <string>

#include <maxscale/query_classifier.h>
#include <maxscale/buffer.hh>
#include <maxscale/paths.h>
#include <maxscale/utils.h>

using std::cout;
using std::endl;

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        cout << "Usage: canonizer <input file> <output file>" << endl;
        return 1;
    }

    mxs_log_init(NULL, NULL, MXS_LOG_TARGET_STDOUT);
    atexit(mxs_log_finish);

    if (!utils_init())
    {
        cout << "Utils library init failed." << endl;
        return 1;
    }

    set_libdir(strdup("../../../../query_classifier/qc_sqlite/"));
    set_datadir(strdup("/tmp"));
    set_langdir(strdup("."));
    set_process_datadir(strdup("/tmp"));

    qc_setup(NULL, QC_SQL_MODE_DEFAULT, "qc_sqlite", NULL);
    qc_process_init(QC_INIT_BOTH);
    qc_thread_init(QC_INIT_BOTH);

    std::ifstream infile(argv[1]);
    std::ofstream outfile(argv[2]);

    if (!infile || !outfile)
    {
        cout << "Opening files failed." << endl;
        return 1;
    }

    for (std::string line; getline(infile, line);)
    {
        while (*line.rbegin() == '\n')
        {
            line.resize(line.size() - 1);
        }

        if (!line.empty())
        {
            size_t psize = line.size() + 1;
            mxs::Buffer buf(psize + 4);
            auto it = buf.begin();
            *it++ = (uint8_t)psize;
            *it++ = (uint8_t)(psize >> 8);
            *it++ = (uint8_t)(psize >> 16);
            *it++ = 0;
            *it++ = 3;
            std::copy(line.begin(), line.end(), it);
            char* tok = qc_get_canonical(buf.get());
            outfile <<  tok << endl;
            free(tok);
        }
    }

    qc_process_end(QC_INIT_BOTH);
    return 0;
}
