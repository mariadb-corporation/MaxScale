/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/string.hh>
#include <ctype.h>
#include <string.h>

namespace
{

thread_local char errbuf[512];      // Enough for all errors
}

const char* mxb_strerror(int error)
{
#ifdef HAVE_GLIBC
    return strerror_r(error, errbuf, sizeof(errbuf));
#else
    strerror_r(error, errbuf, sizeof(errbuf));
    return errbuf;
#endif
}

namespace maxbase
{

char* ltrim(char* str)
{
    char* ptr = str;

    while (isspace(*ptr))
    {
        ptr++;
    }

    if (ptr != str)
    {
        memmove(str, ptr, strlen(ptr) + 1);
    }

    return str;
}

char* rtrim(char* str)
{
    char* ptr = strchr(str, '\0') - 1;

    while (ptr > str && isspace(*ptr))
    {
        ptr--;
    }

    if (isspace(*(ptr + 1)))
    {
        *(ptr + 1) = '\0';
    }

    return str;
}

char* trim(char* str)
{
    return ltrim(rtrim(str));
}

bool get_long(const char* s, int base, long* value)
{
    errno = 0;
    char* end;
    long l = strtol(s, &end, base);

    bool rv = (*end == 0 && errno == 0);

    if (rv && value)
    {
        *value = l;
    }

    return rv;
}

bool get_int(const char* s, int base, int* value)
{
    long l;
    bool rv = get_long(s, base, &l);

    if (rv)
    {
        if (l >= std::numeric_limits<int>::min()
            && l <= std::numeric_limits<int>::max())
        {
            if (value)
            {
                *value = l;
            }
        }
        else
        {
            rv = false;
        }
    }

    return rv;
}
}
