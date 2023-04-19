/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "password.hh"
#include "../pgprotocoldata.hh"
#include <openssl/md5.h>
#include "common.hh"

using std::string;
using std::string_view;

namespace
{
std::array<uint8_t, 9> password_request = {'R', 0, 0, 0, 8, 0, 0, 0, 3};
size_t hex_md5_len = 2 * MD5_DIGEST_LENGTH;
const int scram_klen = SHA256_DIGEST_LENGTH;

void bin2hex_lower(const uint8_t* in, unsigned int len, char* out)
{
    const char hex_lower[] = "0123456789abcdef";
    const uint8_t* in_end = in + len;
    for (; in != in_end; ++in)
    {
        *out++ = hex_lower[*in >> 4];
        *out++ = hex_lower[*in & 0x0F];
    }
    *out = '\0';
}
}

GWBUF PasswordClientAuth::authentication_request()
{
    return GWBUF(password_request.data(), password_request.size());
}

PasswordClientAuth::ExchRes PasswordClientAuth::exchange(GWBUF&& input, PgProtocolData& session)
{
    ExchRes rval;
    mxb_assert(input.length() >= 5);    // Protocol code should have checked.
    if (input[0] == 'p')
    {
        // The client packet should work as a password token as is.
        session.auth_data().client_token.assign(input.data(), input.end());
        rval.status = ExchRes::Status::READY;
    }
    return rval;
}

PgClientAuthenticator::AuthRes
PasswordClientAuth::authenticate(PgProtocolData& session)
{
    AuthRes rval;
    const auto& auth_data = session.auth_data();
    const auto& client_token = auth_data.client_token;
    const auto& secret = auth_data.user_entry.authid_entry.password;

    auto empty_pw_len = pg::HEADER_LEN + 1;     // pw ends in zero.
    if (client_token.size() > empty_pw_len)
    {
        rval.status = AuthRes::Status::FAIL_WRONG_PW;
        auto* ptr = reinterpret_cast<const char*>(client_token.data() + pg::HEADER_LEN);
        string_view password {ptr, client_token.size() - empty_pw_len};

        // If secret is empty (password has not been set), fail password check like a real server.
        // The secret may be in SCRAM or md5-formats.
        if (secret.length() == (3 + hex_md5_len) && secret.substr(0, 3) == "md5")
        {
            string_view secret_md5 {secret.data() + 3, hex_md5_len};
            if (check_password_md5_hash(password, auth_data.user, secret_md5))
            {
                rval.status = AuthRes::Status::SUCCESS;
            }
        }
        else if (!secret.empty())
        {
            // Assume scram format.
            auto scram_data = parse_scram_password(secret);
            if (scram_data)
            {
                if (check_password_scram_hash(password, *scram_data))
                {
                    rval.status = AuthRes::Status::SUCCESS;
                }
            }
            else
            {
                MXB_ERROR("Password hash for role '%s' is of unknown format.", auth_data.user.c_str());
            }
        }
    }

    return rval;
}

bool PasswordClientAuth::check_password_md5_hash(std::string_view pw, std::string_view username,
                                                 std::string_view hash) const
{
    // TODO: ensure that user and pw are not crazy long.
    mxb_assert(hash.length() == hex_md5_len);
    auto salted_len = pw.length() + username.length();
    uint8_t salted_pw[salted_len];
    memcpy(salted_pw, pw.data(), pw.length());
    memcpy(salted_pw + pw.length(), username.data(), username.length());
    uint8_t digest[MD5_DIGEST_LENGTH];
    MD5(salted_pw, salted_len, digest);
    char hex_digest[hex_md5_len + 1];
    bin2hex_lower(digest, MD5_DIGEST_LENGTH, hex_digest);
    return memcmp(hex_digest, hash.data(), hash.length()) == 0;
}

