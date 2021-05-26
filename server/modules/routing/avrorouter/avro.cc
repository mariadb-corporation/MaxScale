/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
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
    return new Avro(service);
}

Avro::Avro(SERVICE* service)
    : service(service)
    , current_pos(4)
    , binlog_fd(-1)
    , trx_count(0)
    , row_count(0)
    , task_handle(0)
    , m_config(service, *this)
{
}

bool Avro::post_configure()
{
    uint64_t block_size = m_config.block_size;
    mxs_avro_codec_type codec = m_config.codec;

    if (!service->get_children().empty())
    {
        MXS_NOTICE("Replicating directly from a master server");
        cdc::Config cnf;
        cnf.service = service;
        cnf.statedir = m_config.avrodir;
        cnf.server_id = m_config.server_id;
        cnf.gtid = m_config.gtid;
        cnf.match = m_config.match.code();
        cnf.exclude = m_config.exclude.code();
        cnf.cooperate = m_config.cooperative_replication;

        conversion_task_ctl(this, false);

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
                    SRowEventHandler(new AvroConverter(service, m_config.avrodir, block_size, codec)),
                    m_config.match.code(), m_config.exclude.code()));

        char filename[BINLOG_FNAMELEN + 1];
        snprintf(filename,
                 sizeof(filename),
                 BINLOG_NAMEFMT,
                 m_config.filestem.c_str(),
                 static_cast<int>(m_config.start_index));
        binlog_name = filename;

        MXS_NOTICE("Reading MySQL binlog files from %s", m_config.binlogdir.c_str());
        MXS_NOTICE("First binlog is: %s", binlog_name.c_str());

        // TODO: Do these in Avro::create
        avro_load_conversion_state(this);
        handler->load_metadata(m_config.avrodir);

        conversion_task_ctl(this, true);
    }

    MXS_NOTICE("Avro files stored at: %s", m_config.avrodir.c_str());
    return true;
}

mxs::RouterSession* Avro::newSession(MXS_SESSION* session, const Endpoints& endpoints)
{
    return AvroSession::create(this, session);
}

json_t* Avro::diagnostics() const
{
    const Avro* router_inst = this;

    json_t* rval = json_object();

    char pathbuf[PATH_MAX + 1];
    snprintf(pathbuf, sizeof(pathbuf), "%s/%s", router_inst->config().avrodir.c_str(), AVRO_PROGRESS_FILE);

    json_object_set_new(rval, "infofile", json_string(pathbuf));
    json_object_set_new(rval, "avrodir", json_string(router_inst->config().avrodir.c_str()));
    json_object_set_new(rval, "binlogdir", json_string(router_inst->config().binlogdir.c_str()));
    json_object_set_new(rval, "binlog_name", json_string(router_inst->binlog_name.c_str()));
    json_object_set_new(rval, "binlog_pos", json_integer(router_inst->current_pos));

    if (router_inst->handler)
    {
        gtid_pos_t gtid = router_inst->handler->get_gtid();
        snprintf(pathbuf, sizeof(pathbuf), "%lu-%lu-%lu", gtid.domain, gtid.server_id, gtid.seq);
        json_object_set_new(rval, "gtid", json_string(pathbuf));
        json_object_set_new(rval, "gtid_timestamp", json_integer(gtid.timestamp));
        json_object_set_new(rval, "gtid_event_number", json_integer(gtid.event_num));
    }

    return rval;
}
