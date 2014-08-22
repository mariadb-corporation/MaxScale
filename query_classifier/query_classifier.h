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
#include <mysql.h>
#include <skygw_utils.h>
#include <buffer.h>

EXTERN_C_BLOCK_BEGIN

/**
 * Query type for skygateway.
 * The meaninful difference is where operation is done and whether master data
 * is modified
 */
typedef enum {
    QUERY_TYPE_UNKNOWN            = 0x0000,  /*< Initial value, can't be tested bitwisely */
    QUERY_TYPE_LOCAL_READ         = 0x0001,  /*< Read non-database data, execute in MaxScale */
    QUERY_TYPE_READ               = 0x0002,  /*< No updates */
    QUERY_TYPE_WRITE              = 0x0004,  /*< Master data will be  modified */
    QUERY_TYPE_SESSION_WRITE      = 0x0008,  /*< Session data will be modified */
    QUERY_TYPE_GLOBAL_WRITE       = 0x0010,  /*< Global system variable modification */
    QUERY_TYPE_BEGIN_TRX          = 0x0020,  /*< BEGIN or START TRANSACTION */
    QUERY_TYPE_ENABLE_AUTOCOMMIT  = 0x0040,  /*< SET autocommit=1 */
    QUERY_TYPE_DISABLE_AUTOCOMMIT = 0x0080,  /*< SET autocommit=0 */
    QUERY_TYPE_ROLLBACK           = 0x0100,  /*< ROLLBACK */
    QUERY_TYPE_COMMIT             = 0x0200,  /*< COMMIT */
    QUERY_TYPE_PREPARE_NAMED_STMT = 0x0400,  /*< Prepared stmt with name from user */
    QUERY_TYPE_PREPARE_STMT       = 0x0800,  /*< Prepared stmt with id provided by server */
    QUERY_TYPE_EXEC_STMT          = 0x1000   /*< Execute prepared statement */
} skygw_query_type_t;


typedef struct parsing_info_st {
#if defined(SS_DEBUG)
        skygw_chk_t pi_chk_top;     
#endif
        void*       pi_handle;                        /*< parsing info object pointer */
        char*       pi_query_plain_str;               /*< query as plain string */
        void     (*pi_done_fp)(void *);             /*< clean-up function for parsing info */
#if defined(SS_DEBUG)
        skygw_chk_t pi_chk_tail;
#endif
} parsing_info_t;


#define QUERY_IS_TYPE(mask,type) ((mask & type) == type)

/** 
 * Create THD and use it for creating parse tree. Examine parse tree and 
 * classify the query.
 */
skygw_query_type_t query_classifier_get_type(GWBUF* querybuf);

/** Free THD context and close MYSQL */
char*           skygw_query_classifier_get_stmtname(MYSQL* mysql);
char*           skygw_get_canonical(GWBUF* querybuf);
bool            parse_query (GWBUF* querybuf);
parsing_info_t* parsing_info_init(void (*donefun)(void *));
void            parsing_info_done(void* ptr);
bool            query_is_parsed(GWBUF* buf);


EXTERN_C_BLOCK_END

