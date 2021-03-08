/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <array>
#include <string>
#include <tuple>

namespace maxbase
{

enum class NumberType
{
    Byte,
    Regular
};

/** Split a number to {value, prefix} for making human readable output
 *
 * @param value The value to convert
 *
 * @param size_type Byte: divide by 1024, prefixes Kib, Mib etc.
 *                  Regular: divide/multiply by 1000, prefixes u, m, k, M, G etc.
 *
 * @return A pair {double, prefix}
 *         pretty_number_split(2000, SizeType::Byte) => {1.953125, "KiB"}
 *         pretty_number_split(3456, SizeType::Regular) => {3.456, "k"}
 */
std::pair<double, const char*> pretty_number_split(double value, NumberType size_type);

/** Pretty string from a size_t, e.g. pretty_size(2000) => "1.95KiB"
 *
 *  @param sz The size to convert
 *  @param separator The separator to put between the value and the prefix
 *  @return The formatted string
 */
std::string pretty_size(size_t sz, const char* separator = "");

/** Pretty string from a double
 *
 *  @param num The number to convert
 *  @param separator The separator to put between the value and the prefix
 *  @param suffix The suffix to append to the prefix
 *                pretty_number(1234, "", "g") => "1.23kg"
 *                pretty_number(-123456789) => "-123.46M"
 *                pretty_number(0.042666) => "42.67m"
 *  @return The formatted string
 */
std::string pretty_number(double num, const char* separator = "", const char* suffix = "");
}
