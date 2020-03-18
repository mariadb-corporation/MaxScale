/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file The embedded HTTP protocol administrative interface
 */
#include "internal/admin.hh"

#include <climits>
#include <new>
#include <fstream>
#include <microhttpd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/stat.h>
#include <jwt-cpp/jwt.h>

#include <maxbase/atomic.h>
#include <maxbase/assert.h>
#include <maxscale/utils.h>
#include <maxscale/config.hh>
#include <maxscale/clock.h>
#include <maxscale/http.hh>
#include <maxscale/adminusers.hh>

#include "internal/resource.hh"

using std::string;
using std::ifstream;

namespace
{

static char auth_failure_response[] = "{\"errors\": [ { \"detail\": \"Access denied\" } ] }";

static struct ThisUnit
{
    struct MHD_Daemon* daemon = nullptr;
    std::string        ssl_key;
    std::string        ssl_cert;
    std::string        ssl_ca;
    bool               using_ssl = false;
    bool               log_daemon_errors = true;
    bool               cors = false;
    std::string        sign_key;
} this_unit;

int header_cb(void* cls,
              enum MHD_ValueKind kind,
              const char* key,
              const char* value)
{
    Client::Headers* res = (Client::Headers*)cls;
    std::string k = key;
    std::transform(k.begin(), k.end(), k.begin(), ::tolower);
    res->emplace(k, value);
    return MHD_YES;
}

Client::Headers get_headers(MHD_Connection* connection)
{
    Client::Headers rval;
    MHD_get_connection_values(connection, MHD_HEADER_KIND, header_cb, &rval);
    return rval;
}

static bool modifies_data(const string& method)
{
    return method == MHD_HTTP_METHOD_POST || method == MHD_HTTP_METHOD_PUT
           || method == MHD_HTTP_METHOD_DELETE || method == MHD_HTTP_METHOD_PATCH;
}

int handle_client(void* cls,
                  MHD_Connection* connection,
                  const char* url,
                  const char* method,
                  const char* version,
                  const char* upload_data,
                  size_t* upload_data_size,
                  void** con_cls)

{
    if (*con_cls == NULL)
    {
        if ((*con_cls = new(std::nothrow) Client(connection)) == NULL)
        {
            return MHD_NO;
        }
    }

    Client* client = static_cast<Client*>(*con_cls);
    return client->handle(url, method, upload_data, upload_data_size);
}

static bool host_to_sockaddr(const char* host, uint16_t port, struct sockaddr_storage* addr)
{
    struct addrinfo* ai = NULL, hint = {};
    int rc;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_family = AF_UNSPEC;
    hint.ai_flags = AI_ALL;

    if ((rc = getaddrinfo(host, NULL, &hint, &ai)) != 0)
    {
        MXS_ERROR("Failed to obtain address for host %s: %s", host, gai_strerror(rc));
        return false;
    }

    /* Take the first one */
    if (ai)
    {
        memcpy(addr, ai->ai_addr, ai->ai_addrlen);

        if (addr->ss_family == AF_INET)
        {
            struct sockaddr_in* ip = (struct sockaddr_in*)addr;
            (*ip).sin_port = htons(port);
        }
        else if (addr->ss_family == AF_INET6)
        {
            struct sockaddr_in6* ip = (struct sockaddr_in6*)addr;
            (*ip).sin6_port = htons(port);
        }
    }

    freeaddrinfo(ai);
    return true;
}

std::string load_file(const std::string& file)
{
    std::ostringstream ss;
    std::ifstream infile(file);

    if (infile)
    {
        ss << infile.rdbuf();
    }
    else
    {
        MXS_ERROR("Failed to load file '%s': %d, %s", file.c_str(), errno, mxs_strerror(errno));
    }

    return ss.str();
}

static bool load_ssl_certificates()
{
    bool rval = true;
    const auto& config = mxs::Config::get();
    const auto& key = config.admin_ssl_key;
    const auto& cert = config.admin_ssl_cert;
    const auto& ca = config.admin_ssl_ca_cert;

    if (!key.empty() && !cert.empty() && !ca.empty())
    {
        this_unit.ssl_key = load_file(key.c_str());
        this_unit.ssl_cert = load_file(cert.c_str());
        this_unit.ssl_ca = load_file(ca.c_str());

        rval = !this_unit.ssl_key.empty() && !this_unit.ssl_cert.empty() && !this_unit.ssl_ca.empty();

        if (rval)
        {
            this_unit.using_ssl = true;
        }
    }

    return rval;
}

void admin_log_error(void* arg, const char* fmt, va_list ap)
{
    if (this_unit.log_daemon_errors)
    {
        char buf[1024];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        MXS_ERROR("REST API HTTP daemon error: %s\n", mxb::trimmed_copy(buf).c_str());
    }
}

void close_client(void* cls,
                  MHD_Connection* connection,
                  void** con_cls,
                  enum MHD_RequestTerminationCode toe)
{
    Client* client = static_cast<Client*>(*con_cls);
    delete client;
}

bool authorize_user(const char* user, const char* method, const char* url)
{
    bool rval = true;

    if (modifies_data(method) && !admin_user_is_inet_admin(user, nullptr))
    {
        if (mxs::Config::get().admin_log_auth_failures.get())
        {
            MXS_WARNING("Authorization failed for '%s', request requires "
                        "administrative privileges. Request: %s %s",
                        user, method, url);
        }
        rval = false;
    }

    return rval;
}

void init_jwt_sign_key()
{
    // Initialize JWT signing key
    std::random_device gen;
    constexpr auto KEY_BITS = 512;
    constexpr auto VALUE_SIZE = sizeof(decltype(gen()));
    constexpr auto NUM_VALUES = KEY_BITS / VALUE_SIZE;
    std::vector<decltype(gen())> key;
    key.reserve(NUM_VALUES);
    std::generate_n(std::back_inserter(key), NUM_VALUES, std::ref(gen));
    this_unit.sign_key.assign((const char*)key.data(), key.size() * VALUE_SIZE);
    mxb_assert(this_unit.sign_key.size() == KEY_BITS);
}
}

