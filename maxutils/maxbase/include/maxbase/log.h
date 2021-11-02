/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

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
#if !defined (MXB_MODULE_NAME)
#define MXB_MODULE_NAME NULL
#endif

extern int mxb_log_enabled_priorities;

typedef enum mxb_log_augmentation_t
{
    MXB_LOG_AUGMENT_WITH_FUNCTION = 1,      // Each logged line is suffixed with [function-name]
    MXB_LOG_AUGMENTATION_MASK     = (MXB_LOG_AUGMENT_WITH_FUNCTION)
} mxb_log_augmentation_t;

typedef struct MXB_LOG_THROTTLING
{
    size_t count;       // Maximum number of a specific message...
    size_t window_ms;   // ...during this many milliseconds.
    size_t suppress_ms; // If exceeded, suppress such messages for this many ms.
} MXB_LOG_THROTTLING;

/**
 * @brief Has the log been initialized.
 *
 * @return True if the log has been initialized, false otherwise.
 */
bool mxb_log_inited();
/**
 * Rotate the log
 *
 * @return True if the rotating was successful
 */
bool mxb_log_rotate();

/**
 * Get log filename
 *
 * @return The current filename.
 *
 * @attention This function can be called only after @c mxs_log_init() has
 *            been called and @c mxs_log_finish() has not been called. The
 *            returned filename stays valid only until @c mxs_log_finish()
 *            is called.
 */
const char* mxb_log_get_filename();

/**
 * Enable/disable a particular syslog priority.
 *
 * @param priority  One of the LOG_ERR etc. constants from sys/syslog.h.
 * @param enabled   True if the priority should be enabled, false if it should be disabled.
 *
 * @return True if the priority was valid, false otherwise.
 */
bool mxb_log_set_priority_enabled(int priority, bool enabled);

bool mxb_log_get_session_trace();

/**
 * Convert log level to string
 */
const char* mxb_log_level_to_string(int level);

/**
 * Query whether a particular syslog priority is enabled.
 *
 * Don't use this to check whether a message at a particular level should be logged,
 * use @c mxb_log_should_log instead.
 *
 * @param priority  One of the LOG_ERR etc. constants from sys/syslog.h.
 *
 * @return True if enabled, false otherwise.
 */
static inline bool mxb_log_is_priority_enabled(int priority)
{
    assert((priority & ~LOG_PRIMASK) == 0);
    return ((mxb_log_enabled_priorities & (1 << priority)) != 0) || (priority == LOG_ALERT);
}

/**
 * Enable/disable syslog logging.
 *
 * @param enabled True, if syslog logging should be enabled, false if it should be disabled.
 */
void mxb_log_set_syslog_enabled(bool enabled);

/**
 * Is syslog logging enabled.
 *
 * @return True if enabled, false otherwise.
 */
bool mxb_log_is_syslog_enabled();

/**
 * Enable/disable maxscale log logging.
 *
 * @param enabled True, if maxlog logging should be enabled, false if it should be disabled.
 */
void mxb_log_set_maxlog_enabled(bool enabled);

/**
 * Is maxlog logging enabled.
 *
 * @return True if enabled, false otherwise.
 */
bool mxb_log_is_maxlog_enabled();

/**
 * Enable/disable highprecision logging.
 *
 * @param enabled True, if high precision logging should be enabled, false if it should be disabled.
 */
void mxb_log_set_highprecision_enabled(bool enabled);

/**
 * Is highprecision logging enabled.
 *
 * @return True if enabled, false otherwise.
 */
bool mxb_log_is_highprecision_enabled();

/**
 * Set the augmentation
 *
 * @param bits  Combination of @c mxb_log_augmentation_t values.
 */
void mxb_log_set_augmentation(int bits);

/**
 * Set the log throttling parameters.
 *
 * @param throttling The throttling parameters.
 */
void mxb_log_set_throttling(const MXB_LOG_THROTTLING* throttling);

/**
 * Get the log throttling parameters.
 *
 * @param throttling The throttling parameters.
 */
void mxb_log_get_throttling(MXB_LOG_THROTTLING* throttling);

/**
 * Redirect  stdout to the log file
 *
 * @param redirect Whether to redirect the output to the log file
 */
void mxs_log_redirect_stdout(bool redirect);

/**
 * Set session specific in-memory log
 *
 * @param enabled True or false to enable or disable session in-memory logging
 */
