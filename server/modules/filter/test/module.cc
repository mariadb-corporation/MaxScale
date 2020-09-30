/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/module.hh"
#include <maxbase/alloc.h>
#include <string>
#include "../../../core/internal/modules.hh"

using std::auto_ptr;

namespace maxscale
{

mxs::ConfigParameters* Module::create_default_parameters() const
{
    mxs::ConfigParameters* rval = new mxs::ConfigParameters;
    const MXS_MODULE_PARAM* param_definition = m_module.parameters;
    while (param_definition->name)
    {
        if (param_definition->default_value)
        {
            rval->set(param_definition->name, param_definition->default_value);
        }
        ++param_definition;
    }
    return rval;
}

//
// Module
//

// static
const MXS_MODULE* Module::load(const char* zName, mxs::ModuleType type)
{
    return get_module(zName, type);
}

// static
const MXS_MODULE* Module::get(const char* zName, mxs::ModuleType type)
{
    return get_module(zName, type);
}

// static
bool Module::process_init()
{
    return modules_process_init();
}

// static
void Module::process_finish()
{
    modules_process_finish();
}

// static
bool Module::thread_init()
{
    return modules_thread_init();
}

// static
void Module::thread_finish()
{
    modules_thread_finish();
}
}
