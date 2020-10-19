<template>
    <div class="mb-2">
        <module-parameters ref="moduleInputs" moduleName="router" :modules="resourceModules" />
        <resource-relationships
            ref="serversRelationship"
            relationshipsType="servers"
            :items="serversList"
            :defaultItems="defaultServerItems"
        />
        <resource-relationships
            ref="filtersRelationship"
            relationshipsType="filters"
            :items="filtersList"
            :defaultItems="defaultFilterItems"
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
 * Change Date: 2024-10-14
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
        defaultItems: { type: [Array, Object], default: () => [] },
    },
    data() {
        return {
            defaultServerItems: [],
            defaultFilterItems: [],
        }
    },
    computed: {
        serversList: function() {
            return this.allServers.map(({ id, type }) => ({ id, type }))
        },
        filtersList: function() {
            return this.allFilters.map(({ id, type }) => ({ id, type }))
        },
        isServerDefaultItems: function() {
            const isValidArr = this.$help.isNotEmptyArray(this.defaultItems)
            return isValidArr && this.defaultItems[0].type === 'servers'
        },
    },
    watch: {
        defaultItems: function() {
            if (this.isServerDefaultItems) this.defaultServerItems = this.defaultItems
            else this.defaultFilterItems = this.defaultItems
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
