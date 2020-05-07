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
#include "columnstore.hh"

class CsConfig : public mxs::config::Configuration
{
public:
    CsConfig(const std::string& name);

    static void populate(MXS_MODULE& info);

    cs::Version version;
    SERVER*     pPrimary;        // Mandatory for 1.0, invalid for 1.2 and 1.5.
    int64_t     admin_port;      // Mandatory for 1.5, invalid for 1.0 and 1.2.
    std::string admin_base_path; // Mandatory for 1.5, invalid for 1.0 and 1.2.
    std::string api_key;         // Mandatory for 1.5, invalid for 1.0 and 1.2.
    std::string local_address;   // Mandatory for 1.5, invalid for 1.0 and 1.2.

private:
    bool post_configure();

    bool check_invalid();
    bool check_mandatory();
};
