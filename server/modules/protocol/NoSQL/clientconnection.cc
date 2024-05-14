/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

#include "clientconnection.hh"
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <mysqld_error.h>
#include <maxscale/dcb.hh>
#include <maxscale/listener.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/session.hh>
#include <maxscale/service.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include "nosqlconfig.hh"
#include "nosqldatabase.hh"

using namespace std;
using namespace nosql;

/*
 * ClientConnection::CacheAsComponent
 *
 * The downstream of a client connection is a mxs::Component. However, a filter is not a
 * mxs::Component but an mxs::Routable, although both of them have identical routeQuery()
 * and clientReply() member functions.
 *
 * When nosqlprotocol has an internal cache, the mxs::Component::routeQuery() calls that
 * are made when no cache is used, needs to be mxs::Routable::routeQuery() calls.
 * The purpose of CacheAsComponent is to wrap the cache and expose an mxs::Component interface
 * that can be used in place of the original mxs::Component, provided when the client connection
 * was created.
 *
 * Normally a particular mxs::Routable or mxs::Component is before or after another routable
 * or component. But here CacheAsComponent and ClientConnectionAsRoutable (further below)
 * are "around" the cache.
 *
 */
class ClientConnection::CacheAsComponent final : public mxs::Component
{
public:
    /*
     * ClientConnectionAsRoutable
     *
     * The cache is an mxs::Routable and hence expects its downstream and upstream
     * to be mxs::Routables as well, something which ClientConnection is not.
     * The purpose of this class, is provide something to use as the down and upstream
     * of the cache.
     */
    class ClientConnectionAsRoutable final : public mxs::Routable
    {
    public:
        ClientConnectionAsRoutable(const ClientConnectionAsRoutable&) = delete;
        ClientConnectionAsRoutable& operator=(const ClientConnectionAsRoutable&) = delete;

        ClientConnectionAsRoutable(ClientConnection* pClient_connection, mxs::Component* pDownstream)
            : m_client_connection(*pClient_connection)
            , m_downstream(*pDownstream)
        {
        }

        bool routeQuery(GWBUF&& packet) override
        {
            // This is called by the cache and the packet must now be sent to the
            // actual downstream. In the call stack, we are below the routeQuery()
            // call to the cache in CacheAsComponent::routeQuery().

            return m_downstream.routeQuery(std::move(packet));
        }

        bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override
        {
            // This is called by the cache and the packet must now be sent to ClientConnection
            // for normal processing. But to handle_reply() and not clientReply(), which would
            // again send it to the cache.

            return m_client_connection.handle_reply(std::move(packet), down, reply);
        }

    private:
        ClientConnection& m_client_connection;
        mxs::Component&   m_downstream; // The downstream provided when the client connection was created.
    };

    CacheAsComponent(const CacheAsComponent&) = delete;
    CacheAsComponent& operator=(const CacheAsComponent&) = delete;

    CacheAsComponent(ClientConnection* pClient_connection, Cache* pCache, mxs::Component* pDownstream)
        : m_client_connection(*pClient_connection)
        , m_cache(*pCache)
        , m_client_connection_as_routable(std::make_shared<ClientConnectionAsRoutable>(pClient_connection, pDownstream))
    {
        // The cache filter session cannot be created here, because when a filter is
        // created it is assumed that the client connection has been fully created,
        // and this instance is created in the constructor of ClientConnection.
    }

    void create_cache()
    {
        mxb_assert(!m_sCache_filter_session);

        auto sSession_cache = SessionCache::create(&m_cache);

        auto* pCache_filter_session = CacheFilterSession::create(std::move(sSession_cache),
                                                                 &m_client_connection.m_session,
                                                                 m_client_connection.m_session.service);

        m_sCache_filter_session.reset(pCache_filter_session);

        m_sCache_filter_session->setDownstream(m_client_connection_as_routable.get());
        m_sCache_filter_session->setUpstream(m_client_connection_as_routable.get());
    }

