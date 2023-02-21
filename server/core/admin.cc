/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
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
#include <unordered_map>

#include <microhttpd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <gnutls/abstract.h>

#include <maxbase/assert.hh>
#include <maxbase/filesystem.hh>
#include <maxbase/http.hh>
#include <maxbase/csv_writer.hh>
#include <maxscale/config.hh>
#include <maxscale/paths.hh>
#include <maxscale/threadpool.hh>

#include "internal/adminusers.hh"
#include "internal/defaults.hh"
#include "internal/resource.hh"
#include "internal/websocket.hh"
#include "internal/jwt.hh"

using std::string;

namespace
{

static char shutting_down_response[] = "{\"errors\": [ { \"detail\": \"MaxScale is shutting down\" } ] }";
static char auth_failure_response[] = "{\"errors\": [ { \"detail\": \"Access denied\" } ] }";
static char no_https_response[] = "{\"errors\": [ { \"detail\": \"Connection is not encrypted\" } ] }";
static char not_admin_response[] = "{\"errors\": [ { \"detail\": \"Administrative access required\" } ] }";

// The page served when the GUI is accessed without HTTPS
const char* gui_not_secure_page =
    R"EOF(
<!DOCTYPE html>
<html>
  <head>
    <style>code {color: grey; background-color: #f1f1f1; padding: 2px;}</style>
    <meta charset="UTF-8">
    <title>Connection Not Secure</title>
  </head>
  <body>
    <p>
      The MaxScale GUI requires HTTPS to work, please enable it by configuring the
      <a href="https://mariadb.com/kb/en/mariadb-maxscale-24-mariadb-maxscale-configuration-guide/#admin_ssl_key">admin_ssl_key</a>
      and <a href="https://mariadb.com/kb/en/mariadb-maxscale-24-mariadb-maxscale-configuration-guide/#admin_ssl_cert">admin_ssl_cert</a> parameters.
      To allow insecure use of the GUI, add <code>admin_secure_gui=false</code> under the <code>[maxscale]</code> section.
      To disable the GUI completely, add  <code>admin_gui=false</code> under the <code>[maxscale]</code> section.
    </p>
    <p>
      For more information about securing the admin interface of your MaxScale installation, refer to the
      <a href="https://mariadb.com/kb/en/mariadb-maxscale-24-rest-api-tutorial/#configuration-and-hardening">Configuration and Hardening</a>
      section of the REST API tutorial.
    </p>
  </body>
</html>
)EOF";

const std::string TOKEN_ISSUER = "maxscale";
const std::string TOKEN_SIG = "token_sig";

const char* CN_ADMIN = "admin";
const char* CN_BASIC = "basic";

// Wrapper for managing GnuTLS certificates and keys
struct Creds
{
public:
    static std::unique_ptr<Creds> create(const std::string& cert_file, const std::string& key_file);

    void set(gnutls_pcert_st** pcert,
             unsigned int* pcert_length,
             gnutls_privkey_t* pkey);

    ~Creds();

private:
    Creds(gnutls_privkey_t pkey, std::vector<gnutls_pcert_st> pcerts);

    gnutls_privkey_t             m_pkey;
    std::vector<gnutls_pcert_st> m_pcerts;
};

static struct ThisUnit
{
    struct MHD_Daemon* daemon = nullptr;
    std::string        ssl_version;
    std::string        ssl_ca;
    bool               using_ssl = false;
    bool               log_daemon_errors = true;
    bool               cors = false;
    std::string        accept_origin = "*";
    std::atomic<bool>  running {true};

    std::mutex             lock;
    std::unique_ptr<Creds> creds;
    std::unique_ptr<Creds> next_creds;

