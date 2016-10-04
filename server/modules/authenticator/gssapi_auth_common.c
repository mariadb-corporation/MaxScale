/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "gssapi_auth.h"
#include <maxscale/alloc.h>

void* gssapi_auth_alloc()
{
    gssapi_auth_t* rval = MXS_MALLOC(sizeof(gssapi_auth_t));

    if (rval)
    {
        rval->state = GSSAPI_AUTH_INIT;
    }

    return rval;
}

void gssapi_auth_free(void *data)
{
    if (data)
    {
        MXS_FREE(data);
    }
}
