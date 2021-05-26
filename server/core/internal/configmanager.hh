/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

namespace maxscale
{

/**
 * Check if a dynamic configuration exists
 *
 * @return True if a dynamic configuration exists
 */
bool have_dynamic_config();

/**
 * Save current configuration as JSON
 *
 * @return True if the configuration was saved successfully
 */
bool save_dynamic_config();

/**
 * Load dynamic configuration from JSON
 *
 * @return True if the configuration was applied successfully
 */
bool load_dynamic_config();
}
