/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXS_MODULE_NAME "kafkaimporter"

#include <maxscale/config2.hh>

#include "../kafkacdc/kafka_common.hh"      // TODO: This file should be placed somewhere else

namespace kafkaimporter
{
enum IDType
{
    ID_FROM_TOPIC,
    ID_FROM_KEY,
};

struct PostConfigurable
{
    virtual bool post_configure() = 0;
};

class Config : public mxs::config::Configuration
{
public:
    Config(const std::string& name, PostConfigurable* router);

    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;

    static mxs::config::Specification* specification();

    mxs::config::String         bootstrap_servers;
    mxs::config::StringList     topics;
    mxs::config::Count          batch_size;
    mxs::config::Enum<IDType>   table_name_in;
    mxs::config::Seconds        timeout;
    mxs::config::Bool           ssl;
    mxs::config::Path           ssl_ca;
    mxs::config::Path           ssl_cert;
    mxs::config::Path           ssl_key;
    mxs::config::String         sasl_user;
    mxs::config::String         sasl_password;
    mxs::config::Enum<SaslMech> sasl_mechanism;

private:
    PostConfigurable* m_router;
};
}
