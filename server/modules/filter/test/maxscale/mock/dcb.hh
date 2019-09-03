/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "mock.hh"
#include <maxscale/dcb.hh>
#include <maxscale/session.hh>

namespace maxscale
{

namespace mock
{

/**
 * The class Dcb provides a mock DCB that can be used when testing.
 */
class Dcb : public ClientDCB
{
    Dcb(const Dcb&);
    Dcb& operator=(const Dcb&);

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
        Handler* pHandler = NULL);
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

    MXS_PROTOCOL_SESSION* protocol_session() const override
    {
        return &m_protocol_session;
    }

private:
    class ProtocolSession : public MXS_PROTOCOL_SESSION
    {
    public:
        ProtocolSession(Handler* pHandler)
            : m_pHandler(pHandler)
        {
        }

        Handler* handler() const
        {
            return m_pHandler;
        }

        Handler* set_handler(Handler* pHandler)
        {
            Handler* p = m_pHandler;
            m_pHandler = pHandler;
            return p;
        }

        int32_t read(DCB*) override
        {
            mxb_assert(!true);
            return 0;
        }

        int32_t write(DCB* dcb, GWBUF* buffer) override;

        int32_t write_ready(DCB*) override
        {
            mxb_assert(!true);
            return 0;
        }

        int32_t error(DCB*) override
        {
            mxb_assert(!true);
            return 0;
        }

        int32_t hangup(DCB*) override
        {
            mxb_assert(!true);
            return 0;
        }

        json_t* diagnostics_json(DCB*) override
        {
            return nullptr;
        }

    private:
        Handler* m_pHandler;
    };

private:
    std::string             m_user;
    std::string             m_host;
    mutable ProtocolSession m_protocol_session;
};
}
}
