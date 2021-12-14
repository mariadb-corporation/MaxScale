/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "config.hh"
#include "mirror.hh"

namespace
{

namespace cfg = mxs::config;

class MirrorSpec : public cfg::Specification
{
public:
    using cfg::Specification::Specification;

protected:
    template<class Params>
    bool do_post_validate(Params params) const;

    bool post_validate(const mxs::ConfigParameters& params) const override
    {
        return do_post_validate(params);
    }

    bool post_validate(json_t* json) const override
    {
        return do_post_validate(json);
    }
};

MirrorSpec s_spec(MXS_MODULE_NAME, cfg::Specification::ROUTER);

cfg::ParamEnum<ExporterType> s_exporter(
    &s_spec, "exporter", "Exporter to use",
    {
        {ExporterType::EXPORT_FILE, "file"},
        {ExporterType::EXPORT_KAFKA, "kafka"},
        {ExporterType::EXPORT_LOG, "log"}
    }, cfg::Param::AT_RUNTIME);

cfg::ParamTarget s_main(
    &s_spec, "main", "Server from which responses are returned",
    cfg::Param::Kind::MANDATORY, cfg::Param::AT_RUNTIME);

cfg::ParamString s_file(
    &s_spec, "file", "File where data is exported", "", cfg::Param::AT_RUNTIME);

cfg::ParamString s_kafka_broker(
    &s_spec, "kafka_broker", "Kafka broker to use", "", cfg::Param::AT_RUNTIME);

cfg::ParamString s_kafka_topic(
    &s_spec, "kafka_topic", "Kafka topic where data is exported", "", cfg::Param::AT_RUNTIME);

cfg::ParamEnum<ErrorAction> s_on_error(
    &s_spec, "on_error", "What to do when a non-main connection fails",
    {
        {ErrorAction::ERRACT_IGNORE, "ignore"},
        {ErrorAction::ERRACT_CLOSE, "close"},
    },
    ErrorAction::ERRACT_IGNORE, cfg::Param::AT_RUNTIME);

cfg::ParamEnum<ReportAction> s_report(
    &s_spec, "report", "When to generate the report for an SQL command",
    {
        {ReportAction::REPORT_ALWAYS, "always"},
        {ReportAction::REPORT_ON_CONFLICT, "on_conflict"},
    },
    ReportAction::REPORT_ALWAYS, cfg::Param::AT_RUNTIME);

template<class Params>
bool MirrorSpec::do_post_validate(Params params) const
{
    bool ok = true;

    switch (s_exporter.get(params))
    {
    case ExporterType::EXPORT_LOG:
        break;

    case ExporterType::EXPORT_FILE:
        if (s_file.get(params).empty())
        {
            MXS_ERROR("'%s' must be defined when exporter=file is used.", s_file.name().c_str());
            ok = false;
        }
        break;

    case ExporterType::EXPORT_KAFKA:
        if (s_kafka_broker.get(params).empty() || s_kafka_topic.get(params).empty())
        {
            MXS_ERROR("Both '%s' and '%s' must be defined when exporter=kafka is used.",
                      s_kafka_broker.name().c_str(), s_kafka_topic.name().c_str());
            ok = false;
        }
        break;
    }

    return ok;
}
}

// static
mxs::config::Specification* Config::spec()
{
    return &s_spec;
}

Config::Config(const char* name, Mirror* instance)
    : mxs::config::Configuration(name, &s_spec)
    , on_error(this, &s_on_error)
    , report(this, &s_report)
    , m_instance(instance)
{
    add_native(&Config::exporter, &s_exporter);
    add_native(&Config::main, &s_main);
    add_native(&Config::file, &s_file);
    add_native(&Config::kafka_broker, &s_kafka_broker);
    add_native(&Config::kafka_topic, &s_kafka_topic);
}

bool Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    return m_instance->post_configure();
}
