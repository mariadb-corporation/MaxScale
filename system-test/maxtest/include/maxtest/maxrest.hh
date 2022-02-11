/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

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
     * Constructor
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
        return m_test;
    }

    /**
     * @return The JSON object corresponding to /v1/servers/:id:
     */
    mxb::Json v1_servers(const std::string& id) const;

    /**
     * @return The JSON object corresponding to /v1/servers.
     */
    mxb::Json v1_servers() const;

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

    /**
     * The equivalent of 'maxctrl list servers'
     *
     * @return The JSON resource /v1/servers as a vector of Server objects.
     */
    std::vector<Server> list_servers() const;

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
     *                  a true path "a/b/c", onlt the leaf may be optional, the
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
     * Issue a curl GET to the REST-API endpoint of the MaxScale running on
     * the maxscale 0 VM instance.
     *
     * The path will be appended to "http://127.0.0.1:8989/v1/".
     *
     * @param path  The path of the resource.
     *
     * @return  The corresponding json_t object.
     */
    mxb::Json curl_get(const std::string& path) const;

    /**
     * Issue a curl POST to the REST-API endpoint of the MaxScale running on
     * the maxscale 0 VM instance.
     *
     * The path will be appended to "http://127.0.0.1:8989/v1/".
     *
     * @param path  The path of the resource.
     *
     * @return  The corresponding json_t object.
     */
    mxb::Json curl_post(const std::string& path) const;

    void raise(const std::string& message) const;

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
        GET,
        POST
    };

    mxb::Json curl(Command command, const std::string& path) const;

private:
    TestConnections& m_test;
    mxt::MaxScale&   m_maxscale;
    bool             m_fail_on_error  {true};
};

template<>
std::string MaxRest::get<std::string>(json_t* pObject, const std::string& path, Presence presence) const;

template<>
int64_t MaxRest::get<int64_t>(json_t* pObject, const std::string& path, Presence presence) const;
