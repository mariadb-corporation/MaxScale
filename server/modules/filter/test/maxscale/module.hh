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
     * @return The module object, if the module could be loaded, otherwise NULL.
     */
    static void* load(const char *zFile_name, const char *zType_name);

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
};

/**
 * The template Module is intended to be derived from using the derived
 * class as template argument.
 *
 *    class XyzModule : public SpecificModule<XyzModule> { ... }
 *
 * @param zFile_name  The name of the module.
 *
 * @return A module instance if the module could be loaded and it was of
 *         the expected type.
 */
template<class T>
class SpecificModule : public Module
{
public:
    static std::auto_ptr<T> load(const char* zFile_name)
    {
        std::auto_ptr<T> sT;

        void* pApi = Module::load(zFile_name, T::zName);

        if (pApi)
        {
            sT.reset(new T(static_cast<typename T::type_t*>(pApi)));
        }

        return sT;
    }

protected:
    SpecificModule()
    {
    }
};

}
