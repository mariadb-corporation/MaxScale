/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

namespace maxbase
{
namespace ini
{
// This should match the type expected by inih. The type can change depending on compilation settings
// so best define it here and hide the library type.
using IniHandler = int (*)(void* userdata, const char* section, const char* name, const char* value);

/**
 * Calls ini_parse.
 */
int ini_parse(const char* filename, IniHandler handler, void* userdata);
}
}
