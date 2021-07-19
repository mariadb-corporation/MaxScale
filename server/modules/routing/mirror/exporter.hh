/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/config.hh>
#include <maxscale/log.hh>
#include <maxscale/jansson.hh>

constexpr const char CN_EXPORTER[] = "exporter";
constexpr const char CN_FILE[] = "file";
constexpr const char CN_KAFKA_BROKER[] = "kafka_broker";
constexpr const char CN_KAFKA_TOPIC[] = "kafka_topic";

struct Exporter
{
    enum class Type : uint64_t
    {
        LOG,
        FILE,
        KAFKA,
    };

    virtual ~Exporter() = default;

    /**
     * Ship a JSON object outside of MaxScale
     *
     * @param obj JSON object to ship
     */
    virtual void ship(json_t* obj) = 0;
};

static const MXS_ENUM_VALUE exporter_type_values[] =
{
    {"log",   (uint64_t)Exporter::Type::LOG  },
    {"file",  (uint64_t)Exporter::Type::FILE },
    {"kafka", (uint64_t)Exporter::Type::KAFKA},
    {NULL}
};

std::unique_ptr<Exporter> build_exporter(mxs::ConfigParameters* params);
