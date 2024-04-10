/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * Common definitions for MariaDBMonitor module. Should be included in every header or source file.
 */

#define MXB_MODULE_NAME "mariadbmon"

#include <maxscale/ccdefs.hh>

#include <string>
#include <maxscale/json_api.hh>

/** Utility macros for printing both MXB_ERROR and json error */
#define PRINT_MXS_JSON_ERROR(err_out, format, ...) \
    do { \
        MXB_ERROR(format, ##__VA_ARGS__); \
        if (err_out) \
        { \
            *err_out = mxs_json_error_append(*err_out, format, ##__VA_ARGS__); \
        } \
    } while (false)

#define PRINT_ERROR_IF(log_mode, err_out, format, ...) \
    if (log_mode == Log::ON) \
    { \
        PRINT_JSON_ERROR(err_out, format, ##__VA_ARGS__); \
    } \

#define PRINT_JSON_ERROR(err_out, format, ...) \
    do { \
        MXB_ERROR(format, ##__VA_ARGS__); \
        mxs_json_error_append(err_out, format, ##__VA_ARGS__); \
    } while (false)

extern const int64_t GTID_DOMAIN_UNKNOWN;
constexpr int64_t CONN_ID_UNKNOWN = -1;     /** Default connection id */
extern const int PORT_UNKNOWN;
extern const char* const CN_HANDLE_EVENTS;
extern const char* SERVER_LOCK_NAME;
extern const char* MASTER_LOCK_NAME;
constexpr char CONFIG_SSH_USER[] = "ssh_user";
constexpr char CONFIG_SSH_KEYFILE[] = "ssh_keyfile";
constexpr char CONFIG_BACKUP_ADDR[] = "backup_storage_address";
constexpr char CONFIG_BACKUP_PATH[] = "backup_storage_path";

// Helper class for concatenating strings with a delimiter.
class DelimitedPrinter
{
public:
    DelimitedPrinter(const DelimitedPrinter&) = delete;
    DelimitedPrinter& operator=(const DelimitedPrinter&) = delete;
    DelimitedPrinter() = delete;
    explicit DelimitedPrinter(std::string separator);

    /**
     * Add to string.
     *
     * @param target String to modify
     * @param addition String to add. The delimiter is printed before the addition.
     */
    void cat(std::string& target, const std::string& addition);

    /**
     * Add to internal string.
     *
     * @param addition The string to add.
     */
    void cat(const std::string& addition);

    std::string message() const;

private:
    const std::string m_separator;
    std::string       m_current_separator;
    std::string       m_message;
};
