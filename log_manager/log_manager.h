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

/**
 * UNINIT means zeroed memory buffer allocated for the struct.
 * INIT   means that struct members may have values, and memory may
 *        have been allocated. Done function must check and free it.
 * RUN    Struct is valid for run-time checking.
 * DONE   means that possible memory allocations have been released.
 */
typedef enum { UNINIT = 0, INIT, RUN, DONE } flat_obj_state_t; 

EXTERN_C_BLOCK_BEGIN

bool skygw_logmanager_init(int argc, char* argv[]);
void skygw_logmanager_done(void);
void skygw_logmanager_exit(void);

/**
 * free private write buffer list
 */
void skygw_log_done(void);
int  skygw_log_write(logfile_id_t id, char* format, ...);
int  skygw_log_flush(logfile_id_t id);
int  skygw_log_write_flush(logfile_id_t id, char* format, ...);



EXTERN_C_BLOCK_END

void writebuf_clear(void* data);

const char* get_trace_prefix_default(void);
const char* get_trace_suffix_default(void);
const char* get_msg_prefix_default(void);
const char* get_msg_suffix_default(void);
const char* get_err_prefix_default(void);
const char* get_err_suffix_default(void);
const char* get_logpath_default(void);
