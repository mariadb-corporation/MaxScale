/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/maxrest.hh>

using namespace std;

namespace
{

string to_json_value(const string& value)
{
    return "\"" + value + "\"";
}

string to_json_value(const MaxRest::Value& value)
{
    if (std::holds_alternative<string>(value))
    {
        return to_json_value(std::get<string>(value));
    }
    else if (std::holds_alternative<int64_t>(value))
    {
        return std::to_string(std::get<int64_t>(value));
    }
    else if (std::holds_alternative<bool>(value))
    {
        return std::get<bool>(value) ? "true" : "false";
    }
    else
    {
        mxb_assert(!true);
    }

    throw std::runtime_error("Variant contains value of wrong type.");
}

}

//
// MaxRest::SystemTestImp
//

class MaxRest::SystemTestImp : public MaxRest::Imp
{
public:
    SystemTestImp(TestConnections* pTest)
        : m_test(*pTest)
        , m_maxscale(*pTest->maxscale)
    {
    }

    SystemTestImp(TestConnections* pTest, mxt::MaxScale* pMaxscale)
        : m_test(*pTest)
        , m_maxscale(*pMaxscale)
    {
    }

    TestConnections& test() const override final
    {
        return m_test;
    }

    void raise(bool fail_on_error, const std::string& message) const override final
    {
        if (fail_on_error)
        {
            ++m_test.global_result;
        }

        throw runtime_error(message);
    }

    mxt::CmdResult execute_curl_command(const std::string& curl_command) const override final
    {
        return m_maxscale.ssh_output(curl_command, false);
    }

private:
    TestConnections& m_test;
    mxt::MaxScale&   m_maxscale;
};

//
// MaxRest::LocalImp
//

class MaxRest::LocalImp : public MaxRest::Imp
{
public:
    LocalImp()
    {
    }

    TestConnections& test() const override final
    {
        throw std::runtime_error("A local MaxRest does not have a TestConnections instance (yet).");
    }

    void raise(bool fail_on_error, const std::string& message) const override final
    {
        throw runtime_error(message);
    }

    mxt::CmdResult execute_curl_command(const std::string& curl_command) const override final
    {
        mxt::CmdResult rv;
        FILE* pFile = popen(curl_command.c_str(), "r");

        if (pFile)
        {
            int c;
            while ((c = fgetc(pFile)) != EOF)
            {
                rv.output += (char)c;
            }

            int rc = pclose(pFile);

            if (!WIFEXITED(rc))
            {
                raise(true, "Execution of curl failed.");
            }

            int curl_rc = WEXITSTATUS(rc);

            switch (curl_rc)
            {
            case 0:
            case 22:
                // 22 HTTP page not retrieved. The requested url was not found or returned another
                //    error with the HTTP error code being 400 or above. This return code only
                //    appears if -f, --fail is used.
                rv.rc = curl_rc;
                break;

            default:
                {
                    ostringstream ss;
                    ss << "Curl failed with exit code %d." << curl_rc << ".";
                    raise(true, ss.str());
                }
            }
        }
        else
        {
            ostringstream ss;
            ss << "Could not popen '%s': " << mxb_strerror(errno);

            rv.output = ss.str();
        }

        return rv;
    }
};

//
// MaxRest
//
MaxRest::MaxRest()
    : m_sImp(new LocalImp())
{
}

MaxRest::MaxRest(TestConnections* pTest)
    : m_sImp(new SystemTestImp(pTest))
{
}

MaxRest::MaxRest(TestConnections* pTest, mxt::MaxScale* pMaxscale)
    : m_sImp(new SystemTestImp(pTest, pMaxscale))
{
}


MaxRest::Server::Server(const MaxRest& maxrest, json_t* pObject)
    : name       (maxrest.get<string>(pObject, "id", Presence::MANDATORY))
    , address    (maxrest.get<string>(pObject, "attributes/parameters/address"))
    , port       (maxrest.get<int64_t>(pObject, "attributes/parameters/port"))
    , connections(maxrest.get<int64_t>(pObject, "attributes/statistics/connections"))
    , state      (maxrest.get<string>(pObject, "attributes/state"))
{
}

MaxRest::Thread::Thread(const MaxRest& maxrest, json_t* pObject)
    : id         (maxrest.get<string>(pObject, "id", Presence::MANDATORY))
    , state      (maxrest.get<string>(pObject, "attributes/stats/state"))
    , listening  (maxrest.get<bool>(pObject, "attributes/stats/listening"))
{
}

mxb::Json MaxRest::v1_maxscale_threads(const string& id) const
{
    string path("maxscale/threads");
    path += "/";
    path += id;

    return curl_get(path);
}

mxb::Json MaxRest::v1_maxscale_threads() const
{
    return curl_get("maxscale/threads");
}

mxb::Json MaxRest::v1_servers(const string& id) const
{
    string path("servers");
    path += "/";
    path += id;

    return curl_get(path);
}

mxb::Json MaxRest::v1_servers() const
{
    return curl_get("servers");
}

mxb::Json MaxRest::v1_services(const string& id) const
{
    string path("services");
    path += "/";
    path += id;

    return curl_get(path);
}

