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

#include "mock.hh"
#include <maxscale/dcb.h>
#include <maxscale/session.h>

namespace maxscale
{

namespace mock
{

/**
 * The class Dcb provides a mock DCB that can be used when testing.
 */
class Dcb : public DCB
{
    Dcb(const Dcb&);
    Dcb& operator = (const Dcb&);

public:
    class Handler
    {
    public:
        virtual int32_t write(GWBUF* pBuffer) = 0;
    };

    /**
     * Constructor
     *
     * @param pSession  The session object of the DCB.
     * @param zUser     The client of the connection.
     * @param zHost     The host of the connection.
     * @param pHandler  Optional handler.
     */
    Dcb(MXS_SESSION* pSession,
        const char* zUser,
        const char* zHost,
        Handler*    pHandler = NULL);
    ~Dcb();

    /**
     * Get the current handler of the Dcb.
     *
     * @return A Handler or NULL.
     */
    Handler* handler() const;

    /**
     * Set the current handler of the Dcb.
     *
     * @param pHandler  The new handler.
     *
     * @return  The previous handler or NULL.
     */
    Handler* set_handler(Handler* pHandler);

private:
    int32_t write(GWBUF* pData);

    static int32_t write(DCB* pDcb, GWBUF* pData);

private:
    std::string m_user;
    std::string m_host;
    Handler*    m_pHandler;
};

}

}
