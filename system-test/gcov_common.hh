/*
 * Copyright (c) 2024 MariaDB plc
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

#include <stdlib.h>
#include <string>

struct GcovConfig
{
    std::string branch;
    std::string repo;
    std::string cmake_flags;
    std::string build_root;
};

static inline GcovConfig gcov_config()
{
    auto env_or = [](const char* key, const char* default_val) {
        const char* val = getenv(key);
        return val ? val : default_val;
    };

    GcovConfig cnf;
    cnf.branch = env_or("MXS_BRANCH", "develop");
    cnf.repo = env_or("MXS_REPO", "https://github.com/mariadb-corporation/MaxScale");
    cnf.cmake_flags = env_or("MXS_CMAKE_FLAGS",
                             "-DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Debug -DGCOV=Y");
    cnf.build_root = env_or("MXS_BUILD_ROOT", "/opt/MaxScale-gcov/");

    return cnf;
}
