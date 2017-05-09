/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cache_storage_api.hh"
#include <ctype.h>
#include <sstream>

using std::string;
using std::stringstream;

std::string cache_key_to_string(const CACHE_KEY& key)
{
    stringstream ss;
    ss << key.data;

    return ss.str();
}
