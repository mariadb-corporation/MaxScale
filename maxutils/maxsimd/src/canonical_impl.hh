/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <string>
#include <vector>

#include "markers.hh"

/** The concrete implementations of get_canonical */
namespace maxsimd
{

/**
 * Helper function for detecting numbers with signs in front of them.
 *
 * @param it    Pointer to the start of the marker
 * @param start The beginning of the SQL string
 *
 * @return True if the number is signed and the sign can be safely omitted
 */
bool is_signed_number(const char* it, const char* start);

namespace generic
{
std::string* get_canonical_old(std::string* pSql, maxsimd::Markers* pMarkers);
void         make_markers(const std::string& sql, maxsimd::Markers* pMarkers);
}
}

namespace maxsimd
{
namespace simd256
{
void make_markers(const std::string& sql, maxsimd::Markers* pMarkers);
}
}
