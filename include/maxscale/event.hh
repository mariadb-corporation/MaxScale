/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
 #pragma once

#include <maxscale/ccdefs.hh>
#include <syslog.h>
#include <string>

namespace maxscale
{

/**
 * @brief Convert a syslog level into a string
 *
 * @param level  One of LOG_WARNING, LOG_ERR, ...
 *
 * @return The corresponding string; "LOG_WARNING", "LOG_ERR", ...
 */
const char* log_level_to_string(int32_t level);

/**
 * @brief Convert a string to a syslog level
 *
 * @param pLevel  [out] A syslog level.
 * @param zValue  A syslog value as string; "LOG_WARNING", "LOG_ERR", ...
 *
 * @return True, if the string corresponds to a syslog level, false otherwise.
 */
bool log_level_from_string(int32_t* pLevel, const char* zValue);

/**
 * @brief Convert a syslog facility into a string
 *
 * @param level  One of LOG_USER, LOG_LOCAL0, ...
 *
 * @return The corresponding string; "LOG_USER", "LOG_LOCAL0", ...
 */
const char* log_facility_to_string(int32_t facility);

/**
 * @brief Convert a string to a syslog facility
 *
 * @param pFacility  [out] A syslog facility.
 * @param zValue     A syslog facility as string; "LOG_USER", "LOG_LOCAL0", ...
 *
 * @return True, if the string corresponds to a syslog facility, false otherwise.
 */
bool log_facility_from_string(int32_t* pFacility, const char* zValue);

namespace event
{

enum id_t
{
    AUTHENTICATION_FAILURE  /**< Authentication failure */
};

enum
{
    DEFAULT_FACILITY = LOG_USER,
    DEFAULT_LEVEL = LOG_WARNING
};

/**
 * @brief Convert an event id to a string.
 *
 * The string corresponding to an event id is the symbolic constant
 * converted to all lower-case.
 *
 * @param id  An event id.
 *
 * @return The corresponding string.
 */
const char* to_string(id_t id);

/**
 * @brief Convert a string to an event id
 *
 * @param pId     [out] An event id.
 * @param zValue  An event id a string; "authentication_failure", ...
 *
 * @return True, if the string could be converted, false otherwise.
 */
bool from_string(id_t* pId, const char* zValue);
inline bool from_string(id_t* pId, const std::string& value)
{
    return from_string(pId, value.c_str());
}

/**
 * @brief Set the syslog facility of an event.
 *
 * @param id        The id of the event.
 * @param facility  One of LOG_USER, LOG_LOCAL0, ...
 *
 * @note If @c facility contains other bits than facility bits, they
 *       will silently be ignored.
 */
void set_log_facility(id_t id, int32_t facility);

/**
 * @brief Get the syslog facility of an event.
 *
 * @param id  An event id.
 *
 * @return The facility of the event.
 */
int32_t get_log_facility(id_t id);

/**
 * @brief Set the syslog level of an event.
 *
 * @param id     The id of the event.
 * @param level  One of LOG_WARNING, LOG_ERR, ...
 *
 * @note If @c level contains other bits than level bits, they
 *       will silently be ignored.
 */
void set_log_level(id_t id, int32_t level);

/**
 * @brief Get the syslog level of an event.
 *
 * @param id  An event id.
 *
 * @return The level of the event.
 */
int32_t get_log_level(id_t id);


/**
 * @brief Log an event.
 *
 * Usually this function should not be used, but the macro
 * @c MXS_LOG_EVENT should be used in its stead.
 *
 * @param event_id  The id of the event.
 * @param modname   The module where the event is logged.
 * @param file      The file where the event is logged.
 * @param line      The line where the event is logged.
 * @param function  The function where the event is logged.
 * @param format    Printf formatting string.
 * @param ...       Formatting string specific additional arguments.
 *
 */
void log(id_t event_id,
         const char* modname,
         const char* file, int line, const char* function,
         const char* format, ...) mxs_attribute((format(printf, 6, 7)));

}

}

/**
 * @brief Log an event.
 *
 * @param event_id  The id of the event.
 * @param format    Printf formatting string.
 * @param ...       Formatting string specific additional arguments.
 *
 */
#define MXS_LOG_EVENT(event_id, format, ...)\
    maxscale::event::log(event_id, MXS_MODULE_NAME, __FILE__, __LINE__, __func__, format, ##__VA_ARGS__)
