<template>
    <div class="mb-2">
        <module-parameters
            ref="moduleInputs"
            moduleName="protocol"
            :defModuleId="MRDB_PROTOCOL"
            :objType="MXS_OBJ_TYPES.LISTENERS"
            v-bind="moduleParamsProps"
        />
        <!-- A listener may be associated with a single service, so multiple select options is false-->
        <resource-relationships
            ref="servicesRelationship"
            relationshipsType="services"
            :items="serviceList"
            :multiple="false"
            :defaultItems="defaultItems"
            required
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
import ResourceRelationships from '@src/components/ObjectForms/ResourceRelationships'
import ModuleParameters from '@src/components/ObjectForms/ModuleParameters'
import { MXS_OBJ_TYPES } from '@share/constants'
import { MRDB_PROTOCOL } from '@src/constants'

export default {
    name: 'listener-form-input',
    components: {
        ModuleParameters,
        ResourceRelationships,
    },
    props: {
        allServices: { type: Array, required: true },
        defaultItems: { type: [Array, Object], default: () => [] },
        moduleParamsProps: { type: Object, required: true },
    },

    computed: {
        //  several listeners may be associated with the same service, so list all current services
        serviceList() {
            return this.allServices.map(({ id, type }) => ({ id, type }))
        },
    },
    created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
        this.MRDB_PROTOCOL = MRDB_PROTOCOL
    },
    methods: {
        getValues() {
            const { moduleId, parameters } = this.$refs.moduleInputs.getModuleInputValues()
            return {
                parameters: { ...parameters, protocol: moduleId },
                relationships: {
                    services: { data: this.$refs.servicesRelationship.getSelectedItems() },
                },
            }
        },
    },
}
</script>
