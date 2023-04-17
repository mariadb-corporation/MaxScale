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

#include "scram-sha-256.hh"
#include <openssl/rand.h>
#include <maxbase/format.hh>
#include "../pgprotocoldata.hh"

using std::string;
using std::string_view;

namespace
{
const std::string MECH = "SCRAM-SHA-256";
const int NONCE_SIZE = 18;

template<class Key, class Data>
Digest hmac(const Key& k, const Data& d)
{
    Digest digest;
    unsigned int size = digest.size();

    HMAC(EVP_sha256(), (uint8_t*)k.data(), k.size(), (uint8_t*)d.data(), d.size(), digest.data(), &size);
    mxb_assert(size == digest.size());

    return digest;
}

Digest digest_xor(const Digest& lhs, const Digest& rhs)
{
    Digest result{};

    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        result[i] = lhs[i] ^ rhs[i];
    }

    return result;
}

template<class Input>
Digest hash(const Input& in)
{
    Digest digest;
    SHA256(in.data(), in.size(), digest.data());
    return digest;
}

std::string create_nonce()
{
    std::array<uint8_t, NONCE_SIZE> nonce{};
    RAND_bytes(nonce.data(), nonce.size());
    // This is what e.g. pgbouncer does when generating the nonce.
    return mxs::to_base64(nonce);
}
}
// This is just an example password which will not work. It needs to be replaced with the actual entry
// from the database for the authentication to work.
const char* THE_PASSWORD =
    "SCRAM-SHA-256$4096:fcyQNek/oqCBB5+HBZYCBw==$IyjIV2enCngF0p4pOouPlvKyISzmHFdoXeM0V/+nUr4=:+vF1tu+XCwHxdmfo1X3zpgvDXpCx06LJjJ2emDgXCs0=";

GWBUF ScramClientAuth::authentication_request()
{
    GWBUF buffer(9 + MECH.size() + 1 + 1);
    uint8_t* ptr = buffer.data();

    // AuthenticationSASL
    *ptr++ = pg::AUTHENTICATION;
    ptr += pg::set_uint32(ptr, buffer.length() - 1);
    ptr += pg::set_uint32(ptr, pg::AUTH_SASL);
    ptr += pg::set_string(ptr, MECH);
    *ptr++ = 0;
    return buffer;
}

PgClientAuthenticator::ExchRes ScramClientAuth::exchange(GWBUF&& input, PgProtocolData& session)
{
    ExchRes rval;
    // All packets from client should have 'p' as first byte.
    mxb_assert(input.length() >= 5);
    if (input[0] == 'p')
    {
        switch (m_state)
        {
        case State::INIT:
            {
                // Client should have responded with SASLInitialResponse.
                auto resp = read_sasl_initial_response(input);
                if (resp)
                {
                    if (resp->mech == MECH)
                    {
                        if (resp->client_data.empty())
                        {
                            // Allowed, send empty challenge.
                            rval.packet = create_sasl_continue({});
                            rval.status = ExchRes::Status::INCOMPLETE;
                            m_state = State::INIT_CONT;
                        }
                        else
                        {
                            GWBUF out = sasl_handle_client_first_msg(resp->client_data, session);
                            if (out)
                            {
                                rval.packet = std::move(out);
                                rval.status = ExchRes::Status::INCOMPLETE;
                                m_state = State::SALT_SENT;
                            }
                        }
                    }
                    else
                    {
                        // Client should not be attempting any other mechanism since our authentication
                        // request only listed SCRAM-SHA-256.
                        MXB_ERROR("Client is trying to use an unrecognized SASL mechanism.");
                    }
                }
            }
            break;

        case State::INIT_CONT:
            {
                auto resp = read_sasl_response(input);
                if (!resp.empty())
                {
                    GWBUF out = sasl_handle_client_first_msg(resp, session);
                    if (out)
                    {
                        rval.packet = std::move(out);
                        rval.status = ExchRes::Status::INCOMPLETE;
                        m_state = State::SALT_SENT;
                    }
                }
            }
            break;

        case State::SALT_SENT:
            {
                auto resp = read_sasl_response(input);
                if (!resp.empty())
                {
                    auto [prot_ok, out] = sasl_handle_client_proof(resp, session);
                    if (prot_ok)
                    {
                        rval.packet = std::move(out);
                        rval.status = ExchRes::Status::READY;
                        m_state = State::READY;
                    }
                }
            }
            break;

        case State::READY:
            mxb_assert(!true);
            break;
        }
    }

    return rval;
}

