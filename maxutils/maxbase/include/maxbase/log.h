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

#include <maxbase/cdefs.h>

#include <assert.h>
#include <stdbool.h>
#include <syslog.h>
#include <unistd.h>

#include <maxbase/jansson.h>

MXB_BEGIN_DECLS

/**
 * If MXB_MODULE_NAME is defined before log_manager.h is included, then all
 * logged messages will be prefixed with that string enclosed in square brackets.
 * For instance, the following
 *
 *     #define MXB_MODULE_NAME "xyz"
 *     #include <log_manager.h>
 *
 * will lead to every logged message looking like:
 *
 *     2016-08-12 13:49:11   error : [xyz] The gadget was not ready
 *
 * In general, the value of MXB_MODULE_NAME should be the name of the shared
 * library to which the source file, where MXB_MODULE_NAME is defined, belongs.
 *
 * Note that a file that is compiled into multiple modules should
 * have MXB_MODULE_NAME defined as something else than the name of a real
 * module, or not at all.
 *
 * Any file that is compiled into maxscale-common should *not* have
 * MXB_MODULE_NAME defined.
 */
#if !defined(MXB_MODULE_NAME)
#define MXB_MODULE_NAME NULL
#endif

typedef enum
{
    MXB_LOG_TARGET_DEFAULT,
    MXB_LOG_TARGET_FS,     // File system
    MXB_LOG_TARGET_STDOUT, // Standard output
} mxb_log_target_t;

/**
* Thread-specific logging information.
*/
typedef struct mxb_log_info
{
    size_t li_sesid;
    int    li_enabled_priorities;
} mxb_log_info_t;

extern int mxb_log_enabled_priorities;

/**
 * Check if specified log type is enabled in general or if it is enabled
 * for the current session.
 *
 * @param priority One of the syslog LOG_ERR, LOG_WARNING, etc. constants.
 */
#define MXB_LOG_PRIORITY_IS_ENABLED(priority) \
    ((mxb_log_enabled_priorities & (1 << priority)) ? true : false)

/**
 * LOG_AUGMENT_WITH_FUNCTION Each logged line is suffixed with [function-name].
 */
typedef enum
{
    MXB_LOG_AUGMENT_WITH_FUNCTION = 1,
    MXB_LOG_AUGMENTATION_MASK     = (MXB_LOG_AUGMENT_WITH_FUNCTION)
} mxb_log_augmentation_t;

typedef struct mxb_log_throttling
{
    size_t count;       // Maximum number of a specific message...
    size_t window_ms;   // ...during this many milliseconds.
    size_t suppress_ms; // If exceeded, suppress such messages for this many ms.
} MXB_LOG_THROTTLING;

bool mxb_log_init(const char* ident,
                  const char* logdir,
                  const char* filename,
                  mxb_log_target_t target,
                  size_t (*get_context)(char*, size_t));
void mxb_log_finish(void);
bool mxb_log_rotate();
int  mxb_log_set_priority_enabled(int priority, bool enabled);
void mxb_log_set_syslog_enabled(bool enabled);
void mxb_log_set_maxlog_enabled(bool enabled);
void mxb_log_set_highprecision_enabled(bool enabled);
void mxb_log_set_augmentation(int bits);
void mxb_log_set_throttling(const MXB_LOG_THROTTLING* throttling);

void mxb_log_get_throttling(MXB_LOG_THROTTLING* throttling);

static inline bool mxb_log_priority_is_enabled(int priority)
{
    assert((priority & ~LOG_PRIMASK) == 0);
    return MXB_LOG_PRIORITY_IS_ENABLED(priority) || priority == LOG_ALERT;
}

int mxb_log_message(int priority,
                    const char* modname,
                    const char* file, int line, const char* function,
                    const char* format, ...) mxb_attribute((format(printf, 6, 7)));
/**
 * Log an error, warning, notice, info, or debug  message.
 *
 * @param priority One of the syslog constants (LOG_ERR, LOG_WARNING, ...)
 * @param format   The printf format of the message.
 * @param ...      Arguments, depending on the format.
 *
 * NOTE: Should typically not be called directly. Use some of the
 *       MXB_ERROR, MXB_WARNING, etc. macros instead.
 */
#define MXB_LOG_MESSAGE(priority, format, ...)\
    (mxb_log_priority_is_enabled(priority) ? \
     mxb_log_message(priority, MXB_MODULE_NAME, __FILE__, __LINE__, __func__, format, ##__VA_ARGS__) :\
     0)

/**
 * Log an alert, error, warning, notice, info, or debug  message.
 *
 * MXB_ALERT   Not throttled  To be used when the system is about to go down in flames.
 * MXB_ERROR   Throttled      For errors.
 * MXB_WARNING Throttled      For warnings.
 * MXB_NOTICE  Not Throttled  For messages deemed important, typically used during startup.
 * MXB_INFO    Not Throttled  For information thought to be of value for investigating some problem.
 * MXB_DEBUG   Not Throttled  For debugging messages during development. Should be removed when a
 *                            feature is ready.
 *
 * @param format The printf format of the message.
 * @param ...    Arguments, depending on the format.
 */
#define MXB_ALERT(format, ...)   MXB_LOG_MESSAGE(LOG_ALERT,   format, ##__VA_ARGS__)
#define MXB_ERROR(format, ...)   MXB_LOG_MESSAGE(LOG_ERR,     format, ##__VA_ARGS__)
#define MXB_WARNING(format, ...) MXB_LOG_MESSAGE(LOG_WARNING, format, ##__VA_ARGS__)
#define MXB_NOTICE(format, ...)  MXB_LOG_MESSAGE(LOG_NOTICE,  format, ##__VA_ARGS__)
#define MXB_INFO(format, ...)    MXB_LOG_MESSAGE(LOG_INFO,    format, ##__VA_ARGS__)

#if defined(SS_DEBUG)
#define MXB_DEBUG(format, ...)   MXB_LOG_MESSAGE(LOG_DEBUG,   format, ##__VA_ARGS__)
#else
#define MXB_DEBUG(format, ...)
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
#define MXB_OOM_MESSAGE(message) MXB_ERROR("OOM: %s", message);

/**
 * Log an out of memory error using custom message, if the
 * provided pointer is NULL.
 *
 * @param p If NULL, an OOM message will be logged.
 * @param message Text to be logged.
 */
#define MXB_OOM_MESSAGE_IFNULL(p, m) do { if (!p) { MXB_OOM_MESSAGE(m); } } while (false)

/**
 * Log an out of memory error using a default message.
 */
#define MXB_OOM() MXB_OOM_MESSAGE(__func__)

/**
 * Log an out of memory error using a default message, if the
 * provided pointer is NULL.
 *
 * @param p If NULL, an OOM message will be logged.
 */
#define MXB_OOM_IFNULL(p) do { if (!p) { MXB_OOM(); } } while (false)

enum
{
    MXB_OOM_MESSAGE_MAXLEN = 80 /** Maximum length of an OOM message, including the
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
 *            mxb_strerror(EINVAL), mxb_strerror(EACCESS));
 */
const char* mxb_strerror(int error);

MXB_END_DECLS
