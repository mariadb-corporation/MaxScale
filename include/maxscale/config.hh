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

#include <maxscale/config.h>

namespace maxscale
{

// Helper class for allocating temporary configuration parameters
class ParamList
{
public:
    ParamList(const ParamList&) = delete;
    ParamList& operator=(const ParamList&) = delete;

    ParamList(std::initializer_list<std::pair<const char*, const char*>> list,
              const MXS_MODULE_PARAM* module_params = NULL);

    ~ParamList();

    MXS_CONFIG_PARAMETER* params();

private:
    CONFIG_CONTEXT m_ctx = {(char*)""};
};

}
