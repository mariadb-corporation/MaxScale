<template>
    <collapse
        :toggleOnClick="() => (showParameters = !showParameters)"
        :isContentVisible="showParameters"
        :title="`${$tc('parameters', 2)}`"
        :editable="editable"
        :isEditing="editableCell"
        :onEdit="() => (editableCell = true)"
        :doneEditingCb="() => (showConfirmDialog = true)"
    >
        <template v-slot:content>
            <v-form ref="form" v-model="isValid">
                <data-table
                    :headers="variableValueTableHeaders"
                    :data="parametersTableRow"
                    tdBorderLeft
                    showAll
                    :editableCell="editableCell"
                    :search="search_keyword"
                    :loading="isLoading"
                    :keepPrimitiveValue="keepPrimitiveValue"
                    :isTree="isTree"
                    @cell-hover="showCellTooltip"
                >
                    <template v-slot:header-append-id>
                        <span class="ml-1 color text-field-text total-row">
                            ({{ parametersTableRow.length }})
                        </span>
                    </template>

                    <template v-slot:id="{ data: { item } }">
                        <parameter-tooltip-activator :item="item" :componentId="componentId" />
                    </template>

                    <template v-if="editableCell" v-slot:value="{ data: { item } }">
                        <parameter-input-container
                            :item="item"
                            :parentForm="$refs.form"
                            :usePortOrSocket="usePortOrSocket"
                            :changedParametersArr="changedParametersArr"
                            :portValue="portValue"
                            :socketValue="socketValue"
                            @get-changed-params="changedParametersArr = $event"
                            @handle-change="assignPortSocketDependencyValues"
                        />
                    </template>
                </data-table>
            </v-form>
            <parameter-tooltip
                v-if="parameterTooltip.item"
                :parameterTooltip="parameterTooltip"
                :activator="`#param-${parameterTooltip.item.id}_${componentId}`"
            />

            <base-dialog
                v-model="showConfirmDialog"
                :onCancel="cancelEdit"
                :onClose="closeConfirmDialog"
                :onSave="acceptEdit"
                :title="`${$t('implementChanges')}`"
                saveText="thatsRight"
                :isSaveDisabled="shouldDisableSaveBtn"
            >
                <template v-slot:body>
                    <span class="d-block confirmation-text mb-4">
                        {{
                            $tc(
                                'changeTheFollowingParameter',
                                changedParametersArr.length > 1 ? 2 : 1,
                                {
                                    quantity: changedParametersArr.length,
                                }
                            )
                        }}
                    </span>

                    <div
                        v-for="(item, i) in changedParametersArr"
                        :key="i"
                        class="d-block changed-parameter"
                        :class="[
                            item.parentNodeInfo !== null && changedParamsInfo(i, item) && 'mt-2',
                        ]"
                    >
                        <div v-if="item.parentNodeInfo !== null">
                            <div class="font-weight-bold">
                                {{ changedParamsInfo(i, item) }}
                            </div>
                            <p class="d-block mb-1">
                                {{ item.id }}
                                <span v-if="item.type !== 'password string'">
                                    : {{ item.value }}
                                </span>
                            </p>
                        </div>
                        <p v-else class="d-block mt-2 ">
                            <span class="font-weight-bold">{{ item.id }}</span>
                            <span v-if="item.type !== 'password string'"> : {{ item.value }} </span>
                        </p>
                    </div>
                </template>
            </base-dialog>
        </template>
    </collapse>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
This component allows to read parameters and edit parameters. It means to be used for details page

PROPS:
- usePortOrSocket: accepts boolean , if true, get portValue, and socketValue,
  passing them to parameter-input for handling special input field when editting server or listener.
  If editing listener, address parameter won't be required.
- overridingModuleParams props allows to override parameters in module_parameters. This props is only used
  in case module_prameters doesn't include type info for nested object. e.g. log_throttling parameter
 */
