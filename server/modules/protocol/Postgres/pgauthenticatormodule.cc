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

#include "pgauthenticatormodule.hh"

#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>

#include <optional>

namespace
{
#define NONCE_SIZE 18

// This is just an example password which will not work. It needs to be replaced with the actual entry
// from the database for the authentication to work.
const char* THE_PASSWORD =
    "SCRAM-SHA-256$4096:fcyQNek/oqCBB5+HBZYCBw==$IyjIV2enCngF0p4pOouPlvKyISzmHFdoXeM0V/+nUr4=:+vF1tu+XCwHxdmfo1X3zpgvDXpCx06LJjJ2emDgXCs0=";

std::string create_nonce()
{
    std::array<uint8_t, NONCE_SIZE> nonce;
    RAND_bytes(nonce.data(), nonce.size());
    // This is what e.g. pgbouncer does when generating the nonce.
    return mxs::to_base64(nonce.data(), nonce.size());
}

std::optional<ScramUser> parse_scram_password(std::string_view pw)
{
    /**
     * The passwords are of the following form:
     *
     *   SCRAM-SHA-256$<iteration count>:<salt>$<StoredKey>:<ServerKey>
     *
     * Here's an example hash for the user "maxuser" with the password "maxpwd":
     *
     * SCRAM-SHA-256$4096:fcyQNek/oqCBB5+HBZYCBw==$IyjIV2enCngF0p4pOouPlvKyISzmHFdoXeM0V/+nUr4=:+vF1tu+XCwHxdmfo1X3zpgvDXpCx06LJjJ2emDgXCs0=
     */

    std::string_view prefix = "SCRAM-SHA-256$";

    if (pw.substr(0, prefix.size()) == prefix)
    {
        pw.remove_prefix(prefix.size());

        auto [iter_and_salt, stored_and_server] = mxb::split(pw, "$");
        const auto [iter, salt] = mxb::split(iter_and_salt, ":");
        const auto [stored, server] = mxb::split(stored_and_server, ":");

        if (!iter.empty() && !salt.empty() && !stored.empty() && !server.empty())
        {
            ScramUser user;

            user.iter = iter;
            user.salt = salt;
            user.stored_key = stored;
            user.server_key = server;

            return user;
        }
    }

    return {};
}

GWBUF create_authentication_sasl()
{
    std::string_view mech = "SCRAM-SHA-256";
    GWBUF buffer(9 + mech.size() + 1 + 1);
    uint8_t* ptr = buffer.data();

    // AuthenticationSASL
    *ptr++ = pg::AUTHENTICATION;
    ptr += pg::set_uint32(ptr, buffer.length() - 1);
    ptr += pg::set_uint32(ptr, pg::AUTH_SASL);
    ptr += pg::set_string(ptr, mech);
    *ptr++ = 0;
    return buffer;
}

GWBUF create_sasl_initial_response(std::string& client_first_message_bare)
{
    client_first_message_bare = mxb::cat("n=,r=", create_nonce());
    std::string client_first_message = mxb::cat("n,,", client_first_message_bare);

    std::string_view mech = "SCRAM-SHA-256";
    GWBUF response(pg::HEADER_LEN + mech.length() + 1 + 4 + client_first_message.size());
    uint8_t* ptr = response.data();

    *ptr++ = pg::SASL_INITIAL_RESPONSE;
    ptr += pg::set_uint32(ptr, response.length() - 1);
    ptr += pg::set_string(ptr, mech);
    ptr += pg::set_uint32(ptr, client_first_message.size());
    memcpy(ptr, client_first_message.data(), client_first_message.size());

    return response;
}

GWBUF create_authentication_sasl_continue(const GWBUF& buffer, std::string& server_first_message)
{
    const uint8_t* ptr = buffer.data();
    uint8_t cmd = *ptr++;
    mxb_assert(cmd == pg::SASL_INITIAL_RESPONSE);
    uint32_t len = pg::get_uint32(ptr);
    ptr += 4;

    auto mech = pg::get_string(ptr);
    mxb_assert(mech == "SCRAM-SHA-256");
    ptr += mech.size() + 1;

    uint32_t msg_len = pg::get_uint32(ptr);
    ptr += 4;
    std::string first_message;
    first_message.assign(ptr, ptr + msg_len);
    std::string client_nonce;
    std::string client_user;

    for (auto tok : mxb::strtok(first_message, ","))
    {
        if (tok.substr(0, 2) == "r=")
        {
            client_nonce = tok.substr(2);
            break;
        }
        else if (tok.substr(0, 2) == "n=")
        {
            // The user should always be empty
            client_user = tok.substr(2);
        }
    }

    std::string server_nonce = create_nonce();

    // TODO: Get this from the UserAccountManager
    auto user = parse_scram_password(THE_PASSWORD);

    // This is needed for the final step
    server_first_message = mxb::cat("r=", client_nonce, server_nonce,
                                    ",s=", user->salt,
                                    ",i=", user->iter);

    GWBUF response(9 + server_first_message.size());
    uint8_t* out = response.data();

    // AuthenticationSASLContinue
    *out++ = pg::AUTHENTICATION;
    out += pg::set_uint32(out, response.length() - 1);
    out += pg::set_uint32(out, pg::AUTH_SASL_CONTINUE);
    memcpy(out, server_first_message.data(), server_first_message.size());

    return response;
}

GWBUF create_sasl_response(const GWBUF& buffer,
                           std::string_view client_first_message_bare,
                           std::string_view server_first_message,
                           const std::array<uint8_t, SHA256_DIGEST_LENGTH>& client_key)
{
    uint32_t len = pg::get_uint32(buffer.data() + 1) - 8;
    std::string_view msg((const char*)buffer.data() + 9, len);
    std::string nonce;

    for (auto token : mxb::strtok(msg, ","))
    {
        if (token.substr(0, 2) == "r=")
        {
            // The server sends the final combined nonce. Since we have the ClientKey, we don't need the salt
            // or the iteration count.
            nonce = std::move(token);
            break;
        }
    }

    // Without a channel bind, this is always "c=biws". Support for channel binding requires the base64 value
    // to be calculated.
    auto client_final_message_without_proof = mxb::cat("c=biws,", nonce);

    // See: https://www.rfc-editor.org/rfc/rfc5802#section-3
    auto auth_message = mxb::cat(client_first_message_bare, ",",
                                 server_first_message, ",",
                                 client_final_message_without_proof);

    // TODO: Get this from the UserAccountManager
    auto user = parse_scram_password(THE_PASSWORD);

    auto stored = mxs::from_base64(user->stored_key);

    std::array<uint8_t, SHA256_DIGEST_LENGTH> client_sig;
    unsigned int client_sig_size = client_sig.size();

    HMAC(EVP_sha256(), stored.data(), stored.size(),
         (uint8_t*)auth_message.data(), auth_message.size(),
         client_sig.data(), &client_sig_size);

    std::array<uint8_t, SHA256_DIGEST_LENGTH> client_proof;

    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        client_proof[i] = client_key[i] ^ client_sig[i];
    }

