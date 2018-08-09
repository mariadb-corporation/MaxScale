#pragma once
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

#include <maxscale/ccdefs.hh>
#include <memory>
#include <maxscale/query_classifier.h>
#include "module.hh"

namespace maxscale
{

/**
 * A QueryClassfierModule instance is an abstraction for a query
 * classifier module.
 */
class QueryClassifierModule : public SpecificModule<QueryClassifierModule, QUERY_CLASSIFIER>
{
    QueryClassifierModule(const QueryClassifierModule&);
    QueryClassifierModule& operator = (const QueryClassifierModule&);

public:
    static const char* zName;  /*< The name describing the module type. */

private:
    QueryClassifierModule(const MXS_MODULE* pModule)
        : Base(pModule)
    {
    }
};

}
