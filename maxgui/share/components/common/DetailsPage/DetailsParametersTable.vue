<template>
    <mxs-collapse
        :toggleOnClick="() => (showParameters = !showParameters)"
        :isContentVisible="showParameters"
        :title="`${$mxs_tc('parameters', 2)}`"
        :editable="isAdmin && isTableEditable"
        :isEditing="editableCell"
        :onEdit="() => (editableCell = true)"
        :doneEditingCb="() => (showConfirmDialog = true)"
    >
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
                :expandAll="expandAll"
                :pauseCompute="editableCell"
                @cell-hover="onCellHover"
            >
                <template v-slot:header-append-id>
                    <span class="ml-1 mxs-color-helper text-grayed-out total-row">
                        ({{ parametersTableRow.length }})
                    </span>
                </template>

                <template v-slot:id="{ data: { item } }">
                    <parameter-tooltip-activator
                        v-mxs-highlighter="{ keyword: search_keyword, txt: item.id }"
                        :isTree="isTree"
                        :item="item"
                        :componentId="componentId"
                    />
                </template>

                <template v-if="editableCell" v-slot:value="{ data: { item } }">
                    <parameter-input-container
                        :item="item"
                        :parentForm="$refs.form"
                        :usePortOrSocket="usePortOrSocket"
                        :changedParametersArr="changedParams"
                        :portValue="portValue"
                        :socketValue="socketValue"
                        @get-changed-params="changedParams = $event"
                        @handle-change="assignPortSocketDependencyValues"
                    />
                </template>
            </data-table>
        </v-form>
        <parameter-tooltip
            v-if="parameterTooltip"
            :parameterTooltip="parameterTooltip"
            :activator="`#param-${parameterTooltip.id}_${componentId}`"
        />

        <mxs-dlg
            v-model="showConfirmDialog"
            :onSave="acceptEdit"
            :title="`${$mxs_t('implementChanges')}`"
            saveText="confirm"
            :hasChanged="hasChanged"
            @on-cancel="cancelEdit"
        >
            <template v-slot:form-body>
                <span class="d-block confirmation-text mb-4">
                    {{
                        $mxs_tc('changeTheFollowingParameter', changedParams.length > 1 ? 2 : 1, {
                            quantity: changedParams.length,
                        })
                    }}
                </span>

                <div
                    v-for="(item, i) in changedParams"
                    :key="i"
                    class="d-block changed-parameter"
                    :class="{ 'mt-2': item.parentNodeId }"
                >
                    <p v-if="item.parentNodeId" class="d-block mt-2 ">
                        <span class="font-weight-bold"> {{ keyifyChangedParams(item) }}</span>
                        <span v-if="item.type !== 'password string'"> : {{ item.value }} </span>
                    </p>
                    <p v-else class="d-block mt-2 ">
                        <span class="font-weight-bold">{{ item.id }}</span>
                        <span v-if="item.type !== 'password string'"> : {{ item.value }} </span>
                    </p>
                </div>
            </template>
        </mxs-dlg>
    </mxs-collapse>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
This component allows to read parameters and edit parameters. It means to be used for details page

PROPS:
- usePortOrSocket: accepts boolean , if true, get portValue, and socketValue,
  passing them to parameter-input for handling special input field when editing server or listener.
  If editing listener, address parameter won't be required.
- overridingModuleParams props allows to override parameters in module_parameters. This props is only used
  in case module_parameters doesn't include type info for nested object. e.g. log_throttling parameter
 */
