<template>
    <div class="mb-2">
        <module-parameters
            ref="moduleInputs"
            :defModuleId="MXS_OBJ_TYPES.SERVERS"
            hideModuleOpts
            :objType="MXS_OBJ_TYPES.SERVERS"
            v-bind="moduleParamsProps"
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
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ModuleParameters from '@share/components/common/ObjectForms/ModuleParameters'
import ResourceRelationships from '@share/components/common/ObjectForms/ResourceRelationships'
import { MXS_OBJ_TYPES } from '@share/constants'

export default {
    name: 'server-form-input',
    components: {
        ModuleParameters,
        ResourceRelationships,
    },
    props: {
        allServices: { type: Array, default: () => [] },
        allMonitors: { type: Array, default: () => [] },
        defaultItems: { type: [Array, Object], default: () => [] },
        withRelationship: { type: Boolean, default: true },
        moduleParamsProps: { type: Object, required: true },
    },
    data() {
        return {
            defaultMonitorItems: [],
            defaultServiceItems: [],
        }
    },
    computed: {
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
    created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
    },
    methods: {
        getValues() {
            const { moduleInputs, monitorsRelationship, servicesRelationship } = this.$refs
            const { parameters } = moduleInputs.getModuleInputValues()
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
