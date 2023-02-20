/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <memory>
#include <maxscale/parser.hh>
#include "module.hh"

namespace maxscale
{

/**
 * A QueryClassifierModule instance is an abstraction for a query
 * classifier module.
 */
class QueryClassifierModule : public SpecificModule<QueryClassifierModule, mxs::ParserPlugin>
{
    QueryClassifierModule(const QueryClassifierModule&);
    QueryClassifierModule& operator=(const QueryClassifierModule&);

public:
    static const char* zName;   /*< The name describing the module type. */

private:
    QueryClassifierModule(const MXS_MODULE* pModule)
        : Base(pModule)
    {
    }
};
}