    auto client_proof_b64 = mxs::to_base64(client_proof.data(), client_proof.size());
    std::string client_final_message = mxb::cat(client_final_message_without_proof, ",p=", client_proof_b64);

    GWBUF response(pg::HEADER_LEN + client_final_message.length());
    uint8_t* ptr = response.data();

    *ptr++ = pg::SASL_RESPONSE;
    ptr += pg::set_uint32(ptr, response.length() - 1);
    memcpy(ptr, client_final_message.data(), client_final_message.size());

    return response;
}

GWBUF create_authentication_sasl_final(const GWBUF& buffer,
                                       std::string_view client_first_message_bare,
                                       std::string_view server_first_message,
                                       std::array<uint8_t, SHA256_DIGEST_LENGTH>& client_key)
{
    uint8_t cmd = buffer[0];
    mxb_assert(cmd == pg::SASL_RESPONSE);
    uint32_t len = pg::get_uint32(buffer.data() + 1);
    std::string_view client_msg((const char*)buffer.data() + pg::HEADER_LEN, len - 4);

    std::string nonce;
    std::string channel_bind;
    std::vector<uint8_t> client_proof;

    for (auto tok : mxb::strtok(client_msg, ","))
    {
        if (tok[0] == 'r')
        {
            nonce = tok;
        }
        else if (tok[0] == 'c')
        {
            channel_bind = tok;
        }
        else if (tok[0] == 'p')
        {
            client_proof = mxs::from_base64(tok.substr(2));
        }
    }

    // See: https://www.rfc-editor.org/rfc/rfc5802#section-3
    auto client_final_message_without_proof = mxb::cat(channel_bind, ",", nonce);
    auto auth_message = mxb::cat(client_first_message_bare, ",",
                                 server_first_message, ",",
                                 client_final_message_without_proof);

    // TODO: Get this from the UserAccountManager
    auto user = parse_scram_password(THE_PASSWORD);

    auto stored = mxs::from_base64(user->stored_key);
    auto server = mxs::from_base64(user->server_key);
    std::array<uint8_t, SHA256_DIGEST_LENGTH> client_sig;
    unsigned int client_sig_size = client_sig.size();

    HMAC(EVP_sha256(), stored.data(), stored.size(),
         (uint8_t*)auth_message.data(), auth_message.size(),
         client_sig.data(), &client_sig_size);

    mxb_assert(client_proof.size() == SHA256_DIGEST_LENGTH);

    // Output the ClientKey
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        client_key[i] = client_proof[i] ^ client_sig[i];
    }

    std::array<uint8_t, SHA256_DIGEST_LENGTH> verification;
    SHA256(client_key.data(), client_key.size(), verification.data());

    if (memcmp(verification.data(), stored.data(), stored.size()) != 0)
    {
        return GWBUF();
    }

    std::array<uint8_t, SHA256_DIGEST_LENGTH> server_proof;
    unsigned int server_proof_size = server_proof.size();
    HMAC(EVP_sha256(), server.data(), server.size(),
         (uint8_t*)auth_message.data(), auth_message.size(),
         server_proof.data(), &server_proof_size);

    std::string server_final = "v=" + mxs::to_base64(server_proof.data(), server_proof.size());

    GWBUF response(9 + server_final.size());
    uint8_t* ptr = response.data();

    // AuthenticationSASLFinal
    *ptr++ = pg::AUTHENTICATION;
    ptr += pg::set_uint32(ptr, 8 + server_final.size());
    ptr += pg::set_uint32(ptr, pg::AUTH_SASL_FINAL);
    memcpy(ptr, server_final.data(), server_final.size());
    ptr += server_final.size();

    // The AuthenticationOk, BackendKeyData and ReadyForQuery messages also need to be sent to the client

    return response;
}

bool verify_authentication_sasl_final(const GWBUF& buffer)
{
    // TODO: Actually verify the server's response
    return true;
}
}

PgAuthenticatorModule::~PgAuthenticatorModule()
{
}

std::string PgAuthenticatorModule::supported_protocol() const
{
    return MXB_MODULE_NAME;
}

std::string PgAuthenticatorModule::name() const
{
    return MXB_MODULE_NAME;
}