bool PasswordClientAuth::check_password_scram_hash(std::string_view pw, const ScramUser& scram) const
{
    bool rval = false;
    int iterations;
    if (mxb::get_int(scram.iter, &iterations))
    {
        auto algo = EVP_sha256();
        auto salted_pw = scram_salted_password(pw, scram.salt, iterations, algo);
        if (salted_pw)
        {
            uint8_t computed_key[scram_klen];
            const char extra[] = "Server Key";
            unsigned int outlen = sizeof(computed_key);
            HMAC_CTX* ctx = HMAC_CTX_new();
            if (HMAC_Init_ex(ctx, salted_pw->data(), salted_pw->size(), algo, nullptr) == 1
                && HMAC_Update(ctx, (uint8_t*)extra, sizeof(extra) - 1) == 1
                && HMAC_Final(ctx, computed_key, &outlen) == 1)
            {
                int rc = memcmp(computed_key, scram.server_key.data(), sizeof(computed_key));
                rval = (rc == 0);
            }
            HMAC_CTX_free(ctx);
        }
    }

    return rval;
}

std::optional<Digest>
PasswordClientAuth::scram_salted_password(std::string_view pw, std::string_view salt, int iterations,
                                          const EVP_MD* algo) const
{
    // The cleartext password would normally need to be normalized to handle utf-8. Disregard this for
    // now and only support ascii-passwords.
    bool is_ascii = true;
    const char* ptr = pw.data();
    auto end = ptr + pw.length();
    for (; ptr < end; ptr++)
    {
        if ((unsigned char)(*ptr) & 0x80)
        {
            is_ascii = false;
            break;
        }
    }

    std::optional<Digest> rval;
    if (is_ascii)
    {
        auto salt_bin = mxs::from_base64(salt);
        uint32_t one = htonl(1);

        uint8_t Ui[scram_klen];
        uint8_t Ui_prev[scram_klen];
        uint8_t salted_password[scram_klen];

        HMAC_CTX* ctx = HMAC_CTX_new();

        // First iteration
        unsigned int mdlen = sizeof(Ui_prev);
        if (HMAC_Init_ex(ctx, (uint8_t*)pw.data(), pw.length(), algo, nullptr) == 1
            && HMAC_Update(ctx, salt_bin.data(), salt_bin.size()) == 1
            && HMAC_Update(ctx, (uint8_t*)&one, sizeof(uint32_t)) == 1
            && HMAC_Final(ctx, Ui_prev, &mdlen) == 1)
        {
            memcpy(salted_password, Ui_prev, scram_klen);

            bool hmac_success = true;
            // Rest of iterations
            for (int i = 2; i <= iterations; i++)
            {
                mdlen = sizeof(Ui_prev);
                if (HMAC_Init_ex(ctx, (uint8_t*) pw.data(), pw.length(), algo, nullptr) == 1
                    && HMAC_Update(ctx, Ui_prev, scram_klen) == 1
                    && HMAC_Final(ctx, Ui, &mdlen) == 1)
                {
                    for (int j = 0; j < scram_klen; j++)
                    {
                        salted_password[j] ^= Ui[j];
                    }
                    memcpy(Ui_prev, Ui, scram_klen);
                }
                else
                {
                    hmac_success = false;
                    break;
                }
            }

            HMAC_CTX_free(ctx);
            if (hmac_success)
            {
                Digest dig;
                memcpy(dig.data(), salted_password, scram_klen);
                rval = dig;
            }
        }
    }
    else
    {
        MXB_ERROR("Client sent a non-ascii password, which is not supported.");
    }
    return rval;
}

std::optional<GWBUF> PasswordBackendAuth::exchange(GWBUF&& input, PgProtocolData& session)
{
    std::optional<GWBUF> rval;
    if (input.length() == password_request.size()
        && memcmp(input.data(), password_request.data(), password_request.size()) == 0)
    {
        const auto& token = session.auth_data().client_token;
        rval = GWBUF(token.data(), token.size());
    }
    return rval;
}

std::unique_ptr<PgClientAuthenticator> PasswordAuthModule::create_client_authenticator() const
{
    return std::make_unique<PasswordClientAuth>();
}

std::unique_ptr<PgBackendAuthenticator> PasswordAuthModule::create_backend_authenticator() const
{
    return std::make_unique<PasswordBackendAuth>();
}

std::string PasswordAuthModule::name() const
{
    return "password";
}
