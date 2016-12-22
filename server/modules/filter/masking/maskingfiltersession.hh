#pragma once
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

#include <maxscale/cppdefs.hh>
#include <maxscale/filter.hh>

class MaskingFilterSession : public maxscale::FilterSession
{
public:
    ~MaskingFilterSession();

    static MaskingFilterSession* create(SESSION* pSession);

private:
    MaskingFilterSession(SESSION* pSession);

    MaskingFilterSession(const MaskingFilterSession&);
    MaskingFilterSession& operator = (const MaskingFilterSession&);
};
