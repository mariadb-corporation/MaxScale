/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <stdexcept>
#include <variant>
#include <maxtest/testconnections.hh>
#include <maxbase/json.hh>

/**
 * @class MaxRest
 *
 * MaxRest is a class that (eventually) provides the same functionality as
 * the command line program maxctrl, but for use in programs.
 */
class MaxRest
{
public:
    MaxRest(const MaxRest&) = delete;
    MaxRest& operator=(const MaxRest&) = delete;

    class Error : public std::runtime_error
    {
    public:
        Error(int http_status, const std::string& message)
            : std::runtime_error(std::to_string(http_status) + ": " + message)
            , http_status(http_status)
        {
        }

        const int http_status;
    };

    /**
     * A class corresponding to a row in the output of 'maxctrl list servers'
     */
    struct Server
    {
        Server() = default;
        Server(const MaxRest& maxrest, json_t* pObject);

        std::string name;
        std::string address;
        int64_t     port;
        int64_t     connections;
        std::string state;
    };

    /**
     * A class corresponding to a row in the output of 'maxctrl show threads'
     */
    struct Thread
    {
        Thread() = default;
        Thread(const MaxRest& maxrest, json_t* pObject);

        std::string id;
        std::string state;
        bool        listening;
    };

    /**
     * System Test Constructor, to be used in the System Test environment.
     *
     * @param pTest  The TestConnections instance. Must exist for the lifetime
     *               of the MaxRest instance.
     */
    MaxRest(TestConnections* pTest);
    MaxRest(TestConnections* pTest, mxt::MaxScale* pMaxscale);

    /**
     * @return  The TestConnections instance used by this instance.
     */
    TestConnections& test() const
    {
        return m_sImp->test();
    }

    /**
     * @return The JSON object corresponding to /v1/maxscale/threads/:id.
     */
    mxb::Json v1_maxscale_threads(const std::string& id) const;

    /**
     * @return The JSON object corresponding to /v1/maxscale/threads.
     */
    mxb::Json v1_maxscale_threads() const;

    /**
     * @return The JSON object corresponding to /v1/servers/:id:
     */
    mxb::Json v1_servers(const std::string& id) const;

    /**
     * @return The JSON object corresponding to /v1/servers.
     */
    mxb::Json v1_servers() const;

    /**
     * @return The JSON object corresponding to /v1/services/:id:
     */
    mxb::Json v1_services(const std::string& id) const;

    /**
     * @return The JSON object corresponding to /v1/services.
     */
    mxb::Json v1_services() const;

    /**
     * POST request to /v1/maxscale/modules/:module:/:command:?instance[&param...]
     *
     * @param module    Module name.
     * @param command   The command.
     * @param instance  The object instance to execute it on.
     * @param params    Optional arguments.
     */
    void v1_maxscale_modules(const std::string& module,
                             const std::string& command,
                             const std::string& instance,
                             const std::vector<std::string>& params = std::vector<std::string>()) const;

    /**
     * Call a module command.
     *
     * @param module    Module name.
     * @param command   The command.
     * @param instance  The object instance to execute it on.
     * @param params    Optional arguments.
     */
    void call_command(const std::string& module,
                      const std::string& command,
                      const std::string& instance,
                      const std::vector<std::string>& params = std::vector<std::string>()) const
    {
        return v1_maxscale_modules(module, command, instance, params);
    }

    class Value : public std::variant<std::string, int64_t, bool>
    {
    public:
        using std::variant<std::string, int64_t, bool>::variant;

        Value(const char* z)
            : std::variant<std::string, int64_t, bool>(std::string(z))
        {
        }

        Value(int i)
            : std::variant<std::string, int64_t, bool>((int64_t) i)
        {
        }

        Value(unsigned u)
            : std::variant<std::string, int64_t, bool>((int64_t) u)
        {
        }
    };

    struct Parameter
    {
        template<class T>
        Parameter(std::string n, T v)
            : name(n)
            , value(v)
        {
        }

        std::string name;
        Value       value;
    };

    /**
     * alter
     */
    void alter(const std::string& resource, const std::vector<Parameter>& parameters) const;

    void alter_maxscale(const std::vector<Parameter>& parameters) const;

    void alter_maxscale(const Parameter& parameter) const;

    void alter_maxscale(const std::string& parameter_name, const Value& parameter_value) const;

    /**
     * create
     */
    void create_listener(const std::string& service, const std::string& name, int port);

    void create_server(const std::string& name,
                       const std::string& address,
                       int port,
                       const std::vector<Parameter>& parameters = std::vector<Parameter>());

    void create_service(const std::string& name,
                        const std::string& router,
                        const std::vector<Parameter>& parameters);

    /**
     * destroy
     */
    void destroy_listener(const std::string& name);

    void destroy_server(const std::string& name);

    void destroy_service(const std::string& name, bool force);

    /**
     * The equivalent of 'maxctrl list servers'
     *
     * @return The JSON resource /v1/servers as a vector of Server objects.
     */
    std::vector<Server> list_servers() const;

    /**
     * The equivalent of 'maxctrl show threads'
     */
    std::vector<Thread> show_threads() const;

    /**
     * The equivalent of 'maxctrl show thread :id'
     */
    Thread show_thread(const std::string& id) const;

