/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/system.hh>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace std;

namespace
{

string get_param_value(const char* zParams, const char* zParam)
{
    string rv;

    const char* z = strstr(zParams, zParam);

    if (z)
    {
        z += strlen(zParam);

        if (*z == '"')
        {
            ++z;
        }

        const char* zEnd = strchrnul(z, '\n');

        if (zEnd > z && *(zEnd - 1) == '"')
        {
            --zEnd;
        }

        rv.assign(z, zEnd);
    }

    return rv;
}

vector<char> get_content(const char* zPath)
{
    vector<char> rv;

    int fd = ::open(zPath, O_RDONLY);
    if (fd != -1)
    {
        struct stat s;
        if (::fstat(fd, &s) == 0)
        {
            rv.resize(s.st_size + 1);
            ssize_t size = ::read(fd, rv.data(), s.st_size);

            if (size > 0)
            {
                rv[size] = 0;
            }
            else
            {
                rv.clear();
            }
        }

        ::close(fd);
    }

    return rv;
}

string get_release_from_os_release()
{
    string rv;

    vector<char> buffer = get_content("/etc/os-release");

    if (!buffer.empty())
    {
        string name = get_param_value(buffer.data(), "NAME=");
        string version = get_param_value(buffer.data(), "VERSION=");

        if (!name.empty())
        {
            rv += name;

            if (!version.empty())
            {
                rv += " ";
            }
        }

        rv += version;
    }

    return rv;
}

string get_release_from_lsb_release()
{
    string rv;

    vector<char> buffer = get_content("/etc/lsb-release");

    if (!buffer.empty())
    {
        rv = get_param_value(buffer.data(), "DISTRIB_DESCRIPTION=");
    }

    return rv;
}

}

string maxbase::get_release_string(ReleaseSource source)
{
    // Unless told otherwise, we first look in /etc/os-release, but if the file
    // does not exist or what we look for is not there, we make a second attempt
    // with /etc/lsb-release.

    string rv;

    if (source == ReleaseSource::OS_RELEASE || source == ReleaseSource::ANY)
    {
        rv = get_release_from_os_release();
    }

    if (rv.empty() && (source == ReleaseSource::LSB_RELEASE || source == ReleaseSource::ANY))
    {
        rv = get_release_from_lsb_release();
    }

    return rv;
}
