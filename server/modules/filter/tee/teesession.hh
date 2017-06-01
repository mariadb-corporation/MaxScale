#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>

#include <maxscale/filter.hh>

#include "local_client.hh"

class Tee;

/**
 * A Tee session
 */
class TeeSession: public mxs::FilterSession
{
    TeeSession(const TeeSession&);
    const TeeSession& operator=(const TeeSession&);

public:
    ~TeeSession();
    static TeeSession* create(Tee* my_instance, MXS_SESSION* session);

    void    close();
    int     routeQuery(GWBUF* pPacket);
    void    diagnostics(DCB *pDcb);
    json_t* diagnostics_json() const;

private:
    TeeSession(MXS_SESSION* session, LocalClient* client);
    LocalClient*   m_client;  /**< The client connection to the local service */
};
