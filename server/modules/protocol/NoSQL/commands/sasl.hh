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

#include "defs.hh"
#include <maxscale/utils.hh>

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
            // We expect "=" to be followed by "2D" or "3D", the former is ',' and the latter '='.
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
        auto mechanism = required<string_view>(key::MECHANISM);

        if (mechanism.compare("SCRAM-SHA-1") != 0)
        {
            ostringstream ss;
            ss << "Received authentication for mechanism " << mechanism
               << " which is unknown or not enabled";

            throw SoftError(ss.str(), error::MECHANISM_UNAVAILABLE);
        }

        auto payload = required<bsoncxx::types::b_binary>(key::PAYLOAD);

        authenticate(string_view(reinterpret_cast<const char*>(payload.bytes), payload.size), doc);
    }

private:
    void authenticate(string_view payload, DocumentBuilder& doc)
    {
        MXS_NOTICE("Payload: %.*s", (int)payload.length(), payload.data());

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

        if (payload.find("n=") != 0)
        {
            ostringstream ss;
            ss << "Missing \"n=\" in the first SCRAM payload.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        payload = payload.substr(2); // Strip "n="

        auto i = payload.find(',');

        string user = decode_user(payload.begin(), i != string_view::npos ? payload.begin() + i : payload.end());

        payload = payload.substr(i + 1); // Strip up until the comma, inclusive

        i = payload.find("r=");

        if (i == string_view::npos)
        {
            ostringstream ss;
            ss << "Did not find the nonce in the payload.";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        auto client_nonce_b64 = payload.substr(i + 2); // Skip "r="

        authenticate(gs2_header, user, client_nonce_b64, doc);
    }

    void authenticate(string_view gs2_header,
                      string_view user,
                      string_view client_nonce_b64,
                      DocumentBuilder& doc)
    {
        MXS_NOTICE("User: %.*s, nonce: %.*s",
                   (int)user.length(), user.data(),
                   (int)client_nonce_b64.length(), client_nonce_b64.data());

        // TODO: Create a proper server_nonce and salt.

        string server_nonce = "aaaaaaaaaaaa";

        auto server_nonce_b64 = mxs::to_base64(reinterpret_cast<const uint8_t*>(server_nonce.data()),
                                               server_nonce.length());

        string salt = "1234567890123456";
        string salt_b64 = mxs::to_base64(reinterpret_cast<const uint8_t*>(salt.data()), salt.length());

        auto& sasl = m_database.context().sasl();

        sasl.set_gs2_header(gs2_header);
        sasl.set_user(user);
        sasl.set_client_nonce_b64(client_nonce_b64);
        sasl.set_server_nonce_b64(server_nonce_b64);
        sasl.set_salt(salt);

        ostringstream ss;

        ss << "r=" << client_nonce_b64 << server_nonce_b64
           << ",s=" << salt_b64
           << ",i=" << NoSQL::Sasl::ITERATIONS;

        auto s = ss.str();

        auto sub_type = bsoncxx::binary_sub_type::k_binary;
        uint32_t size = s.length();
        auto* bytes = reinterpret_cast<const uint8_t*>(s.data());

        bsoncxx::types::b_binary payload { sub_type, size, bytes };

        doc.append(kvp(key::DONE, false));
        doc.append(kvp(key::CONVERSATION_ID, sasl.bump_conversation_id()));
        doc.append(kvp(key::PAYLOAD, payload));
        doc.append(kvp(key::OK, 1));
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
        auto conversation_id = required<int32_t>(key::CONVERSATION_ID);

        auto& sasl = m_database.context().sasl();

        if (conversation_id != sasl.conversation_id())
        {
            ostringstream ss;
            ss << "Invalid conversation id, got " << conversation_id
               << ", expected " << sasl.conversation_id() << ".";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        auto payload = required<bsoncxx::types::b_binary>(key::PAYLOAD);

        authenticate(conversation_id,
                     string_view(reinterpret_cast<const char*>(payload.bytes), payload.size),
                     doc);
    }

private:
    void authenticate(int32_t conversation_id, string_view payload, DocumentBuilder& doc)
    {
        MXS_NOTICE("Payload: %.*s", (int)payload.length(), payload.data());

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

        auto& sasl = m_database.context().sasl();

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

        MXS_NOTICE("ClientProof r_b64: %.*s", (int)client_proof_b64.length(), client_proof_b64.data());

        authenticate(client_proof_b64, doc);
    }

    void authenticate(string_view client_proof_64, DocumentBuilder& doc)
    {
        // TODO: Check the client proof and generate the server signature.

        string server_signature("todo");
        string server_signature_b64 = mxs::to_base64(reinterpret_cast<const uint8_t*>(server_signature.data()),
                                                     server_signature.length());

        ostringstream ss;

        ss << "v=" << server_signature_b64;

        auto s = ss.str();

        auto sub_type = bsoncxx::binary_sub_type::k_binary;
        uint32_t size = s.length();
        auto* bytes = reinterpret_cast<const uint8_t*>(s.data());

        bsoncxx::types::b_binary payload { sub_type, size, bytes };

        doc.append(kvp(key::PAYLOAD, payload));
        doc.append(kvp(key::OK, 1));
    }
};

}

}
