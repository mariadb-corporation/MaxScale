/*
 * Copyright (c) 2021 MariaDB Corporation Ab
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

#include <maxtest/maxrest.hh>

#include <iostream>
#include <chrono>

using RestApi = std::unique_ptr<MaxRest>;

RestApi create_api1(TestConnections& test)
{
    auto rval = std::make_unique<MaxRest>(&test, test.maxscale);
    rval->fail_on_error(false);
    return rval;
}

RestApi create_api2(TestConnections& test)
{
    auto rval = std::make_unique<MaxRest>(&test, test.maxscale2);
    rval->fail_on_error(false);
    return rval;
}

static inline mxb::Json get(const RestApi& api, const std::string& endpoint, const std::string& js_ptr)
{
    mxb::Json rval(mxb::Json::Type::UNDEFINED);

    try
    {
        if (auto json = api->curl_get(endpoint))
        {
            rval = json.at(js_ptr.c_str());
        }
    }
    catch (const std::runtime_error& e)
    {
        std::cout << e.what() << std::endl;
    }

    return rval;
}

static inline int64_t get_version(const RestApi& api)
{
    return get(api, "maxscale", "/data/attributes/config_sync/version").get_int();
}
