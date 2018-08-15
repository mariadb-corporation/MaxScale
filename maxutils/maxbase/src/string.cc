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

#include <maxbase/string.h>

#include <string.h>

namespace
{

thread_local char errbuf[512]; // Enough for all errors

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
