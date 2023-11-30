/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <string>

namespace maxbase
{

enum class ReleaseSource
{
    LSB_RELEASE, // From /etc/lsb-release
    OS_RELEASE,  // From /etc/os-release
    ANY          // First /etc/os-release, then /etc/lsb-release
};

/**
 * Get the linux distribution info
 *
 * @param source Where the release should be looked for.
 *
 * @return If successful, the distribution, otherwise an empty string.
 */
std::string get_release_string(ReleaseSource source = ReleaseSource::ANY);

}
