<template>
    <div class="mb-2">
        <module-parameters
            ref="moduleInputs"
            :isListener="true"
            moduleName="protocol"
            usePortOrSocket
            :defModuleId="MRDB_PROTOCOL"
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
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import ResourceRelationships from '@share/components/common/ObjectForms/ResourceRelationships'
import ModuleParameters from '@share/components/common/ObjectForms/ModuleParameters'

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
        ...mapState({ MRDB_PROTOCOL: state => state.app_config.MRDB_PROTOCOL }),
        //  several listeners may be associated with the same service, so list all current services
        serviceList() {
            return this.allServices.map(({ id, type }) => ({ id, type }))
        },
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
