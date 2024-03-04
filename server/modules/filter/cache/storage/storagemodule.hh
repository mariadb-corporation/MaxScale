/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

template<class StorageType>
class StorageModuleT : public StorageModule
{
public:
    bool initialize(cache_storage_kind_t* pKind, uint32_t* pCapabilities) override final
    {
        return StorageType::initialize(pKind, pCapabilities);
    }

    void finalize() override final
    {
        StorageType::finalize();
    }

    bool get_limits(const mxs::ConfigParameters& parameters, Storage::Limits* pLimits) const override final
    {
        return StorageType::get_limits(parameters, pLimits);
    }

    Storage* create_storage(const char* zName,
                            const Storage::Config& config,
                            const mxs::ConfigParameters& parameters) override final
    {
        mxb_assert(zName);

        return StorageType::create(zName, config, parameters);
    }

    const mxs::config::Specification& specification() const override final
    {
        return StorageType::specification();
    }
};
