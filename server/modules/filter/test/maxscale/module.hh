#pragma once
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

#include <maxscale/cppdefs.hh>
#include <memory>

namespace maxscale
{

/**
 * The class Module is an abstraction for a MaxScale module, to
 * be used as the base class of a specific module.
 */
class Module
{
public:
    /**
     * Load a module with a specific name, assumed to be of a specific type.
     *
     * @param zFile_name  The name of the module.
     * @param zType_name  The expected type of the module.
     *
     * @return The module specific entry point structure or NULL.
     */
    static void* load(const char *zFile_name, const char *zType_name);

    /**
     * Get a module with a specific name, assumed to be of a specific type.
     *
     * @param zFile_name  The name of the module.
     * @param zType_name  The expected type of the module.
     *
     * @return The loaded module, if the module could be loaded, otherwise NULL.
     */
    static const MXS_MODULE* get(const char *zFile_name, const char *zType_name);

    /**
     * Perform process initialization of all modules. Should be called only
     * when all modules intended to be loaded have been loaded.
     *
     * @return True, if the process initialization succeeded.
     */
    static bool process_init();

    /**
     * Perform process finalization of all modules.
     */
    static void process_finish();

    /**
     * Perform thread initialization of all modules. Should be called only
     * when all modules intended to be loaded have been loaded.
     *
     * @return True, if the thread initialization could be performed.
     */
    static bool thread_init();

    /**
     * Perform thread finalization of all modules.
     */
    static void thread_finish();

protected:
    Module(const MXS_MODULE* pModule)
        : m_module(*pModule)
    {
    }

    const MXS_MODULE& m_module;
};

/**
 * The template Module is intended to be derived from using the derived
 * class as template argument.
 *
 *    class XyzModule : public SpecificModule<XyzModule, XYZ_MODULE_OBJECT> { ... }
 *
 * @param zFile_name  The name of the module.
 *
 * @return A module instance if the module could be loaded and it was of
 *         the expected type.
 */
template<class T, class API>
class SpecificModule : public Module
{
public:
    typedef SpecificModule<T, API> Base;

    static std::auto_ptr<T> load(const char* zFile_name)
    {
        std::auto_ptr<T> sT;

        const MXS_MODULE* pModule = Module::get(zFile_name, T::zName);

        if (pModule)
        {
            sT.reset(new T(pModule));
        }

        return sT;
    }

protected:
    SpecificModule(const MXS_MODULE* pModule)
        : Module(pModule)
        , m_pApi(static_cast<API*>(pModule->module_object))
    {
    }

    API* m_pApi;
};

}
