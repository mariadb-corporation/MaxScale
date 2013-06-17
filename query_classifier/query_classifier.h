/*
This file is distributed as part of the SkySQL Gateway. It is free
software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation,
version 2.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 51
Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

Copyright SkySQL Ab

*/

/** getpid */
#include <unistd.h>
#include "../utils/skygw_utils.h"

EXTERN_C_BLOCK_BEGIN

/**
 * Query type for skygateway.
 * The meaninful difference is whether master data was modified
 */
typedef enum {
    QUERY_TYPE_UNKNOWN = 7,  /*!< Couln't find out or parse error */
    QUERY_TYPE_WRITE,        /*!< Master data will be  modified */
    QUERY_TYPE_READ,         /*!< No updates */
    QUERY_TYPE_SESSION_WRITE /*!< Session data will be modified */
} skygw_query_type_t;



skygw_query_type_t skygw_query_classifier_get_type(
        const char*   query_str,
        unsigned long client_flags);


EXTERN_C_BLOCK_END