    /**
     * The equivalent of 'maxctrl show server'
     *
     * @return The JSON resource /v1/servers/:id: as a Server object.
     */
    Server show_server(const std::string& id) const;

    enum class Presence
    {
        OPTIONAL,
        MANDATORY
    };

    /**
     * Turns a JSON array at a specific path into a vector of desired type.
     *
     * @param pObject   The JSON object containing the JSON array.
     * @param path      The path of the resource, e.g. "a/b/c"
     * @param presence  Whether the path must exist or not. Note that if it is
     *                  a true path "a/b/c", only the leaf may be optional, the
     *                  components leading to the leaf must be present.
     *
     * @return Vector of object of desired type.
     */
    template<class T>
    std::vector<T> get_array(json_t* pObject, const std::string& path, Presence presence) const
    {
        std::vector<T> rv;
        pObject = get_leaf_object(pObject, path, presence);

        if (pObject)
        {
            if (!json_is_array(pObject))
            {
                raise("'" + path + "' exists, but is not an array.");
            }

            size_t size = json_array_size(pObject);

            for (size_t i = 0; i < size; ++i)
            {
                json_t* pElement = json_array_get(pObject, i);

                rv.push_back(T(*this, pElement));
            }
        }

        return rv;
    }

    /**
     * Get JSON object at specific key.
     *
     * @param pObject   The object containing the path.
     * @param key       The key of the resource. May *not* be a path.
     * @param presence  Whether the key must exist or not.
     *
     * @return The desired object, or NULL if it does not exist and @c presence is OPTIONAL.
     */
    json_t* get_object(json_t* pObject, const std::string& path, Presence presence) const;

    /**
     * Get JSON object at specific path.
     *
     * @param pObject   The object containing the path.
     * @param path      The path of the resource, e.g. "a/b/c"
     * @param presence  Whether the leaf must exist or not. Note that if it is
     *                  a true path "a/b/c", only the leaf may be optional, the
     *                  components leading to the leaf must be present.
     *
     * @return The desired object, or NULL if it does not exist and @c presence is OPTIONAL.
     */
    json_t* get_leaf_object(json_t* pObject, const std::string& path, Presence presence) const;

    /**
     * Get a JSON value as a particular C++ type.
     *
     * @param pObject   The object containing the path.
     * @param path      The path of the resource, e.g. "a/b/c"
     * @param presence  Whether the leaf must exist or not. Note that if it is
     *                  a true path "a/b/c", onlt the leaf may be optional, the
     *                  components leading to the leaf must be present.
     *
     * @return The desired object, or NULL if it does not exist and @c presence is OPTIONAL.
     */
    template<class T>
    T get(json_t* pObject, const std::string& path, Presence presence = Presence::OPTIONAL) const;

    /**
     * Parse a JSON object in a string.
     *
     * @return  The corresponding json_t object.
     */
    mxb::Json parse(const std::string& json) const;

    /**
     * Issue a curl DELETE/GET/PATCH/POST/PUT to the REST-API endpoint of MaxScale.
     *
     * The path will be appended to "http://127.0.0.1:8989/v1/".
     *
     * @param path  The path of the resource.
     * @param body  The body to be provided.
     *
     * @return  The returned json_t object.
     */
    mxb::Json curl_delete(const std::string& path) const;
    mxb::Json curl_get(const std::string& path) const;
    mxb::Json curl_patch(const std::string& path, const std::string& body) const;
    mxb::Json curl_post(const std::string& path, const std::string& body = std::string {}) const;
    mxb::Json curl_put(const std::string& path) const;

    void raise(bool fail, const std::string& message) const;
    void raise(const std::string& message) const
    {
        raise(m_fail_on_error, message);
    }
    void raise(int http_status, const std::string& message) const;

    /**
     * Enable or disable failing the test whenever an exception is thrown
     */
    void fail_on_error(bool value)
    {
        m_fail_on_error = value;
    }

private:
    enum Command
    {
        DELETE,
        GET,
        PATCH,
        POST,
        PUT
    };

    mxb::Json curl(Command command, const std::string& path, const std::string& body = std::string()) const;

    class Imp
    {
    public:
        virtual ~Imp() = default;

        virtual std::string body_quote() const = 0;
        virtual TestConnections& test() const = 0;
        virtual mxt::CmdResult execute_curl_command(const std::string& curl_command) const = 0;

    protected:
        Imp(MaxRest* pOwner)
            : m_owner(*pOwner)
        {
        }

        void raise(bool fail, const std::string& message) const
        {
            m_owner.raise(fail, message);
        }

        MaxRest& m_owner;
    };

    class LocalImp;
    class SystemTestImp;

private:
    Imp* create_imp(TestConnections* pTest, mxt::MaxScale* pMaxscale = nullptr);

    bool                 m_fail_on_error  {true};
    std::unique_ptr<Imp> m_sImp;
};

template<>
bool MaxRest::get<bool>(json_t* pObject, const std::string& path, Presence presence) const;

template<>
int64_t MaxRest::get<int64_t>(json_t* pObject, const std::string& path, Presence presence) const;

template<>
std::string MaxRest::get<std::string>(json_t* pObject, const std::string& path, Presence presence) const;
