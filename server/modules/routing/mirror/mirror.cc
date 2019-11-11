/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mirror.hh"
#include "mirrorsession.hh"

Mirror::Mirror(SERVICE* pService, MXS_CONFIG_PARAMETER* params, std::unique_ptr<Exporter> exporter)
    : Router<Mirror, MirrorSession>(pService)
    , m_main(params->get_target("main"))
    , m_exporter(std::move(exporter))
{
}

Mirror::~Mirror()
{
}

// static
Mirror* Mirror::create(SERVICE* pService, MXS_CONFIG_PARAMETER* params)
{
    Mirror* rval = nullptr;

    if (auto exporter = build_exporter(params))
    {
        rval = new Mirror(pService, params, std::move(exporter));
    }

    return rval;
}

MirrorSession* Mirror::newSession(MXS_SESSION* pSession, const Endpoints& endpoints)
{
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

uint64_t Mirror::getCapabilities()
{
    return caps;
}

bool Mirror::configure(MXS_CONFIG_PARAMETER* params)
{
    m_main = params->get_target("main");
    return true;
}

void Mirror::ship(json_t* obj)
{
    m_exporter->ship(obj);
    json_decref(obj);
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    const char* desc = "Mirrors SQL statements to multiple targets";

    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_ALPHA_RELEASE,
        MXS_ROUTER_VERSION,
        desc,
        "V1.0.0",
        caps,
        &Mirror::s_object,
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
                MXS_MODULE_OPT_REQUIRED,
                exporter_type_values
            },
            {
                CN_FILE,
                MXS_MODULE_PARAM_STRING
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