    CacheFilterSession* cache_filter_session() const
    {
        return m_sCache_filter_session.get();
    }

    bool routeQuery(GWBUF&& packet) override
    {
        // This is called when nosqlprotocol wants to send a packet further down
        // the request chain. Here, the packet is provided to the internal cache.

        bool rv = m_sCache_filter_session->routeQuery(std::move(packet));

        if (rv)
        {
            if (session_has_response(&m_client_connection.m_session))
            {
                // Ok, so the cache could provide the response immediately.
                // Now it needs to be delivered directly to ClientConnection,
                // but using an lcall() so as not to break assumptions.

                mxb::Worker::get_current()->lcall([this]() {
                        GWBUF response = session_release_response(&m_client_connection.m_session);
                        mxs::ReplyRoute down;
                        mxs::Reply reply;

                        // handle_reply() and not clientReply() as the latter would cause the
                        // packet to first be delivered to the cache's clientReply() function
                        // and it is not expecting anything at this point (it could provide the
                        // response immediately).
                        m_client_connection.handle_reply(std::move(response), down, reply);
                    });
            }

            // If the cache could not provide the response immediately, then the server
            // response will be delivered to ClientConnection::clientReply(), bypassing the
            // cache as the system is not aware of it.
        }

        return rv;
    }

    bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override
    {
        // This is called from ClientConnection::clientReply(). The packet must now be delivered
        // to the cache, which eventually will call ClientConnectionAsRoutable::clientReply().
        return m_sCache_filter_session->clientReply(std::move(packet), down, reply);
    }

    bool handleError(mxs::ErrorType type, const std::string& error, mxs::Endpoint* down,
                     const mxs::Reply& reply) override
    {
        mxb_assert(!true);
        return true;
    }

    mxs::Component* parent() const override
    {
        return nullptr;
    }

private:
    ClientConnection&                   m_client_connection;
    Cache&                              m_cache;
    std::shared_ptr<mxs::Routable>      m_client_connection_as_routable;
    std::unique_ptr<CacheFilterSession> m_sCache_filter_session;
};


/*
 * ClientConnection
 */
ClientConnection::ClientConnection(const Configuration& config,
                                   nosql::UserManager* pUm,
                                   MXS_SESSION* pSession,
                                   mxs::Component* pDownstream,
                                   Cache* pCache)
    : m_config(config)
    , m_session(*pSession)
    , m_session_data(*static_cast<MYSQL_session*>(pSession->protocol_data()))
    , m_sDownstream(pCache ? create_downstream(pDownstream, pCache) : nullptr)
    , m_pCache(pCache)
    , m_nosql(pSession, this, pCache ? m_sDownstream.get() : pDownstream, &m_config, pUm)
    , m_ssl_required(m_session.listener_data()->m_ssl.config().enabled)
{
    prepare_session(m_config.user, m_config.password);
}

ClientConnection::~ClientConnection()
{
}

bool ClientConnection::init_connection()
{
    if (m_sDownstream)
    {
        m_sDownstream->create_cache();

        m_nosql.set_cache_filter_session(m_sDownstream->cache_filter_session());
    }

    return true;
}

void ClientConnection::finish_connection()
{
    // Nothing need to be done.
}

ClientDCB* ClientConnection::dcb()
{
    return static_cast<ClientDCB*>(m_pDcb);
}

const ClientDCB* ClientConnection::dcb() const
{
    return static_cast<const ClientDCB*>(m_pDcb);
}

bool ClientConnection::ssl_is_ready()
{
    mxb_assert(m_ssl_required);

    return m_pDcb->ssl_state() == DCB::SSLState::ESTABLISHED ? true : setup_ssl();
}