Client::Client(MHD_Connection* connection)
    : m_connection(connection)
    , m_state(INIT)
    , m_headers(get_headers(connection))
{
}

std::string Client::get_header(const std::string& key) const
{
    auto k = key;
    std::transform(k.begin(), k.end(), k.begin(), ::tolower);
    auto it = m_headers.find(k);
    return it != m_headers.end() ? it->second : "";
}

size_t Client::request_data_length() const
{
    return atoi(get_header("Content-Length").c_str());
}

void Client::send_basic_auth_error() const
{
    MHD_Response* resp =
        MHD_create_response_from_buffer(sizeof(auth_failure_response) - 1,
                                        auth_failure_response,
                                        MHD_RESPMEM_PERSISTENT);

    MHD_queue_basic_auth_fail_response(m_connection, "maxscale", resp);
    MHD_destroy_response(resp);
}

void Client::send_token_auth_error() const
{
    MHD_Response* response =
        MHD_create_response_from_buffer(sizeof(auth_failure_response) - 1,
                                        auth_failure_response,
                                        MHD_RESPMEM_PERSISTENT);

    MHD_queue_response(m_connection, MHD_HTTP_UNAUTHORIZED, response);
    MHD_destroy_response(response);
}

void Client::add_cors_headers(MHD_Response* response) const
{
    MHD_add_response_header(response, "Access-Control-Allow-Origin", get_header("Origin").c_str());
    MHD_add_response_header(response, "Vary", "Origin");

    auto request_headers = get_header("Access-Control-Request-Headers");
    auto request_method = get_header("Access-Control-Request-Method");

    if (!request_headers.empty())
    {
        MHD_add_response_header(response, "Access-Control-Allow-Headers", request_headers.c_str());
    }

    if (!request_method.empty())
    {
        MHD_add_response_header(response, "Access-Control-Allow-Methods", request_headers.c_str());
    }
}

