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

#include "nosqlscram.hh"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <maxbase/json.hh>
#include <maxbase/worker.hh>

using namespace std;

namespace nosql
{

vector<scram::Mechanism> scram::supported_mechanisms()
{
    static vector<Mechanism> mechanisms = { Mechanism::SHA_1 };

    return mechanisms;
}

const char* scram::to_string(scram::Mechanism mechanism)
{
    switch (mechanism)
    {
    case scram::Mechanism::SHA_1:
        return "SCRAM-SHA-1";

    /*
    case scram::Mechanism::SHA_256:
        return "SCRAM-SHA-256";
    */
    }

    mxb_assert(!true);
    return "unknown";
}

bool scram::from_string(const string& mechanism, scram::Mechanism* pMechanism)
{
    bool rv = true;
    if (mechanism == "SCRAM-SHA-1")
    {
        *pMechanism = scram::Mechanism::SHA_1;
    }
    /*
    else if (mechanism == "SCRAM-SHA-256")
    {
        *pMechanism = scram::Mechanism::SHA_256;
    }
    */
    else
    {
        rv = false;
    }

    return rv;
}

string scram::to_json(const vector<Mechanism>& mechanisms)
{
    ostringstream ss;

    ss << "[";

    auto it = mechanisms.begin();

    for (; it != mechanisms.end(); ++it)
    {
        if (it != mechanisms.begin())
        {
            ss << ", ";
        }

        ss << "\"" << to_string(*it) << "\"";
    }

    ss << "]";

    return ss.str();
}

bool scram::from_json(const std::string& s, std::vector<Mechanism>* pMechanisms)
{
    bool rv = false;

    mxb::Json json;

    if (json.load_string(s))
    {
        if (json.type() == mxb::Json::Type::ARRAY)
        {
            std::vector<Mechanism> mechanisms;

            auto elements = json.get_array_elems();

            rv = true;
            for (const auto& element : elements)
            {
                if (element.type() == mxb::Json::Type::STRING)
                {
                    auto value = element.get_string();
                    Mechanism mechanism;
                    if (from_string(value, &mechanism))
                    {
                        mechanisms.push_back(mechanism);
                    }
                    else
                    {
                        MXB_ERROR("'%s' is not a valid Scram mechanism.", value.c_str());
                        rv = false;
                        break;
                    }
                }
                else
                {
                    MXB_ERROR("'%s' is a Json array, but all elements are not strings.", s.c_str());
                    rv = false;
                    break;
                }
            }

            if (rv)
            {
                pMechanisms->swap(mechanisms);
            }
        }
        else
        {
            MXB_ERROR("'%s' is valid JSON, but not an array.", s.c_str());
        }
    }
    else
    {
        MXB_ERROR("'%s' is not valid JSON: %s", s.c_str(), json.error_msg().c_str());
    }

    return rv;
}

namespace
{

void add_mechanism(const string_view& mechanism_name, vector<scram::Mechanism>& mechanisms)
{
    scram::Mechanism mechanism;

    if (!scram::from_string(mechanism_name, &mechanism))
    {
        ostringstream ss;
        ss << "\"" << mechanism_name << "\" is an unknown mechanism";

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    mechanisms.push_back(mechanism);
}

}

void scram::from_bson(const bsoncxx::array::view& bson, std::vector<Mechanism>* pMechanisms)
{
    vector<Mechanism> mechanisms;

    for (const auto& element : bson)
    {
        switch (element.type())
        {
        case bsoncxx::type::k_utf8:
            add_mechanism(element.get_utf8(), mechanisms);
            break;

        default:
            throw SoftError("mechanism field must be an array of strings", error::BAD_VALUE);
        }
    }

    pMechanisms->swap(mechanisms);
}


void scram::pbkdf2_hmac_sha_1(const char* pPassword, size_t password_len,
                              const uint8_t* pSalt, size_t salt_len,
                              size_t iterations,
                              uint8_t* pOutput)
{
    uint8_t start_key[NOSQL_SHA_1_HASH_SIZE];

    memcpy(start_key, pSalt, salt_len);

    start_key[salt_len] = 0;
    start_key[salt_len + 1] = 0;
    start_key[salt_len + 2] = 0;
    start_key[salt_len + 3] = 1;

    crypto::hmac_sha_1(reinterpret_cast<const uint8_t*>(pPassword), password_len,
                       start_key, NOSQL_SHA_1_HASH_SIZE,
                       pOutput);

    uint8_t intermediate_digest[NOSQL_SHA_1_HASH_SIZE];

    memcpy(intermediate_digest, pOutput, NOSQL_SHA_1_HASH_SIZE);

    for (size_t i = 2; i <= iterations; ++i)
    {
        crypto::hmac_sha_1(reinterpret_cast<const uint8_t*>(pPassword), password_len,
                           intermediate_digest, NOSQL_SHA_1_HASH_SIZE,
                           intermediate_digest);

        for (int j = 0; j < NOSQL_SHA_1_HASH_SIZE; j++)
        {
            pOutput[j] ^= intermediate_digest[j];
        }
    }
}

vector<uint8_t> scram::pbkdf2_hmac_sha_1(const char* pPassword, size_t password_len,
                                         const uint8_t* pSalt, size_t salt_len,
                                         size_t iterations)
{
    vector<uint8_t> rv(NOSQL_SHA_1_HASH_SIZE);

    pbkdf2_hmac_sha_1(pPassword, password_len, pSalt, salt_len, iterations, rv.data());

    return rv;
}

unique_ptr<scram::Scram> scram::create(Mechanism mechanism)
{
    switch (mechanism)
    {
    case Mechanism::SHA_1:
        return make_unique<ScramSHA1>();
    }

    mxb_assert(!true);
    return unique_ptr<Scram>();
}

namespace scram
{

Scram::~Scram()
{
}

vector<uint8_t> ScramSHA1::Hi(const string& password, const vector<uint8_t>& salt, size_t iterations) const
{
    return scram::pbkdf2_hmac_sha_1(password, salt, iterations);
}

vector<uint8_t> ScramSHA1::HMAC(const vector<uint8_t>& key, const uint8_t* pData, size_t len) const
{
    return crypto::hmac_sha_1(key.data(), key.size(), pData, len);
}

vector<uint8_t> ScramSHA1::H(const vector<uint8_t>& data) const
{
    return crypto::sha_1(data);
}

}

}