import { mapState } from 'vuex'
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
export default {
    name: 'details-parameters-table',
    props: {
        resourceId: { type: String, required: true },
        parameters: { type: Object, required: true },
        overridingModuleParams: { type: Array },
        updateResourceParameters: { type: Function, required: false },
        onEditSucceeded: { type: Function, required: false },
        // specical props to manipulate required or dependent input attribute
        usePortOrSocket: { type: Boolean, default: false },
        isTree: { type: Boolean, default: false },
        editable: { type: Boolean, default: true },
    },

    data() {
        return {
            // parameters
            keepPrimitiveValue: true,
            isValid: false,
            showParameters: true,
            variableValueTableHeaders: [
                { text: 'Variable', value: 'id', width: '55%' },
                {
                    text: 'Value',
                    value: 'value',
                    width: '45%',
                    editableCol: true,
                    cellTruncated: true,
                },
            ],
            loadingEditableParams: false,
            editableCell: false,
            changedParametersArr: [],
            showConfirmDialog: false,

            portValue: null,
            socketValue: null,

            parameterTooltip: {
                item: null,
            },
            // this is needed when using custom activator in v-tooltip.
            componentId: this.$help.lodash.uniqueId('component_tooltip_'),
            isMounting: true,
        }
    },
    computed: {
        ...mapState({
            overlay_type: 'overlay_type',
            module_parameters: 'module_parameters',
            search_keyword: 'search_keyword',
        }),
        isLoading: function() {
            return this.isMounting ? true : this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
        parametersArr: function() {
            const {
                lodash: { cloneDeep },
                objToArrOfNodes,
            } = this.$help

            const parameters = cloneDeep(this.parameters)
            const parametersArr = objToArrOfNodes({
                obj: parameters,
                keepPrimitiveValue: this.keepPrimitiveValue,
                level: 0,
            })

            return parametersArr
        },
        parametersTableRow: function() {
            const {
                lodash: { cloneDeep },
            } = this.$help
            // parametersArr will be mutated by processingTableRow method
            let parametersArr = cloneDeep(this.parametersArr)
            if (this.moduleParams.length) this.processingTableRow(parametersArr)
            return parametersArr
        },

        shouldDisableSaveBtn: function() {
            if (this.changedParametersArr.length > 0 && this.isValid) return false
            else return true
        },
        moduleParams: function() {
            return this.overridingModuleParams
                ? this.overridingModuleParams
                : this.module_parameters
        },
    },
    watch: {
        showConfirmDialog: function(val) {
            if (val && this.editableCell) this.$refs.form.validate()
        },
    },
    async mounted() {
        await this.$help.delay(400).then(() => (this.isMounting = false))
    },
    methods: {
        /**
         * This function assign item info to parameterTooltip which will be read
         * by v-tooltip component to show parameter info
         */
        showCellTooltip({ e, item }) {
            if (e.type === 'mouseenter') {
                const { id, type, description, unit, default_value } = item
                let obj = {
                    id,
                }
                // assign
                if (type !== undefined) obj.type = type
                if (description !== undefined) obj.description = description
                if (unit !== undefined) obj.unit = unit
                if (default_value !== undefined) obj.default_value = default_value

                this.parameterTooltip = {
                    item: obj,
                }
            } else
                this.parameterTooltip = {
                    item: null,
                }
        },

        /**
         * This function mutates tableRows Array
         * @param {Array} tableRows table rows
         */
        processingTableRow(tableRows) {
            // mutated each row
            tableRows.forEach(row => {
                if (row.leaf === false)
                    row.children.forEach(childParam => this.assignParamsTypeInfo(childParam))
                this.assignParamsTypeInfo(row)
            })
        },

        /**
         * This function mutates resource param object if it finds a corresponding param
         * in moduleParams array.
         * @param {Object} resourceParam table object {id:'', value:''}
         */
        assignParamsTypeInfo(resourceParam) {
            const { id: paramId } = resourceParam

            const moduleParam = this.moduleParams.find(param => param.name === paramId)

            if (moduleParam) {
                const {
                    mandatory = false,
                    type,
                    description,
                    default_value,
                    unit,
                    enum_values,
                } = moduleParam

                // assign
                if (type !== undefined) resourceParam.type = type
                if (description !== undefined) resourceParam.description = description
                if (unit !== undefined) resourceParam.unit = unit
                if (default_value !== undefined) resourceParam.default_value = default_value
                resourceParam.mandatory = mandatory

                const hasModifiable = 'modifiable' in moduleParam

                resourceParam['disabled'] = hasModifiable && !moduleParam.modifiable

                switch (resourceParam.type) {
                    case 'duration':
                        if (unit) {
                            let hasAppendedSuffix = false
                            if (typeof resourceParam.value === 'string') {
                                let suffixInfo = this.$help.getSuffixFromValue(resourceParam, [
                                    'ms',
                                    's',
                                    'm',
                                    'h',
                                ])
                                if (suffixInfo.suffix) hasAppendedSuffix = true
                            }
                            if (!hasAppendedSuffix) {
                                resourceParam.value = `${resourceParam.value}${unit}`
                            }
                        }
                        break
                    case 'enum':
                    case 'enum_mask':
                        resourceParam['enum_values'] = enum_values
                        break
                }
            } else resourceParam['disabled'] = true

            this.assignPortSocketDependencyValues(resourceParam)
        },

        /**
         * This function helps to assign value to component's data: portValue, socketValue
         * @param {Object} resourceParam object
         */
        assignPortSocketDependencyValues(resourceParam) {
            const { id, value } = resourceParam
            if (this.usePortOrSocket) {
                switch (id) {
                    case 'port':
                        this.portValue = value
                        break
                    case 'socket':
                        this.socketValue = value
                        break
                }
            }
        },

        // ------------------------- Parameters editing Confirm methods
        changedParamsInfo(i, item) {
            const arr = this.changedParametersArr
            const { parentNodeInfo: { id = null } = null } = item
            if (i > 0) {
                const prevNodeParent = arr[i - 1].parentNodeInfo || null
                const prevNodeParentId = prevNodeParent && prevNodeParent.id
                if (prevNodeParentId !== id) return id
            } else if (i === 0) {
                return id
            } else return ''
        },

        closeConfirmDialog() {
            this.showConfirmDialog = false
        },

        // this simply put everything back to original state
        cancelEdit() {
            this.editableCell = false
            this.closeConfirmDialog()
            this.changedParametersArr = []
            // this helps to assign accurate parameter info and trigger assignPortSocketDependencyValues
            this.processingTableRow(this.parametersTableRow)
        },

        async acceptEdit() {
            await this.updateResourceParameters({
                id: this.resourceId,
                parameters: this.$help.arrToObject({ arr: this.changedParametersArr }),
                callback: this.onEditSucceeded,
            })
            this.cancelEdit()
        },
    },
}
</script>