bool Client::send_cors_preflight_request(const std::string& verb)
{
    bool rval = false;

    if (verb == MHD_HTTP_METHOD_OPTIONS && !get_header("Origin").empty())
    {
        MHD_Response* response =
            MHD_create_response_from_buffer(0, (void*)"", MHD_RESPMEM_PERSISTENT);

        add_cors_headers(response);

        MHD_queue_response(m_connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        rval = true;
    }

    return rval;
}

int Client::handle(const char* url, const char* method, const char* upload_data, size_t* upload_data_size)
{
    if (this_unit.cors && send_cors_preflight_request(method))
    {
        return MHD_YES;
    }

    Client::state state = get_state();
    int rval = MHD_NO;

    if (state != Client::CLOSED)
    {
        if (state == Client::INIT)
        {
            // First request, do authentication
            if (!auth(m_connection, url, method))
            {
                rval = MHD_YES;
            }
        }

        if (get_state() == Client::OK)
        {
            // Authentication was successful, start processing the request
            if (state == Client::INIT && request_data_length())
            {
                // The first call doesn't have any data
                rval = MHD_YES;
            }
            else
            {
                rval = process(url, method, upload_data, upload_data_size);
            }
        }
        else if (get_state() == Client::FAILED)
        {
            // Authentication has failed, an error will be sent to the client
            rval = MHD_YES;

            if (*upload_data_size || (state == Client::INIT && request_data_length()))
            {
                // The client is uploading data, discard it so we can send the error
                *upload_data_size = 0;
            }
            else if (state != Client::INIT)
            {
                // No pending upload data, close the connection
                close();
            }
        }
    }

    return rval;
}

int Client::process(string url, string method, const char* upload_data, size_t* upload_size)
{
    json_t* json = NULL;

    if (*upload_size)
    {
        m_data.append(upload_data, *upload_size);
        *upload_size = 0;
        return MHD_YES;
    }

    json_error_t err = {};

    if (m_data.length()
        && (json = json_loadb(m_data.c_str(), m_data.size(), 0, &err)) == NULL)
    {
        string msg = string("{\"errors\": [ { \"detail\": \"Invalid JSON in request: ")
            + err.text + "\" } ] }";
        MHD_Response* response = MHD_create_response_from_buffer(msg.size(),
                                                                 &msg[0],
                                                                 MHD_RESPMEM_MUST_COPY);
        MHD_queue_response(m_connection, MHD_HTTP_BAD_REQUEST, response);
        MHD_destroy_response(response);
        return MHD_YES;
    }

    HttpRequest request(m_connection, url, method, json);
    HttpResponse reply(MHD_HTTP_NOT_FOUND);
    MXS_DEBUG("Request:\n%s", request.to_string().c_str());
    request.fix_api_version();


    if (request.uri_part_count() == 1 && request.uri_segment(0, 1) == "auth")
    {
        auto now = std::chrono::system_clock::now();
        auto token = jwt::create()
            .set_issuer("maxscale")
            .set_audience(m_user)
            .set_issued_at(now)
            .set_expires_at(now + std::chrono::seconds {28800})
            .sign(jwt::algorithm::hs256 {this_unit.sign_key});

        reply = HttpResponse(MHD_HTTP_OK, json_pack("{s {s: s}}", "meta", "token", token.c_str()));
    }
    else
    {
        reply = resource_handle_request(request);
    }

    string data;

    if (json_t* js = reply.get_response())
    {
        int flags = 0;
        string pretty = request.get_option("pretty");

        if (pretty == "true" || pretty.length() == 0)
        {
            flags |= JSON_INDENT(4);
        }

        data = mxs::json_dump(js, flags);
    }

    MHD_Response* response =
        MHD_create_response_from_buffer(data.size(),
                                        (void*)data.c_str(),
                                        MHD_RESPMEM_MUST_COPY);

    for (const auto& a : reply.get_headers())
    {
        MHD_add_response_header(response, a.first.c_str(), a.second.c_str());
    }

    if (this_unit.cors && !get_header("Origin").empty())
    {
        add_cors_headers(response);
    }

    int rval = MHD_queue_response(m_connection, reply.get_code(), response);
    MHD_destroy_response(response);

    return rval;
}

bool Client::auth_with_token(const std::string& token)
{
    bool rval = false;

    try
    {
        auto d = jwt::decode(token);
        jwt::verify()
        .allow_algorithm(jwt::algorithm::hs256 {this_unit.sign_key})
        .with_issuer("maxscale")
        .verify(d);

        m_user = *d.get_audience().begin();
        rval = true;
    }
    catch (const std::exception& e)
    {
        if (mxs::Config::get().admin_log_auth_failures.get())
        {
            MXS_ERROR("Failed to validate token: %s", e.what());
        }
    }

    return rval;
}

bool Client::auth(MHD_Connection* connection, const char* url, const char* method)
{
    bool rval = true;

    if (mxs::Config::get().admin_auth)
    {
        auto token = get_header(MHD_HTTP_HEADER_AUTHORIZATION);

        if (token.substr(0, 7) == "Bearer ")
        {
            if (!auth_with_token(token.substr(7)))
            {
                send_token_auth_error();
                rval = false;
            }
        }
        else
        {
            rval = false;
            char* pw = NULL;
            char* user = MHD_basic_auth_get_username_password(connection, &pw);

            if (!user || !pw || !admin_verify_inet_user(user, pw))
            {
                if (mxs::Config::get().admin_log_auth_failures.get())
                {
                    MXS_WARNING("Authentication failed for '%s', %s. Request: %s %s",
                                user ? user : "",
                                pw ? "using password" : "no password",
                                method, url);
                }
            }
            else if (authorize_user(user, method, url))
            {
                MXS_INFO("Accept authentication from '%s', %s. Request: %s",
                         user ? user : "",
                         pw ? "using password" : "no password",
                         url);

                // Store the username for later in case we are generating a token
                m_user = user ? user : "";
                rval = true;
            }
            MXS_FREE(user);
            MXS_FREE(pw);

            if (!rval)
            {
                send_basic_auth_error();
            }
        }
    }

    m_state = rval ? Client::OK : Client::FAILED;

    return rval;
}

bool mxs_admin_init()
{
    struct sockaddr_storage addr;
    const auto& config = mxs::Config::get();

    init_jwt_sign_key();

    if (!load_ssl_certificates())
    {
        MXS_ERROR("Failed to load REST API TLS certificates.");
    }
    else if (host_to_sockaddr(config.admin_host.c_str(), config.admin_port, &addr))
    {
        int options = MHD_USE_EPOLL_INTERNALLY_LINUX_ONLY | MHD_USE_DEBUG;

        if (addr.ss_family == AF_INET6)
        {
            options |= MHD_USE_DUAL_STACK;
        }

        if (this_unit.using_ssl)
        {
            options |= MHD_USE_SSL;
        }

        // The port argument is ignored and the port in the struct sockaddr is used instead
        this_unit.daemon = MHD_start_daemon(options, 0, NULL, NULL, handle_client, NULL,
                                            MHD_OPTION_EXTERNAL_LOGGER, admin_log_error, NULL,
                                            MHD_OPTION_NOTIFY_COMPLETED, close_client, NULL,
                                            MHD_OPTION_SOCK_ADDR, &addr,
                                            !this_unit.using_ssl ? MHD_OPTION_END :
                                            MHD_OPTION_HTTPS_MEM_KEY, this_unit.ssl_key.c_str(),
                                            MHD_OPTION_HTTPS_MEM_CERT, this_unit.ssl_cert.c_str(),
                                            MHD_OPTION_HTTPS_MEM_TRUST, this_unit.ssl_cert.c_str(),
                                            MHD_OPTION_END);
    }

    // Silence all other errors to prevent malformed requests from flooding the log
    this_unit.log_daemon_errors = false;

    return this_unit.daemon != NULL;
}

void mxs_admin_shutdown()
{
    MHD_stop_daemon(this_unit.daemon);
    MXS_NOTICE("Stopped MaxScale REST API");
}

bool mxs_admin_https_enabled()
{
    return this_unit.using_ssl;
}

bool mxs_admin_enable_cors()
{
    return this_unit.cors = true;
}
