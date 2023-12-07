/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

#include <maxbase/shareddata.hh>
#include <iostream>
#include <iomanip>

namespace maxbase
{

CachelineAtomic<int64_t> num_collector_updates {0};
CachelineAtomic<int64_t> num_collector_copies {0};
CachelineAtomic<int64_t> num_shareddata_collector_blocks {0};
CachelineAtomic<int64_t> num_shareddata_worker_blocks {0};
// this one rightfully belongs in collector.hh/cc, but there is no cc
CachelineAtomic<int64_t> num_collector_cap_waits {0};

std::string get_collector_stats()
{
    std::ostringstream os;
    os << "num_collector_updates           = " << num_collector_updates << std::endl;
    os << "num_collector_copies            = " << num_collector_copies << std::endl;
    os << "num_shareddata_collector_blocks = " << num_shareddata_collector_blocks << std::endl;
    os << "num_shareddata_worker_blocks    = " << num_shareddata_worker_blocks << std::endl;
    os << "num_collector_cap_waits         = " << num_collector_cap_waits << std::endl;

    return os.str();
}
}
