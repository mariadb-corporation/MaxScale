<template>
    <div class="mb-2">
        <module-parameters
            ref="moduleInputs"
            moduleName="module"
            :defModuleId="MRDB_MON"
            :objType="MXS_OBJ_TYPES.MONITORS"
            v-bind="moduleParamsProps"
        />
        <resource-relationships
            ref="serversRelationship"
            relationshipsType="servers"
            :items="serversList"
            :defaultItems="defServers"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ModuleParameters from '@src/components/ObjectForms/ModuleParameters'
import ResourceRelationships from '@src/components/ObjectForms/ResourceRelationships'
import { MXS_OBJ_TYPES } from '@share/constants'
import { MRDB_MON } from '@src/constants'

export default {
    name: 'monitor-form-input',
    components: {
        ModuleParameters,
        ResourceRelationships,
    },
    props: {
        allServers: { type: Array, required: true },
        defaultItems: { type: Array, default: () => [] },
        moduleParamsProps: { type: Object, required: true },
    },
    computed: {
        // get only server that are not monitored
        serversList() {
            let serverItems = []
            this.allServers.forEach(({ id, type, relationships: { monitors = null } = {} }) => {
                if (!monitors) serverItems.push({ id, type })
            })
            return serverItems
        },
        defServers() {
            return this.serversList.filter(item =>
                this.defaultItems.some(defItem => defItem.id === item.id)
            )
        },
    },
    created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
        this.MRDB_MON = MRDB_MON
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
