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
        />

        <parameters-collapse
            v-if="selectedModule"
            ref="parametersTable"
            :parameters="moduleParameters"
            :usePortOrSocket="usePortOrSocket"
            :isTree="isTree"
            :parentForm="parentForm"
            :isListener="isListener"
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
 * Change Date: 2025-08-17
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
- usePortOrSocket: accepts boolean , if true, get portValue, and socketValue to pass to parameter-input
  for handling special input field when editting server or listener.
- isListener: accepts boolean , if true, address parameter won't be required
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
        parentForm: { type: Object },
        isListener: { type: Boolean, default: false },
        isTree: { type: Boolean, default: false },
    },
    data: function() {
        return {
            // router module input
            selectedModule: null,
        }
    },
    computed: {
        moduleParameters: function() {
            if (this.selectedModule) {
                const {
                    attributes: { parameters = [] },
                } = this.$help.lodash.cloneDeep(this.selectedModule)
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
<style lang="scss" scoped>
.label {
    font-size: 0.625rem;
}
</style>
