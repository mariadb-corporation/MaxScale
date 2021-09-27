/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "gssapi_common.hh"
#include "gssapi_client_auth.hh"

#include <maxbase/alloc.h>

#include <maxscale/protocol/mariadb/authenticator.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/service.hh>

using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using std::string;

GSSAPIClientAuthenticator::GSSAPIClientAuthenticator(const std::string& service_principal)
    : m_service_principal(service_principal)
{
}

/**
 * @brief Create a AuthSwitchRequest packet
 *
 * This function also contains the first part of the GSSAPI authentication.
 * The server (MaxScale) send the principal name that will be used to generate
 * the token the client will send us. The principal name needs to exist in the
 * GSSAPI server in order for the client to be able to request a token.
 *
 * @return Allocated packet or NULL if memory allocation failed
 * @see
 * https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthSwitchRequest
 * @see https://web.mit.edu/kerberos/krb5-1.5/krb5-1.5.4/doc/krb5-user/What-is-a-Kerberos-Principal_003f.html
 */
mxs::Buffer GSSAPIClientAuthenticator::create_auth_change_packet()
{
    const char auth_plugin_name[] = "auth_gssapi_client";
    const int auth_plugin_name_len = sizeof(auth_plugin_name);
    size_t principal_name_len = m_service_principal.length() + 1;

    /**
     * The AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name
     * string[NUL] - Principal
     * string[NUL] - Mechanisms
     */
    size_t plen = 1 + auth_plugin_name_len + principal_name_len + 1;
    size_t buflen = MYSQL_HEADER_LEN + plen;
    uint8_t bufdata[buflen];
    uint8_t* data = mariadb::write_header(bufdata, plen, 0);
    *data++ = MYSQL_REPLY_AUTHSWITCHREQUEST;
    data = mariadb::copy_chars(data, auth_plugin_name, auth_plugin_name_len);
    data = mariadb::copy_chars(data, m_service_principal.c_str(), principal_name_len);
    *data = '\0';   // No mechanisms
    return {bufdata, buflen};
}

/**
 * @brief Store the client's GSSAPI token
 *
 * This token will be shared with all the DCBs for this session when the backend
 * GSSAPI authentication is done.
 *
 * @param buffer Buffer containing the key
 */
void GSSAPIClientAuthenticator::store_client_token(MYSQL_session* session, GWBUF* buffer)
{
    // Buffer is known to be complete.
    auto* data = gwbuf_link_data(buffer);
    auto header = mariadb::get_header(data);
    size_t plen = header.pl_length;
    session->client_token.resize(plen);
    gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, plen, session->client_token.data());
}

mariadb::ClientAuthenticator::ExchRes
GSSAPIClientAuthenticator::exchange(GWBUF* read_buffer, MYSQL_session* session)
{
    using ExchRes = mariadb::ClientAuthenticator::ExchRes;
    ExchRes rval;

    switch (m_state)
    {
    case State::INIT:
        {
            /** We need to send the authentication switch packet to change the
             * authentication to something other than the 'mysql_native_password'
             * method */
            auto buffer = create_auth_change_packet();
            if (buffer.length())
            {
                rval.packet = std::move(buffer);
                m_state = State::DATA_SENT;
                rval.status = ExchRes::Status::INCOMPLETE;
            }
            break;
        }

    case State::DATA_SENT:
        store_client_token(session, read_buffer);
        m_state = State::TOKEN_READY;
        rval.status = ExchRes::Status::READY;
        break;

    default:
        MXS_ERROR("Unexpected authentication state: %d", static_cast<int>(m_state));
        mxb_assert(false);
        break;
    }

    return rval;
}

/**
 * Check if the client token is valid
 *
 * @param ses Session
 * @param entry User entry
 * @return True if client token is valid
 */
