/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "scram-sha-256.hh"
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <maxbase/format.hh>
#include <maxscale/utils.hh>
#include "../pgprotocoldata.hh"

using std::string;
using std::string_view;

namespace
{
const std::string MECH = "SCRAM-SHA-256";
const size_t DIGEST_B64_LEN = 44;   // Base64 from 32 bytes

Digest hmac(const Digest& k, string_view d)
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
    for (size_t i = 0; i < result.size(); i++)
    {
        result[i] = lhs[i] ^ rhs[i];
    }
    return result;
}

Digest hash(const Digest& in)
{
    Digest digest;
    SHA256(in.data(), in.size(), digest.data());
    return digest;
}

std::optional<Digest> from_base64(string_view input)
{
    std::optional<Digest> rval;
    // Using OpenSSL decoding, which assumes input length is divisible by 4 (=-padded). Luckily this is what
    // PG also uses. The input must be exactly 44 chars with one = at the end to produce 32 bytes.
    if (input.length() == DIGEST_B64_LEN && input.back() == '=' && input[input.length() - 2] != '=')
    {
        const auto decoded_len = 32 + 1;    // One extra due to OpenSSL 0-padding.
        uint8_t bytes[decoded_len];
        int res = EVP_DecodeBlock(bytes, (uint8_t*)input.data(), input.size());
        if (res == decoded_len)
        {
            rval = Digest();
            memcpy(rval->data(), bytes, rval->size());
        }
    }
    return rval;
}

std::array<char, DIGEST_B64_LEN> to_base64(const Digest digest)
{
    uint8_t temp[DIGEST_B64_LEN + 1 + 1];   // 1 for padding, 1 for 0-term.
    MXB_AT_DEBUG(int n = ) EVP_EncodeBlock(temp, digest.data(), digest.size());
    mxb_assert(n == DIGEST_B64_LEN);
    std::array<char, DIGEST_B64_LEN> rval;
    memcpy(rval.data(), temp, rval.size());
    return rval;
}

