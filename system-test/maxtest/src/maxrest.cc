/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/maxrest.hh>

using namespace std;

MaxRest::Server::Server(const MaxRest& maxrest, json_t* pObject)
    : name       (maxrest.get<string> (pObject, "id", Presence::MANDATORY))
    , address    (maxrest.get<string> (pObject, "attributes/parameters/address"))
    , port       (maxrest.get<int64_t>(pObject, "attributes/parameters/port"))
    , connections(maxrest.get<int64_t>(pObject, "attributes/statistics/connections"))
    , state      (maxrest.get<string> (pObject, "attributes/state"))
{
}

MaxRest::MaxRest(TestConnections* pTest)
    : m_test(*pTest)
{
}

unique_ptr<json_t> MaxRest::v1_servers(const string& id) const
{
    string path("servers");
    path += "/";
    path += id;

    return curl_get(path);
}

unique_ptr<json_t> MaxRest::v1_servers() const
{
    return curl_get("servers");
}

void MaxRest::v1_maxscale_modules(const string& module,
                                  const string& command,
                                  const string& instance,
                                  const std::vector<string>& params) const
{
    string path("maxscale/modules");

    path += "/";
    path += module;
    path += "/";
    path += command;
    path += "?";
    path += instance;

    if (!params.empty())
    {
        for (const auto& param : params)
        {
            path += "\\&";
            path += param;
        }
    }

    curl_post(path);
}

MaxRest::Server MaxRest::show_server(const std::string& id) const
{
    unique_ptr<json_t> sObject = v1_servers(id);
    json_t* pData = get_object(sObject.get(), "data", Presence::MANDATORY);
    return Server(*this, pData);
}

vector<MaxRest::Server> MaxRest::list_servers() const
{
    return get_array<Server>(v1_servers().get(), "data", Presence::MANDATORY);
}

json_t* MaxRest::get_object(json_t* pObject, const string& key, Presence presence) const
{
    json_t* pValue = json_object_get(pObject, key.c_str());

    if (!pValue && (presence == Presence::MANDATORY))
    {
        raise("Mandatory key '" + key + "' not present.");
    }

    return pValue;
}

json_t* MaxRest::get_leaf_object(json_t* pObject, const string& key, Presence presence) const
{
    auto i = key.find("/");

    if (i == string::npos)
    {
        pObject = get_object(pObject, key, presence);
    }
    else
    {
        string head = key.substr(0, i);
        string tail = key.substr(i + 1);

        pObject = get_object(pObject, head, Presence::MANDATORY);
        pObject = get_leaf_object(pObject, tail, presence);
    }

    return pObject;
}

unique_ptr<json_t> MaxRest::parse(const string& json) const
{
    json_error_t error;
    unique_ptr<json_t> sRoot(json_loads(json.c_str(), 0, &error));

    if (!sRoot)
    {
        raise("JSON parsing failed: " + string(error.text));
    }

    return sRoot;
}

unique_ptr<json_t> MaxRest::curl_get(const string& path) const
{
    return curl(GET, path);
}

unique_ptr<json_t> MaxRest::curl_post(const string& path) const
{
    return curl(POST, path);
}

unique_ptr<json_t> MaxRest::curl(Command command, const string& path) const
{
    string url = "http://127.0.0.1:8989/v1/" + path;
    string curl_command = "curl -u admin:mariadb ";

    switch (command)
    {
    case GET:
        curl_command += "-X GET ";
        break;

    case POST:
        curl_command += "-X POST ";
        break;
    }

    curl_command += url;

    auto result = m_test.maxscales->ssh_output(curl_command.c_str(), 0, false);

    if (result.first != 0)
    {
        raise("Invocation of curl failed: " + to_string(result.first));
    }

    unique_ptr<json_t> sRv;

    if (!result.second.empty())
    {
        sRv = parse(result.second);
    }

    return sRv;
}

void MaxRest::raise(const std::string& message) const
{
    ++m_test.global_result;
    throw runtime_error(message);
}

template<>
string MaxRest::get<string>(json_t* pObject, const string& key, Presence presence) const
{
    json_t* pValue = get_leaf_object(pObject, key, presence);

    if (pValue && !json_is_string(pValue))
    {
        raise("Key '" + key + "' is present, but value is not a string.");
    }

    return pValue ? json_string_value(pValue) : "";
}

template<>
int64_t MaxRest::get<int64_t>(json_t* pObject, const string& key, Presence presence) const
{
    json_t* pValue = get_leaf_object(pObject, key, presence);

    if (pValue && !json_is_integer(pValue))
    {
        raise("Key '" + key + "' is present, but value is not an integer.");
    }

    return pValue ? json_integer_value(pValue) : 0;
}
