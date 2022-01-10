/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxbase/ccdefs.hh>

#include <cstdint>
#include <string>

namespace maxbase
{

/**
 * Format parameters to a string. Uses printf-formatting.
 *
 * @param format Format string
 * @param ... Items to convert according to format string
 * @return The result string
 */
std::string string_printf(const char* format, ...) mxb_attribute((format (printf, 1, 2)));

std::string string_vprintf(const char *format, va_list args);
}