PgClientAuthenticator::AuthRes ScramClientAuth::authenticate(PgProtocolData& session)
{
    // Client token already checked in exchange().
    AuthRes rval;
    rval.status = session.auth_data().client_token.empty() ? AuthRes::Status::FAIL_WRONG_PW :
        AuthRes::Status::SUCCESS;
    return rval;
}

GWBUF ScramClientAuth::create_sasl_continue(string_view response)
{
    GWBUF rval(9 + response.size());
    auto ptr = rval.data();
    *ptr++ = pg::AUTHENTICATION;
    ptr += pg::set_uint32(ptr, 8 + response.size());
    ptr += pg::set_uint32(ptr, pg::AUTH_SASL_CONTINUE);
    if (!response.empty())
    {
        memcpy(ptr, response.data(), response.size());
    }
    return rval;
}

std::optional<ScramClientAuth::InitialResponse>
ScramClientAuth::read_sasl_initial_response(const GWBUF& input)
{
    std::optional<InitialResponse> rval;
    auto ptr = input.data() + pg::HEADER_LEN;
    const auto end = input.end();
    if (end > ptr && memchr(ptr, '\0', end - ptr))
    {
        InitialResponse resp;
        resp.mech = pg::get_string(ptr);
        ptr += resp.mech.length() + 1;

        if (end - ptr >= 4)
        {
            auto client_resp_len = pg::get_uint32(ptr);
            ptr += 4;
            if (client_resp_len > 0)
            {
                if (client_resp_len == (end - ptr))
                {
                    resp.client_data = {reinterpret_cast<const char*>(ptr), client_resp_len};
                    rval = resp;
                }
            }
            else
            {
                rval = resp;
            }
        }
    }
    return rval;
}

std::string_view ScramClientAuth::read_sasl_response(const GWBUF& input)
{
    string_view rval;
    if (input.length() > pg::HEADER_LEN)
    {
        rval = {reinterpret_cast<const char*>(input.data()) + pg::HEADER_LEN,
                input.length() - pg::HEADER_LEN};
    }
    return rval;
}

/**
 * Parse and check SASL data. If valid, return AuthenticationSASLContinue packet.
 *
 * @param sasl_data Mechanism data
 * @param session Protocol data
 * @return Packet to client, if successful.
 */
GWBUF ScramClientAuth::sasl_handle_client_first_msg(std::string_view sasl_data, PgProtocolData& session)
{
    GWBUF rval;
    // The client message has several fields that are not essential for the currently supported features.
    // Only check that they seem reasonable. Closer inspection may be added later.

    if (sasl_data.length() >= 8)
    {
        // Should start with either 'n,,' or 'y,,'.
        auto start = sasl_data.substr(0, 3);
        if (start == "n,," || start == "y,,")
        {
            m_cbind_flag = start[0];

            // Next should be n=username,r=nonce[,extensions].
            string_view client_first_message_bare = sasl_data.substr(3);
            auto [user, nonce_extensions] = mxb::split(client_first_message_bare, ",");
            if (user.substr(0, 2) == "n=")
            {
                // Ignore the username for now.
                if (!nonce_extensions.empty())
                {
                    auto [nonce, extensions] = mxb::split(nonce_extensions, ",");
                    if (nonce.substr(0, 2) == "r=" && nonce.length() > 2)
                    {
                        m_client_first_message_bare = client_first_message_bare;
                        m_client_nonce = nonce.substr(2);
                        m_server_nonce = create_nonce();

                        auto scram = parse_scram_password(
                            session.auth_data().user_entry.authid_entry.password);
                        if (scram)
                        {
                            m_stored_key = scram->stored_key;
                            m_server_key = scram->server_key;

                            m_server_first_message = mxb::string_printf(
                                "r=%s%s,s=%s,i=%s", m_client_nonce.c_str(), m_server_nonce.c_str(),
                                scram->salt.c_str(), scram->iter.c_str());
                            rval = create_sasl_continue(m_server_first_message);
                        }
                        else
                        {
                            MXB_ERROR("Password hash for role '%s' is not in SCRAM format.",
                                      session.auth_data().user.c_str());
                        }
                    }
                }
            }

            if (m_client_nonce.empty())
            {
                MXB_ERROR("Client sent malformed SCRAM message.");
            }
        }
        else
        {
            MXB_ERROR("Client uses unsupported SASL features. Channel binding and authorization identity are "
                      "not supported.");
        }
    }

    return rval;
}

