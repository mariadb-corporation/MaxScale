<template>
    <div class="mb-2">
        <parameters-collapse
            ref="parametersTable"
            :parameters="getServerParameters"
            usePortOrSocket
            :parentForm="parentForm"
        />
        <resource-relationships
            ref="servicesRelationship"
            relationshipsType="services"
            :items="servicesList"
        />
        <!-- A server can be only monitored with a monitor, so multiple select options is false-->
        <resource-relationships
            ref="monitorsRelationship"
            relationshipsType="monitors"
            :items="monitorsList"
            :multiple="false"
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
import ParametersCollapse from './common/ParametersCollapse'
import ResourceRelationships from './common/ResourceRelationships'

export default {
    name: 'server-form-input',
    components: {
        ParametersCollapse,
        ResourceRelationships,
    },
    props: {
        parentForm: { type: Object, required: true },
        resourceModules: { type: Array, required: true },
        allServices: { type: Array, required: true },
        allMonitors: { type: Array, required: true },
    },

    computed: {
        getServerParameters: function() {
            const self = this
            if (self.resourceModules.length) {
                const {
                    attributes: { parameters = [] },
                } = self.$help.lodash.cloneDeep(self.resourceModules[0]) // always 0

                return parameters.filter(item => item.name !== 'type')
            }
            return []
        },
        servicesList: function() {
            let cloneArr = this.$help.lodash.cloneDeep(this.allServices)
            for (let i = 0; i < cloneArr.length; ++i) {
                let obj = cloneArr[i]
                delete obj.attributes
                delete obj.links
                delete obj.relationships
                delete obj.idNum
            }
            return cloneArr
        },

        monitorsList: function() {
            let cloneArr = this.$help.lodash.cloneDeep(this.allMonitors)
            for (let i = 0; i < cloneArr.length; ++i) {
                let obj = cloneArr[i]
                delete obj.attributes
                delete obj.links
                delete obj.relationships
                delete obj.idNum
            }
            return cloneArr
        },
    },

    methods: {
        getValues() {
            const obj = {
                parameters: this.$refs.parametersTable.getParameterObj(),
                relationships: {},
            }
            const monitors = this.$refs.monitorsRelationship.getSelectedItems()
            const services = this.$refs.servicesRelationship.getSelectedItems()

            if (!this.$help.lodash.isEmpty(monitors)) {
                obj.relationships.monitors = { data: monitors }
            }
            if (!this.$help.lodash.isEmpty(services)) {
                obj.relationships.services = { data: services }
            }

            return obj
        },
    },
}
</script>
