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

class NullFilter;

class NullFilterSession : public maxscale::FilterSession
{
public:
    ~NullFilterSession();

    static NullFilterSession* create(MXS_SESSION* pSession, const NullFilter* pFilter);

private:
    NullFilterSession(MXS_SESSION* pSession, const NullFilter* pFilter);

    NullFilterSession(const NullFilterSession&);
    NullFilterSession& operator = (const NullFilterSession&);

private:
    const NullFilter& m_filter;
};
