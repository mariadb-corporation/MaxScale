/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxctrl.hh"

using namespace std;

MaxCtrl::Server::Server(const MaxCtrl& maxctrl, json_t* pObject)
    : name       (maxctrl.get<string> (pObject, "id", Presence::MANDATORY))
    , address    (maxctrl.get<string> (pObject, "attributes/parameters/address"))
    , port       (maxctrl.get<int64_t>(pObject, "attributes/parameters/port"))
    , connections(maxctrl.get<int64_t>(pObject, "attributes/statistics/connections"))
    , state      (maxctrl.get<string> (pObject, "attributes/state"))
{
}

MaxCtrl::MaxCtrl(TestConnections* pTest)
    : m_test(*pTest)
{
}

unique_ptr<json_t> MaxCtrl::servers() const
{
    return curl("servers");
}

vector<MaxCtrl::Server> MaxCtrl::list_servers() const
{
    return get_array<Server>(servers().get(), "data", Presence::MANDATORY);
}

json_t* MaxCtrl::get_object(json_t* pObject, const string& key, Presence presence) const
{
    json_t* pValue = json_object_get(pObject, key.c_str());

    if (!pValue && (presence == Presence::MANDATORY))
    {
        raise("Mandatory key '" + key + "' not present.");
    }

    return pValue;
}

json_t* MaxCtrl::get_leaf_object(json_t* pObject, const string& key, Presence presence) const
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

unique_ptr<json_t> MaxCtrl::parse(const string& json) const
{
    json_error_t error;
    unique_ptr<json_t> sRoot(json_loads(json.c_str(), 0, &error));

    if (!sRoot)
    {
        raise("JSON parsing failed: " + string(error.text));
    }

    return sRoot;
}

unique_ptr<json_t> MaxCtrl::curl(const string& path) const
{
    string url = "http://127.0.0.1:8989/v1/" + path;
    string command = "curl -u admin:mariadb " + url;

    auto result = m_test.maxscales->ssh_output(command.c_str(), 0, false);

    if (result.first != 0)
    {
        raise("Invocation of curl failed: " + to_string(result.first));
    }

    return parse(result.second);
}

void MaxCtrl::raise(const std::string& message) const
{
    ++m_test.global_result;
    throw runtime_error(message);
}

template<>
string MaxCtrl::get<string>(json_t* pObject, const string& key, Presence presence) const
{
    json_t* pValue = get_leaf_object(pObject, key, presence);

    if (pValue && !json_is_string(pValue))
    {
        raise("Key '" + key + "' is present, but value is not a string.");
    }

    return pValue ? json_string_value(pValue) : "";
}

template<>
int64_t MaxCtrl::get<int64_t>(json_t* pObject, const string& key, Presence presence) const
{
    json_t* pValue = get_leaf_object(pObject, key, presence);

    if (pValue && !json_is_integer(pValue))
    {
        raise("Key '" + key + "' is present, but value is not an integer.");
    }

    return pValue ? json_integer_value(pValue) : 0;
}
