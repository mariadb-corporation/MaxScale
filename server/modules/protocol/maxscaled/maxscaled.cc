/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/protocol/maxscaled/module_names.hh>
#define MXS_MODULE_NAME MXS_MAXSCALED_PROTOCOL_NAME

#include "maxscaled.hh"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <sys/socket.h>

#include <maxbase/alloc.h>
#include <maxscale/authenticator2.hh>
#include <maxscale/dcb.hh>
#include <maxscale/buffer.hh>
#include <maxscale/session.hh>
#include <maxscale/router.hh>
#include <maxscale/adminusers.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/maxadmin.h>

#define GETPWUID_BUF_LEN 255

class MAXSCALEDProtocolModule : public mxs::ProtocolModule
{
public:
    static MAXSCALEDProtocolModule* create()
    {
        return new MAXSCALEDProtocolModule();
    }

    std::unique_ptr<mxs::ClientProtocol>
    create_client_protocol(MXS_SESSION* session, mxs::Component* component) override
    {
        return std::unique_ptr<mxs::ClientProtocol>(new (std::nothrow) MAXSCALEDClientProtocol());
    }

    std::string auth_default() const override
    {
        return MXS_MAXADMINAUTH_AUTHENTICATOR_NAME;
    }

    std::string name() const override
    {
        return MXS_MODULE_NAME;
    }
};

bool MAXSCALEDClientProtocol::authenticate_unix_socket(DCB* generic_dcb)
{
    auto dcb = static_cast<ClientDCB*>(generic_dcb);
    bool authenticated = false;

    struct ucred ucred;
    socklen_t len = sizeof(struct ucred);

    /* Get UNIX client credentials from socket*/
    if (getsockopt(dcb->fd(), SOL_SOCKET, SO_PEERCRED, &ucred, &len) == 0)
    {
        struct passwd pw_entry;
        struct passwd* pw_tmp;
        char buf[GETPWUID_BUF_LEN];

        /* Fetch username from UID */
        if (getpwuid_r(ucred.uid, &pw_entry, buf, sizeof(buf), &pw_tmp) == 0)
        {
            /* Set user in protocol */
            m_username = pw_entry.pw_name;
            GWBUF* username = gwbuf_alloc(m_username.length() + 1);

            strcpy((char*)GWBUF_DATA(username), m_username.c_str());

            /* Authenticate the user */
            if (dcb->authenticator()->extract(dcb, username) && dcb->authenticator()->authenticate(dcb) == 0)
            {
                dcb_printf(dcb, MAXADMIN_AUTH_SUCCESS_REPLY);
                m_state = MAXSCALED_STATE_DATA;
                dcb->m_user = strdup(m_username.c_str());
            }
            else
            {
                dcb_printf(dcb, MAXADMIN_AUTH_FAILED_REPLY);
            }

            gwbuf_free(username);
            authenticated = true;
        }
        else
        {
            MXS_ERROR("Failed to get UNIX user %ld details for 'MaxScale Admin'", (unsigned long)ucred.uid);
        }
    }
    else
    {
        MXS_ERROR("Failed to get UNIX domain socket credentials for 'MaxScale Admin'.");
    }

    return authenticated;
}


static bool authenticate_inet_socket(DCB* dcb)
{
    dcb_printf(dcb, MAXADMIN_AUTH_USER_PROMPT);
    return true;
}

bool MAXSCALEDClientProtocol::authenticate_socket(DCB* dcb)
{
    bool authenticated = false;

    struct sockaddr address;
    socklen_t address_len = sizeof(address);

    if (getsockname(dcb->fd(), &address, &address_len) == 0)
    {
        if (address.sa_family == AF_UNIX)
        {
            authenticated = authenticate_unix_socket(dcb);
        }
        else
        {
            authenticated = authenticate_inet_socket(dcb);
        }
    }
    else
    {
        MXS_ERROR("Could not get socket family of client connection: %s",
                  mxs_strerror(errno));
    }

    return authenticated;
}

