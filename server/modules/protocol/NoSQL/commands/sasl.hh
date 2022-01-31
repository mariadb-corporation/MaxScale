/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "defs.hh"
#include "../nosqlscram.hh"

namespace nosql
{

namespace
{

string decode_user(string_view::const_iterator begin, string_view::const_iterator end)
{
    string user;
    for (auto it = begin; it != end; ++it)
    {
        auto c = *it;

        if (c == '=')
        {
            bool fail = true;
            // RFC5802: We expect "=" to be followed by "2C" or "3D", the former is ',' and the latter '='.
            if (it + 3 <= end)
            {
                ++it;
                c = *it++;
                if (c == '2' && *it == 'C')
                {
                    user += ',';
                    fail = false;
                }
                else if (c == '3' && *it == 'D')
                {
                    user += '=';
                    fail = false;
                }
            }

            if (fail)
            {
                throw SoftError("Invalid encoding in user name.", error::BAD_VALUE);
            }
        }
        else
        {
            user += c;
        }
    }

    return user;
}

}

namespace command
{

class SaslStart final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "saslStart";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        auto mechanism_name = required<string_view>(key::MECHANISM);
        scram::Mechanism mechanism;

        if (!scram::from_string(mechanism_name, &mechanism))
        {
            ostringstream ss;
            ss << "Received authentication for mechanism " << mechanism_name
               << " which is unknown or not enabled";

            throw SoftError(ss.str(), error::MECHANISM_UNAVAILABLE);
        }

        auto payload = required<bsoncxx::types::b_binary>(key::PAYLOAD);

