<template>
    <div class="mb-2">
        <module-parameters ref="moduleInputs" moduleName="module" :modules="resourceModules" />
        <resource-relationships
            ref="serversRelationship"
            relationshipsType="servers"
            :items="serversList"
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
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ModuleParameters from './ModuleParameters'
import ResourceRelationships from './ResourceRelationships'

export default {
    name: 'monitor-form-input',
    components: {
        ModuleParameters,
        ResourceRelationships,
    },
    props: {
        resourceModules: { type: Array, required: true },
        allServers: { type: Array, required: true },
        defaultItems: { type: [Array, Object], default: () => [] },
    },

    computed: {
        // get only server that are not monitored
        serversList: function() {
            let serverItems = []
            this.allServers.forEach(({ id, type, relationships: { monitors = null } = {} }) => {
                if (!monitors) serverItems.push({ id, type })
            })
            return serverItems
        },
    },

    methods: {
        getValues() {
            const { moduleInputs, serversRelationship } = this.$refs
            const { moduleId, parameters } = moduleInputs.getModuleInputValues()
            const data = serversRelationship.getSelectedItems()
            return {
                moduleId,
                parameters,
                relationships: {
                    servers: { data },
                },
            }
        },
    },
}
</script>
