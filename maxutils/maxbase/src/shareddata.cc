/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/shareddata.hh>

namespace maxbase
{

CachelineAtomic<int64_t> num_updater_updates {0};
CachelineAtomic<int64_t> num_shareddata_updater_blocks {0};
CachelineAtomic<int64_t> num_shareddata_worker_blocks {0};
// this one rightfully belongs in gcupdater.hh/cc, but there is no cc
CachelineAtomic<int64_t> num_gcupdater_cap_waits {0};
}