mxb::Json MaxRest::v1_services() const
{
    return curl_get("services");
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

void MaxRest::alter(const std::string& resource, const std::vector<Parameter>& parameters) const
{
    ostringstream body;
    body << "{\"data\": {\"attributes\": {\"parameters\": {";

    auto end = parameters.end();
    auto i = parameters.begin();

    while (i != end)
    {
        const Parameter& parameter = *i;

        body << "\"" << parameter.name << "\": " << to_json_value(parameter.value);

        if (++i != end)
        {
            body << ", ";
        }
    }

    body << "}}}}";

    curl_patch(resource, body.str());
}

void MaxRest::alter_maxscale(const vector<Parameter>& parameters) const
{
    alter("maxscale", parameters);
}

void MaxRest::alter_maxscale(const Parameter& parameter) const
{
    vector<Parameter> parameters = { parameter };
    alter_maxscale(parameters);
}

void MaxRest::alter_maxscale(const string& parameter_name, const Value& parameter_value) const
{
    alter_maxscale(Parameter { parameter_name, parameter_value });
}

void MaxRest::create_service(const std::string& name, const std::string& router, const std::vector<Parameter>& parameters)
{
    ostringstream body;
    body << "{\"data\": {"
         <<    "\"id\": " << to_json_value(name) << ","
         <<    "\"attributes\": {"
         <<      "\"router\": " << to_json_value(router) << ","
         <<      "\"parameters\": {";

    auto end = parameters.end();
    auto i = parameters.begin();

    while (i != end)
    {
        const Parameter& parameter = *i;

        body << "\"" << parameter.name << "\": " << to_json_value(parameter.value);

        if (++i != end)
        {
            body << ", ";
        }
    }

    body << "}}}}";

    curl_post("services", body.str());
}

void MaxRest::create_listener(const std::string& service, const std::string& name, int port)
{
    ostringstream body;
    body << "{\"data\": {"
         <<    "\"id\": " << to_json_value(name) << ","
         <<    "\"type\": \"listeners\","
         <<    "\"attributes\": {"
         <<      "\"parameters\": {"
         <<        "\"port\": " << port
         <<      "}"
         <<    "},"
         <<    "\"relationships\": {"
         <<      "\"services\": {"
         <<        "\"data\": [{ \"id\": " << to_json_value(service) << ", \"type\": \"services\" }]"
         <<      "}"
         <<   "}"
         <<  "}"
         << "}";

    curl_post("listeners", body.str());
}

MaxRest::Server MaxRest::show_server(const std::string& id) const
{
    mxb::Json object = v1_servers(id);
    json_t* pData = get_object(object.get_json(), "data", Presence::MANDATORY);
    return Server(*this, pData);
}

vector<MaxRest::Thread> MaxRest::show_threads() const
{
    return get_array<Thread>(v1_maxscale_threads().get_json(), "data", Presence::MANDATORY);
}

MaxRest::Thread MaxRest::show_thread(const std::string& id) const
{
    mxb::Json object = v1_maxscale_threads(id);
    json_t* pData = get_object(object.get_json(), "data", Presence::MANDATORY);
    return Thread(*this, pData);
}

vector<MaxRest::Server> MaxRest::list_servers() const
{
    return get_array<Server>(v1_servers().get_json(), "data", Presence::MANDATORY);
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

mxb::Json MaxRest::curl_get(const string& path) const
{
    return curl(GET, path);
}

mxb::Json MaxRest::curl_patch(const std::string& path, const std::string& body) const
{
    return curl(PATCH, path, body);
}

mxb::Json MaxRest::curl_post(const string& path, const std::string& body) const
{
    return curl(POST, path, body);
}

mxb::Json MaxRest::curl_put(const string& path) const
{
    return curl(PUT, path);
}

mxb::Json MaxRest::curl(Command command, const string& path, const string& body) const
{
    string url = "http://127.0.0.1:8989/v1/" + path;
    string curl_command = "curl --fail -s -u admin:mariadb ";

    switch (command)
    {
    case GET:
        curl_command += "-X GET ";
        break;

    case PATCH:
        curl_command += "-X PATCH ";
        break;

    case POST:
        curl_command += "-X POST ";
        break;

    case PUT:
        curl_command += "-X PUT ";
        break;
    }

    curl_command += url;

    if (!body.empty())
    {
        curl_command += " -d '";
        curl_command += body;
        curl_command += "'";
    }

    auto result = m_sImp->execute_curl_command(curl_command);

    mxb::Json rv;

    if (!result.output.empty() && !rv.load_string(result.output))
    {
        raise("JSON parsing failed: " + rv.error_msg());
    }

    mxb::Json errors = rv.get_object("errors");

    if (errors)
    {
        throw runtime_error(errors.to_string());
    }

    return rv;
}


template<>
bool MaxRest::get<bool>(json_t* pObject, const string& key, Presence presence) const
{
    json_t* pValue = get_leaf_object(pObject, key, presence);

    if (pValue && !json_is_boolean(pValue))
    {
        raise("Key '" + key + "' is present, but value is not a boolean.");
    }

    return pValue ? json_boolean_value(pValue) : false;
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
