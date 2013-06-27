/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright SkySQL Ab 2013
 */


typedef struct filewriter_st  filewriter_t;
typedef struct logfile_st     logfile_t;
typedef struct fnames_conf_st fnames_conf_t;
typedef struct logmanager_st  logmanager_t;

typedef enum {
    LOGFILE_TRACE = 0,
    LOGFILE_FIRST = LOGFILE_TRACE,
    LOGFILE_MESSAGE,
    LOGFILE_ERROR,
    LOGFILE_LAST = LOGFILE_ERROR
} logfile_id_t;

typedef enum { FILEWRITER_INIT, FILEWRITER_RUN, FILEWRITER_DONE }
    filewriter_state_t;
typedef enum { LOGFILE_INIT, LOGFILE_OPENED, LOGFILE_DONE } logfile_state_t;

EXTERN_C_BLOCK_BEGIN

bool skygw_logmanager_init(void** ctx, int argc, char* argv[]);
void skygw_logmanager_done(void** ctx);
int skygw_log_write(void* ctx, logfile_id_t id, char* str);
int skygw_log_flush(logfile_id_t id);
int skygw_log_write_flush(void* ctx, logfile_id_t id, char* str);

EXTERN_C_BLOCK_END

const char* get_trace_prefix_default(void);
const char* get_trace_suffix_default(void);
const char* get_msg_prefix_default(void);
const char* get_msg_suffix_default(void);
const char* get_err_prefix_default(void);
const char* get_err_suffix_default(void);
const char* get_logpath_default(void);

/*
bool logfile_write(
        skygw_ctx_t* ctx,
        logmgr_t*    mgr,
        logfile_id_t id,
        char*        msg);

bool logfile_write_flush(
        skygw_ctx_t* ctx,
        logmgr_t*    mgr,
        logfile_id_t id,
        char*        msg);

bool logfile_flush(
        logmgr_t*    mgr,
        logfile_id_t id);

bool logfile_init(
        logmgr_t*    mgr,
        logfile_id_t id);

void logfile_done(
        logmgr_t*    mgr,
        logfile_id_t id);
*/
