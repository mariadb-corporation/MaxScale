/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include <maxbase/filesystem.hh>

#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace maxbase
{

std::string save_file(std::string file, const void* ptr, size_t size)
{
    std::string err;
    std::string tmp = file + "XXXXXX";
    int fd = mkstemp(tmp.data());

    if (fd == -1)
    {
        err = mxb::string_printf("Failed to open temporary file '%s': %d, %s",
                                 tmp.c_str(), errno, mxb_strerror(errno));
    }
    else
    {
        if (write(fd, ptr, size) == -1)
        {
            err = mxb::string_printf("Write to file '%s' failed: %d, %s",
                                     tmp.c_str(), errno, mxb_strerror(errno));
        }
        else if (rename(tmp.c_str(), file.c_str()) == -1)
        {
            err = mxb::string_printf("Failed to rename '%s' to '%s': %d, %s",
                                     tmp.c_str(), file.c_str(), errno, mxb_strerror(errno));
        }

        close(fd);

        if (!err.empty())
        {
            // Remove the temporary file in case we failed to write to it.
            remove(tmp.c_str());
        }
    }

    return err;
}
}
