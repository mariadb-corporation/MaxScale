#pragma once

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

/**
 * Common definitions for MariaDBMonitor module. Should be included in every header or source file.
 */

#define MXS_MODULE_NAME "mariadbmon"

#include <maxscale/cppdefs.hh>

#include <string>
#include <maxscale/json_api.h>

/** Utility macro for printing both MXS_ERROR and json error */
#define PRINT_MXS_JSON_ERROR(err_out, format, ...)\
    do {\
       MXS_ERROR(format, ##__VA_ARGS__);\
       if (err_out)\
       {\
            *err_out = mxs_json_error_append(*err_out, format, ##__VA_ARGS__);\
       }\
    } while (false)

extern const int64_t SERVER_ID_UNKNOWN;
extern const int64_t GTID_DOMAIN_UNKNOWN;
extern const int PORT_UNKNOWN;

// Helper class for concatenating strings with a delimiter.
class DelimitedPrinter
{
private:
    DelimitedPrinter(const DelimitedPrinter&) = delete;
    DelimitedPrinter& operator = (const DelimitedPrinter&) = delete;
    DelimitedPrinter() = delete;
public:
    DelimitedPrinter(const std::string& separator);

    /**
     * Add to string.
     *
     * @param target String to modify
     * @param addition String to add. The delimiter is printed before the addition.
     */
    void cat(std::string& target, const std::string& addition);
private:
    const std::string m_separator;
    std::string m_current_separator;
};

enum class ClusterOperation
{
    SWITCHOVER,
    FAILOVER
};