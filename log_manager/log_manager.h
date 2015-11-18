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
#define LOG_MANAGER_H

#include <syslog.h>

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * We need a common.h file that is included by every component.
 */
#if !defined(STRERROR_BUFLEN)
#define STRERROR_BUFLEN 512
#endif

enum mxs_log_priorities
{
    MXS_LOG_EMERG   = (1 << LOG_EMERG),
    MXS_LOG_ALERT   = (1 << LOG_ALERT),
    MXS_LOG_CRIT    = (1 << LOG_CRIT),
    MXS_LOG_ERR     = (1 << LOG_ERR),
    MXS_LOG_WARNING = (1 << LOG_WARNING),
    MXS_LOG_NOTICE  = (1 << LOG_NOTICE),
    MXS_LOG_INFO    = (1 << LOG_INFO),
    MXS_LOG_DEBUG   = (1 << LOG_DEBUG),

    MXS_LOG_MASK    = (MXS_LOG_EMERG | MXS_LOG_ALERT | MXS_LOG_CRIT | MXS_LOG_ERR |
                       MXS_LOG_WARNING | MXS_LOG_NOTICE | MXS_LOG_INFO | MXS_LOG_DEBUG),
};

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

extern int lm_enabled_priorities_bitmask;
extern int lm_enabled_logfiles_bitmask;
extern ssize_t log_ses_count[];
extern __thread log_info_t tls_log_info;

/**
 * Check if specified log type is enabled in general or if it is enabled
 * for the current session.
 */
#define LOG_IS_ENABLED(id) (((lm_enabled_logfiles_bitmask & id) ||      \
                             (log_ses_count[id] > 0 &&                  \
                              tls_log_info.li_enabled_logs & id)) ? true : false)

#define LOG_MAY_BE_ENABLED(id) (((lm_enabled_logfiles_bitmask & id) ||  \
                                 log_ses_count[id] > 0) ? true : false)

// TODO: Add this at a later stage.
#define MXS_LOG_PRIORITY_IS_ENABLED(priority) false

/**
 * Execute the given command if specified log is enabled in general or
 * if the log is enabled for the current session.
 */
#define LOGIF(id,cmd) if (LOG_IS_ENABLED(id))   \
    {                                           \
        cmd;                                    \
    }

/**
 * LOG_AUGMENT_WITH_FUNCTION Each logged line is suffixed with [function-name].
 */
typedef enum
{
    LOG_AUGMENT_WITH_FUNCTION = 1,
    LOG_AUGMENTATION_MASK     = (LOG_AUGMENT_WITH_FUNCTION)
} log_augmentation_t;

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
                    const char* format, ...) __attribute__((format(printf, 5, 6)));

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

#if defined(__cplusplus)
}
#endif

#endif /** LOG_MANAGER_H */
