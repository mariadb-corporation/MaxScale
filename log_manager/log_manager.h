/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2014
 */
#if !defined(LOG_MANAGER_H)
# define LOG_MANAGER_H

/*
 * We need a common.h file that is included by every component.
 */
#if !defined(STRERROR_BUFLEN)
#define STRERROR_BUFLEN 512
#endif

typedef struct filewriter_st  filewriter_t;
typedef struct logfile_st     logfile_t;
typedef struct fnames_conf_st fnames_conf_t;
typedef struct logmanager_st  logmanager_t;

typedef enum {
  BB_READY = 0x00,
  BB_FULL,
  BB_CLEARED
} blockbuf_state_t;

typedef enum {
    LOGFILE_ERROR = 1,
    LOGFILE_FIRST = LOGFILE_ERROR,
    LOGFILE_MESSAGE = 2,
    LOGFILE_TRACE = 4,
    LOGFILE_DEBUG = 8,
    LOGFILE_LAST = LOGFILE_DEBUG
} logfile_id_t;


typedef enum { FILEWRITER_INIT, FILEWRITER_RUN, FILEWRITER_DONE }
    filewriter_state_t;

/**
* Thread-specific logging information.
*/
typedef struct log_info_st
{
	size_t li_sesid;
	int    li_enabled_logs;
} log_info_t;

#define LE LOGFILE_ERROR
#define LM LOGFILE_MESSAGE
#define LT LOGFILE_TRACE
#define LD LOGFILE_DEBUG

/**
 * Check if specified log type is enabled in general or if it is enabled
 * for the current session.
 */
#define LOG_IS_ENABLED(id) (((lm_enabled_logfiles_bitmask & id) || 	\
		(log_ses_count[id] > 0 && 				\
		tls_log_info.li_enabled_logs & id)) ? true : false)


#define LOG_MAY_BE_ENABLED(id) (((lm_enabled_logfiles_bitmask & id) ||	\
				log_ses_count[id] > 0) ? true : false)
/**
 * Execute the given command if specified log is enabled in general or
 * if there is at least one session for whom the log is enabled.
 */
#define LOGIF_MAYBE(id,cmd) if (LOG_MAY_BE_ENABLED(id))	\
	{						\
		cmd;					\
	}

/**
 * Execute the given command if specified log is enabled in general or
 * if the log is enabled for the current session.
 */
#define LOGIF(id,cmd) if (LOG_IS_ENABLED(id))	\
	{					\
		cmd;				\
	}

#if !defined(LOGIF)
#define LOGIF(id,cmd) if (lm_enabled_logfiles_bitmask & id)     \
	{                                                       \
		cmd;                                            \
	}
#endif

/**
 * UNINIT means zeroed memory buffer allocated for the struct.
 * INIT   means that struct members may have values, and memory may
 *        have been allocated. Done function must check and free it.
 * RUN    Struct is valid for run-time checking.
 * DONE   means that possible memory allocations have been released.
 */
typedef enum { UNINIT = 0, INIT, RUN, DONE } flat_obj_state_t;

/**
 * LOG_AUGMENT_WITH_FUNCTION Each logged line is suffixed with [function-name].
 */
typedef enum
{
    LOG_AUGMENT_WITH_FUNCTION = 1,
    LOG_AUGMENTATION_MASK     = (LOG_AUGMENT_WITH_FUNCTION)
} log_augmentation_t;

EXTERN_C_BLOCK_BEGIN

bool skygw_logmanager_init(int argc, char* argv[]);
void skygw_logmanager_done(void);
void skygw_logmanager_exit(void);

/**
 * free private write buffer list
 */
void skygw_log_done(void);
int  skygw_log_write_context(logfile_id_t id,
                             const char* file, int line, const char* function,
                             const char* format, ...);
int  skygw_log_flush(logfile_id_t id);
void skygw_log_sync_all(void);
int  skygw_log_rotate(logfile_id_t id);
int  skygw_log_write_context_flush(logfile_id_t id,
                                   const char* file, int line, const char* function,
                                   const char* format, ...);
int  skygw_log_enable(logfile_id_t id);
int  skygw_log_disable(logfile_id_t id);
void skygw_log_sync_all(void);
void skygw_set_highp(int);
void logmanager_enable_syslog(int);
void logmanager_enable_maxscalelog(int);

#define skygw_log_write(id, format, ...)\
    skygw_log_write_context(id, __FILE__, __LINE__, __FUNCTION__, format, ##__VA_ARGS__)

#define skygw_log_write_flush(id, format, ...)\
    skygw_log_write_context_flush(id, __FILE__, __LINE__, __FUNCTION__, format, ##__VA_ARGS__)

/**
 * What augmentation if any should a logged message be augmented with.
 *
 * Currently this is a global setting and affects all loggers.
 */
void skygw_log_set_augmentation(int bits);
int skygw_log_get_augmentation();

EXTERN_C_BLOCK_END

const char* get_trace_prefix_default(void);
const char* get_trace_suffix_default(void);
const char* get_msg_prefix_default(void);
const char* get_msg_suffix_default(void);
const char* get_err_prefix_default(void);
const char* get_err_suffix_default(void);
const char* get_logpath_default(void);

#endif /** LOG_MANAGER_H */