std::string create_nonce()
{
    // Similar to Pg Server. Ensures that the nonce is composed of printable characters.
    std::array<uint8_t, 18> nonce{};

    if (RAND_bytes(nonce.data(), nonce.size()) == 1)
    {
        return mxs::to_base64(nonce);
    }

    return "";
}
}

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

                        if (m_server_nonce.empty())
                        {
                            MXB_ERROR("Failed to generate random nonce.");
                            return rval;
                        }

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
                    auto proof_bytes = from_base64(proof.substr(2));
                    if (proof_bytes)
                    {
                        protocol_ok = true;
                        sasl_final = sasl_verify_proof(*proof_bytes, msg_without_proof, session);
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
        // Save ServerKey as well, required for checking server signature.
        auto& token_storage = session.auth_data().client_token;
        const size_t digest_len = SHA256_DIGEST_LENGTH;
        token_storage.resize(3 * digest_len);
        auto storage_ptr = mempcpy(token_storage.data(), client_key.data(), digest_len);
        storage_ptr = mempcpy(storage_ptr, m_stored_key.data(), digest_len);
        memcpy(storage_ptr, m_server_key.data(), digest_len);

        // ServerSignature := HMAC(ServerKey, AuthMessage)
        auto server_sig_b64 = to_base64(hmac(m_server_key, auth_message));

        const char delim[] = "v=";
        const size_t delim_len = sizeof(delim) - 1;

        // Send AuthenticationSASLFinal packet to client.
        GWBUF sasl_final(pg::HEADER_LEN + 4 + delim_len + server_sig_b64.size());
        auto ptr = sasl_final.data();
        *ptr++ = pg::AUTHENTICATION;
        ptr += pg::set_uint32(ptr, sasl_final.length() - 1);
        ptr += pg::set_uint32(ptr, pg::AUTH_SASL_FINAL);
        memcpy(ptr, delim, delim_len);
        ptr += delim_len;
        memcpy(ptr, server_sig_b64.data(), server_sig_b64.size());
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

std::optional<GWBUF> ScramBackendAuth::exchange(GWBUF&& input, PgProtocolData& session)
{
    mxb_assert(input.length() >= 5);
    std::optional<GWBUF> rval;

    // All packets should start with 'R'.
    if (input[0] == pg::AUTHENTICATION)
    {
        switch (m_state)
        {
        case State::AUTH_REQ:
            rval = handle_auth_request(input);
            m_state = rval ? State::SALT : State::DONE;
            break;

        case State::SALT:
            rval = handle_sasl_continue(input, session);
            m_state = rval ? State::FINAL : State::DONE;
            break;

        case State::FINAL:
            if (check_sasl_final(input, session))
            {
                rval = GWBUF();     // Ok, nothing to do.
            }
            else
            {
                MXB_ERROR("Invalid server key.");
            }
            break;

        case State::DONE:
            mxb_assert(!true);
        }
    }
    return rval;
}

std::optional<GWBUF> ScramBackendAuth::handle_auth_request(const GWBUF& input)
{
    std::optional<GWBUF> rval;
    auto [auth_type, sasl_data] = read_scram_data(input);
    if (auth_type == pg::Auth::AUTH_SASL && !sasl_data.empty() && sasl_data.back() == '\0')
    {
        bool mechanism_found = false;
        auto ptr = sasl_data.data();
        auto end = ptr + sasl_data.length();
        while (ptr < end)
        {
            // At this point, it's enough if server supports the standard mechanism.
            string_view mech(ptr, strlen(ptr));
            if (mech == MECH)
            {
                mechanism_found = true;
                break;
            }
            else
            {
                ptr += mech.length() + 1;
            }
        }

        if (mechanism_found)
        {
            m_client_nonce = create_nonce();

            if (!m_client_nonce.empty())
            {
                rval = create_sasl_initial_response();
            }
            else
            {
                MXB_ERROR("Failed to generate random nonce.");
            }
        }
        else
        {
            MXB_ERROR("Server does not support %s SASL mechanism.", MECH.c_str());
        }
    }

    return rval;
}

GWBUF ScramBackendAuth::create_sasl_initial_response()
{
    m_client_first_message_bare = mxb::cat("n=,r=", m_client_nonce);
    int client_msg_size = 3 + m_client_first_message_bare.size();
    GWBUF response(pg::HEADER_LEN + MECH.length() + 1 + 4 + client_msg_size);

    uint8_t* ptr = response.data();
    *ptr++ = pg::SASL_INITIAL_RESPONSE;
    ptr += pg::set_uint32(ptr, response.length() - 1);
    ptr += pg::set_string(ptr, MECH);
    ptr += pg::set_uint32(ptr, client_msg_size);
    memcpy(ptr, "n,,", 3);
    ptr += 3;
    memcpy(ptr, m_client_first_message_bare.data(), m_client_first_message_bare.size());
    return response;
}

std::optional<GWBUF> ScramBackendAuth::handle_sasl_continue(const GWBUF& input, PgProtocolData& session)
{
    std::optional<GWBUF> rval;
    auto [auth_type, sasl_data] = read_scram_data(input);
    if (auth_type == pg::Auth::AUTH_SASL_CONTINUE && !sasl_data.empty())
    {
        // In theory, the server first message could contain various extension data. However, server source
        // code shows that it always contains just the minimum.
        auto [full_nonce, salt_iter] = mxb::split(sasl_data, ",");
        auto [salt, iter] = mxb::split(salt_iter, ",");
        if (full_nonce.substr(0, 2) != "r=" || salt.substr(0, 2) != "s=" || iter.substr(0, 2) != "i=")
        {
            MXB_ERROR("Malformed SCRAM message from server.");
        }
        else if (full_nonce.length() <= (2 + m_client_nonce.length()))
        {
            MXB_ERROR("No valid server nonce in SCRAM message.");
        }
        else if (full_nonce.substr(2, m_client_nonce.length()) != m_client_nonce)
        {
            MXB_ERROR("Server sent mismatching client nonce.");
        }
        else
        {
            // The salt and iteration count are not needed since we don't have real client password.
            // Just assume the ClientKey received from the original client is calculated using the
            // same parameters. Can't do anything if it's not, in any case.
            rval = create_scram_proof(full_nonce, sasl_data, session);
        }
    }

    return rval;
}

GWBUF ScramBackendAuth::create_scram_proof(string_view full_nonce, string_view server_first_message,
                                           PgProtocolData& session)
{
    string client_final_msg_wo_proof = "c=biws";    // Base64 of n,,
    client_final_msg_wo_proof.append(",r=").append(full_nonce.substr(2));
    m_auth_message = mxb::cat(m_client_first_message_bare, ",", server_first_message, ",",
                              client_final_msg_wo_proof);

    auto& token_storage = session.auth_data().client_token;
    Digest client_key;
    memcpy(client_key.data(), token_storage.data(), client_key.size());
    Digest stored_key;
    memcpy(stored_key.data(), token_storage.data() + client_key.size(), stored_key.size());

    // ClientSignature := HMAC(StoredKey, AuthMessage)
    // ClientProof     := ClientKey XOR ClientSignature
    Digest client_sig = hmac(stored_key, m_auth_message);
    auto client_proof_b64 = to_base64(digest_xor(client_key, client_sig));

    const char delim[] = ",p=";
    const size_t delim_len = sizeof(delim) - 1;

    GWBUF response(pg::HEADER_LEN + client_final_msg_wo_proof.length() + delim_len + client_proof_b64.size());
    uint8_t* ptr = response.data();
    *ptr++ = pg::SASL_RESPONSE;
    ptr += pg::set_uint32(ptr, response.length() - 1);
    memcpy(ptr, client_final_msg_wo_proof.data(), client_final_msg_wo_proof.length());
    ptr += client_final_msg_wo_proof.length();
    memcpy(ptr, delim, delim_len);
    ptr += delim_len;
    memcpy(ptr, client_proof_b64.data(), client_proof_b64.size());
    return response;
}

std::tuple<int, std::string_view> ScramBackendAuth::read_scram_data(const GWBUF& input)
{
    int sasl_state = 0;
    string_view sasl_data;
    auto plen = input.length();
    const size_t base_len = pg::HEADER_LEN + 4;
    if (plen >= base_len)
    {
        sasl_state = pg::get_uint32(input.data() + pg::HEADER_LEN);
        if (plen > base_len)
        {
            sasl_data = {reinterpret_cast<const char*>(input.data() + base_len), plen - base_len};
        }
    }
    return {sasl_state, sasl_data};
}

bool ScramBackendAuth::check_sasl_final(const GWBUF& input, PgProtocolData& session)
{
    bool rval = false;
    auto [auth_type, sasl_data] = read_scram_data(input);
    if (auth_type == pg::Auth::AUTH_SASL_FINAL && sasl_data.substr(0, 2) == "v=")
    {
        auto server_sig = from_base64(sasl_data.substr(2));
        if (server_sig)
        {
            Digest server_key;
            memcpy(server_key.data(), session.auth_data().client_token.data() + 2 * server_key.size(),
                   server_key.size());
            Digest correct_server_sig = hmac(server_key, m_auth_message);
            rval = *server_sig == correct_server_sig;
        }
    }
    return rval;
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
