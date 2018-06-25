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

#include <maxscale/cppdefs.hh>
#include <maxscale/session.h>

namespace maxscale
{
/**
 * Specialization of RegistryTraits for the session registry.
 */
template<>
struct RegistryTraits<MXS_SESSION>
{
    typedef uint64_t id_type;
    typedef MXS_SESSION* entry_type;

    static id_type get_id(entry_type entry)
    {
        return entry->ses_id;
    }
    static entry_type null_entry()
    {
        return NULL;
    }
};

}