/**
 * Parse and check SCRAM proof. If proof is valid (= client has correct password), return
 * AuthenticationSASLFinal packet.
 *
 * @param sasl_data Mechanism data
 * @param session Protocol data
 * @return {true, packet} if authentication succeeded. {true, empty} if authentication failed. {false, empty}
 * if client sent malformed or erroneous messages.
 */
std::tuple<bool, GWBUF>
ScramClientAuth::sasl_handle_client_proof(std::string_view sasl_data, PgProtocolData& session)
{
    bool protocol_ok = false;
    GWBUF sasl_final;

    auto rsplit = [](std::string_view str, char delim) -> std::pair<string_view, string_view> {
        auto pos = str.rfind(delim);
        return {str.substr(0, pos), pos != std::string_view::npos ? str.substr(pos + 1) : ""};
    };

    auto [msg_without_proof, proof] = rsplit(sasl_data, ',');
    auto [ch_binding, nonce_extensions] = mxb::split(msg_without_proof, ",");
    auto [nonce, extensions] = mxb::split(nonce_extensions, ",");

    if (!ch_binding.empty() && !nonce.empty() && !proof.empty())
    {
        // Not using channel binding yet, so should start with c=biws or c=eSws.
        if ((m_cbind_flag == 'n' && ch_binding == "c=biws")
            || (m_cbind_flag == 'y' && ch_binding == "c=eSws"))
        {
            if (nonce.substr(0, 2) == "r=" && proof.substr(0, 2) == "p=")
            {
                if (nonces_match(nonce.substr(2)))
                {
                    // Because scram sends a separate AuthenticationSASLFinal before AuthenticationOk,
                    // need to check the password here. This means scram does not work with
                    // "skip_authentication". TODO: handle this later
                    auto proof_decoded = mxs::from_base64(proof.substr(2));
                    if (proof_decoded.size() == SHA256_DIGEST_LENGTH)
                    {
                        protocol_ok = true;
                        Digest proof_bytes;
                        memcpy(proof_bytes.data(), proof_decoded.data(), proof_decoded.size());
                        sasl_final = sasl_verify_proof(proof_bytes, msg_without_proof, session);
                    }
                    else
                    {
                        MXB_ERROR("Client sent malformed SCRAM proof.");
                    }
                }
                else
                {
                    MXB_ERROR("Client sent mismatching SCRAM nonces.");
                }
            }
            else
            {
                MXB_ERROR("Client sent malformed final SCRAM message, no nonce and/or proof.");
            }
        }
        else
        {
            MXB_ERROR("Client sent mismatching SCRAM channel binding in client-final-message.");
        }
    }
    else
    {
        MXB_ERROR("Client sent malformed final SCRAM message.");
    }

    return {protocol_ok, std::move(sasl_final)};
}

