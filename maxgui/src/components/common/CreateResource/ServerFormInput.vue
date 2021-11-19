<template>
    <div class="mb-2">
        <parameters-collapse
            ref="parametersTable"
            :parameters="serverParameters"
            usePortOrSocket
            :parentForm="parentForm"
        />
        <resource-relationships
            ref="servicesRelationship"
            relationshipsType="services"
            :items="servicesList"
            :defaultItems="defaultServiceItems"
        />
        <!-- A server can be only monitored with a monitor, so multiple select options is false-->
        <resource-relationships
            ref="monitorsRelationship"
            relationshipsType="monitors"
            :items="monitorsList"
            :multiple="false"
            clearable
            :defaultItems="defaultMonitorItems"
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
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ParametersCollapse from './ParametersCollapse'
import ResourceRelationships from './ResourceRelationships'

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
        defaultItems: { type: [Array, Object], default: () => [] },
    },
    data() {
        return {
            defaultMonitorItems: [],
            defaultServiceItems: [],
        }
    },
    computed: {
        serverParameters: function() {
            if (this.resourceModules.length) {
                const {
                    attributes: { parameters = [] },
                } = this.$help.lodash.cloneDeep(this.resourceModules[0]) // always 0
                return parameters.filter(item => item.name !== 'type')
            }
            return []
        },
        servicesList: function() {
            return this.allServices.map(({ id, type }) => ({ id, type }))
        },

        monitorsList: function() {
            return this.allMonitors.map(({ id, type }) => ({ id, type }))
        },
        isMonitorDefaultItems: function() {
            return (
                this.$help.isNotEmptyObj(this.defaultItems) && this.defaultItems.type === 'monitors'
            )
        },
    },
    watch: {
        defaultItems: function() {
            if (this.isMonitorDefaultItems) this.defaultMonitorItems = this.defaultItems
            else this.defaultServiceItems = this.defaultItems
        },
    },
    methods: {
        getValues() {
            const { parametersTable, monitorsRelationship, servicesRelationship } = this.$refs
            return {
                parameters: parametersTable.getParameterObj(),
                relationships: {
                    monitors: { data: monitorsRelationship.getSelectedItems() },
                    services: { data: servicesRelationship.getSelectedItems() },
                },
            }
        },
    },
}
</script>