extern "C"
{
/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    MXS_INFO("Initialise MaxScaled Protocol module.");

    static MXS_MODULE info =
    {
        MXS_MODULE_API_PROTOCOL,
        MXS_MODULE_GA,
        MXS_PROTOCOL_VERSION,
        "A maxscale protocol for the administration interface",
        "V2.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::ClientProtocolApi<MAXSCALEDProtocolModule>::s_api,
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}

/**
 * Read event for EPOLLIN on the maxscaled protocol module.
 *
 * @param dcb   The descriptor control block
 * @return
 */
void MAXSCALEDClientProtocol::ready_for_reading(DCB* dcb)
{
    GWBUF* head = NULL;
    if (dcb->read(&head, 0) != -1)
    {
        if (head)
        {
            if (GWBUF_LENGTH(head))
            {
                switch (m_state)
                {
                case MAXSCALED_STATE_LOGIN:
                    {
                        size_t len = GWBUF_LENGTH(head);
                        char user[len + 1];
                        memcpy(user, GWBUF_DATA(head), len);
                        user[len] = '\0';
                        m_username = user;
                        dcb->m_user = MXS_STRDUP_A(user);
                        m_state = MAXSCALED_STATE_PASSWD;
                        dcb_printf(dcb, MAXADMIN_AUTH_PASSWORD_PROMPT);
                        gwbuf_free(head);
                    }
                    break;

                case MAXSCALED_STATE_PASSWD:
                    {
                        char* password = strndup((char*)GWBUF_DATA(head), GWBUF_LENGTH(head));
                        if (admin_verify_inet_user(m_username.c_str(), password))
                        {
                            dcb_printf(dcb, MAXADMIN_AUTH_SUCCESS_REPLY);
                            m_state = MAXSCALED_STATE_DATA;
                        }
                        else
                        {
                            dcb_printf(dcb, MAXADMIN_AUTH_FAILED_REPLY);
                            m_state = MAXSCALED_STATE_LOGIN;
                        }
                        gwbuf_free(head);
                        free(password);
                    }
                    break;

                case MAXSCALED_STATE_DATA:
                    {
                        mxs_route_query(dcb->session(), head);
                        dcb_printf(dcb, "OK");
                    }
                    break;
                }
            }
            else
            {
                // Force the free of the buffer header
                gwbuf_free(head);
            }
        }
    }
}

/**
 * EPOLLOUT handler for the maxscaled protocol module.
 *
 * @param dcb   The descriptor control block
 * @return
 */
void MAXSCALEDClientProtocol::write_ready(DCB* dcb)
{
    dcb->writeq_drain();
}

/**
 * Write routine for the maxscaled protocol module.
 *
 * Writes the content of the buffer queue to the socket
 * observing the non-blocking principles of MaxScale.
 *
 * @param dcb   Descriptor Control Block for the socket
 * @param queue Linked list of buffes to write
 */
int32_t MAXSCALEDClientProtocol::write(DCB* dcb, GWBUF* queue)
{
    return dcb->writeq_append(queue);
}

/**
 * Handler for the EPOLLERR event.
 *
 * @param dcb   The descriptor control block
 */
void MAXSCALEDClientProtocol::error(DCB* dcb)
{
}

/**
 * Handler for the EPOLLHUP event.
 *
 * @param dcb   The descriptor control block
 */
void MAXSCALEDClientProtocol::hangup(DCB* dcb)
{
    DCB::close(dcb);
}

bool MAXSCALEDClientProtocol::init_connection(DCB* client_dcb)
{
    bool rv = true;
    if (authenticate_socket(client_dcb))
    {
        if (session_start(client_dcb->session()))
        {
            rv = true;
        }
    }

    return rv;
}

void MAXSCALEDClientProtocol::finish_connection(DCB* dcb)
{
}
