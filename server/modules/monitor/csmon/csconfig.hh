/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "csmon.hh"
#include <maxscale/config2.hh>

class CsConfig : public mxs::config::Configuration
{
public:
    CsConfig(const std::string& name);

    bool post_configure(const mxs::ConfigParameters&);

    static void populate(MXS_MODULE& info);

    SERVER*     pPrimary;
    mxb::Host   admin_host;

    std::string rest_base;
};