bool GSSAPIClientAuthenticator::validate_gssapi_token(MYSQL_session* ses, const mariadb::UserEntry* entry)
{
    gss_buffer_desc service_name_buf = GSS_C_EMPTY_BUFFER;
    service_name_buf.value = (void*)m_service_principal.c_str();
    service_name_buf.length = m_service_principal.length() + 1;

    gss_name_t service_name = GSS_C_NO_NAME;
    OM_uint32 minor = 0;
    OM_uint32 major = gss_import_name(&minor, &service_name_buf, GSS_C_NT_USER_NAME, &service_name);

    gss_cred_id_t credentials = GSS_C_NO_CREDENTIAL;
    bool cred_init_ok = false;
    if (GSS_ERROR(major))
    {
        report_error(major, minor, "gss_import_name");
    }
    else
    {
        major = gss_acquire_cred(&minor, service_name,
                                 GSS_C_INDEFINITE, GSS_C_NO_OID_SET, GSS_C_ACCEPT,
                                 &credentials, nullptr, nullptr);
        if (GSS_ERROR(major))
        {
            report_error(major, minor, "gss_acquire_cred");
        }
        else
        {
            cred_init_ok = true;    // MaxScale/server credentials are ok.
        }
    }

    bool auth_ok = false;
    if (cred_init_ok)
    {
        // MaxScale does not support complicated authentication schemes involving multiple messages. If
        // gssapi wants more communication, authentication fails.
        gss_ctx_id_t handle = GSS_C_NO_CONTEXT;
        gss_buffer_desc in = GSS_C_EMPTY_BUFFER;
        in.value = ses->client_token.data();
        in.length = ses->client_token.size();

        gss_name_t client = GSS_C_NO_NAME;
        gss_buffer_desc out = GSS_C_EMPTY_BUFFER;

        major = gss_accept_sec_context(&minor, &handle, credentials, &in, GSS_C_NO_CHANNEL_BINDINGS,
                                       &client, nullptr, &out, nullptr, nullptr, nullptr);
        if (GSS_ERROR(major))
        {
            report_error(major, minor, "gss_accept_sec_context");
        }
        else if (major & GSS_S_CONTINUE_NEEDED)
        {
            MXB_ERROR("'gss_accept_sec_context' requires additional communication with client. "
                      "Not supported.");
        }
        else
        {
            gss_buffer_desc client_name = GSS_C_EMPTY_BUFFER;
            major = gss_display_name(&minor, client, &client_name, nullptr);
            if (GSS_ERROR(major))
            {
                report_error(major, minor, "gss_display_name");
            }
            else
            {
                // Finally, check that username as reported by gssapi is same as the client username.
                // Similarly to server, if authentication string is given, compare to that. If not, compare
                // against username.
                string found_name;
                found_name.assign((const char*)client_name.value, client_name.length);
                const std::string* expected_str = nullptr;
                if (entry->auth_string.empty())
                {
                    expected_str = &entry->username;
                    found_name.erase(found_name.find('@'));
                }
                else
                {
                    expected_str = &entry->auth_string;
                }

                if (found_name == *expected_str)
                {
                    auth_ok = true;
                }
                else
                {
                    MXB_ERROR("Name mismatch: found '%s', expected '%s'.",
                              found_name.c_str(), expected_str->c_str());
                }

                gss_release_buffer(&minor, &client_name);
            }
        }

        gss_release_buffer(&minor, &out);
        gss_release_name(&minor, &client);
        gss_delete_sec_context(&minor, &handle, GSS_C_NO_BUFFER);
    }

    gss_release_cred(&minor, &credentials);
    gss_release_name(&minor, &service_name);
    return auth_ok;
}

/**
 * @brief Authenticate the client
 *
 * @param dcb Client DCB
 * @return MXS_AUTH_INCOMPLETE if authentication is not yet complete, MXS_AUTH_SUCCEEDED
 * if authentication was successfully completed or MXS_AUTH_FAILED if authentication
 * has failed.
 */
AuthRes GSSAPIClientAuthenticator::authenticate(const mariadb::UserEntry* entry, MYSQL_session* session)
{
    mxb_assert(m_state == State::TOKEN_READY);
    AuthRes rval;

    /** We sent the principal name and the client responded with the GSSAPI
     * token that we must validate */
    if (validate_gssapi_token(session, entry))
    {
        rval.status = AuthRes::Status::SUCCESS;
        session->backend_token = session->client_token;
    }
    return rval;
}
