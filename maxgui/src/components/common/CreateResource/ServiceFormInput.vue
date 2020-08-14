<template>
    <div class="mb-2">
        <module-parameters ref="moduleInputs" moduleName="router" :modules="resourceModules" />
        <resource-relationships
            ref="serversRelationship"
            relationshipsType="servers"
            :items="serversList"
            :defaultItems="defaultItems"
        />
        <resource-relationships
            ref="filtersRelationship"
            relationshipsType="filters"
            :items="filtersList"
            :defaultItems="defaultItems"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import ModuleParameters from './ModuleParameters'
import ResourceRelationships from './ResourceRelationships'

export default {
    name: 'service-form-input',
    components: {
        ModuleParameters,
        ResourceRelationships,
    },
    props: {
        resourceModules: { type: Array, required: true },
        allServers: { type: Array, required: true },
        allFilters: { type: Array, required: true },
        defaultItems: { type: [Array, Object], required: true },
    },

    computed: {
        serversList: function() {
            return this.allServers.map(({ id, type }) => ({ id, type }))
        },
        filtersList: function() {
            return this.allFilters.map(({ id, type }) => ({ id, type }))
        },
    },
    methods: {
        getValues() {
            const { moduleInputs, serversRelationship, filtersRelationship } = this.$refs
            const { moduleId, parameters } = moduleInputs.getModuleInputValues()
            return {
                moduleId,
                parameters,
                relationships: {
                    servers: { data: serversRelationship.getSelectedItems() },
                    filters: { data: filtersRelationship.getSelectedItems() },
                },
            }
        },
    },
}
</script>
