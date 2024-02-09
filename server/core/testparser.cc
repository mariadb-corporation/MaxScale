/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/testparser.hh>
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include <sstream>

using namespace std;

namespace
{

std::unique_ptr<mxs::Parser> create_parser(const mxs::Parser::Helper* pHelper,
                                           const string& plugin,
                                           mxs::Parser::SqlMode sql_mode)
{
    mxs::ParserPlugin* pPlugin = mxs::ParserPlugin::load(plugin.c_str());

    if (!pPlugin)
    {
        ostringstream ss;
        ss << "Could not load parser plugin '" << plugin << "'.";

        throw std::runtime_error(ss.str());
    }

    if (!pPlugin->setup(sql_mode))
    {
        mxs::ParserPlugin::unload(pPlugin);
        ostringstream ss;

        ss << "Could not setup parser plugin '" << plugin << "'.";

        throw std::runtime_error(ss.str());
    }

    if (!pPlugin->thread_init())
    {
        mxs::ParserPlugin::unload(pPlugin);
        ostringstream ss;

        ss << "Could not perform thread initialization for parser plugin '" << plugin << "'.";

        throw std::runtime_error(ss.str());
    }

    mxs::CachingParser::thread_init();

    return pPlugin->create_parser(pHelper);
}

}

namespace maxscale
{

TestParser::TestParser()
    : TestParser(&MariaDBParser::Helper::get(), DEFAULT_PLUGIN, SqlMode::DEFAULT)
{
}

TestParser::TestParser(const Parser::Helper* pHelper,
                       const string& plugin,
                       SqlMode sql_mode)
    : CachingParser(create_parser(pHelper, plugin, sql_mode))
{
}

TestParser::~TestParser()
{
    m_sParser->plugin().thread_end();
    mxs::CachingParser::thread_finish();
}

}
