<template>
    <div>
        <label class="text-capitalize label color text-small-text d-block">
            {{ $tc(moduleName, 1) }}
        </label>
        <v-select
            id="module-select"
            v-model="selectedModule"
            :items="modules"
            item-text="id"
            return-object
            name="resource"
            outlined
            dense
            class="std mariadb-select-input error--text__bottom"
            :menu-props="{ contentClass: 'mariadb-select-v-menu', bottom: true, offsetY: true }"
            :placeholder="$tc('select', 1, { entityName: $tc(moduleName, 1) })"
            :rules="[v => !!v || $t('errors.requiredInput', { inputName: $tc(moduleName, 1) })]"
            required
        />

        <parameters-collapse
            v-if="selectedModule"
            :key="selectedModule.id"
            ref="parametersTable"
            :parameters="getModuleParameters"
            :requiredParams="requiredParams"
            :usePortOrSocket="usePortOrSocket"
            :isTree="isTree"
            :parentForm="parentForm"
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

/*
This component takes modules props to render v-select component for selecting a module.
When a module is selelcted, a parameters input table will be rendered.
moduleName props is defined to render correct label for select input 
PROPS:
- requiredParams: accepts array of string , it simply enables required attribute in parameter-input dynamically
- usePortOrSocket: accepts boolean , if true, get portValue, addressValue, and socketValue to pass to parameter-input 
  for handling special input field when editting server or listener. If editing listener, 
- isListener: accepts boolean , if true, address won't be required
*/
import ParametersCollapse from './ParametersCollapse'

export default {
    name: 'module-parameters',
    components: {
        ParametersCollapse,
    },
    props: {
        moduleName: { type: String, required: true },
        modules: { type: Array, required: true },
        // specical props to manipulate required or dependent input attribute
        usePortOrSocket: { type: Boolean, default: false },
        requiredParams: { type: Array, default: () => [] },
        parentForm: { type: Object },
        isListener: { type: Boolean, default: false },
        isTree: { type: Boolean, default: false },
    },
    data: function() {
        return {
            // router module input
            selectedModule: undefined,
        }
    },
    computed: {
        getModuleParameters: function() {
            const self = this
            if (self.selectedModule) {
                const {
                    attributes: { parameters = [] },
                } = self.$help.lodash.cloneDeep(self.selectedModule)
                return parameters
            }
            return []
        },
    },

    methods: {
        getModuleInputValues() {
            /*
            When using module parameters, only parameters that have changed by the user
            will be sent in the post request, omitted parameters will be assigned default_value by MaxScale
            */
            const moduleInputs = {
                moduleId: this.selectedModule.id,
                parameters: this.$refs.parametersTable.getParameterObj(),
            }
            return moduleInputs
        },
    },
}
</script>