import { mapState, mapGetters } from 'vuex'
import { OVERLAY_TRANSPARENT_LOADING } from '@share/overlayTypes'
import getParamInfo from '@share/mixins/getParamInfo'
export default {
    name: 'details-parameters-table',
    mixins: [getParamInfo],
    props: {
        resourceId: { type: String, required: true },
        parameters: { type: Object, required: true },
        overridingModuleParams: { type: Array },
        updateResourceParameters: { type: Function, required: false },
        onEditSucceeded: { type: Function, required: false },
        // specical props to manipulate required or dependent input attribute
        usePortOrSocket: { type: Boolean, default: false },
        isTree: { type: Boolean, default: false },
        expandAll: { type: Boolean, default: false },
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
                    autoTruncate: true,
                },
            ],
            loadingEditableParams: false,
            editableCell: false,
            changedParams: [],
            showConfirmDialog: false,

            portValue: null,
            socketValue: null,

            parameterTooltip: null,
            // this is needed when using custom activator in v-tooltip.
            componentId: this.$helpers.lodash.uniqueId('component_tooltip_'),
            isMounting: true,
        }
    },
    computed: {
        ...mapState({
            overlay_type: state => state.mxsApp.overlay_type,
            module_parameters: 'module_parameters',
            search_keyword: 'search_keyword',
        }),
        ...mapGetters({ isAdmin: 'user/isAdmin' }),
        isLoading() {
            return this.isMounting ? true : this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
        treeParams() {
            const {
                lodash: { cloneDeep },
                objToTree,
            } = this.$helpers
            const parameters = cloneDeep(this.parameters)
            return objToTree({
                obj: parameters,
                keepPrimitiveValue: this.keepPrimitiveValue,
                level: 0,
            })
        },

        parametersTableRow() {
            const {
                lodash: { cloneDeep },
            } = this.$helpers
            // treeParamsClone will be mutated by processingTableRow method
            let treeParamsClone = cloneDeep(this.treeParams)
            if (this.moduleParams.length) this.processingTableRow(treeParamsClone)
            return treeParamsClone
        },

        hasChanged() {
            if (this.changedParams.length > 0 && this.isValid) return true
            else return false
        },
        moduleParams() {
            return this.overridingModuleParams
                ? this.overridingModuleParams
                : this.module_parameters
        },
        isTableEditable() {
            return this.editable
        },
    },
    watch: {
        showConfirmDialog(val) {
            if (val && this.editableCell) this.$refs.form.validate()
        },
    },
    async mounted() {
        await this.$helpers.delay(400).then(() => (this.isMounting = false))
    },
    methods: {
        /**
         *  This function assign item info to parameterTooltip which will be read by <parameter-tooltip/>
         * @param {Object} param.e - mouseEvent
         * @param {Object} param.item - param object
         * @returns {Object} tooltip object
         */
        onCellHover({ e, item }) {
            if (e.type === 'mouseenter') this.parameterTooltip = this.getParamInfo(item)
            else this.parameterTooltip = null
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
                                let suffixInfo = this.$helpers.getSuffixFromValue(resourceParam, [
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
            if (this.usePortOrSocket)
                if (id === 'port' || id === 'socket') this[`${id}Value`] = value
        },

        /**
         * If a node changes its value, its ancestor needs to be included as well
         * this gets it ancestor obj then calls keyify to
         * get key name. e.g. rootProp.childProp.grandChildProp
         * @param {Object} node - parameter node
         * @return {String} the key name of its ancestor. e.g. rootProp.childProp.grandChildProp
         */
        keyifyChangedParams(node) {
            const changedObj = this.$helpers.treeToObj({
                changedNodes: [node],
                tree: this.treeParams,
            })
            const allKeys = this.keyify(changedObj)
            let result = ''
            for (const key of allKeys) {
                if (key.includes(node.id)) {
                    result = key
                    break
                }
            }
            return result
        },

        /**
         *
         * @param {Object} obj - nested object
         * @param {String} prefix - prefix to add to each properties
         * @returns {String} e.g. rootProp.childProp.grandChildProp
         */
        keyify(obj, prefix = '') {
            return Object.keys(obj).reduce((res, el) => {
                if (typeof obj[el] === 'object' && obj[el] !== null) {
                    return [...res, ...this.keyify(obj[el], prefix + el + '.')]
                }
                return [...res, prefix + el]
            }, [])
        },

        // this simply put everything back to original state
        cancelEdit() {
            this.editableCell = false
            this.changedParams = []
            // this helps to assign accurate parameter info and trigger assignPortSocketDependencyValues
            this.processingTableRow(this.parametersTableRow)
        },

        async acceptEdit() {
            const parameters = this.$helpers.treeToObj({
                changedNodes: this.changedParams,
                tree: this.treeParams,
            })
            await this.updateResourceParameters({
                id: this.resourceId,
                parameters,
                callback: this.onEditSucceeded,
            })
            this.cancelEdit()
        },
    },
}
</script>
