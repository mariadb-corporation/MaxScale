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

/**
 * @file modutil.hh C++ additions/alternatives for modutil.h functions
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/modutil.h>

#include <string>

namespace maxscale
{

std::string extract_sql(GWBUF* buffer, size_t len = -1);

std::string get_canonical(GWBUF* querybuf);

}
