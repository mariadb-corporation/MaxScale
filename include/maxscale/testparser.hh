/*
 * Copyright (c) 2023 MariaDB plc
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
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/cachingparser.hh>

namespace maxscale
{

/**
 * @class TestParser
 *
 * A instantiable parser convenience class, intended to be used in testing.
 * It loads the plugin, initializes it and the CachingParser as well. Any
 * errors during the setup are reported using std::runtime_error exceptions.
 */
class TestParser : public CachingParser
{
public:
    static constexpr const char* DEFAULT_PLUGIN = "pp_sqlite";

    TestParser(const TestParser&) = delete;
    TestParser& operator=(const TestParser&) = delete;

    TestParser();

    TestParser(const Parser::Helper* pHelper, const std::string& plugin)
        : TestParser(pHelper, plugin, SqlMode::DEFAULT)
    {
    }

    TestParser(const Parser::Helper* pHelper, const std::string& plugin, SqlMode sql_mode);

    TestParser(const Parser::Helper* pHelper,
               const std::string& plugin,
               SqlMode sql_mode,
               const std::string& plugin_args);
    ~TestParser();
};

}
