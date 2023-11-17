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
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import ModuleParameters from '@share/components/common/ObjectForms/ModuleParameters'
import ResourceRelationships from '@share/components/common/ObjectForms/ResourceRelationships'

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
        ...mapState({
            MRDB_MON: state => state.app_config.MRDB_MON,
            MXS_OBJ_TYPES: state => state.app_config.MXS_OBJ_TYPES,
        }),
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
