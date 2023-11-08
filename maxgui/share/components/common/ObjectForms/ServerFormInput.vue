<template>
    <div class="mb-2">
        <parameters-collapse
            ref="parametersTable"
            :parameters="serverParameters"
            usePortOrSocket
            :validate="validate"
        />
        <resource-relationships
            v-if="withRelationship"
            ref="servicesRelationship"
            relationshipsType="services"
            :items="servicesList"
            :defaultItems="defaultServiceItems"
        />
        <!-- A server can be only monitored with a monitor, so multiple select options is false-->
        <resource-relationships
            v-if="withRelationship"
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
import ParametersCollapse from '@share/components/common/ObjectForms/ParametersCollapse'
import ResourceRelationships from '@share/components/common/ObjectForms/ResourceRelationships'

export default {
    name: 'server-form-input',
    components: {
        ParametersCollapse,
        ResourceRelationships,
    },
    props: {
        validate: { type: Function, required: true },
        modules: { type: Array, required: true },
        allServices: { type: Array, default: () => [] },
        allMonitors: { type: Array, default: () => [] },
        defaultItems: { type: [Array, Object], default: () => [] },
        withRelationship: { type: Boolean, default: true },
    },
    data() {
        return {
            defaultMonitorItems: [],
            defaultServiceItems: [],
        }
    },
    computed: {
        serverParameters() {
            if (this.modules.length) {
                const {
                    attributes: { parameters = [] },
                } = this.$helpers.lodash.cloneDeep(this.modules[0]) // always 0
                return parameters.filter(item => item.name !== 'type')
            }
            return []
        },
        servicesList() {
            return this.allServices.map(({ id, type }) => ({ id, type }))
        },

        monitorsList() {
            return this.allMonitors.map(({ id, type }) => ({ id, type }))
        },
        isMonitorDefaultItems() {
            return (
                this.$helpers.isNotEmptyObj(this.defaultItems) &&
                this.defaultItems.type === 'monitors'
            )
        },
    },
    watch: {
        defaultItems() {
            if (this.withRelationship) {
                if (this.isMonitorDefaultItems) this.defaultMonitorItems = this.defaultItems
                else this.defaultServiceItems = this.defaultItems
            }
        },
    },
    methods: {
        getValues() {
            const { parametersTable, monitorsRelationship, servicesRelationship } = this.$refs
            let parameters = parametersTable.getParameterObj()
            if (this.withRelationship)
                return {
                    parameters,
                    relationships: {
                        monitors: { data: monitorsRelationship.getSelectedItems() },
                        services: { data: servicesRelationship.getSelectedItems() },
                    },
                }
            return { parameters }
        },
    },
}
</script>
