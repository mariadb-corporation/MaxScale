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
    MXS_LOG_TARGET_DEFAULT = 0,
    MXS_LOG_TARGET_FS      = 1, // File system
    MXS_LOG_TARGET_SHMEM   = 2, // Shared memory
} mxs_log_target_t;

/**
* Thread-specific logging information.
*/
typedef struct mxs_log_info
{
    size_t li_sesid;
    int    li_enabled_priorities;
} mxs_log_info_t;

extern int mxs_log_enabled_priorities;
extern ssize_t mxs_log_session_count[];
extern __thread mxs_log_info_t mxs_log_tls;

/**
 * Check if specified log type is enabled in general or if it is enabled
 * for the current session.
 */
#define MXS_LOG_PRIORITY_IS_ENABLED(priority) \
    (((mxs_log_enabled_priorities & (1 << priority)) ||      \
      (mxs_log_session_count[priority] > 0 && \
       mxs_log_tls.li_enabled_priorities & (1 << priority))) ? true : false)

/**
 * LOG_AUGMENT_WITH_FUNCTION Each logged line is suffixed with [function-name].
 */
typedef enum
{
    MXS_LOG_AUGMENT_WITH_FUNCTION = 1,
    MXS_LOG_AUGMENTATION_MASK     = (MXS_LOG_AUGMENT_WITH_FUNCTION)
} mxs_log_augmentation_t;

bool mxs_log_init(const char* ident, const char* logdir, mxs_log_target_t target);
void mxs_log_finish(void);

int mxs_log_flush();
int mxs_log_flush_sync();
int mxs_log_rotate();

int  mxs_log_set_priority_enabled(int priority, bool enabled);
void mxs_log_set_syslog_enabled(bool enabled);
void mxs_log_set_maxlog_enabled(bool enabled);
void mxs_log_set_highprecision_enabled(bool enabled);
void mxs_log_set_augmentation(int bits);

int mxs_log_message(int priority,
                    const char* file, int line, const char* function,
                    const char* format, ...) __attribute__((format(printf, 5, 6)));
/**
 * Log an error, warning, notice, info, or debug  message.
 *
 * @param priority One of the syslog constants (LOG_ERR, LOG_WARNING, ...)
 * @param format   The printf format of the message.
 * @param ...      Arguments, depending on the format.
 *
 * NOTE: Should typically not be called directly. Use some of the
 *       MXS_ERROR, MXS_WARNING, etc. macros instead.
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
