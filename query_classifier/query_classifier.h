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
 * The meaninful difference is where operation is done and whether master data
 * is modified
 */
typedef enum {
    QUERY_TYPE_UNKNOWN          = 0x00,       /*< Couln't find out or parse error */
    QUERY_TYPE_LOCAL_READ       = 0x01,  /*< Read non-database data, execute in MaxScale */
    QUERY_TYPE_READ             = 0x02,  /*< No updates */
    QUERY_TYPE_WRITE            = 0x04,  /*< Master data will be  modified */
    QUERY_TYPE_SESSION_WRITE    = 0x08,  /*< Session data will be modified */
    QUERY_TYPE_GLOBAL_WRITE     = 0x10,  /*< Global system variable modification */
    QUERY_TYPE_BEGIN_TRX        = 0x20,  /*< BEGIN or START TRANSACTION */
    QUERY_TYPE_ENABLE_AUTOCOMMIT  = 0x30,/*< SET autocommit=1 */
    QUERY_TYPE_DISABLE_AUTOCOMMIT = 0x40,/*< SET autocommit=0 */
    QUERY_TYPE_ROLLBACK         = 0x50,  /*< ROLLBACK */
    QUERY_TYPE_COMMIT           = 0x60   /*< COMMIT */
} skygw_query_type_t;

#define QUERY_IS_TYPE(mask,type) ((mask & type) == type)

skygw_query_type_t skygw_query_classifier_get_type(
        const char*   query_str,
        unsigned long client_flags);


EXTERN_C_BLOCK_END

