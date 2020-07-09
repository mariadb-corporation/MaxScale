<template>
    <div class="mb-2">
        <module-parameters
            ref="moduleInputs"
            moduleName="module"
            :modules="resourceModules"
            :requiredParams="['user', 'password']"
        />
        <resource-relationships
            ref="serversRelationship"
            relationshipsType="servers"
            :items="serversList"
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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ModuleParameters from './common/ModuleParameters'
import ResourceRelationships from './common/ResourceRelationships'

export default {
    name: 'monitor-form-input',
    components: {
        ModuleParameters,
        ResourceRelationships,
    },
    props: {
        resourceModules: { type: Array, required: true },
        allServers: { type: Array, required: true },
    },

    computed: {
        // get only server that are not monitored
        serversList: function() {
            let cloneArr = this.$help.lodash.cloneDeep(this.allServers)
            let result = []
            for (let i = 0; i < cloneArr.length; ++i) {
                let obj = cloneArr[i]
                if (this.$help.isUndefined(obj.relationships.monitors)) {
                    delete obj.attributes
                    delete obj.links
                    delete obj.relationships
                    delete obj.idNum
                    result.push(obj)
                }
            }
            return result
        },
    },

    methods: {
        getValues() {
            const { moduleId, parameters } = this.$refs.moduleInputs.getModuleInputValues()
            return {
                moduleId: moduleId,
                parameters: parameters,
                relationships: {
                    servers: { data: this.$refs.serversRelationship.getSelectedItems() },
                },
            }
        },
    },
}
</script>