        authenticate(mechanism,
                     string_view(reinterpret_cast<const char*>(payload.bytes), payload.size),
                     doc);
    }

private:
    void authenticate(scram::Mechanism mechanism, string_view payload, DocumentBuilder& doc)
    {
        unique_ptr<NoSQL::Sasl> sSasl = m_database.context().get_sasl();

        if (sSasl)
        {
            throw SoftError("Was expecting saslContinue, authentication attempt aborted",
                            error::PROTOCOL_ERROR);
        }

        // TODO: Make it possible to re-authenticate, i.e. generate COM_CHANGE_USER and whatnot.
        if (m_database.context().authenticated())
        {
            throw SoftError("Client already authenticated, re-authentication not yet supported.",
                            error::AUTHENTICATION_FAILED);
        }

        // We are expecting a string like "n,,n=USER,r=NONCE" where "n,," is the gs2 header,
        // USER is the user name and NONCE the nonce created by the client.

        string_view gs2_header = payload.substr(0, 3);

        if (gs2_header.compare("n,,") != 0)
        {
            ostringstream ss;
            ss << "Missing gs2 header \"n,,\" at the beginning of the first SCRAM payload.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        payload = payload.substr(3); // Strip the "n,," header.

        auto initial_message = payload;

        if (payload.find("n=") != 0)
        {
            ostringstream ss;
            ss << "Missing \"n=\" in the first SCRAM payload.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        payload = payload.substr(2); // Strip "n="

        auto i = payload.find(',');

        auto end = i != string_view::npos ? payload.begin() + i : payload.end();
        string user = decode_user(payload.begin(), end);
        string scope = m_database.name();

        auto& um = m_database.context().um();

        UserManager::UserInfo info;
        if (!um.get_info(scope, user, &info))
        {
            MXS_WARNING("User '%s' does not exist.", user.c_str());
            throw SoftError("Authentication failed", error::AUTHENTICATION_FAILED);
        }

        payload = payload.substr(i + 1); // Strip up until the comma, inclusive

        i = payload.find("r=");

        if (i == string_view::npos)
        {
            ostringstream ss;
            ss << "Did not find the nonce in the payload.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        auto client_nonce_b64 = payload.substr(i + 2); // Skip "r="

        sSasl.reset(new NoSQL::Sasl);

        sSasl->set_user_info(std::move(info));
        sSasl->set_gs2_header(gs2_header);
        sSasl->set_client_nonce_b64(client_nonce_b64);
        sSasl->set_initial_message(initial_message);
        sSasl->set_mechanism(mechanism);

        authenticate(mechanism, std::move(sSasl), doc);
    }

    void authenticate(scram::Mechanism mechanism, unique_ptr<NoSQL::Sasl> sSasl, DocumentBuilder& doc)
    {
        vector<uint8_t> server_nonce = crypto::create_random_bytes(scram::SERVER_NONCE_SIZE);

        auto server_nonce_b64 = mxs::to_base64(server_nonce.data(), server_nonce.size());

        sSasl->set_server_nonce_b64(server_nonce_b64);

        ostringstream ss;

        ss << "r=" << sSasl->client_nonce_b64() << sSasl->server_nonce_b64()
           << ",s=" << sSasl->user_info().salt_b64(mechanism)
           << ",i=" << scram::ITERATIONS;

        auto s = ss.str();

        sSasl->set_server_first_message(s);

        auto sub_type = bsoncxx::binary_sub_type::k_binary;
        uint32_t size = s.length();
        auto* bytes = reinterpret_cast<const uint8_t*>(s.data());

        bsoncxx::types::b_binary payload { sub_type, size, bytes };

        doc.append(kvp(key::CONVERSATION_ID, sSasl->bump_conversation_id()));
        doc.append(kvp(key::DONE, false));
        doc.append(kvp(key::PAYLOAD, payload));
        doc.append(kvp(key::OK, 1));

        m_database.context().put_sasl(std::move(sSasl));
    }
};

class SaslContinue final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "saslContinue";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        unique_ptr<NoSQL::Sasl> sSasl = m_database.context().get_sasl();

        if (!sSasl)
        {
            throw SoftError("No SASL session state found", error::PROTOCOL_ERROR);
        }

        auto conversation_id = required<int32_t>(key::CONVERSATION_ID);

        if (conversation_id != sSasl->conversation_id())
        {
            ostringstream ss;
            ss << "Invalid conversation id, got " << conversation_id
               << ", expected " << sSasl->conversation_id() << ".";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        auto b = required<bsoncxx::types::b_binary>(key::PAYLOAD);
        string_view payload(reinterpret_cast<const char*>(b.bytes), b.size);

        authenticate(*sSasl.get(), payload, doc);
    }

private:
    void authenticate(const NoSQL::Sasl& sasl, string_view payload, DocumentBuilder& doc)
    {
        auto backup = payload;

        // We are expecting a string like "c=GS2_HEADER,r=NONCE,p=CLIENT_PROOF

        if (payload.find("c=") != 0)
        {
            ostringstream ss;
            ss << "Missing value \"c=\" in second SCRAM payload.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        payload = payload.substr(2);

        auto i = payload.find(',');

        auto c_b64 = payload.substr(0, i);
        vector<uint8_t> c = mxs::from_base64(to_string(c_b64));
        string_view gs2_header(reinterpret_cast<const char*>(c.data()), c.size());

        if (gs2_header != sasl.gs2_header())
        {
            ostringstream ss;
            ss << "Gs2 header at step 1 was \"" << sasl.gs2_header() << "\", "
               << "gs2 header at step 2 is \"" << gs2_header << "\".";
            auto s = ss.str();

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        payload = payload.substr(i + 1);
        if (payload.find("r=") != 0)
        {
            ostringstream ss;
            ss << "Missing value \"r=\" in second SCRAM payload.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        payload = payload.substr(2);
        i = payload.find(',');

        auto nonce_b64 = payload.substr(0, i);

        if (nonce_b64 != sasl.nonce_b64())
        {
            ostringstream ss;
            ss << "Combined nonce invalid.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        payload = payload.substr(i + 1);
        if (payload.find("p=") != 0)
        {
            ostringstream ss;
            ss << "Missing value \"p=\" in second SCRAM payload.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        auto client_proof_b64 = payload.substr(2);

        // c=GS2_HEADER,r=NONCE
        string_view s = backup.substr(0, 2 + c_b64.length() + 1 + 2 + nonce_b64.length());
        string client_final_message_bare = to_string(s);

        authenticate(sasl, client_final_message_bare, client_proof_b64, doc);
    }

    void authenticate(const NoSQL::Sasl& sasl,
                      const string& client_final_message_bare,
                      string_view client_proof_64,
                      DocumentBuilder& doc)
    {
        const auto mechanism = sasl.mechanism();
        const auto& scram = nosql::scram::get(mechanism);
        const auto& info = sasl.user_info();

        string digested_password = scram.get_digested_password(info.user, info.pwd);

        auto salted_password = scram.Hi(digested_password, info.salt(mechanism), scram::ITERATIONS);
        auto client_key = scram.HMAC(salted_password, "Client Key");
        auto stored_key = scram.H(client_key);
        string auth_message = sasl.initial_message()
            + "," + sasl.server_first_message()
            + "," + client_final_message_bare;

        auto client_signature = scram.HMAC(stored_key, auth_message);

        vector<uint8_t> server_client_proof;

        for (size_t i = 0; i < client_key.size(); ++i)
        {
            server_client_proof.push_back(client_key[i] ^ client_signature[i]);
        }

        auto client_proof = mxs::from_base64(to_string(client_proof_64));

        if (server_client_proof != client_proof)
        {
            MXS_WARNING("Invalid client proof.");
            throw SoftError("Authentication failed", error::AUTHENTICATION_FAILED);
        }

        // Ok, the client was authenticated, the response can be generated.
        authenticate(sasl, salted_password, auth_message, doc);
    }

    void authenticate(const NoSQL::Sasl& sasl,
                      const vector<uint8_t>& salted_password,
                      const string& auth_message,
                      DocumentBuilder& doc)
    {
        const auto& scram = nosql::scram::get(sasl.mechanism());

        auto server_key = scram.HMAC(salted_password, "Server Key");
        auto server_signature = scram.HMAC(server_key, auth_message);
        string server_signature_b64 = mxs::to_base64(server_signature);

        ostringstream ss;
        ss << "v=" << server_signature_b64;

        auto s = ss.str();

        auto sub_type = bsoncxx::binary_sub_type::k_binary;
        uint32_t size = s.length();
        auto* bytes = reinterpret_cast<const uint8_t*>(s.data());

        bsoncxx::types::b_binary payload { sub_type, size, bytes };

        doc.append(kvp(key::CONVERSATION_ID, sasl.conversation_id()));
        doc.append(kvp(key::DONE, true));
        doc.append(kvp(key::PAYLOAD, payload));
        doc.append(kvp(key::OK, 1));

        const auto& info = sasl.user_info();

        auto& config = m_database.config();
        config.user = mariadb::get_user_name(info.db, info.user);
        config.password = info.pwd;

        auto& context = m_database.context();
        context.client_connection().setup_session(config.user, config.password);
        context.set_roles(role::to_bitmasks(info.roles));
        context.set_authenticated(true);
    }
};

}

}
