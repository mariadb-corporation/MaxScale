/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/testparser.hh>
#include <sstream>
#include <maxscale/query_classifier.hh>

using namespace std;

namespace
{

QUERY_CLASSIFIER* load_parser(const string& plugin, qc_sql_mode_t sql_mode, const string& plugin_args)
{
    QUERY_CLASSIFIER* pClassifier = qc_load(plugin.c_str());

    if (!pClassifier)
    {
        ostringstream ss;
        ss << "Could not load parser plugin '" << plugin << "'.";

        throw std::runtime_error(ss.str());
    }

    if (pClassifier->setup(sql_mode, plugin_args.c_str()) != QC_RESULT_OK)
    {
        qc_unload(pClassifier);
        ostringstream ss;

        ss << "Could not setup parser plugin '" << plugin << "'.";

        throw std::runtime_error(ss.str());
    }

    if (pClassifier->thread_init() != QC_RESULT_OK)
    {
        qc_unload(pClassifier);
        ostringstream ss;

        ss << "Could not perform thread initialization for parser plugin '" << plugin << "'.";

        throw std::runtime_error(ss.str());
    }

    mxs::CachingParser::thread_init();

    return pClassifier;
}

}

namespace maxscale
{

TestParser::TestParser(const string& plugin, qc_sql_mode_t sql_mode, const string& plugin_args)
    : CachingParser(load_parser(plugin, sql_mode, plugin_args))
{
}

TestParser::~TestParser()
{
    m_classifier.thread_end();
    mxs::CachingParser::thread_finish();
}

}