GWBUF ScramClientAuth::sasl_verify_proof(const Digest& proof, string_view client_final_message_without_proof,
                                         PgProtocolData& session)
{
    // See: https://www.rfc-editor.org/rfc/rfc5802#section-3

    // AuthMessage     := client-first-message-bare + "," +
    //                    server-first-message + "," +
    //                    client-final-message-without-proof
    auto auth_message = mxb::cat(m_client_first_message_bare, ",", m_server_first_message, ",",
                                 client_final_message_without_proof);

    // ClientSignature := HMAC(StoredKey, AuthMessage)
    Digest client_sig = hmac(m_stored_key, auth_message);
    // ClientProof     := ClientKey XOR ClientSignature
    // We do the inverse with the ClientProof to get the ClientKey
    Digest client_key = digest_xor(proof, client_sig);
    // StoredKey       := H(ClientKey)
    if (hash(client_key) == m_stored_key)
    {
        // Correct password! Save ClientKey and StoredKey, they will be needed when logging in to backends.
        auto& token_storage = session.auth_data().client_token;
        token_storage.resize(client_key.size() + m_stored_key.size());
        memcpy(token_storage.data(), client_key.data(), client_key.size());
        memcpy(token_storage.data() + client_key.size(), m_stored_key.data(), m_stored_key.size());

        // ServerSignature := HMAC(ServerKey, AuthMessage)
        auto server_sig = hmac(m_server_key, auth_message);
        string server_sig_msg = "v=";
        server_sig_msg.append(mxs::to_base64(server_sig));

        // Send AuthenticationSASLFinal packet to client.
        GWBUF sasl_final(pg::HEADER_LEN + 4 + server_sig_msg.size());
        auto ptr = sasl_final.data();
        *ptr++ = pg::AUTHENTICATION;
        ptr += pg::set_uint32(ptr, 8 + server_sig_msg.size());
        ptr += pg::set_uint32(ptr, pg::AUTH_SASL_FINAL);
        memcpy(ptr, server_sig_msg.data(), server_sig_msg.size());
        return sasl_final;
    }
    return GWBUF();
}

bool ScramClientAuth::nonces_match(std::string_view client_final_nonce)
{
    bool rval = false;
    if (client_final_nonce.length() == m_client_nonce.length() + m_server_nonce.length())
    {
        const auto ptr = client_final_nonce.data();
        rval = (memcmp(ptr, m_client_nonce.data(), m_client_nonce.length()) == 0
            && memcmp(ptr + m_client_nonce.length(), m_server_nonce.data(), m_server_nonce.length()) == 0);
    }
    return rval;
}

GWBUF ScramBackendAuth::exchange(GWBUF&& input, PgProtocolData& session)
{
    return GWBUF();
}

GWBUF ScramBackendAuth::create_sasl_initial_response(std::string& client_first_message_bare)
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



GWBUF ScramBackendAuth::create_sasl_response(const GWBUF& buffer,
                                             std::string_view client_first_message_bare,
                                             std::string& server_first_message,
                                             std::string& client_final_message_without_proof,
                                             const Digest& client_key)
{
    mxb_assert(buffer[0] == pg::AUTHENTICATION);
    mxb_assert(pg::get_uint32(buffer.data() + pg::HEADER_LEN) == pg::AUTH_SASL_CONTINUE);
    uint32_t len = pg::get_uint32(buffer.data() + 1) - 8;
    server_first_message.assign((const char*)buffer.data() + 9, len);
    std::string nonce;

    for (auto token : mxb::strtok(server_first_message, ","))
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
    client_final_message_without_proof = mxb::cat("c=biws,", nonce);

    // See: https://www.rfc-editor.org/rfc/rfc5802#section-3

    // AuthMessage     := client-first-message-bare + "," +
    //                    server-first-message + "," +
    //                    client-final-message-without-proof
    auto auth_message = mxb::cat(client_first_message_bare, ",",
                                 server_first_message, ",",
                                 client_final_message_without_proof);

    // TODO: Get this from the UserAccountManager
    auto user = parse_scram_password(THE_PASSWORD);

    // ClientSignature := HMAC(StoredKey, AuthMessage)
    Digest client_sig = hmac(user->stored_key, auth_message);

    // ClientProof     := ClientKey XOR ClientSignature
    auto client_proof = mxs::to_base64(digest_xor(client_key, client_sig));
    std::string client_final_message = mxb::cat(client_final_message_without_proof, ",p=", client_proof);

    // SASLResponse
    GWBUF response(pg::HEADER_LEN + client_final_message.length());
    uint8_t* ptr = response.data();

    *ptr++ = pg::SASL_RESPONSE;
    ptr += pg::set_uint32(ptr, response.length() - 1);
    memcpy(ptr, client_final_message.data(), client_final_message.size());

    return response;
}

std::unique_ptr<PgClientAuthenticator> ScramAuthModule::create_client_authenticator() const
{
    return std::make_unique<ScramClientAuth>();
}

std::unique_ptr<PgBackendAuthenticator> ScramAuthModule::create_backend_authenticator() const
{
    return std::make_unique<ScramBackendAuth>();
}

std::string ScramAuthModule::name() const
{
    return "scram-sha-256";
}
