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

#include "gssapi_auth.hh"
#include <maxscale/alloc.h>

/**
 * @brief Report GSSAPI errors
 *
 * @param major GSSAPI major error number
 * @param minor GSSAPI minor error number
 */
void report_error(OM_uint32 major, OM_uint32 minor)
{
    OM_uint32 status_maj = major;
    OM_uint32 status_min = minor;
    OM_uint32 res = 0;
    gss_buffer_desc buf = {0, 0};

    major = gss_display_status(&minor, status_maj, GSS_C_GSS_CODE, NULL, &res, &buf);

    {
        char sbuf[buf.length + 1];
        memcpy(sbuf, buf.value, buf.length);
        sbuf[buf.length] = '\0';
        MXS_ERROR("GSSAPI Major Error: %s", sbuf);
    }

    major = gss_display_status(&minor, status_min, GSS_C_MECH_CODE, NULL, &res, &buf);

    {
        char sbuf[buf.length + 1];
        memcpy(sbuf, buf.value, buf.length);
        sbuf[buf.length] = '\0';
        MXS_ERROR("GSSAPI Minor Error: %s", sbuf);
    }
}