    std::unordered_map<std::string, std::string> files;
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
        if ((*con_cls = new(std::nothrow) Client(connection, url, method)) == NULL)
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
        MXB_ERROR("Failed to obtain address for host %s: %s", host, gai_strerror(rc));
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

std::string get_file(const std::string& file)
{
    std::string rval;

    if (this_unit.using_ssl || !mxs::Config::get().secure_gui)
    {
        if (this_unit.files.find(file) == this_unit.files.end())
        {
            this_unit.files[file] = mxb::load_file<std::string>(file).first;
        }

        rval = this_unit.files[file];
    }
    else
    {
        // Don't serve files over insecure connections
        rval = gui_not_secure_page;
    }

    return rval;
}

std::string get_filename(const HttpRequest& request)
{
    std::string sharedir = mxs::sharedir();
    sharedir += "/gui/";
    std::string path = sharedir;

    if (request.uri_part_count() == 0)
    {
        path += "index.html";
    }
    else
    {
        path += request.uri_segment(0, request.uri_part_count());
    }

    char pathbuf[PATH_MAX + 1] = "";
    char sharebuf[PATH_MAX + 1] = "";

    if (realpath(path.c_str(), pathbuf) && access(pathbuf, R_OK) == 0
        && realpath(sharedir.c_str(), sharebuf)
        && strncmp(pathbuf, sharebuf, strlen(sharebuf)) == 0)
    {
        // A valid file that's stored in the GUI directory
        path.assign(pathbuf);
    }
    else
    {
        path.clear();
    }

    return path;
}

// Converts mxb::ssl_version::Version into the corresponding GNUTLS configuration string
static const char* get_ssl_version(const mxb::ssl_version::Version ssl_version)
{
    switch (ssl_version)
    {
    case mxb::ssl_version::SSL_MAX:
    case mxb::ssl_version::TLS_MAX:
    case mxb::ssl_version::SSL_TLS_MAX:
    case mxb::ssl_version::TLS10:
        return "NORMAL:-VERS-SSL3.0";

    case mxb::ssl_version::TLS11:
        return "NORMAL:-VERS-SSL3.0:-VERS-TLS1.0";

    case mxb::ssl_version::TLS12:
        return "NORMAL:-VERS-SSL3.0:-VERS-TLS1.0:-VERS-TLS1.1";

    case mxb::ssl_version::TLS13:
        return "NORMAL:-VERS-SSL3.0:-VERS-TLS1.0:-VERS-TLS1.1:-VERS-TLS1.2";

    case mxb::ssl_version::SSL_UNKNOWN:
    default:
        mxb_assert(!true);
        break;
    }

    return "";
}

// static
std::unique_ptr<Creds> Creds::create(const std::string& cert_file, const std::string& key_file)
{
    std::unique_ptr<Creds> rval;

    auto cert = mxb::load_file<std::vector<uint8_t>>(cert_file.c_str()).first;
    gnutls_datum_t data;
    data.data = cert.data();
    data.size = cert.size();
    std::vector<gnutls_pcert_st> pcerts;
    unsigned int num_pcert = 100;       // The maximum number of certificates that are read from the file
    pcerts.resize(num_pcert);

    int rc = gnutls_pcert_list_import_x509_raw(pcerts.data(), &num_pcert, &data, GNUTLS_X509_FMT_PEM, 0);

    if (rc == 0)
    {
        pcerts.resize(num_pcert);
        pcerts.shrink_to_fit();

        gnutls_privkey_t pkey;
        gnutls_privkey_init(&pkey);

        auto key = mxb::load_file<std::vector<uint8_t>>(key_file.c_str()).first;
        data.data = key.data();
        data.size = key.size();
        rc = gnutls_privkey_import_x509_raw(pkey, &data, GNUTLS_X509_FMT_PEM, nullptr, 0);

        if (rc == 0)
        {
            rval.reset(new Creds(pkey, std::move(pcerts)));
        }
        else
        {
            MXB_ERROR("Failed to load REST API TLS private key: %s", gnutls_strerror(rc));
            gnutls_privkey_deinit(pkey);

            for (auto& cert : pcerts)
            {
                gnutls_pcert_deinit(&cert);
            }
        }
    }
    else
    {
        MXB_ERROR("Failed to load REST API TLS public certificate: %s", gnutls_strerror(rc));
    }

    return rval;
}

void Creds::set(gnutls_pcert_st** pcert,
                unsigned int* pcert_length,
                gnutls_privkey_t* pkey)
{
    *pcert = m_pcerts.data();
    *pcert_length = m_pcerts.size();
    *pkey = m_pkey;
}

Creds::Creds(gnutls_privkey_t pkey, std::vector<gnutls_pcert_st> pcerts)
    : m_pkey(pkey)
    , m_pcerts(std::move(pcerts))
{
}

Creds::~Creds()
{
    gnutls_privkey_deinit(m_pkey);

    for (auto& cert : m_pcerts)
    {
        gnutls_pcert_deinit(&cert);
    }
}

static bool load_ssl_certificates()
{
    bool rval = true;
    const auto& config = mxs::Config::get();
    const auto& key = config.admin_ssl_key;
    const auto& cert = config.admin_ssl_cert;
    const auto& ca = config.admin_ssl_ca;

    if (!key.empty() && !cert.empty())
    {
        rval = false;
        this_unit.ssl_version = get_ssl_version(config.admin_ssl_version);

        if (!ca.empty())
        {
            this_unit.ssl_ca = mxb::load_file<std::string>(ca.c_str()).first;
        }

        if (auto creds = Creds::create(cert, key))
        {
            std::lock_guard guard(this_unit.lock);
            this_unit.creds = std::move(creds);

            if (ca.empty() || !this_unit.ssl_ca.empty())
            {
                this_unit.using_ssl = true;
                rval = true;
            }
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
        MXB_ERROR("REST API HTTP daemon error: %s\n", mxb::trimmed_copy(buf).c_str());
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

void add_extra_headers(MHD_Response* response)
{
    MHD_add_response_header(response, "X-Frame-Options", "Deny");
    MHD_add_response_header(response, "X-XSS-Protection", "1");
    MHD_add_response_header(response, "Referrer-Policy", "same-origin");
}

void add_content_type_header(MHD_Response* response, const std::string& path)
{
    static const std::unordered_map<std::string, std::string> content_types =
    {
        {".bmp",    "image/bmp"            },
        {".bz",     "application/x-bzip"   },
        {".bz2",    "application/x-bzip2"  },
        {".css",    "text/css"             },
        {".csv",    "text/csv"             },
        {".epub",   "application/epub+zip" },
        {".gz",     "application/gzip"     },
        {".gif",    "image/gif"            },
        {".htm",    "text/html"            },
        {".html",   "text/html"            },
        {".jpeg",   "image/jpeg"           },
        {".jpg",    "image/jpeg"           },
        {".js",     "text/javascript"      },
        {".json",   "application/json"     },
        {".jsonld", "application/ld+json"  },
        {".mjs",    "text/javascript"      },
        {".mp3",    "audio/mpeg"           },
        {".mpeg",   "video/mpeg"           },
        {".otf",    "font/otf"             },
        {".png",    "image/png"            },
        {".pdf",    "application/pdf"      },
        {".php",    "application/php"      },
        {".rar",    "application/vnd.rar"  },
        {".rtf",    "application/rtf"      },
        {".svg",    "image/svg+xml"        },
        {".tar",    "application/x-tar"    },
        {".tif",    "image/tiff"           },
        {".tiff",   "image/tiff"           },
        {".ts",     "video/mp2t"           },
        {".ttf",    "font/ttf"             },
        {".txt",    "text/plain"           },
        {".wav",    "audio/wav"            },
        {".weba",   "audio/webm"           },
        {".webm",   "video/webm"           },
        {".webp",   "image/webp"           },
        {".woff",   "font/woff"            },
        {".woff2",  "font/woff2"           },
        {".xhtml",  "application/xhtml+xml"},
        {".xml",    "application/xml"      },
    };

    auto pos = path.find_last_of('.');
    std::string suffix;

    if (pos != std::string::npos)
    {
        suffix = path.substr(pos);
        auto it = content_types.find(suffix);

        if (it != content_types.end())
        {
            MHD_add_response_header(response, "Content-Type", it->second.c_str());
        }
    }


    if (suffix == ".html")
    {
        // The GUI HTML files should be validated by the browser, this causes MaxScale upgrades to eventually
        // trigger a reloading of the GUI.
        MHD_add_response_header(response, "Cache-Control", "public, no-cache");
    }
    else
    {
        MHD_add_response_header(response, "Cache-Control", "public, max-age=31536000");
    }
}

bool is_auth_endpoint(const HttpRequest& request)
{
    return request.uri_part_count() == 1 && request.uri_segment(0, 1) == "auth";
}

std::vector<std::string> audit_log_columns {"Timestamp", "Duration", "User",
                                            "Host", "URI", "Method",
                                            "Status", "Response code", "Body"};

enum class LogAction
{
    None,
    CheckRotate
};

maxbase::CsvWriter& get_audit_log(LogAction action = LogAction::None)
{
    auto path = mxs::Config::get().admin_audit_file.get();
    static maxbase::CsvWriter s_log(path, audit_log_columns);
    static int s_rotation_count = mxs_get_log_rotation_count();

    if (action == LogAction::CheckRotate)
    {
        if (s_log.path() != path)
        {
            s_log = maxbase::CsvWriter(path, audit_log_columns);
        }
        else if (s_rotation_count != mxs_get_log_rotation_count())
        {
            s_rotation_count = mxs_get_log_rotation_count();
            s_log.rotate();
        }
    }

    return s_log;
}

json_t* hide_passwords(json_t* pJson)
{
    if (json_is_array(pJson))
    {
        size_t index{};
        json_t* pElem;
        json_array_foreach(pJson, index, pElem)
        {
            hide_passwords(pElem);
        }
    }
    else if (json_is_object(pJson))
    {
        const char* key;
        json_t* ignored;
        json_object_foreach(pJson, key, ignored)
        {
            if (strcasecmp(key, "password") == 0)
            {
                json_object_set_new(pJson, key, json_string("****"));
            }
            else
            {
                json_t* pElem = json_object_get(pJson, key);
                hide_passwords(pElem);
            }
        }
    }

    return pJson;
}

std::string hide_passwords_in_json(const std::string& json_str)
{
    if (json_str.empty())
    {
        return json_str;
    }

    std::string ret;
    json_t* pJson = json_loads(json_str.c_str(), 0, nullptr);
    if (!pJson)
    {
        ret = "invalid";
    }
    else
    {
        hide_passwords(pJson);
        ret = json_dumps(pJson, 0);
        json_decref(pJson);
    }
    return ret;
}
}

Client::Client(MHD_Connection* connection, const char* url, const char* method)
    : m_connection(connection)
    , m_state(INIT)
    , m_headers(get_headers(connection))
    , m_request(connection, url, method, nullptr)
    , m_http_response_code(MHD_HTTP_INTERNAL_SERVER_ERROR)
    , m_start_time(maxbase::Clock::now())
{
}

Client::~Client()
{
    m_end_time = maxbase::Clock::now();
    log_to_audit();
}

bool Client::is_basic_endpoint() const
{
    // TODO: Move this into resource.cc, this is not the best place to do this
    return m_request.uri_part(0) == "sql";
}

bool Client::authorize_user(const char* user, mxs::user_account_type type, const char* method,
                            const char* url) const
{
    bool rval = true;

    if (modifies_data(method))
    {
        if (type != mxs::USER_ACCOUNT_ADMIN && !is_basic_endpoint())
        {
            if (mxs::Config::get().admin_log_auth_failures.get())
            {
                MXB_WARNING("Authorization failed for '%s', request requires "
                            "administrative privileges. Request: %s %s",
                            user, method, url);
            }
            rval = false;
        }
    }
    else if (type == mxs::USER_ACCOUNT_UNKNOWN)
    {
        if (mxs::Config::get().admin_log_auth_failures.get())
        {
            MXB_WARNING("Authorization failed for '%s', user does not exist. Request: %s %s",
                        user, method, url);
        }
        rval = false;
    }

    return rval;
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

int Client::wrap_MHD_queue_response(unsigned int status_code, MHD_Response* response)
{
    set_http_response_code(status_code);
    return MHD_queue_response(m_connection, status_code, response);
}

void Client::send_shutting_down_error()
{
    MHD_Response* resp =
        MHD_create_response_from_buffer(sizeof(shutting_down_response) - 1,
                                        shutting_down_response,
                                        MHD_RESPMEM_PERSISTENT);

    wrap_MHD_queue_response(MHD_HTTP_SERVICE_UNAVAILABLE, resp);
    MHD_destroy_response(resp);
}

void Client::send_basic_auth_error()
{
    MHD_Response* resp =
        MHD_create_response_from_buffer(sizeof(auth_failure_response) - 1,
                                        auth_failure_response,
                                        MHD_RESPMEM_PERSISTENT);

    if (auto it = m_headers.find("x-requested-with");
        it != m_headers.end() && strcasecmp(it->second.c_str(), "XMLHttpRequest") == 0)
    {
        wrap_MHD_queue_response(MHD_HTTP_UNAUTHORIZED, resp);
    }
    else
    {
        set_http_response_code(MHD_HTTP_UNAUTHORIZED);
        MHD_queue_basic_auth_fail_response(m_connection, "maxscale", resp);
    }

    MHD_destroy_response(resp);
}

void Client::send_token_auth_error()
{
    MHD_Response* response =
        MHD_create_response_from_buffer(sizeof(auth_failure_response) - 1,
                                        auth_failure_response,
                                        MHD_RESPMEM_PERSISTENT);

    wrap_MHD_queue_response(MHD_HTTP_UNAUTHORIZED, response);
    MHD_destroy_response(response);
}

void Client::send_write_access_error()
{
    MHD_Response* response =
        MHD_create_response_from_buffer(sizeof(not_admin_response) - 1,
                                        not_admin_response,
                                        MHD_RESPMEM_PERSISTENT);

    wrap_MHD_queue_response(MHD_HTTP_FORBIDDEN, response);
    MHD_destroy_response(response);
}

void Client::send_no_https_error()
{
    MHD_Response* response =
        MHD_create_response_from_buffer(sizeof(no_https_response) - 1,
                                        no_https_response,
                                        MHD_RESPMEM_PERSISTENT);

    wrap_MHD_queue_response(MHD_HTTP_UNAUTHORIZED, response);
    MHD_destroy_response(response);
}

void Client::add_cors_headers(MHD_Response* response) const
{
    MHD_add_response_header(response, "Access-Control-Allow-Origin", this_unit.accept_origin.c_str());
    MHD_add_response_header(response, "Access-Control-Allow-Credentials", "true");
    MHD_add_response_header(response, "Vary", "Origin");

    auto request_headers = get_header("Access-Control-Request-Headers");
    auto request_method = get_header("Access-Control-Request-Method");

    if (!request_headers.empty())
    {
        MHD_add_response_header(response, "Access-Control-Allow-Headers", request_headers.c_str());
    }

    if (!request_method.empty())
    {
        MHD_add_response_header(response, "Access-Control-Allow-Methods", request_method.c_str());
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

        wrap_MHD_queue_response(MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        rval = true;
    }

    return rval;
}

bool Client::serve_file(const std::string& url)
{
    bool rval = false;
    std::string path = get_filename(m_request);

    if (!path.empty())
    {
        MXB_DEBUG("Client requested file: %s", path.c_str());
        MXB_DEBUG("Request:\n%s", m_request.to_string().c_str());
        std::string data = get_file(path);

        if (!data.empty())
        {
            rval = true;

            MHD_Response* response =
                MHD_create_response_from_buffer(data.size(),
                                                (void*)data.c_str(),
                                                MHD_RESPMEM_MUST_COPY);

            if (this_unit.cors && !m_request.get_header("Origin").empty())
            {
                add_cors_headers(response);
            }

            add_content_type_header(response, path);
            add_extra_headers(response);

            if (wrap_MHD_queue_response(MHD_HTTP_OK, response) == MHD_YES)
            {
                rval = true;
            }

            MHD_destroy_response(response);
        }
        else
        {
            MXB_DEBUG("File not found: %s", path.c_str());
        }
    }

    return rval;
}

void Client::set_http_response_code(uint code)
{
    m_http_response_code = code;
}

uint Client::get_http_response_code() const
{
    return m_http_response_code;
}

void Client::log_to_audit()
{
    if (!mxs::Config::get().admin_audit_enabled.get())
    {
        return;
    }

    // Don't exclude if authentication failed
    if (!(m_state == Client::CLOSED || m_state == Client::FAILED))
    {
        auto method = mxb::http::from_string(m_request.get_verb());
        auto excludes = mxs::Config::get().admin_audit_exclude_methods.get();
        auto ite = std::find(begin(excludes), end(excludes), method);
        if (ite != end(excludes))
        {
            return;
        }
    }

    std::string status = maxbase::http::code_to_string(get_http_response_code());
    auto body = hide_passwords_in_json(m_data);

    std::vector<std::string> values
    {
        wall_time::to_string(wall_time::Clock::now()),
        maxbase::to_string(m_end_time - m_start_time),
        m_user,
        m_request.host(),
        m_request.get_uri(),
        m_request.get_verb(),
        status,
        std::to_string(m_http_response_code),
        body
    };

    if (!get_audit_log().add_row(values))
    {
        if (!s_admin_log_error_reported)
        {
            s_admin_log_error_reported = true;
            MXB_SERROR("Failed to write to admin audit file: " << get_audit_log().path());
        }
    }
    else
    {
        s_admin_log_error_reported = false;
    }

    // If the path has been runtime changed or rotate issued,
    // rotate after write so that the API call is logged
    // to the "current" log.
    get_audit_log(LogAction::CheckRotate);
}

bool Client::s_admin_log_error_reported = false;

// static
void Client::handle_ws_upgrade(void* cls, MHD_Connection* connection, void* con_cls,
                               const char* extra_in, size_t extra_in_size,
                               int socket, MHD_UpgradeResponseHandle* urh)
{
    Client* client = static_cast<Client*>(cls);
    WebSocket::create(socket, urh, client->m_ws_handler);
}

void Client::upgrade_to_ws()
{
    // The WebSocket protocol requires the server to perform a "complex" task to make sure it understands the
    // protocol. This means taking the literal value of the Sec-WebSocket-Key header and concatenating it with
    // a special UUID, taking the SHA1 of the result and sending the Base64 encoded result back in the
    // Sec-WebSocket-Accept header.
    auto key = get_header("Sec-WebSocket-Key") + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[SHA_DIGEST_LENGTH];
    SHA1((uint8_t*)key.data(), key.size(), digest);
    auto encoded = mxs::to_base64(digest, sizeof(digest));

    auto resp = MHD_create_response_for_upgrade(handle_ws_upgrade, this);
    MHD_add_response_header(resp, "Sec-WebSocket-Accept", encoded.c_str());
    MHD_add_response_header(resp, "Upgrade", "websocket");
    MHD_add_response_header(resp, "Connection", "Upgrade");

    // This isn't exactly correct but it'll do for now
    MHD_add_response_header(resp, "Sec-WebSocket-Protocol", get_header("Sec-WebSocket-Protocol").c_str());

    wrap_MHD_queue_response(MHD_HTTP_SWITCHING_PROTOCOLS, resp);
    MHD_destroy_response(resp);
}

int Client::handle(const std::string& url, const std::string& method,
                   const char* upload_data, size_t* upload_data_size)
{
    if (!this_unit.running.load(std::memory_order_relaxed))
    {
        send_shutting_down_error();
        return MHD_YES;
    }
    else if (this_unit.cors && send_cors_preflight_request(method))
    {
        return MHD_YES;
    }
    else if (mxs::Config::get().gui && method == MHD_HTTP_METHOD_GET && serve_file(url))
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
            if (!auth(m_connection, url.c_str(), method.c_str()))
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

            if (*upload_data_size != 0)
            {
                m_data = std::string(upload_data, *upload_data_size);
            }

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
        wrap_MHD_queue_response(MHD_HTTP_BAD_REQUEST, response);
        MHD_destroy_response(response);
        return MHD_YES;
    }

    m_request.set_json(json);
    MXB_DEBUG("Request:\n%s", m_request.to_string().c_str());

    HttpResponse reply = is_auth_endpoint(m_request) ?
        generate_token(m_request) : resource_handle_request(m_request);

    int rc = MHD_NO;

    if ((m_ws_handler = reply.websocket_handler()))
    {
        if (m_request.get_header("Upgrade") == "websocket")
        {
            // The endpoint requested a WebSocket connection, start the upgrade
            upgrade_to_ws();
            rc = MHD_YES;
        }
        else
        {
            rc = queue_response(HttpResponse(MHD_HTTP_UPGRADE_REQUIRED));
        }
    }
    else if (auto cb = reply.callback())
    {
        rc = queue_delayed_response(cb);
    }
    else
    {
        rc = queue_response(reply);
    }

    return rc;
}

int Client::queue_response(const HttpResponse& reply)
{
    char* data = nullptr;
    size_t len = 0;

    MXB_DEBUG("Response:\n%s", reply.to_string().c_str());

    if (json_t* js = reply.get_response())
    {
        int flags = JSON_SORT_KEYS;

        if (m_request.is_falsy_option("pretty"))
        {
            flags |= JSON_COMPACT;
        }
        else
        {
            flags |= JSON_INDENT(4);
        }

        data = json_dumps(js, flags);
        len = strlen(data);
    }

    MHD_Response* response = MHD_create_response_from_buffer(len, data, MHD_RESPMEM_MUST_FREE);

    for (const auto& a : reply.get_headers())
    {
        MHD_add_response_header(response, a.first.c_str(), a.second.c_str());
    }

    if (this_unit.cors && !get_header("Origin").empty())
    {
        add_cors_headers(response);
    }

    add_extra_headers(response);

    // Prevent caching without verification
    MHD_add_response_header(response, "Cache-Control", "no-cache");

    for (const auto& c : reply.cookies())
    {
        MHD_add_response_header(response, MHD_HTTP_HEADER_SET_COOKIE, c.c_str());
    }

    int rval = wrap_MHD_queue_response(reply.get_code(), response);
    MHD_destroy_response(response);

    MXB_DEBUG("Response: HTTP %d", reply.get_code());

    return rval;
}

int Client::queue_delayed_response(const HttpResponse::Callback& cb)
{
    MHD_suspend_connection(m_connection);

    mxs::thread_pool().execute(
        [cb, this]() {
        queue_response(cb());
        MHD_resume_connection(m_connection);
    }, "mhd_resume");

    return MHD_YES;
}

HttpResponse Client::generate_token(const HttpRequest& request)
{
    int64_t token_age = 28800;
    auto max_age = request.get_option("max-age");

    if (!max_age.empty())
    {
        char* end;
        auto l = strtol(max_age.c_str(), &end, 10);

        if (l > 0 && l < INT_MAX && *end == '\0')
        {
            token_age = l;
        }
    }

    token_age = std::min(token_age, mxs::Config::get().admin_jwt_max_age.count());

    mxb_assert(m_account != mxs::USER_ACCOUNT_UNKNOWN);
    const char* type = m_account == mxs::USER_ACCOUNT_ADMIN ? CN_ADMIN : CN_BASIC;
    auto token = mxs::jwt::create(TOKEN_ISSUER, m_user, token_age, {{"account", type}});

    if (request.is_truthy_option("persist"))
    {
        // Store the token signature part in a HttpOnly cookie and the claims in a normal one. This allows
        // the token information to be displayed while preventing the actual token from leaking due to a
        // CSRF attack. This also prevents JavaScript from ever accessing the token which completely prevents
        // the token from leaking.
        HttpResponse reply = HttpResponse(MHD_HTTP_NO_CONTENT);
        reply.add_cookie(TOKEN_SIG, token, !max_age.empty() ? token_age : 0);
        return reply;
    }
    else
    {
        // Normal auth, return token as JSON
        return HttpResponse(MHD_HTTP_OK, json_pack("{s {s: s}}", "meta", "token", token.c_str()));
    }
}

bool Client::auth_with_token(const std::string& token, const char* method, const char* client_url)
{
    const auto& cnf = mxs::Config::get();
    bool rval = false;

    if (!cnf.admin_verify_url.empty())
    {
        // Authentication and authorization is being delegated to a remote server. If the GET request on the
        // configured URL works, the user is allowed access. The headers contain enough information to
        // uniquely identify the requested endpoint.
        mxb::http::Config config;
        std::string referer = (this_unit.using_ssl ? "https://" : "http://") + m_headers["host"] + client_url;
        config.headers[MHD_HTTP_HEADER_AUTHORIZATION] = "Bearer " + token;
        config.headers[MHD_HTTP_HEADER_REFERER] = referer;
        config.headers["X-Referrer-Method"] = method;   // Non-standard but we need something for the method

        auto response = mxb::http::get(cnf.admin_verify_url, config);

        if (response.is_success())
        {
            rval = true;
        }
        else
        {
            send_token_auth_error();

            if (cnf.admin_log_auth_failures.get())
            {
                MXB_WARNING("Request verification failed, %s. Request: %s %s",
                            response.to_string(response.code), method, client_url);
            }
        }
    }
    else
    {
        // Normal token authentication, tokens are generated and verified by MaxScale
        if (auto claims = mxs::jwt::decode(TOKEN_ISSUER, token))
        {
            auto user = claims->get("sub");
            mxs::user_account_type type = mxs::USER_ACCOUNT_UNKNOWN;

            if (auto account = claims->get("account"))
            {
                if (*account == CN_ADMIN)
                {
                    type = mxs::USER_ACCOUNT_ADMIN;
                }
                else if (*account == CN_BASIC)
                {
                    type = mxs::USER_ACCOUNT_BASIC;
                }
            }
            else if (user)
            {
                type = admin_inet_user_exists(user->c_str());
            }

            if (user && authorize_user(user->c_str(), type, method, client_url))
            {
                rval = true;
                m_user = std::move(user.value());
                m_account = type;
            }
            else
            {
                send_write_access_error();
            }
        }
        else
        {
            send_token_auth_error();
        }
    }


    return rval;
}

bool Client::auth(MHD_Connection* connection, const char* url, const char* method)
{
    bool rval = true;

    if (mxs::Config::get().admin_auth)
    {
        bool done = false;

        if (!is_auth_endpoint(m_request))
        {
            // Not the /auth endpoint, use the cookie or Bearer token
            auto cookie_token = m_request.get_cookie(TOKEN_SIG);
            auto token = get_header(MHD_HTTP_HEADER_AUTHORIZATION);

            if (!cookie_token.empty())
            {
                done = true;
                rval = auth_with_token(cookie_token, method, url);
            }
            else if (token.substr(0, 7) == "Bearer ")
            {
                done = true;
                rval = auth_with_token(token.substr(7), method, url);
            }
        }
        else if (!this_unit.using_ssl && mxs::Config::get().secure_gui)
        {
            // The /auth endpoint must be used with an encrypted connection
            done = true;
            rval = false;
            send_no_https_error();
        }

        if (!done)
        {
            rval = false;
            char* pw = NULL;
            char* user = MHD_basic_auth_get_username_password(connection, &pw);
            mxs::user_account_type type;

            if (!user || !pw || (type = admin_verify_inet_user(user, pw)) == mxs::USER_ACCOUNT_UNKNOWN)
            {
                if (mxs::Config::get().admin_log_auth_failures.get())
                {
                    MXB_WARNING("Authentication failed for '%s', %s. Request: %s %s",
                                user ? user : "",
                                pw ? "using password" : "no password",
                                method, url);
                }
            }
            else if (authorize_user(user, type, method, url))
            {
                MXB_INFO("Accept authentication from '%s', %s. Request: %s",
                         user ? user : "",
                         pw ? "using password" : "no password",
                         url);

                // Store the username for later in case we are generating a token
                m_user = user ? user : "";
                m_account = type;
                rval = true;
            }
            MXB_FREE(user);
            MXB_FREE(pw);

            if (!rval)
            {
                if (is_auth_endpoint(m_request))
                {
                    send_token_auth_error();
                }
                else
                {
                    send_basic_auth_error();
                }
            }
        }
    }

    m_state = rval ? Client::OK : Client::FAILED;

    return rval;
}

int cert_callback(gnutls_session_t session,
                  const gnutls_datum_t* req_ca_dn,
                  int nreqs,
                  const gnutls_pk_algorithm_t* pk_algos,
                  int pk_algos_length,
                  gnutls_pcert_st** pcert,
                  unsigned int* pcert_length,
                  gnutls_privkey_t* pkey)
{
    std::lock_guard guard(this_unit.lock);
    mxb_assert(this_unit.creds);

    if (this_unit.next_creds)
    {
        this_unit.creds.reset(this_unit.next_creds.release());
    }

    this_unit.creds->set(pcert, pcert_length, pkey);
    return 0;
}

bool mxs_admin_init()
{
    struct sockaddr_storage addr;
    const auto& config = mxs::Config::get();

    if (!load_ssl_certificates())
    {
        MXB_ERROR("Failed to load REST API TLS certificates.");
    }
    else if (!mxs::jwt::init())
    {
        MXB_ERROR("Failed to initialize JWT signature keys for the REST API.");
    }
    else if (host_to_sockaddr(config.admin_host.c_str(), config.admin_port, &addr))
    {
        int options = MHD_USE_EPOLL_INTERNAL_THREAD | MHD_USE_DEBUG | MHD_ALLOW_UPGRADE;

        if (addr.ss_family == AF_INET6)
        {
            options |= MHD_USE_DUAL_STACK;
        }

        if (this_unit.using_ssl)
        {
            options |= MHD_USE_SSL;
            MXB_NOTICE("The REST API will be encrypted, all requests must use HTTPS.");
        }
        else if (mxs::Config::get().gui && mxs::Config::get().secure_gui)
        {
            MXB_WARNING("The MaxScale GUI is enabled but encryption for the REST API is not enabled, "
                        "the GUI will not be enabled. Configure `admin_ssl_key` and `admin_ssl_cert` "
                        "to enable HTTPS or add `admin_secure_gui=false` to allow use of the GUI without encryption.");
        }

        // The port argument is only used for error reporting. The actual address and port that the daemon
        // binds to is in the `struct sockaddr`.
        this_unit.daemon = MHD_start_daemon(options, config.admin_port,
                                            NULL, NULL, handle_client, NULL,
                                            MHD_OPTION_EXTERNAL_LOGGER, admin_log_error, NULL,
                                            MHD_OPTION_NOTIFY_COMPLETED, close_client, NULL,
                                            MHD_OPTION_SOCK_ADDR, &addr,
                                            !this_unit.using_ssl ? MHD_OPTION_END :
                                            MHD_OPTION_HTTPS_CERT_CALLBACK, cert_callback,
                                            MHD_OPTION_HTTPS_PRIORITIES, this_unit.ssl_version.c_str(),
                                            this_unit.ssl_ca.empty() ? MHD_OPTION_END :
                                            MHD_OPTION_HTTPS_MEM_TRUST, this_unit.ssl_ca.c_str(),
                                            MHD_OPTION_END);
    }

    // Silence all other errors to prevent malformed requests from flooding the log
    this_unit.log_daemon_errors = false;

    return this_unit.daemon != NULL;
}

void mxs_admin_shutdown()
{
    // Using MHD_quiesce_daemon might be an option but we'd have to manage the socket ourselves and the
    // documentation doesn't say whether it deadlocks when a request is being processed. Having the daemon
    // thread reject connections after the shutdown has started is simpler and is guaranteed to work.
    this_unit.running.store(false, std::memory_order_relaxed);
}

void mxs_admin_finish()
{
    WebSocket::shutdown();
    MHD_stop_daemon(this_unit.daemon);
    MXB_NOTICE("Stopped MaxScale REST API");
}

bool mxs_admin_https_enabled()
{
    return this_unit.using_ssl;
}

bool mxs_admin_use_cors()
{
    return this_unit.cors;
}

void mxs_admin_enable_cors()
{
    this_unit.cors = true;
}

void mxs_admin_allow_origin(std::string_view origin)
{
    this_unit.accept_origin = origin;
}

bool mxs_admin_reload_tls()
{
    bool rval = true;
    mxb_assert(mxs::MainWorker::is_current());
    const auto& config = mxs::Config::get();
    const auto& cert = config.admin_ssl_cert;
    const auto& key = config.admin_ssl_key;

    if (!cert.empty() && !key.empty())
    {
        if (auto creds = Creds::create(cert, key))
        {
            std::lock_guard guard(this_unit.lock);

            if (mxs::jwt::init())
            {
                this_unit.next_creds = std::move(creds);
            }
            else
            {
                rval = false;
            }
        }
        else
        {
            rval = false;
        }
    }
    else
    {
        // TLS not enabled, just reload JWT signing keys
        rval = mxs::jwt::init();
    }

    return rval;
}
