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

#include "maxscale/module.hh"
#include <string>
#include "../../../core/internal/modules.h"

using std::auto_ptr;

namespace maxscale
{

//
// Module::ConfigParameters
//

Module::ConfigParameters::ConfigParameters(const MXS_MODULE_PARAM* pParams)
{
    this->name = NULL;
    this->value = NULL;
    this->next = NULL;

    MXS_CONFIG_PARAMETER* pCurrent = this;

    while (pParams->name)
    {
        if (pParams->name && pParams->default_value)
        {
            if (this->name == NULL)
            {
                this->name = const_cast<char*>(pParams->name);
                this->value = const_cast<char*>(pParams->default_value);
            }
            else
            {
                MXS_CONFIG_PARAMETER* pNext = new MXS_CONFIG_PARAMETER;
                pNext->name = const_cast<char*>(pParams->name);
                pNext->value = const_cast<char*>(pParams->default_value);
                pNext->next = NULL;

                pCurrent->next = pNext;
                pCurrent = pNext;
            }
        }

        ++pParams;
    }
}

Module::ConfigParameters::~ConfigParameters()
{
    MXS_CONFIG_PARAMETER* pNext = this->next;

    while (pNext)
    {
        MXS_CONFIG_PARAMETER* pCurrent = pNext;
        pNext = pNext->next;

        delete pCurrent;
    }
}

const char* Module::ConfigParameters::get(const char* zName) const
{
    const MXS_CONFIG_PARAMETER* pParam = get_param(zName);

    return pParam ? pParam->value : NULL;
}

void Module::ConfigParameters::set_value(const char* zName, const char* zValue)
{
    set_value(zName, std::string(zValue));
}

void Module::ConfigParameters::set_value(const char* zName, const std::string& value)
{
    MXS_CONFIG_PARAMETER* pParam = get_param(zName);

    if (!pParam)
    {
        MXS_CONFIG_PARAMETER* pTail = get_tail();

        pParam = new MXS_CONFIG_PARAMETER;
        m_values.push_back(zName);
        pParam->name = const_cast<char*>(m_values.back().c_str());
        pParam->value = NULL;
        pParam->next = NULL;

        pTail->next = pParam;
    }

    m_values.push_back(value);

    pParam->value = const_cast<char*>(m_values.back().c_str());
}

const MXS_CONFIG_PARAMETER* Module::ConfigParameters::get_param(const char* zName) const
{
    return const_cast<Module::ConfigParameters*>(this)->get_param(zName);
}

MXS_CONFIG_PARAMETER* Module::ConfigParameters::get_param(const char* zName)
{
    MXS_CONFIG_PARAMETER* pParam = NULL;

    if (this->name && (strcmp(this->name, zName) == 0))
    {
        pParam = this;
    }
    else
    {
        pParam = this->next;

        while (pParam && (strcmp(pParam->name, zName) != 0))
        {
            pParam = pParam->next;
        }
    }

    return pParam;
}

MXS_CONFIG_PARAMETER* Module::ConfigParameters::get_tail()
{
    MXS_CONFIG_PARAMETER* pTail = this;

    while (pTail->next)
    {
        pTail = pTail->next;
    }

    return pTail;
}

auto_ptr<Module::ConfigParameters> Module::create_default_parameters() const
{
    return auto_ptr<ConfigParameters>(new ConfigParameters(m_module.parameters));
}

//
// Module
//

//static
void* Module::load(const char* zName, const char* zType)
{
    return load_module(zName, zType);
}

//static
const MXS_MODULE* Module::get(const char* zName, const char* zType)
{
    return get_module(zName, zType);
}

//static
bool Module::process_init()
{
    bool initialized = false;

    MXS_MODULE_ITERATOR i = mxs_module_iterator_get(NULL);
    MXS_MODULE* module = NULL;

    while ((module = mxs_module_iterator_get_next(&i)) != NULL)
    {
        if (module->process_init)
        {
            int rc = (module->process_init)();

            if (rc != 0)
            {
                break;
            }
        }
    }

    if (module)
    {
        // If module is non-NULL it means that the initialization failed for
        // that module. We now need to call finish on all modules that were
        // successfully initialized.
        MXS_MODULE* failed_module = module;
        i = mxs_module_iterator_get(NULL);

        while ((module = mxs_module_iterator_get_next(&i)) != failed_module)
        {
            if (module->process_finish)
            {
                (module->process_finish)();
            }
        }
    }
    else
    {
        initialized = true;
    }

    return initialized;
}

//static
void Module::process_finish()
{
    MXS_MODULE_ITERATOR i = mxs_module_iterator_get(NULL);
    MXS_MODULE* module = NULL;

    while ((module = mxs_module_iterator_get_next(&i)) != NULL)
    {
        if (module->process_finish)
        {
            (module->process_finish)();
        }
    }
}

//static
bool Module::thread_init()
{
    bool initialized = false;

    MXS_MODULE_ITERATOR i = mxs_module_iterator_get(NULL);
    MXS_MODULE* module = NULL;

    while ((module = mxs_module_iterator_get_next(&i)) != NULL)
    {
        if (module->thread_init)
        {
            int rc = (module->thread_init)();

            if (rc != 0)
            {
                break;
            }
        }
    }

    if (module)
    {
        // If module is non-NULL it means that the initialization failed for
        // that module. We now need to call finish on all modules that were
        // successfully initialized.
        MXS_MODULE* failed_module = module;
        i = mxs_module_iterator_get(NULL);

        while ((module = mxs_module_iterator_get_next(&i)) != failed_module)
        {
            if (module->thread_finish)
            {
                (module->thread_finish)();
            }
        }
    }
    else
    {
        initialized = true;
    }

    return initialized;
}

//static
void Module::thread_finish()
{
    MXS_MODULE_ITERATOR i = mxs_module_iterator_get(NULL);
    MXS_MODULE* module = NULL;

    while ((module = mxs_module_iterator_get_next(&i)) != NULL)
    {
        if (module->thread_finish)
        {
            (module->thread_finish)();
        }
    }
}

}
