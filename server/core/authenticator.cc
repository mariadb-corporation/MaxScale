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

#include <maxscale/authenticator.h>
#include <maxscale/modutil.h>
#include <maxscale/alloc.h>

#include "internal/modules.hh"
/**
 * @file authenticator.c - Authenticator module functions
 */

/**
 * @brief Initialize an authenticator module
 *
 * Process the options into an array and pass them to the authenticator
 * initialization function
 *
 * The authenticator must implement the @c initialize entry point if this
 * function is called. If the authenticator does not implement this, behavior is
 * undefined.
 *
 * @param func Authenticator entry point
 * @param options Authenticator options
 * @return Authenticator instance or NULL on error
 */
bool authenticator_init(void** dest, const char* authenticator, const char* options)
{
    bool rval = true;
    void* instance = NULL;
    MXS_AUTHENTICATOR* func = (MXS_AUTHENTICATOR*)load_module(authenticator, MODULE_AUTHENTICATOR);

    if (func == NULL)
    {
        rval = false;
    }
    else if (func->initialize)
    {
        char* optarray[AUTHENTICATOR_MAX_OPTIONS + 1];
        size_t optlen = options && *options ? strlen(options) : 0;
        char optcopy[optlen + 1];
        int optcount = 0;

        if (options && *options)
        {
            strcpy(optcopy, options);
            char* opt = optcopy;

            while (opt && optcount < AUTHENTICATOR_MAX_OPTIONS)
            {
                char* end = strnchr_esc(opt, ',', sizeof(optcopy) - (opt - optcopy));

                if (end)
                {
                    *end++ = '\0';
                }

                optarray[optcount++] = opt;
                opt = end;
            }
        }

        optarray[optcount] = NULL;

        if ((instance = func->initialize(optarray)) == NULL)
        {
            rval = false;
        }
    }

    *dest = instance;
    return rval;
}

/**
 * @brief Get the default authenticator for a protocol
 *
 * @param protocol Protocol to inspect
 * @return The default authenticator for the protocol or NULL if the protocol
 * does not provide one
 */
const char* get_default_authenticator(const char* protocol)
{
    char* rval = NULL;
    MXS_PROTOCOL* protofuncs = (MXS_PROTOCOL*)load_module(protocol, MODULE_PROTOCOL);

    if (protofuncs && protofuncs->auth_default)
    {
        rval = protofuncs->auth_default();
    }

    return rval;
}
