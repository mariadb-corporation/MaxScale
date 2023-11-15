<template>
    <div>
        <template v-if="!hideModuleOpts">
            <label
                data-test="label"
                class="text-capitalize field__label mxs-color-helper text-small-text d-block"
            >
                {{ $mxs_tc(moduleName, 1) }}
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
                :height="36"
                class="vuetify-input--override v-select--mariadb error--text__bottom"
                :menu-props="{
                    contentClass: 'v-select--menu-mariadb',
                    bottom: true,
                    offsetY: true,
                }"
                :placeholder="$mxs_tc('select', 1, { entityName: $mxs_tc(moduleName, 1) })"
                :rules="[
                    v =>
                        !!v ||
                        $mxs_t('errors.requiredInput', { inputName: $mxs_tc(moduleName, 1) }),
                ]"
            />
        </template>
        <parameters-collapse
            v-if="selectedModule"
            ref="parametersTable"
            :class="{ 'mt-4': !hideModuleOpts }"
            :parameters="moduleParameters"
            :usePortOrSocket="usePortOrSocket"
            :validate="validate"
            :isListener="isListener"
        >
            <template v-if="showAdvanceToggle" v-slot:header-right>
                <v-switch
                    v-model="isAdvanced"
                    :label="$mxs_t('advanced')"
                    class="v-switch--mariadb mt-0 pt-3 mr-2"
                    hide-details
                />
            </template>
        </parameters-collapse>
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

/*
This component takes modules props to render v-select component for selecting a module.
When a module is selected, a parameter inputs table will be rendered.
moduleName props is defined to render correct label for select input
PROPS:
- usePortOrSocket: accepts boolean , if true, get portValue, and socketValue to pass to parameter-input
  for handling special input field when editting server or listener.
- isListener: accepts boolean , if true, address parameter won't be required
*/
import ParametersCollapse from '@share/components/common/ObjectForms/ParametersCollapse'

export default {
    name: 'module-parameters',
    components: {
        ParametersCollapse,
    },
    props: {
        modules: { type: Array, required: true },
        moduleName: { type: String, default: '' },
        hideModuleOpts: { type: Boolean, default: false },
        // usePortOrSocket is used to add a special required constraint
        usePortOrSocket: { type: Boolean, default: false },
        validate: { type: Function, default: () => null },
        isListener: { type: Boolean, default: false },
        defModuleId: { type: String, default: '' },
        showAdvanceToggle: { type: Boolean, default: false },
    },
    data() {
        return {
            // router module input
            selectedModule: null,
            isAdvanced: false,
        }
    },
    computed: {
        /**
         * These params for `servers` and `listeners` are not mandatory from
         * the API perspective but it should be always shown to the users, so
         * that they can either define socket or address and port.
         */
        specialParams() {
            return ['address', 'port', 'socket']
        },
        moduleParameters() {
            if (this.selectedModule) {
                let params = this.$helpers.lodash.cloneDeep(
                    this.$typy(this.selectedModule, 'attributes.parameters').safeArray
                )
                if (this.showAdvanceToggle && !this.isAdvanced) {
                    params = params.filter(
                        param =>
                            param.mandatory ||
                            (this.usePortOrSocket && this.specialParams.includes(param.name))
                    )
                }
                return params
            }
            return []
        },
    },
    watch: {
        defModuleId: {
            immediate: true,
            handler(v) {
                const defModule = this.modules.find(item => item.id === v)
                if (defModule) this.selectedModule = defModule
            },
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