bool ClientConnection::setup_ssl()
{
    auto state = m_pDcb->ssl_state();
    mxb_assert(state != DCB::SSLState::ESTABLISHED);

    if (state == DCB::SSLState::HANDSHAKE_UNKNOWN)
    {
        m_pDcb->set_ssl_state(DCB::SSLState::HANDSHAKE_REQUIRED);
    }

    auto rv = m_pDcb->ssl_handshake();

    const char* zRemote = m_pDcb->remote().c_str();
    const char* zService = m_session.service->name();

    if (rv == 1)
    {
        MXB_INFO("NoSQL client from '%s' connected to service '%s' with SSL.",
                 zRemote, zService);
    }
    else
    {
        if (rv < 0)
        {
            MXB_INFO("NoSQL client from '%s' failed to connect to service '%s' with SSL.",
                     zRemote, zService);
        }
        else
        {
            MXB_INFO("NoSQL client from '%s' is in progress of connecting to service '%s' with SSL.",
                     zRemote, zService);
        }
    }

    return rv == 1;
}

void ClientConnection::ready_for_reading(GWBUF* pBuffer)
{
    // Got the header, the full packet may be available.
    protocol::HEADER* pHeader = reinterpret_cast<protocol::HEADER*>(pBuffer->data());

    int buffer_len = pBuffer->length();
    if (buffer_len >= pHeader->msg_len)
    {
        // Ok, we have at least one full packet.

        GWBUF* pPacket = nullptr;

        if (buffer_len == pHeader->msg_len)
        {
            // Exactly one.
            pPacket = pBuffer;
        }
        else
        {
            // More than one.
            pPacket = nosql::gwbuf_to_gwbufptr(pBuffer->split(pHeader->msg_len));
            mxb_assert((int)pPacket->length() == pHeader->msg_len);

            m_pDcb->unread(nosql::gwbufptr_to_gwbuf(pBuffer));
            m_pDcb->trigger_read_event();
        }

        mxb_assert(pPacket->length() >= protocol::HEADER_LEN);
        m_nosql.handle_request(pPacket);
    }
    else
    {
        MXB_INFO("%d bytes received, still need %d bytes for the package.",
                 buffer_len, pHeader->msg_len - buffer_len);
        m_pDcb->unread(nosql::gwbufptr_to_gwbuf(pBuffer));
    }
}

void ClientConnection::ready_for_reading(DCB* pDcb)
{
    mxb_assert(m_pDcb == pDcb);

    if (!m_ssl_required || ssl_is_ready())
    {
        auto [read_ok, buffer] = m_pDcb->read(protocol::HEADER_LEN, protocol::MAX_MSG_SIZE);

        if (!buffer.empty())
        {
            ready_for_reading(new GWBUF(move(buffer)));
        }
    }
}

void ClientConnection::error(DCB* pDcb, const char* errmsg)
{
    mxb_assert(m_pDcb == pDcb);

    m_session.kill();
}

const char* dbg_decode_response(GWBUF* pPacket);

bool ClientConnection::handle_reply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    bool rv = true;

    if (m_nosql.is_busy())
    {
        rv = m_nosql.clientReply(std::move(buffer), down, reply);
    }
    else
    {
        ComResponse response(&buffer);

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            MXB_ERROR("OK packet received from server when no request was in progress, ignoring.");
            break;

        case ComResponse::EOF_PACKET:
            MXB_ERROR("EOF packet received from server when no request was in progress, ignoring.");
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                switch (err.code())
                {
                case ER_ACCESS_DENIED_ERROR:
                case ER_CONNECTION_KILLED:
                    // Errors should have been logged already.
                    MXB_INFO("ERR packet received from server when no request was in progress: (%d) %s",
                             err.code(), err.message().c_str());
                    break;

                default:
                    MXB_ERROR("ERR packet received from server when no request was in progress: (%d) %s",
                              err.code(), err.message().c_str());
                }
            }
            break;

        default:
            MXB_ERROR("Unexpected %lu bytes received from server when no request was in progress, ignoring.",
                      buffer.length());
        }
    }

    return rv;
}

