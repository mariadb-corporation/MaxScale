/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include "cache_storage_api.hh"

class Storage;
class StorageLimits;

class StorageFactory
{
public:
    StorageFactory(const StorageFactory&) = delete;
    StorageFactory& operator=(const StorageFactory&) = delete;

    ~StorageFactory();

    static StorageFactory* open(const char* zName);
    static StorageFactory* open(const std::string& name)
    {
        return open(name.c_str());
    }

    /**
     * @return The specification of the storage created by this factory.
     */
    const mxs::config::Specification& specification() const;

    /**
     * The capabilities of storages created using this factory.
     * These capabilities may be a superset of those reported
     * by @c storage_capabilities.
     *
     * @return Bitmask of @c cache_storage_capabilities_t values.
     */
    uint32_t capabilities() const
    {
        return m_caps;
    }

    /**
     * The capabilities of storages loaded via this factory. These
     * capabilities may be a subset of those reported by @ capabilities.
     *
     * @return Bitmask of @c cache_storage_capabilities_t values.
     */
    uint32_t storage_capabilities() const
    {
        return m_storage_caps;
    }

    /**
     * Returns the limits of a storage created with this factory.
     *
     * @param parameters Parameters for the storage.
     * @param pLimits    Pointer to object that will be updated.
     *
     * @return True, if the parameters were valid and sufficient, false otherwise.
     */
    bool get_limits(const mxs::ConfigParameters& parameters, StorageLimits* pLimits) const;

    /**
     * Create storage instance.
     *
     * If some of the required functionality (max_count != 0 and/or
     * max_size != 0) is not provided by the underlying storage
     * implementation that will be provided on top of what is "natively"
     * provided.
     *
     * @param zName      The name of the storage.
     * @param config     The storage configuration.
     * @param parameters Storage specific parameters.
     *
     * @return A storage instance or NULL in case of errors.
     */
    Storage* create_storage(const char* zName,
                            const Storage::Config& config,
                            const mxs::ConfigParameters& parameters = mxs::ConfigParameters());

    /**
     * Create raw storage instance.
     *
     * The returned instance provides exactly the functionality the
     * underlying storage module is capable of providing. The provided
     * arguments (notably max_count and max_size) should be adjusted
     * accordingly.
     *
     * @param zName      The name of the storage.
     * @param config     The storage configuration.
     * @param parameters Storage specific parameters.
     *
     * @return A storage instance or NULL in case of errors.
     */
    Storage* create_raw_storage(const char* zName,
                                const Storage::Config& config,
                                const mxs::ConfigParameters& parameters = mxs::ConfigParameters());

private:
    StorageFactory(void* handle,
                   StorageModule* pModule,
                   cache_storage_kind_t kind,
                   uint32_t capabilities);

    Storage* create_private_storage(const char* zName,
                                    const Storage::Config& config,
                                    const mxs::ConfigParameters& parameters);

    Storage* create_shared_storage(const char* zName,
                                   const Storage::Config& config,
                                   const mxs::ConfigParameters& parameters);

private:
    void*                m_handle;        /*< dl handle of storage. */
    StorageModule*       m_pModule;       /*< API of storage. */
    cache_storage_kind_t m_kind;          /*< The kind of storage; private or shared. */
    uint32_t             m_storage_caps;  /*< Capabilities of underlying storage. */
    uint32_t             m_caps;          /*< Capabilities of storages of this factory. */
};
