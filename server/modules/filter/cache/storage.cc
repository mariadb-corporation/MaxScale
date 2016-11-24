/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "storage.h"
#include <dlfcn.h>
#include <sys/param.h>
#include <maxscale/alloc.h>
#include <maxscale/gwdirs.h>
#include <maxscale/log_manager.h>

CACHE_STORAGE_MODULE* cache_storage_open(const char *name)
{
    CACHE_STORAGE_MODULE* module = (CACHE_STORAGE_MODULE*)MXS_CALLOC(1, sizeof(CACHE_STORAGE_MODULE));

    if (module)
    {
        char path[MAXPATHLEN + 1];
        sprintf(path, "%s/lib%s.so", get_libdir(), name);

        void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);

        if (handle)
        {
            module->handle = handle;

            void *f = dlsym(module->handle, CACHE_STORAGE_ENTRY_POINT);

            if (f)
            {
                module->api = ((CacheGetStorageAPIFN)f)();

                if (module->api)
                {
                    if (!(module->api->initialize)())
                    {
                        MXS_ERROR("Initialization of %s failed.", path);

                        (void)dlclose(module->handle);
                        MXS_FREE(module);
                        module = NULL;
                    }
                }
                else
                {
                    MXS_ERROR("Could not obtain API object from %s.", name);

                    (void)dlclose(module->handle);
                    MXS_FREE(module);
                    module = NULL;
                }
            }
            else
            {
                const char* s = dlerror();
                MXS_ERROR("Could not look up symbol %s from %s: %s",
                          name, CACHE_STORAGE_ENTRY_POINT, s ? s : "");
                MXS_FREE(module);
                module = NULL;
            }
        }
        else
        {
            const char* s = dlerror();
            MXS_ERROR("Could not load %s: %s", name, s ? s : "");
            MXS_FREE(module);
            module = NULL;
        }
    }

    return module;
}


void cache_storage_close(CACHE_STORAGE_MODULE *module)
{
    if (module)
    {
        if (dlclose(module->handle) != 0)
        {
            const char *s = dlerror();
            MXS_ERROR("Could not close module %s: ", s ? s : "");
        }

        MXS_FREE(module);
    }
}