json_t* ClientConnection::diagnostics() const
{
    return nullptr;
}

void ClientConnection::set_dcb(DCB* pDcb)
{
    mxb_assert(!m_pDcb);
    m_pDcb = pDcb;

    m_nosql.set_dcb(pDcb);
}

bool ClientConnection::is_movable() const
{
    return true;
}

bool ClientConnection::is_idle() const
{
    return !m_nosql.is_busy();
}

size_t ClientConnection::sizeof_buffers() const
{
    return m_pDcb ? m_pDcb->runtime_size() : 0;
}

mxs::Parser* ClientConnection::parser()
{
    return &MariaDBParser::get();
}

void ClientConnection::setup_session(const string& user, const vector<uint8_t>& password)
{
    auto& auth_data = *m_session_data.auth_data;
    auth_data.user = user;
    m_session.set_user(auth_data.user);

    if (!password.empty())
    {
        // This will be used when authenticating with the backend.
        auth_data.backend_token = password;
    }
    else
    {
        auth_data.backend_token.clear();
    }
}

void ClientConnection::prepare_session(const string& user, const vector<uint8_t>& password)
{
    m_session_data.auth_data = std::make_unique<mariadb::AuthenticationData>();
    auto& auth_data = *m_session_data.auth_data;
    auth_data.default_db = "";
    auth_data.plugin = "mysql_native_password";

    const auto& authenticators = m_session.listener_data()->m_authenticators;
    mxb_assert(authenticators.size() == 1);
    auto* pAuthenticator = static_cast<mariadb::AuthenticatorModule*>(authenticators.front().get());

    auth_data.client_auth_module = pAuthenticator;
    auth_data.be_auth_module = pAuthenticator;
    m_session_data.client_caps.basic_capabilities = CLIENT_LONG_FLAG
        | CLIENT_LOCAL_FILES
        | CLIENT_PROTOCOL_41
        | CLIENT_INTERACTIVE
        | CLIENT_TRANSACTIONS
        | CLIENT_SECURE_CONNECTION
        | CLIENT_MULTI_STATEMENTS
        | CLIENT_MULTI_RESULTS
        | CLIENT_PS_MULTI_RESULTS
        | CLIENT_PLUGIN_AUTH
        | CLIENT_SESSION_TRACKING
        | CLIENT_PROGRESS;
    m_session_data.client_caps.ext_capabilities = MXS_MARIA_CAP_STMT_BULK_OPERATIONS;
    auth_data.collation = 33;       // UTF8

    // The statement is injected into the session history before the session
    // is started. That way it will be executed on all servers, irrespective
    // of when a connection to a particular server is created.
    uint32_t id = 1;
    GWBUF stmt = mariadb::create_query("set names utf8mb4 collate utf8mb4_bin");
    stmt.set_id(id);

    m_session_data.history().add(move(stmt), true);

    setup_session(user, password);
}

bool ClientConnection::clientReply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    bool rv = true;

    if (m_sDownstream)
    {
        // Ok, so we have a cache. The response must now be routed via the cache,
        // so that it can cache the response if appropriate. And it must be routed
        // via the cache as otherwise it will think it is missing a response.
        //
        // The cache will eventually call ClientConnectionAsRoutable::clientReply(),
        // which will call ClientConnection::handle_reply(). I.e. compared to the
        // direct call to handle_reply() below, we make a detour via the cache.
        rv = m_sDownstream->clientReply(std::move(buffer), down, reply);
    }
    else
    {
        rv = handle_reply(std::move(buffer), down, reply);
    }

    return rv;
}

ClientConnection::SComponent ClientConnection::create_downstream(mxs::Component* pDownstream,
                                                                 Cache* pCache)
{
    SComponent sComponent;

    if (pCache)
    {
        sComponent.reset(new CacheAsComponent(this, pCache, pDownstream));
    }

    return sComponent;
}
