/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
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

using namespace std;

namespace
{

#define RELEASE_STR_LENGTH 256

/**
 * Get the linux distribution info
 *
 * @param release The buffer where the found distribution is copied.
 *                Assumed to be at least RELEASE_STR_LENGTH bytes.
 *
 * @return 1 on success, 0 on failure
 */
int get_release_string(char* release)
{
    const char* masks[] =
    {
        "/etc/*-version", "/etc/*-release",
        "/etc/*_version", "/etc/*_release"
    };

    bool have_distribution;
    char distribution[RELEASE_STR_LENGTH] = "";
    int fd;

    have_distribution = false;

    /* get data from lsb-release first */
    if ((fd = open("/etc/lsb-release", O_RDONLY)) != -1)
    {
        /* LSB-compliant distribution! */
        size_t len = read(fd, (char*)distribution, sizeof(distribution) - 1);
        close(fd);

        if (len != (size_t) -1)
        {
            distribution[len] = 0;

            char* found = strstr(distribution, "DISTRIB_DESCRIPTION=");

            if (found)
            {
                have_distribution = true;
                char* end = strstr(found, "\n");
                if (end == NULL)
                {
                    end = distribution + len;
                }
                found += 20;    // strlen("DISTRIB_DESCRIPTION=")

                if (*found == '"' && end[-1] == '"')
                {
                    found++;
                    end--;
                }
                *end = 0;

                char* to = strcpy(distribution, "lsb: ");
                memmove(to, found, end - found + 1 < INT_MAX ? end - found + 1 : INT_MAX);

                strcpy(release, to);

                return 1;
            }
        }
    }

    /* if not an LSB-compliant distribution */
    for (int i = 0; !have_distribution && i < 4; i++)
    {
        glob_t found;
        char* new_to;

        if (glob(masks[i], GLOB_NOSORT, NULL, &found) == 0)
        {
            int fd;
            size_t k = 0;
            int skipindex = 0;
            int startindex = 0;

            for (k = 0; k < found.gl_pathc; k++)
            {
                if (strcmp(found.gl_pathv[k], "/etc/lsb-release") == 0)
                {
                    skipindex = k;
                }
            }

            if (skipindex == 0)
            {
                startindex++;
            }

            if ((fd = open(found.gl_pathv[startindex], O_RDONLY)) != -1)
            {
                /*
                 +5 and -8 below cut the file name part out of the
                 *  full pathname that corresponds to the mask as above.
                 */
                new_to = strncpy(distribution, found.gl_pathv[0] + 5, RELEASE_STR_LENGTH - 1);
                new_to += 8;
                *new_to++ = ':';
                *new_to++ = ' ';

                size_t to_len = distribution + sizeof(distribution) - 1 - new_to;
                size_t len = read(fd, (char*)new_to, to_len);

                close(fd);

                if (len != (size_t) -1)
                {
                    new_to[len] = 0;
                    char* end = strstr(new_to, "\n");
                    if (end)
                    {
                        *end = 0;
                    }

                    have_distribution = true;
                    strncpy(release, new_to, RELEASE_STR_LENGTH - 1);
                    release[RELEASE_STR_LENGTH - 1] = '\0';
                }
            }
        }
        globfree(&found);
    }

    if (have_distribution)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

}

string maxbase::get_release_string()
{
    string rv;
    char release_string[RELEASE_STR_LENGTH];

    if (::get_release_string(release_string) == 1)
    {
        rv = release_string;
    }

    return rv;
}
