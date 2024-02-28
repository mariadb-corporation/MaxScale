/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/config_state.hh>
#include "internal/config.hh"

namespace maxscale
{

void ConfigState::store_config_state()
{
    m_stored = get_config_state();
}

bool ConfigState::in_static_config_state() const
{
    return m_stored && m_stored == get_config_state();
}

mxb::Json ConfigState::get_config_state() const
{
    UnmaskPasswords unmask;
    return config_state();
}
}
