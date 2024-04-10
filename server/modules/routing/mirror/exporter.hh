/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "common.hh"

#include <maxbase/jansson.hh>
#include <maxscale/ccdefs.hh>

#include "config.hh"

struct Exporter
{
    virtual ~Exporter() = default;

    /**
     * Ship a JSON object outside of MaxScale
     *
     * @param obj JSON object to ship
     */
    virtual void ship(json_t* obj) = 0;
};

std::unique_ptr<Exporter> build_exporter(const Config& config);
