/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "avrorouter.hh"

#include <ctype.h>
#include <ini.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <glob.h>
#include <ini.h>
#include <sys/stat.h>
#include <maxbase/atomic.h>
#include <maxscale/cn_strings.hh>
#include <maxscale/maxscale.h>
#include <maxbase/worker.hh>
#include <maxbase/alloc.h>
#include <maxscale/dcb.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/paths.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/random.h>
#include <maxscale/router.hh>
#include <maxscale/server.hh>
#include <maxscale/service.hh>
#include <maxscale/utils.h>
#include <maxscale/routingworker.hh>
#include <maxscale/config2.hh>

#include "avro_converter.hh"

using namespace maxbase;
namespace cfg = mxs::config;
using Opt = cfg::ParamPath::Options;
constexpr uint32_t opts = Opt::C | Opt::X | Opt::R | Opt::W;

static cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::Kind::ROUTER);

static cfg::ParamPath s_binlogdir(
    &s_spec, "binlogdir", "Path to directory containing binlog files", opts, mxs::datadir());

static cfg::ParamPath s_avrodir(
    &s_spec, "avrodir", "Path to directory where avro files are stored", opts);

static cfg::ParamString s_filestem(
    &s_spec, "filestem", "Root part of the binlog file name", "mysql-bin");

static cfg::ParamCount s_group_rows(
    &s_spec, "group_rows",
    "Controls the number of row events that are grouped into a single Avro data block",
    1000);

static cfg::ParamCount s_group_trx(
    &s_spec, "group_trx",
    "Controls the number of transactions that are grouped into a single Avro data block",
    1);

static cfg::ParamCount s_start_index(
    &s_spec, "start_index", "The starting index number of the binlog file", 1);

static cfg::ParamSize s_block_size(
    &s_spec, "block_size", "The Avro data block size in bytes", 0);

static cfg::ParamEnum<mxs_avro_codec_type> s_codec(
    &s_spec, "codec", "Avro compression codec",
{
    {MXS_AVRO_CODEC_NULL, "null", },
    {MXS_AVRO_CODEC_DEFLATE, "deflate"},
},
    MXS_AVRO_CODEC_NULL);

static cfg::ParamRegex s_match(
    &s_spec, "match", "Process events whose table matches this regex", "");

static cfg::ParamRegex s_exclude(
    &s_spec, "exclude", "Exclude events whose table matches this regex", "");

static cfg::ParamCount s_server_id(
    &s_spec, "server_id", "Server ID for direct replication mode", 1234);

static cfg::ParamString s_gtid_start_pos(
    &s_spec, "gtid_start_pos", "GTID position to start replicating from", "");


AvroConfig::AvroConfig(const std::string& name)
    : mxs::config::Configuration(name, &s_spec)
{
    add_native(&AvroConfig::filestem, &s_filestem);
    add_native(&AvroConfig::binlogdir, &s_binlogdir);
    add_native(&AvroConfig::avrodir, &s_avrodir);
    add_native(&AvroConfig::gtid, &s_gtid_start_pos);
    add_native(&AvroConfig::trx_target, &s_group_trx);
    add_native(&AvroConfig::row_target, &s_group_rows);
    add_native(&AvroConfig::server_id, &s_server_id);
    add_native(&AvroConfig::start_index, &s_start_index);
    add_native(&AvroConfig::block_size, &s_block_size);
    add_native(&AvroConfig::match, &s_match);
    add_native(&AvroConfig::exclude, &s_exclude);
    add_native(&AvroConfig::codec, &s_codec);
}

bool converter_func(Worker::Call::action_t action, Avro* router)
{
    if (action == Worker::Call::CANCEL)
    {
        return false;
    }

    bool progress = false;
    avro_binlog_end_t binlog_end = AVRO_BINLOG_ERROR;

    uint64_t start_pos = router->current_pos;
    std::string binlog_name = router->binlog_name;

    if (avro_open_binlog(router->config().binlogdir.c_str(), router->binlog_name.c_str(), &router->binlog_fd))
    {
        binlog_end = avro_read_all_events(router);

        if (router->current_pos != start_pos || binlog_name != router->binlog_name)
        {
            progress = true;
        }

        close(router->binlog_fd);
    }

    static int logged = true;

    /** We reached end of file, flush unwritten records to disk */
    if (progress)
    {
        router->handler->flush();
        avro_save_conversion_state(router);
        logged = false;
    }

    if (binlog_end == AVRO_LAST_FILE && !logged)
    {
        logged = true;
        MXS_INFO("Stopped processing file %s at position %lu. Waiting until"
                 " more data is written before continuing.",
                 router->binlog_name.c_str(),
                 router->current_pos);
    }

    return true;
}

