<template>
    <div class="mb-2">
        <module-parameters
            ref="moduleInputs"
            :parentForm="parentForm"
            :isListener="true"
            moduleName="protocol"
            :modules="resourceModules"
            usePortOrSocket
        />
        <!-- A listener may be associated with a single service, so multiple select options is false-->
        <resource-relationships
            ref="servicesRelationship"
            relationshipsType="services"
            :items="serviceList"
            :multiple="false"
            required
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
import ResourceRelationships from './common/ResourceRelationships'
import ModuleParameters from './common/ModuleParameters'

export default {
    name: 'listener-form-input',
    components: {
        ModuleParameters,
        ResourceRelationships,
    },
    props: {
        resourceModules: { type: Array, required: true },
        allServices: { type: Array, required: true },
        parentForm: { type: Object, required: true },
    },
    data() {
        return {
            parameters: [],
        }
    },

    computed: {
        //  several listeners may be associated with the same service, so list all current services
        serviceList: function() {
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
    },

    methods: {
        getValues() {
            const { parameters } = this.$refs.moduleInputs.getModuleInputValues()

            return {
                parameters: parameters,
                relationships: {
                    services: { data: this.$refs.servicesRelationship.getSelectedItems() },
                },
            }
        },
    },
}
</script>
