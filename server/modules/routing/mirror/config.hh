/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "common.hh"

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>

enum ExporterType
{
    EXPORT_LOG,
    EXPORT_FILE,
    EXPORT_KAFKA,
};

enum ErrorAction
{
    ERRACT_IGNORE,
    ERRACT_CLOSE,
};

enum ReportAction
{
    REPORT_ALWAYS,
    REPORT_ON_CONFLICT,
};

class Mirror;

struct Config : public mxs::config::Configuration
{
    Config(const char* name, Mirror* instance);

    mxs::Target* main;
    ExporterType exporter;
    std::string  file;
    std::string  kafka_broker;
    std::string  kafka_topic;

    mxs::config::Enum<ErrorAction>  on_error;
    mxs::config::Enum<ReportAction> report;

    static mxs::config::Specification* spec();

protected:
    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;

private:
    Mirror* m_instance;
};
