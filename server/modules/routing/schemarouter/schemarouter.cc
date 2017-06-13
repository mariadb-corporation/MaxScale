/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "schemarouter.hh"

namespace schemarouter
{

void SRBackend::set_mapped(bool value)
{
    m_mapped = value;
}

bool SRBackend::is_mapped() const
{
    return m_mapped;
}

}