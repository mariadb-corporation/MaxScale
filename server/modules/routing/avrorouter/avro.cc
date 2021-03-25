/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file avro.c - Avro router, allows MaxScale to act as an intermediary for
 * MySQL replication binlog files and AVRO binary files
 */

#include "avrorouter.hh"

#include <ctype.h>
#include <ini.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <ini.h>
#include <avro/errors.h>
#include <maxbase/atomic.h>
#include <maxbase/worker.hh>
#include <maxbase/alloc.h>
#include <maxscale/cn_strings.hh>
#include <maxscale/dcb.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/paths.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/random.h>
#include <maxscale/router.hh>
#include <maxscale/server.hh>
#include <maxscale/service.hh>
#include <maxscale/utils.hh>
#include <maxscale/pcre2.hh>
#include <maxscale/routingworker.hh>

#include "avro_converter.hh"

using namespace maxscale;

// static
Avro* Avro::create(SERVICE* service)
{
    auto params = service->params();
    return new(std::nothrow) Avro(service, &params);
}

Avro::Avro(SERVICE* service, mxs::ConfigParameters* params)
    : service(service)
    , filestem(params->get_string("filestem"))
    , binlogdir(params->get_string("binlogdir"))
    , avrodir(params->get_string("avrodir"))
    , current_pos(4)
    , binlog_fd(-1)
    , trx_count(0)
    , trx_target(params->get_integer("group_trx"))
    , row_count(0)
    , row_target(params->get_integer("group_rows"))
    , task_handle(0)
{
    uint64_t block_size = service->params().get_size("block_size");
    mxs_avro_codec_type codec = static_cast<mxs_avro_codec_type>(
        service->params().get_enum("codec", codec_values));

    if (params->contains(CN_SERVERS) || params->contains(CN_CLUSTER))
    {
        MXS_NOTICE("Replicating directly from a master server");
        cdc::Config cnf;
        cnf.service = service;
        cnf.statedir = avrodir;
        cnf.server_id = params->get_integer("server_id");
        cnf.gtid = params->get_string("gtid_start_pos");
        cnf.match = params->get_compiled_regex("match", 0, NULL).release();
        cnf.exclude = params->get_compiled_regex("exclude", 0, NULL).release();

        auto worker = mxs::RoutingWorker::get(mxs::RoutingWorker::MAIN);
        worker->execute(
            [this, cnf, block_size, codec]() {
                SRowEventHandler hndl(new AvroConverter(cnf.service, cnf.statedir, block_size, codec));
                m_replicator = cdc::Replicator::start(cnf, std::move(hndl));
                mxb_assert(m_replicator);
            },
            mxs::RoutingWorker::EXECUTE_QUEUED);
    }
    else
    {
        handler.reset(
            new Rpl(service,
                    SRowEventHandler(new AvroConverter(service, avrodir, block_size, codec)),
                    params->get_compiled_regex("match", 0, NULL).release(),
                    params->get_compiled_regex("exclude", 0, NULL).release()));

        char filename[BINLOG_FNAMELEN + 1];
        snprintf(filename,
                 sizeof(filename),
                 BINLOG_NAMEFMT,
                 filestem.c_str(),
                 static_cast<int>(params->get_integer("start_index")));
        binlog_name = filename;

        MXS_NOTICE("Reading MySQL binlog files from %s", binlogdir.c_str());
        MXS_NOTICE("First binlog is: %s", binlog_name.c_str());

        // TODO: Do these in Avro::create
        avro_load_conversion_state(this);
        handler->load_metadata(avrodir);
    }

    MXS_NOTICE("Avro files stored at: %s", avrodir.c_str());
}
