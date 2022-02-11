/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "mock.hh"
#include <maxscale/dcb.hh>
#include <maxscale/protocol.hh>
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
     * @param zHost     The host of the connection.
     * @param pHandler  Optional handler.
     */
    Dcb(MXS_SESSION* pSession,
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

private:
    class Protocol : public mxs::ClientConnection
    {
    public:
        Protocol(Dcb::Handler* pHandler)
            : m_pHandler(pHandler)
        {
        }

        Dcb::Handler* handler() const
        {
            return m_pHandler;
        }

        Dcb::Handler* set_handler(Dcb::Handler* pHandler)
        {
            Dcb::Handler* p = m_pHandler;
            m_pHandler = pHandler;
            return p;
        }

        bool init_connection() override
        {
            mxb_assert(!true);
            return false;
        }

        void finish_connection() override
        {
            mxb_assert(!true);
        }

        void ready_for_reading(DCB*) override
        {
            mxb_assert(!true);
        }

        void write_ready(DCB*) override
        {
            mxb_assert(!true);
        }

        void error(DCB*) override
        {
            mxb_assert(!true);
        }

        void hangup(DCB*) override
        {
            mxb_assert(!true);
        }

        int32_t write(GWBUF* buffer) override;

        json_t* diagnostics() const override
        {
            return nullptr;
        }

        void set_dcb(DCB* dcb) override
        {
            m_dcb = static_cast<Dcb*>(dcb);
        }

        ClientDCB* dcb() override
        {
            return m_dcb;
        }

        const ClientDCB* dcb() const override
        {
            return m_dcb;
        }

    private:
        Dcb::Handler* m_pHandler;
        Dcb*          m_dcb {nullptr};
    };

public:
    Protocol* protocol() const override
    {
        return &m_protocol;
    }

private:
    mutable Protocol m_protocol;
};
}
}
