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
                    :search="searchKeyword"
                    :loading="loading"
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
 * Change Date: 2024-07-16
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
  If editing listener, address parameter won't be required
 */

export default {
    name: 'details-parameters-collapse',

    props: {
        searchKeyword: { type: String, required: true },
        resourceId: { type: String, required: true },
        parameters: { type: Object, required: true },
        moduleParameters: { type: Array, required: true },
        updateResourceParameters: { type: Function, required: false },
        onEditSucceeded: { type: Function, required: false },
        loading: { type: Boolean, required: true },
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
        }
    },
    computed: {
        parametersTableRow: function() {
            const {
                lodash: { cloneDeep },
                objToArrOfNodes,
            } = this.$help

            const parameters = cloneDeep(this.parameters)
            let tableRow = objToArrOfNodes({
                obj: parameters,
                keepPrimitiveValue: this.keepPrimitiveValue,
                level: 0,
            })

            let moduleParameters = cloneDeep(this.moduleParameters)

            this.processingTableRow(tableRow, moduleParameters)

            return tableRow
        },

        shouldDisableSaveBtn: function() {
            if (this.changedParametersArr.length > 0 && this.isValid) return false
            else return true
        },
    },
    watch: {
        showConfirmDialog: function(val) {
            if (val && this.editableCell) this.$refs.form.validate()
        },
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
         * Return mutated tableRow Array
         * @param {Array} tableRow  mutated processing Table row
         * @param {Array} moduleParameters Module parameters object {id:'', value:'', type:'', unit:'',...}
         */
        processingTableRow(tableRow, moduleParameters) {
            for (let o = 0; o < tableRow.length; ++o) {
                const resourceParam = tableRow[o]
                if (resourceParam.leaf === false) {
                    for (let i = 0; i < resourceParam.children.length; ++i) {
                        const childParam = resourceParam.children[i]
                        this.assignParamsTypeInfo(childParam, moduleParameters)
                    }
                }
                this.assignParamsTypeInfo(resourceParam, moduleParameters)
            }
        },

        /**
         * Return mutated resourceParam Object
         * @param {Object} resourceParam table object {id:'', value:''}
         * @param {Array} moduleParameters Module parameters object {id:'', value:'', type:'', unit:'',...}
         */
        assignParamsTypeInfo(resourceParam, moduleParameters) {
            const { id: resourceParamId } = resourceParam
            const moduleParam = moduleParameters.find(param => param.name === resourceParamId)

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
            } else {
                resourceParam['disabled'] = true
            }

            this.assignPortSocketDependencyValues(resourceParam)
        },

        /**
         * This function helps to assign value to component's data: portValue, socketValue
         * @param {Object} parameter object
         */
        assignPortSocketDependencyValues(parameter) {
            const { id, value } = parameter
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
            this.processingTableRow(
                this.parametersTableRow,
                this.$help.lodash.cloneDeep(this.moduleParameters)
            )
        },

        async acceptEdit() {
            let self = this
            await self.updateResourceParameters({
                id: self.resourceId,
                parameters: self.$help.arrToObject({ arr: self.changedParametersArr }),
                callback: self.onEditSucceeded,
            })
            self.cancelEdit()
        },
    },
}
</script>