void mxb_log_set_session_trace(bool enabled);


/**
 * Log a message of a particular priority.
 *
 * @param priority One of the syslog constants: LOG_ERR, LOG_WARNING, ...
 * @param modname  The name of the module.
 * @param file     The name of the file where the message was logged.
 * @param line     The line where the message was logged.
 * @param function The function where the message was logged.
 * @param format   The printf format of the following arguments.
 * @param ...      Optional arguments according to the format.
 *
 * @return 0 for success, non-zero otherwise.
 */
int mxb_log_message(int priority,
                    const char* modname,
                    const char* file,
                    int line,
                    const char* function,
                    const char* format,
                    ...) mxb_attribute((format(printf, 6, 7)));

/**
 * Log an Out-Of-Memory message.
 *
 * @param message  The message to be logged.
 *
 * @attention The literal string should have a trailing "\n".
 *
 * @return 0 for success, non-zero otherwise.
 */
int mxb_log_oom(const char* message);

/**
 * Check if a message at this priority should be logged in the current context
 *
 * This function takes the current context (i.e. the session) into consideration when
 * inspecting whether the message should be logged.
 *
 * @param priority The log priority of the message
 *
 * @return True if the message should be logged
 */
bool mxb_log_should_log(int priority);

/**
 * Log an error, warning, notice, info, or debug  message.
 *
 * @param priority One of the syslog constants (LOG_ERR, LOG_WARNING, ...)
 * @param format   The printf format of the message.
 * @param ...      Arguments, depending on the format.
 *
 * @return 0 for success, non-zero otherwise.
 *
 * @attention Should typically not be called directly. Use some of the
 *            MXB_ERROR, MXB_WARNING, etc. macros instead.
 */
#define MXB_LOG_MESSAGE(priority, format, ...) \
    (mxb_log_should_log(priority)  \
     ? mxb_log_message(priority, MXB_MODULE_NAME, __FILE__, __LINE__, __func__, format, ##__VA_ARGS__)  \
     : 0)

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
 *
 * @return 0 for success, non-zero otherwise.
 */
#define MXB_ALERT(format, ...)   MXB_LOG_MESSAGE(LOG_ALERT, format, ##__VA_ARGS__)
#define MXB_ERROR(format, ...)   MXB_LOG_MESSAGE(LOG_ERR, format, ##__VA_ARGS__)
#define MXB_WARNING(format, ...) MXB_LOG_MESSAGE(LOG_WARNING, format, ##__VA_ARGS__)
#define MXB_NOTICE(format, ...)  MXB_LOG_MESSAGE(LOG_NOTICE, format, ##__VA_ARGS__)
#define MXB_INFO(format, ...)    MXB_LOG_MESSAGE(LOG_INFO, format, ##__VA_ARGS__)

#if defined (SS_DEBUG)
#define MXB_DEBUG(format, ...) MXB_LOG_MESSAGE(LOG_DEBUG, format, ##__VA_ARGS__)
#else
#define MXB_DEBUG(format, ...)
#endif

/**
 * Log an out of memory error using custom message.
 *
 * @param message  Text to be logged. Must be a literal string.
 *
 * @return 0 for success, non-zero otherwise.
 */
#define MXB_OOM_MESSAGE(message) mxb_log_oom("OOM: " message "\n")

#define MXB_OOM_FROM_STRINGIZED_MACRO(macro) MXB_OOM_MESSAGE(#macro)

/**
 * Log an out of memory error using a default message.
 *
 * @return 0 for success, non-zero otherwise.
 */
#define MXB_OOM() MXB_OOM_FROM_STRINGIZED_MACRO(__func__)

/**
 * Log an out of memory error using a default message, if the
 * provided pointer is NULL.
 *
 * @param p  If NULL, an OOM message will be logged.
 *
 * @return 0 for success, non-zero otherwise.
 */
#define MXB_OOM_IFNULL(p) do {if (!p) {MXB_OOM();}} while (false)

/**
 * Log an out of memory error using custom message, if the
 * provided pointer is NULL.
 *
 * @param p        If NULL, an OOM message will be logged.
 * @param message  Text to be logged. Must be literal string.
 *
 * @return 0 for success, non-zero otherwise.
 */
#define MXB_OOM_MESSAGE_IFNULL(p, message) do {if (!p) {MXB_OOM_MESSAGE(message);}} while (false)

MXB_END_DECLS
