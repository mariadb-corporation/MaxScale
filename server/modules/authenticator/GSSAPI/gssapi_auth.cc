/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "gssapi_auth.hh"

#include <maxbase/alloc.h>
#include <maxscale/authenticator.hh>
#include <maxscale/dcb.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/secrets.hh>
#include <maxscale/service.hh>

using AuthRes = mariadb::ClientAuthenticator::AuthRes;

uint64_t GSSAPIAuthenticatorModule::capabilities() const
{
    return 0;
}

std::string GSSAPIAuthenticatorModule::supported_protocol() const
{
    return MXS_MARIADB_PROTOCOL_NAME;
}

/**
 * @brief Initialize the GSSAPI authenticator
 *
 * This function processes the service principal name that is given to the client.
 *
 * @param options Listener options
 * @return Authenticator instance
 */
GSSAPIAuthenticatorModule* GSSAPIAuthenticatorModule::create(mxs::ConfigParameters* options)
{
    /** This is mainly for testing purposes */
    const char default_princ_name[] = "mariadb/localhost.localdomain";

    auto instance = new(std::nothrow) GSSAPIAuthenticatorModule();
    if (instance)
    {
        const std::string princ_option = "principal_name";
        if (options->contains(princ_option))
        {
            instance->principal_name = options->get_string(princ_option);
            options->remove(princ_option);
        }
        else
        {
            instance->principal_name = default_princ_name;
            MXS_NOTICE("Using default principal name: %s", instance->principal_name.c_str());
        }
    }
    return instance;
}

mariadb::SClientAuth GSSAPIAuthenticatorModule::create_client_authenticator()
{
    auto new_ses = new(std::nothrow) GSSAPIClientAuthenticator(this);
    return mariadb::SClientAuth(new_ses);
}

GSSAPIClientAuthenticator::GSSAPIClientAuthenticator(GSSAPIAuthenticatorModule* module)
    : ClientAuthenticatorT(module)
{
}

GSSAPIClientAuthenticator::~GSSAPIClientAuthenticator()
{
    MXS_FREE(principal_name);
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
GWBUF* GSSAPIClientAuthenticator::create_auth_change_packet()
{
    /** Client auth plugin name */
    const char auth_plugin_name[] = "auth_gssapi_client";

    size_t principal_name_len = m_module.principal_name.length();
    size_t plen = sizeof(auth_plugin_name) + 1 + principal_name_len;
    GWBUF* buffer = gwbuf_alloc(plen + MYSQL_HEADER_LEN);

    if (buffer)
    {
        uint8_t* data = (uint8_t*)GWBUF_DATA(buffer);
        gw_mysql_set_byte3(data, plen);
        data += 3;
        *data++ = ++m_sequence;                                     // Second packet
        *data++ = 0xfe;                                             // AuthSwitchRequest command
        memcpy(data, auth_plugin_name, sizeof(auth_plugin_name));   // Plugin name
        data += sizeof(auth_plugin_name);
        memcpy(data, m_module.principal_name.c_str(), principal_name_len);      // Plugin data
    }

    return buffer;
}

/**
 * @brief Store the client's GSSAPI token
 *
 * This token will be shared with all the DCBs for this session when the backend
 * GSSAPI authentication is done.
 *
 * @param buffer Buffer containing the key
 * @return True on success, false if memory allocation failed
 */
bool GSSAPIClientAuthenticator::store_client_token(MYSQL_session* session, GWBUF* buffer)
{
    bool rval = false;
    uint8_t hdr[MYSQL_HEADER_LEN];

    if (gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN, hdr) == MYSQL_HEADER_LEN)
    {
        size_t plen = gw_mysql_get_byte3(hdr);
        session->client_token.resize(plen);
        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, plen, session->client_token.data());
        rval = true;
    }

    return rval;
}

/**
 * @brief Copy username to shared session data
 *
 * @param buffer Buffer containing the first authentication response
 */
void GSSAPIClientAuthenticator::copy_client_information(GWBUF* buffer)
{
    gwbuf_copy_data(buffer, MYSQL_SEQ_OFFSET, 1, &m_sequence);
}

/**
 * @brief Extract data from client response
 *
 * @param read_buffer Buffer containing the client's response
 * @return True if authentication can continue, false if not
 */
