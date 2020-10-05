/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mirror.hh"
#include "mirrorsession.hh"

// static
Mirror* Mirror::create(SERVICE* pService, mxs::ConfigParameters* params)
{
    std::unique_ptr<Mirror> rval(new Mirror(pService));

    if (!rval->configure(params))
    {
        rval.reset();
    }

    return rval.release();
}

mxs::RouterSession* Mirror::newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints)
{
    const auto& children = m_service->get_children();

    if (std::find(children.begin(), children.end(), m_main) == children.end())
    {
        MXS_ERROR("Main target '%s' is not listed in `targets`", m_main->name());
        return nullptr;
    }

    auto backends = MyBackend::from_endpoints(endpoints);
    bool connected = false;

    for (const auto& a : backends)
    {
        if (a->can_connect() && a->connect())
        {
            connected = true;
        }
    }

    return connected ? new MirrorSession(pSession, this, std::move(backends)) : NULL;
}

json_t* Mirror::diagnostics() const
{
    return nullptr;
}

const uint64_t caps = RCAP_TYPE_REQUEST_TRACKING | RCAP_TYPE_RUNTIME_CONFIG;

uint64_t Mirror::getCapabilities() const
{
    return caps;
}

bool Mirror::configure(mxs::ConfigParameters* params)
{
    bool rval = false;
    auto main_tgt = params->get_target("main");
    const auto& children = m_service->get_children();

    std::lock_guard<mxb::shared_mutex> guard(m_rw_lock);

    if (auto exporter = build_exporter(params))
    {
        m_exporter = std::move(exporter);
        m_main = main_tgt;
        rval = true;
    }

    return rval;
}

void Mirror::ship(json_t* obj)
{
    {
        mxb::shared_lock<mxb::shared_mutex> guard(m_rw_lock);
        m_exporter->ship(obj);
    }

    json_decref(obj);
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    const char* desc = "Mirrors SQL statements to multiple targets";

    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        "mirror",
        mxs::ModuleType::ROUTER,
        mxs::ModuleStatus::ALPHA,
        MXS_ROUTER_VERSION,
        desc,
        "V1.0.0",
        caps,
        &mxs::RouterApi<Mirror>::s_api,
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {
                "main",
                MXS_MODULE_PARAM_TARGET,
                nullptr,
                MXS_MODULE_OPT_REQUIRED
            },
            {
                CN_EXPORTER,
                MXS_MODULE_PARAM_ENUM,
                nullptr,
                MXS_MODULE_OPT_REQUIRED | MXS_MODULE_OPT_ENUM_UNIQUE,
                exporter_type_values
            },
            {
                CN_FILE,
                MXS_MODULE_PARAM_STRING
            },
            {
                CN_KAFKA_BROKER,
                MXS_MODULE_PARAM_STRING
            },
            {
                CN_KAFKA_TOPIC,
                MXS_MODULE_PARAM_STRING
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
