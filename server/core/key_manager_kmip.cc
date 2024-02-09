/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/key_manager_kmip.hh"

extern "C"
{
#include <kmip/kmip.h>
#include <kmip/kmip_bio.h>
#include <kmip/kmip_memset.h>
}

#include <openssl/ssl.h>

namespace
{

using Opt = mxs::config::ParamPath::Options;

static mxs::config::Specification s_spec("key_manager_kmip", mxs::config::Specification::GLOBAL);

static mxs::config::ParamString s_host(&s_spec, "host", "KMIP server host");
static mxs::config::ParamInteger s_port(&s_spec, "port", "KMIP server port");
static mxs::config::ParamPath s_ca(&s_spec, "ca", "CA certificate", Opt::R, "");
static mxs::config::ParamPath s_cert(&s_spec, "cert", "Client certificate", Opt::R);
static mxs::config::ParamPath s_key(&s_spec, "key", "Private key", Opt::R);

const char* get_kmip_error(int code)
{
    switch (code)
    {
    case KMIP_NOT_IMPLEMENTED:
        return "KMIP_NOT_IMPLEMENTED";

    case KMIP_ERROR_BUFFER_FULL:
        return "KMIP_ERROR_BUFFER_FULL";

    case KMIP_ERROR_ATTR_UNSUPPORTED:
        return "KMIP_ERROR_ATTR_UNSUPPORTED";

    case KMIP_TAG_MISMATCH:
        return "KMIP_TAG_MISMATCH";

    case KMIP_TYPE_MISMATCH:
        return "KMIP_TYPE_MISMATCH";

    case KMIP_LENGTH_MISMATCH:
        return "KMIP_LENGTH_MISMATCH";

    case KMIP_PADDING_MISMATCH:
        return "KMIP_PADDING_MISMATCH";

    case KMIP_BOOLEAN_MISMATCH:
        return "KMIP_BOOLEAN_MISMATCH";

    case KMIP_ENUM_MISMATCH:
        return "KMIP_ENUM_MISMATCH";

    case KMIP_ENUM_UNSUPPORTED:
        return "KMIP_ENUM_UNSUPPORTED";

    case KMIP_INVALID_FOR_VERSION:
        return "KMIP_INVALID_FOR_VERSION";

    case KMIP_MEMORY_ALLOC_FAILED:
        return "KMIP_MEMORY_ALLOC_FAILED";

    case KMIP_IO_FAILURE:
        return "KMIP_IO_FAILURE";

    case KMIP_EXCEED_MAX_MESSAGE_SIZE:
        return "KMIP_EXCEED_MAX_MESSAGE_SIZE";

    case KMIP_MALFORMED_RESPONSE:
        return "KMIP_MALFORMED_RESPONSE";

    case KMIP_OBJECT_MISMATCH:
        return "KMIP_OBJECT_MISMATCH";

        // The following errors are from the libkmip manual and they indicate server-side errors. Currently
        // the server error is not printed due to the awkward API of libkmip.
    case 1:
        return "KMIP_STATUS_OPERATION_FAILED";

    case 2:
        return "KMIP_STATUS_OPERATION_PENDING";

    case 3:
        return "KMIP_STATUS_OPERATION_UNDONE";

    default:
        return "UNKNOWN";
    }
}

std::vector<uint8_t> load_key(std::string host, int64_t port, std::string ca,
                              std::string cert, std::string key, std::string id)
{
    std::vector<uint8_t> rval;
    SSL_CTX* ctx = SSL_CTX_new(SSLv23_method());

    if (SSL_CTX_use_certificate_file(ctx, cert.c_str(), SSL_FILETYPE_PEM) != 1)
    {
        MXB_ERROR("Loading the client certificate failed: %s", mxb::get_openssl_errors().c_str());
    }
    else
    {
        if (SSL_CTX_use_PrivateKey_file(ctx, key.c_str(), SSL_FILETYPE_PEM) != 1)
        {
            MXB_ERROR("Loading the client key failed: %s", mxb::get_openssl_errors().c_str());
        }
        else
        {
            if (ca.empty() && SSL_CTX_set_default_verify_paths(ctx) != 0)
            {
                MXB_ERROR("Failed to set default CA verify paths: %s", mxb::get_openssl_errors().c_str());
            }
            else if (!ca.empty() && SSL_CTX_load_verify_locations(ctx, ca.c_str(), NULL) != 1)
            {
                MXB_ERROR("Loading the CA file failed: %s", mxb::get_openssl_errors().c_str());
            }
            else
            {
                if (BIO* bio = BIO_new_ssl_connect(ctx))
                {
                    SSL* ssl = nullptr;
                    BIO_get_ssl(bio, &ssl);
                    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
                    BIO_set_conn_hostname(bio, host.c_str());
                    BIO_set_conn_port(bio, std::to_string(port).c_str());

                    if (BIO_do_connect(bio) != 1)
                    {
                        MXB_ERROR("BIO_do_connect failed: %s", mxb::get_openssl_errors().c_str());
                    }
                    else
                    {
                        KMIP kmip_context {0};
                        kmip_init(&kmip_context, NULL, 0, KMIP_1_0);

                        int len = 0;
                        char* out = nullptr;

                        int result = kmip_bio_get_symmetric_key_with_context(
                            &kmip_context, bio, id.data(), id.size(), &out, &len);


                        if (result != 0)
                        {
                            std::string errmsg;

                            if (kmip_context.error_message && kmip_context.error_message_size)
                            {
                                errmsg.assign(kmip_context.error_message, kmip_context.error_message_size);
                            }

                            MXB_ERROR("Failed to get key '%s': %d, %s, %s", id.c_str(), result,
                                      get_kmip_error(result), errmsg.c_str());
                        }
                        else
                        {
                            rval.assign(out, out + len);
                            free(out);
                        }

                        kmip_destroy(&kmip_context);
                    }

                    BIO_free_all(bio);
                }
                else
                {
                    MXB_ERROR("BIO_new_ssl_connect failed: %s", mxb::get_openssl_errors().c_str());
                }
            }
        }
    }

    SSL_CTX_free(ctx);
    return rval;
}
}

// static
mxs::config::Specification* KMIPKey::specification()
{
    return &s_spec;
}

// static
std::unique_ptr<mxs::KeyManager::MasterKey> KMIPKey::create(const mxs::ConfigParameters& params)
{
    KMIPKey::Config config;
    std::unique_ptr<KMIPKey> rv;

    if (s_spec.validate(params) && config.configure(params))
    {
        rv = std::make_unique<KMIPKey>(std::move(config));
    }

    return rv;
}

KMIPKey::KMIPKey(Config config)
    : m_config(std::move(config))
{
}

KMIPKey::Config::Config()
    : mxs::config::Configuration("key_manager_kmip", &s_spec)
{
    add_native(&Config::host, &s_host);
    add_native(&Config::port, &s_port);
    add_native(&Config::ca, &s_ca);
    add_native(&Config::cert, &s_cert);
    add_native(&Config::key, &s_key);
}

std::tuple<bool, uint32_t, std::vector<uint8_t>>
KMIPKey::get_key(const std::string& id, uint32_t version) const
{
    auto key = load_key(m_config.host, m_config.port, m_config.ca, m_config.cert, m_config.key, id);
    return {!key.empty(), MasterKey::NO_VERSIONING, key};
}