mariadb::ClientAuthenticator::ExchRes
GSSAPIClientAuthenticator::exchange(GWBUF* read_buffer, MYSQL_session* session, mxs::Buffer* output)
{
    auto rval = ExchRes::FAIL;

    switch (state)
    {
    case GSSAPI_AUTH_INIT:
        {
            /** We need to send the authentication switch packet to change the
             * authentication to something other than the 'mysql_native_password'
             * method */
            GWBUF* buffer = create_auth_change_packet();
            if (buffer)
            {
                output->reset(buffer);
                state = GSSAPI_AUTH_DATA_SENT;
                rval = ExchRes::INCOMPLETE;
            }
            break;
        }

    case GSSAPI_AUTH_DATA_SENT:
        if (store_client_token(session, read_buffer))
        {
            state = GSSAPI_AUTH_TOKEN_READY;
            rval = ExchRes::READY;
        }
        break;

    default:
        MXS_ERROR("Unexpected authentication state: %d", state);
        mxb_assert(false);
        break;
    }

    return rval;
}

static gss_name_t server_name = GSS_C_NO_NAME;

/**
 * @brief Check if the client token is valid
 *
 * @param token Client token
 * @param len Length of the token
 * @param output Pointer where the client principal name is stored
 * @return True if client token is valid
 */
bool GSSAPIClientAuthenticator::validate_gssapi_token(uint8_t* token, size_t len, char** output)
{
    OM_uint32 major = 0, minor = 0;
    gss_buffer_desc server_buf = {0, 0};
    gss_cred_id_t credentials;

    const char* pr = m_module.principal_name.c_str();
    server_buf.value = (void*)pr;
    server_buf.length = strlen(pr) + 1;

    major = gss_import_name(&minor, &server_buf, GSS_C_NT_USER_NAME, &server_name);

    if (GSS_ERROR(major))
    {
        report_error(major, minor);
        return false;
    }

    major = gss_acquire_cred(&minor,
                             server_name,
                             GSS_C_INDEFINITE,
                             GSS_C_NO_OID_SET,
                             GSS_C_ACCEPT,
                             &credentials,
                             NULL,
                             NULL);
    if (GSS_ERROR(major))
    {
        report_error(major, minor);
        return false;
    }

    do
    {

        gss_ctx_id_t handle = NULL;
        gss_buffer_desc in = {0, 0};
        gss_buffer_desc out = {0, 0};
        gss_buffer_desc client_name = {0, 0};
        gss_OID_desc* oid;
        gss_name_t client;

        in.value = token;
        in.length = len;

        major = gss_accept_sec_context(&minor,
                                       &handle,
                                       GSS_C_NO_CREDENTIAL,
                                       &in,
                                       GSS_C_NO_CHANNEL_BINDINGS,
                                       &client,
                                       &oid,
                                       &out,
                                       0,
                                       0,
                                       NULL);
        if (GSS_ERROR(major))
        {
            report_error(major, minor);
            return false;
        }

        major = gss_display_name(&minor, client, &client_name, NULL);

        if (GSS_ERROR(major))
        {
            report_error(major, minor);
            return false;
        }

        char* princ_name = static_cast<char*>(MXS_MALLOC(client_name.length + 1));

        if (!princ_name)
        {
            return false;
        }

        memcpy(princ_name, (const char*)client_name.value, client_name.length);
        princ_name[client_name.length] = '\0';
        *output = princ_name;
    }
    while (major & GSS_S_CONTINUE_NEEDED);

    return true;
}

/**
 * @brief Verify the user has access to the database
 *
 * @param session MySQL session
 * @param princ Client principal name
 * @return True if the user has access to the database
 */
bool GSSAPIClientAuthenticator::validate_user(MYSQL_session* session, const char* princ,
                                              const mariadb::UserEntry* entry)
{
    mxb_assert(princ);
    std::string princ_user = princ;
    princ_user.erase(princ_user.find('@'));
    return session->user == princ_user || entry->auth_string == princ;
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
    AuthRes rval;

    mxb_assert(state == GSSAPI_AUTH_TOKEN_READY);

    /** We sent the principal name and the client responded with the GSSAPI
     * token that we must validate */
    char* princ = NULL;

    if (validate_gssapi_token(session->client_token.data(), session->client_token.size(), &princ)
        && validate_user(session, princ, entry))
    {
        rval.status = AuthRes::Status::SUCCESS;
        session->backend_token = session->client_token;
    }

    MXS_FREE(princ);

    return rval;
}

mariadb::SBackendAuth
GSSAPIAuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
{
    return mariadb::SBackendAuth(new(std::nothrow) GSSAPIBackendAuthenticator(auth_data));
}

std::string GSSAPIAuthenticatorModule::name() const
{
    return MXS_MODULE_NAME;
}

const std::unordered_set<std::string>& GSSAPIAuthenticatorModule::supported_plugins() const
{
    static const std::unordered_set<std::string> plugins = {"gssapi"};
    return plugins;
}

extern "C"
{
/**
 * Module handle entry point
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::AUTHENTICATOR,
        mxs::ModuleStatus::GA,
        MXS_AUTHENTICATOR_VERSION,
        "GSSAPI authenticator",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::AuthenticatorApiGenerator<GSSAPIAuthenticatorModule>::s_api,
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
