#pragma once
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

#include <maxscale/cdefs.h>

#include <assert.h>
#include <stdbool.h>
#include <syslog.h>
#include <unistd.h>

#include <maxscale/jansson.h>

MXS_BEGIN_DECLS

/**
 * If MXS_MODULE_NAME is defined before log_manager.h is included, then all
 * logged messages will be prefixed with that string enclosed in square brackets.
 * For instance, the following
 *
 *     #define MXS_MODULE_NAME "xyz"
 *     #include <log_manager.h>
 *
 * will lead to every logged message looking like:
 *
 *     2016-08-12 13:49:11   error : [xyz] The gadget was not ready
 *
 * In general, the value of MXS_MODULE_NAME should be the name of the shared
 * library to which the source file, where MXS_MODULE_NAME is defined, belongs.
 *
 * Note that a file that is compiled into multiple modules should
 * have MXS_MODULE_NAME defined as something else than the name of a real
 * module, or not at all.
 *
 * Any file that is compiled into maxscale-common should *not* have
 * MXS_MODULE_NAME defined.
 */
#if !defined(MXS_MODULE_NAME)
#define MXS_MODULE_NAME NULL
#endif

typedef enum
{
    MXS_LOG_TARGET_DEFAULT,
    MXS_LOG_TARGET_FS,     // File system
    MXS_LOG_TARGET_STDOUT, // Standard output
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

/**
 * Check if specified log type is enabled in general or if it is enabled
 * for the current session.
 *
 * @param priority One of the syslog LOG_ERR, LOG_WARNING, etc. constants.
 */
#define MXS_LOG_PRIORITY_IS_ENABLED(priority) \
    ((mxs_log_enabled_priorities & (1 << priority)) ? true : false)

/**
 * LOG_AUGMENT_WITH_FUNCTION Each logged line is suffixed with [function-name].
 */
typedef enum
{
    MXS_LOG_AUGMENT_WITH_FUNCTION = 1,
    MXS_LOG_AUGMENTATION_MASK     = (MXS_LOG_AUGMENT_WITH_FUNCTION)
} mxs_log_augmentation_t;

typedef struct mxs_log_throttling
{
    size_t count;       // Maximum number of a specific message...
    size_t window_ms;   // ...during this many milliseconds.
    size_t suppress_ms; // If exceeded, suppress such messages for this many ms.
} MXS_LOG_THROTTLING;

bool mxs_log_init(const char* ident, const char* logdir, mxs_log_target_t target);
void mxs_log_finish(void);

/**
 * Start log flushing thread
 *
 * @return True if log flusher thread was started
 */
bool mxs_log_start_flush_thr();

/**
 * Stop log flushing thread
 */
void mxs_log_stop_flush_thr();

int mxs_log_flush();
int mxs_log_flush_sync();
bool mxs_log_rotate();

int  mxs_log_set_priority_enabled(int priority, bool enabled);
void mxs_log_set_syslog_enabled(bool enabled);
void mxs_log_set_maxlog_enabled(bool enabled);
void mxs_log_set_highprecision_enabled(bool enabled);
void mxs_log_set_augmentation(int bits);
void mxs_log_set_throttling(const MXS_LOG_THROTTLING* throttling);

void mxs_log_get_throttling(MXS_LOG_THROTTLING* throttling);
json_t* mxs_logs_to_json(const char* host);

static inline bool mxs_log_priority_is_enabled(int priority)
{
    assert((priority & ~LOG_PRIMASK) == 0);
    return MXS_LOG_PRIORITY_IS_ENABLED(priority) || priority == LOG_ALERT;
}

int mxs_log_message(int priority,
                    const char* modname,
                    const char* file, int line, const char* function,
                    const char* format, ...) mxs_attribute((format(printf, 6, 7)));
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
    (mxs_log_priority_is_enabled(priority) ? \
     mxs_log_message(priority, MXS_MODULE_NAME, __FILE__, __LINE__, __func__, format, ##__VA_ARGS__) :\
     0)

/**
 * Log an alert, error, warning, notice, info, or debug  message.
 *
 * MXS_ALERT   Not throttled  To be used when the system is about to go down in flames.
 * MXS_ERROR   Throttled      For errors.
 * MXS_WARNING Throttled      For warnings.
 * MXS_NOTICE  Not Throttled  For messages deemed important, typically used during startup.
 * MXS_INFO    Not Throttled  For information thought to be of value for investigating some problem.
 * MXS_DEBUG   Not Throttled  For debugging messages during development. Should be removed when a
 *                            feature is ready.
 *
 * @param format The printf format of the message.
 * @param ...    Arguments, depending on the format.
 */
#define MXS_ALERT(format, ...)   MXS_LOG_MESSAGE(LOG_ALERT,   format, ##__VA_ARGS__)
#define MXS_ERROR(format, ...)   MXS_LOG_MESSAGE(LOG_ERR,     format, ##__VA_ARGS__)
#define MXS_WARNING(format, ...) MXS_LOG_MESSAGE(LOG_WARNING, format, ##__VA_ARGS__)
#define MXS_NOTICE(format, ...)  MXS_LOG_MESSAGE(LOG_NOTICE,  format, ##__VA_ARGS__)
#define MXS_INFO(format, ...)    MXS_LOG_MESSAGE(LOG_INFO,    format, ##__VA_ARGS__)

#if defined(SS_DEBUG)
#define MXS_DEBUG(format, ...)   MXS_LOG_MESSAGE(LOG_DEBUG,   format, ##__VA_ARGS__)
#else
#define MXS_DEBUG(format, ...)
#endif

/**
 * Log an out of memory error using custom message.
 *
 * @param message Text to be logged.
 */
// TODO: In an OOM situation, the default logging will (most likely) *not* work,
// TODO: as memory is allocated as part of the process. A custom route, that does
// TODO: not allocate memory, must be created for OOM messages.
// TODO: So, currently these are primarily placeholders.
#define MXS_OOM_MESSAGE(message) MXS_ERROR("OOM: %s", message);

/**
 * Log an out of memory error using custom message, if the
 * provided pointer is NULL.
 *
 * @param p If NULL, an OOM message will be logged.
 * @param message Text to be logged.
 */
#define MXS_OOM_MESSAGE_IFNULL(p, m) do { if (!p) { MXS_OOM_MESSAGE(m); } } while (false)

/**
 * Log an out of memory error using a default message.
 */
#define MXS_OOM() MXS_OOM_MESSAGE(__func__)

/**
 * Log an out of memory error using a default message, if the
 * provided pointer is NULL.
 *
 * @param p If NULL, an OOM message will be logged.
 */
#define MXS_OOM_IFNULL(p) do { if (!p) { MXS_OOM(); } } while (false)

enum
{
    MXS_OOM_MESSAGE_MAXLEN = 80 /** Maximum length of an OOM message, including the
                                    trailing NULL. If longer, it will be cut. */
};

/**
 * Return a thread specific pointer to a string describing the error
 * code passed as argument. The string is obtained using strerror_r.
 *
 * @param error  One of the errno error codes.
 *
 * @return Thread specific pointer to string describing the error code.
 *
 * @attention The function is thread safe, but not re-entrant. That is,
 * calling it twice with different error codes between two sequence points
 * will not work. E.g:
 *
 *     printf("EINVAL = %s, EACCESS = %s",
 *            mxs_strerror(EINVAL), mxs_strerror(EACCESS));
 */
const char* mxs_strerror(int error);

MXS_END_DECLS
