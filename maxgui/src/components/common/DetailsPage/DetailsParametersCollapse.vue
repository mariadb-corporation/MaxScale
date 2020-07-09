<template>
    <collapse
        :toggleOnClick="() => (showParameters = !showParameters)"
        :isContentVisible="showParameters"
        :title="`${$tc('parameters', 2)}`"
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
                    :search="searchKeyWord"
                    :loading="loading"
                    keepPrimitiveValue
                    :isTree="isTree"
                    @cell-hover="showCellTooltip"
                >
                    <template v-slot:header-append-id>
                        <span class="ml-1 color text-field-text">
                            ({{ parametersTableRow.length }})
                        </span>
                    </template>

                    <template v-slot:id="{ data: { item } }">
                        <parameter-tooltip-activator :item="item" :componentId="componentId" />
                    </template>

                    <template v-if="editableCell" v-slot:value="{ data: { item } }">
                        <parameter-input-container
                            :item="item"
                            :requiredParams="requiredParams"
                            :parentForm="$refs.form"
                            :usePortOrSocket="usePortOrSocket"
                            :changedParametersArr="changedParametersArr"
                            :assignPortSocketDependencyValues="assignPortSocketDependencyValues"
                            :portValue="portValue"
                            :socketValue="socketValue"
                            :addressValue="addressValue"
                            @handle-change="changedParametersArr = $event"
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
                    <span class="d-block mb-4">
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
                        class="d-block"
                        :class="[
                            item.parentNodeInfo !== null && changedParamsInfo(i, item) && 'mt-2',
                        ]"
                    >
                        <div v-if="item.parentNodeInfo !== null">
                            <div class="font-weight-bold">
                                {{ changedParamsInfo(i, item) }}
                            </div>
                            <p class="d-block mb-1">{{ item.id }}: {{ item.value }}</p>
                        </div>
                        <p v-else class="d-block mt-2 ">
                            <span class="font-weight-bold">{{ item.id }}:</span
                            ><span> {{ item.value }}</span>
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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
This component allows to read parameters and edit parameters. It means to be used for details page

PROPS:
- requiredParams: accepts array of string , it simply enables required attribute in parameter-input dynamically
- usePortOrSocket: accepts boolean , if true, get portValue, addressValue, and socketValue, 
  passing them to parameter-input for handling special input field when editting server or listener. 
  If editing listener, addressValue will be null
- isListener: accepts boolean , if true, address won't be required
 */

export default {
    name: 'details-parameters-collapse',

    props: {
        searchKeyWord: { type: String, required: true },
        resourceId: { type: String, required: true },
        parameters: { type: Object, required: true },
        moduleParameters: { type: Array, required: true },
        updateResourceParameters: { type: Function, required: true },
        onEditSucceeded: { type: Function, required: true },
        loading: { type: Boolean, required: true },
        // specical props to manipulate required or dependent input attribute
        usePortOrSocket: { type: Boolean, default: false },
        requiredParams: { type: Array },
        isTree: { type: Boolean, default: false },
    },

    data() {
        return {
            // parameters
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

            addressValue: null,
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
            const parameters = this.$help.lodash.cloneDeep(this.parameters)
            const keepPrimitiveValue = true
            let level = 0
            let tableRow = this.$help.objToArrOfObj(parameters, keepPrimitiveValue, level)

            let moduleParameters = this.$help.lodash.cloneDeep(this.moduleParameters)

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
                const obj = {
                    id,
                }

                !this.$help.isUndefined(type) && (obj.type = type)
                !this.$help.isUndefined(description) && (obj.description = description)
                !this.$help.isUndefined(unit) && (obj.unit = unit)
                !this.$help.isUndefined(default_value) && (obj.default_value = default_value)

                this.parameterTooltip = {
                    item: obj,
                }
            } else
                this.parameterTooltip = {
                    item: null,
                }
        },

        /**
         * @param {Object} resourceParam table object {id:'', value:''}
         * @param {Array} moduleParameters Module parameters object {id:'', value:'', type:'', unit:'',...}
         * @return {Object} mutated resourceParam
         */
        assignParamsTypeInfo(resourceParam, moduleParameters) {
            const { id: resourceParamId, value: resourceParamValue } = resourceParam
            const moduleParam = moduleParameters.find(param => param.name === resourceParamId)

            if (moduleParam) {
                const { type, description, default_value, unit, enum_values } = moduleParam

                // assign
                type !== undefined && (resourceParam.type = type)
                description !== undefined && (resourceParam.description = description)
                unit !== undefined && (resourceParam.unit = unit)
                default_value !== undefined && (resourceParam.default_value = default_value)

                const hasModifiable = 'modifiable' in moduleParam
                if (hasModifiable && !moduleParam.modifiable) {
                    resourceParam['disabled'] = true
                } else if (!hasModifiable) resourceParam['disabled'] = false

                if (resourceParam.type === 'duration' && unit) {
                    resourceParam.value = `${resourceParam.value}${unit}`
                } else if (resourceParam.type === 'enum' || resourceParam.type === 'enum_mask') {
                    resourceParam['enum_values'] = enum_values
                }
            } else {
                resourceParam['disabled'] = true
            }

            this.assignPortSocketDependencyValues(resourceParamId, resourceParamValue)
        },

        /**
         * @param {String} resourceParamId Name of the parameter
         * @param {String} resourceParamValue Value of the parameter
         * @return assigining value to component's data: portValue, socketValue, addressValue
         */
        assignPortSocketDependencyValues(resourceParamId, resourceParamValue) {
            if (this.usePortOrSocket) {
                switch (resourceParamId) {
                    case 'port':
                        this.portValue = resourceParamValue
                        break
                    case 'socket':
                        this.socketValue = resourceParamValue
                        break
                    case 'address':
                        this.addressValue = resourceParamValue
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
        },

        async acceptEdit() {
            let self = this
            await self.updateResourceParameters({
                id: self.resourceId,
                parameters: self.$help.arrOfObjToObj(self.changedParametersArr),
                callback: self.onEditSucceeded,
            })
            self.cancelEdit()
        },
    },
}
</script>
