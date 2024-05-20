/*
 * Copyright (c) 2025 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxscale/parser.hh>

mxs::ParserPlugin* load_plugin(const char* zName)
{
    bool loaded = false;
    size_t len = strlen(zName);
    char libdir[len + 3 + 1];   // Extra for ../

    sprintf(libdir, "../%s", zName);

    mxs::set_libdir(libdir);

    mxs::ParserPlugin* pPlugin = mxs::ParserPlugin::load(zName);

    if (!pPlugin)
    {
        std::cerr << "error: Could not load classifier " << zName << "." << std::endl;
    }

    return pPlugin;
}

mxs::ParserPlugin* get_plugin(const char* zName, mxs::Parser::SqlMode sql_mode, const char* zArgs)
{
    mxs::ParserPlugin* pPlugin = nullptr;

    if (zName)
    {
        pPlugin = load_plugin(zName);

        if (pPlugin)
        {
            setenv("PP_ARGS", zArgs, 1);

            if (!pPlugin->setup(sql_mode) || !pPlugin->thread_init())
            {
                std::cerr << "error: Could not setup or init classifier " << zName << "." << std::endl;
                mxs::ParserPlugin::unload(pPlugin);
                pPlugin = 0;
            }
        }
    }

    return pPlugin;
}

void put_plugin(mxs::ParserPlugin* pPlugin)
{
    if (pPlugin)
    {
        pPlugin->thread_end();
        mxs::ParserPlugin::unload(pPlugin);
    }
}