class ConversionCtlTask : public Worker::DisposableTask
{
public:
    ConversionCtlTask(Avro* instance, bool start)
        : m_instance(instance)
        , m_start(start)
    {
    }

    void execute(Worker& worker)
    {
        if (m_instance->task_handle)
        {
            worker.cancel_delayed_call(m_instance->task_handle);
            m_instance->task_handle = 0;
        }

        if (m_start)
        {
            m_instance->task_handle = worker.delayed_call(1000, converter_func, m_instance);
        }
    }

private:
    Avro* m_instance;
    bool  m_start;
};

bool conversion_task_ctl(Avro* inst, bool start)
{
    bool rval = false;

    if (!maxscale_is_shutting_down())
    {
        Worker* worker = static_cast<Worker*>(mxs_rworker_get(MXS_RWORKER_MAIN));
        std::unique_ptr<ConversionCtlTask> task(new(std::nothrow) ConversionCtlTask(inst, start));

        if (task.get())
        {
            worker->execute(std::move(task), Worker::EXECUTE_AUTO);
            rval = true;
        }
    }

    return rval;
}

bool avro_handle_convert(const MODULECMD_ARG* args, json_t** output)
{
    bool rval = false;

    if (strcmp(args->argv[1].value.string, "start") == 0
        && conversion_task_ctl((Avro*)args->argv[0].value.service->router_instance, true))
    {
        MXS_NOTICE("Started conversion for service '%s'.", args->argv[0].value.service->name());
        rval = true;
    }
    else if (strcmp(args->argv[1].value.string, "stop") == 0
             && conversion_task_ctl((Avro*)args->argv[0].value.service->router_instance, false))
    {
        MXS_NOTICE("Stopped conversion for service '%s'.", args->argv[0].value.service->name());
        rval = true;
    }

    return rval;
}


static bool do_unlink(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    char filename[PATH_MAX + 1];
    vsnprintf(filename, sizeof(filename), format, args);

    va_end(args);

    int rc = unlink(filename);
    return rc == 0 || rc == ENOENT;
}

static bool do_unlink_with_pattern(const char* format, ...)
{
    bool rval = true;
    va_list args;
    va_start(args, format);

    char filename[PATH_MAX + 1];
    vsnprintf(filename, sizeof(filename), format, args);

    va_end(args);

    glob_t g;
    int rc = glob(filename, 0, NULL, &g);

    if (rc == 0)
    {
        for (size_t i = 0; i < g.gl_pathc; i++)
        {
            if (!do_unlink("%s", g.gl_pathv[i]))
            {
                rval = false;
            }
        }
    }
    else if (rc != GLOB_NOMATCH)
    {
        modulecmd_set_error("Failed to search '%s': %d, %s",
                            filename,
                            errno,
                            mxs_strerror(errno));
        rval = false;
    }

    globfree(&g);

    return rval;
}

static bool avro_handle_purge(const MODULECMD_ARG* args, json_t** output)
{
    Avro* inst = (Avro*)args->argv[0].value.service->router_instance;

    // First stop the conversion service
    conversion_task_ctl(inst, false);

    // Then delete the files
    return do_unlink("%s/%s", inst->config().avrodir.c_str(), AVRO_PROGRESS_FILE)   // State file
           && do_unlink_with_pattern("/%s/*.avro", inst->config().avrodir.c_str())  // .avro files
           && do_unlink_with_pattern("/%s/*.avsc", inst->config().avrodir.c_str()); // .avsc files
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static modulecmd_arg_type_t args_convert[] =
    {
        {MODULECMD_ARG_SERVICE | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
         "The avrorouter service"},
        {MODULECMD_ARG_STRING,
         "Action, whether to 'start' or 'stop' the conversion process"}
    };
    modulecmd_register_command(MXS_MODULE_NAME,
                               "convert",
                               MODULECMD_TYPE_ACTIVE,
                               avro_handle_convert,
                               2,
                               args_convert,
                               "Start or stop the binlog to avro conversion process");

    static modulecmd_arg_type_t args_purge[] =
    {
        {
            MODULECMD_ARG_SERVICE | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
            "The avrorouter service to purge (NOTE: THIS REMOVES ALL CONVERTED FILES)"
        }
    };
    modulecmd_register_command(MXS_MODULE_NAME,
                               "purge",
                               MODULECMD_TYPE_ACTIVE,
                               avro_handle_purge,
                               1,
                               args_purge,
                               "Purge created Avro files and reset conversion state. "
                               "NOTE: MaxScale must be restarted after this call.");

    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_GA,
        MXS_ROUTER_VERSION,
        "Avrorouter",
        "V1.0.0",
        0,
        &Avro::s_object,
        NULL,
        NULL,
        NULL,
        NULL,
        {{nullptr}},
        &s_spec
    };

    return &info;
}
