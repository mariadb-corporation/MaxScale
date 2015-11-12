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

#include <syslog.h>

/*
 * We need a common.h file that is included by every component.
 */
#if !defined(STRERROR_BUFLEN)
#define STRERROR_BUFLEN 512
#endif

typedef enum
{
    BB_READY = 0x00,
    BB_FULL,
    BB_CLEARED
} blockbuf_state_t;

typedef enum
{
    LOGFILE_ERROR = 1,
    LOGFILE_FIRST = LOGFILE_ERROR,
    LOGFILE_MESSAGE = 2,
    LOGFILE_TRACE = 4,
    LOGFILE_DEBUG = 8,
    LOGFILE_LAST = LOGFILE_DEBUG
} logfile_id_t;

typedef enum
{
    FILEWRITER_INIT,
    FILEWRITER_RUN,
    FILEWRITER_DONE
} filewriter_state_t;

typedef enum
{
    LOG_TARGET_DEFAULT = 0,
    LOG_TARGET_FS      = 1, // File system
    LOG_TARGET_SHMEM   = 2, // Shared memory
} log_target_t;

/**
* Thread-specific logging information.
*/
typedef struct log_info
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
#define LOG_IS_ENABLED(id) (((lm_enabled_logfiles_bitmask & id) ||      \
                             (log_ses_count[id] > 0 &&                  \
                              tls_log_info.li_enabled_logs & id)) ? true : false)


#define LOG_MAY_BE_ENABLED(id) (((lm_enabled_logfiles_bitmask & id) ||  \
                                 log_ses_count[id] > 0) ? true : false)
/**
 * Execute the given command if specified log is enabled in general or
 * if there is at least one session for whom the log is enabled.
 */
#define LOGIF_MAYBE(id,cmd) if (LOG_MAY_BE_ENABLED(id)) \
    {                                                   \
        cmd;                                            \
    }

/**
 * Execute the given command if specified log is enabled in general or
 * if the log is enabled for the current session.
 */
#define LOGIF(id,cmd) if (LOG_IS_ENABLED(id))   \
    {                                           \
        cmd;                                    \
    }

#if !defined(LOGIF)
#define LOGIF(id,cmd) if (lm_enabled_logfiles_bitmask & id)     \
    {                                                           \
        cmd;                                                    \
    }
#endif

/**
 * UNINIT means zeroed memory buffer allocated for the struct.
 * INIT   means that struct members may have values, and memory may
 *        have been allocated. Done function must check and free it.
 * RUN    Struct is valid for run-time checking.
 * DONE   means that possible memory allocations have been released.
 */
typedef enum
{
    UNINIT = 0,
    INIT,
    RUN,
    DONE
} flat_obj_state_t;

/**
 * LOG_AUGMENT_WITH_FUNCTION Each logged line is suffixed with [function-name].
 */
typedef enum
{
    LOG_AUGMENT_WITH_FUNCTION = 1,
    LOG_AUGMENTATION_MASK     = (LOG_AUGMENT_WITH_FUNCTION)
} log_augmentation_t;

EXTERN_C_BLOCK_BEGIN

extern int lm_enabled_logfiles_bitmask;
extern ssize_t log_ses_count[];
extern __thread log_info_t tls_log_info;

bool mxs_log_init(const char* ident, const char* logdir, log_target_t target);
void mxs_log_finish(void);

int mxs_log_flush();
int mxs_log_flush_sync();
int mxs_log_rotate();

int  mxs_log_set_priority_enabled(int priority, bool enabled);
void mxs_log_set_syslog_enabled(bool enabled);
void mxs_log_set_maxscalelog_enabled(bool enabled);
void mxs_log_set_highprecision_enabled(bool enabled);
void mxs_log_set_augmentation(int bits);

int mxs_log_message(int priority,
                    const char* file, int line, const char* function,
                    const char* format, ...);

int  skygw_log_enable(logfile_id_t id);
int  skygw_log_disable(logfile_id_t id);

inline int mxs_log_id_to_priority(logfile_id_t id)
{
    if (id & LOGFILE_ERROR) return LOG_ERR;
    if (id & LOGFILE_MESSAGE) return LOG_NOTICE;
    if (id & LOGFILE_TRACE) return LOG_INFO;
    if (id & LOGFILE_DEBUG) return LOG_DEBUG;
    return LOG_ERR;
}

#define skygw_log_write(id, format, ...)\
    mxs_log_message(mxs_log_id_to_priority(id), __FILE__, __LINE__, __func__, format, ##__VA_ARGS__)

#define skygw_log_write_flush(id, format, ...) skygw_log_write(id, format, ##__VA_ARGS__)

EXTERN_C_BLOCK_END

/**
 * Helper, not to be called directly.
 */
#define MXS_LOG_MESSAGE(priority, format, ...)\
    mxs_log_message(priority, __FILE__, __LINE__, __func__, format, ##__VA_ARGS__)

/**
 * Log an error, warning, notice, info, or debug  message.
 *
 * @param format The printf format of the message.
 * @param ...    Arguments, depending on the format.
 */
#define MXS_ERROR(format, ...)   MXS_LOG_MESSAGE(LOG_ERR,     format, ##__VA_ARGS__)
#define MXS_WARNING(format, ...) MXS_LOG_MESSAGE(LOG_WARNING, format, ##__VA_ARGS__)
#define MXS_NOTICE(format, ...)  MXS_LOG_MESSAGE(LOG_NOTICE,  format, ##__VA_ARGS__)
#define MXS_INFO(format, ...)    MXS_LOG_MESSAGE(LOG_INFO,    format, ##__VA_ARGS__)
#define MXS_DEBUG(format, ...)   MXS_LOG_MESSAGE(LOG_DEBUG,   format, ##__VA_ARGS__)

#endif /** LOG_MANAGER_H */
