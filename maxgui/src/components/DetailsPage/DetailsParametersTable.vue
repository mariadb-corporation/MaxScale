<template>
    <collapsible-ctr
        :title="`${$mxs_tc('parameters', 2)}`"
        @mouseenter.native="mouseHandler"
        @mouseleave.native="mouseHandler"
    >
        <template v-slot:title-append>
            <v-fade-transition>
                <v-btn v-if="showEditBtn || isEditing" icon class="edit-btn" @click="onEdit">
                    <v-icon color="primary" size="18">
                        $vuetify.icons.mxs_edit
                    </v-icon>
                </v-btn>
            </v-fade-transition>
        </template>
        <template v-slot:header-right>
            <v-fade-transition>
                <v-btn
                    v-if="isEditing"
                    color="primary"
                    rounded
                    small
                    class="done-editing-btn text-capitalize"
                    @click="() => (showConfirmDialog = true)"
                >
                    {{ $mxs_t('doneEditing') }}
                </v-btn>
            </v-fade-transition>
        </template>
        <v-form ref="form" v-model="isValid">
            <data-table
                :headers="headers"
                :data="parametersTableRow"
                tdBorderLeft
                showAll
                :editableCell="isEditing"
                :search="search_keyword"
                :loading="isLoading"
                :keepPrimitiveValue="keepPrimitiveValue"
                :isTree="isTree"
                :expandAll="expandAll"
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

                <template v-if="isEditing" v-slot:value="{ data: { item } }">
                    <parameter-input-container
                        :item="item"
                        :validate="$typy($refs, 'form.validate').safeFunction"
                        :changedParametersArr="changedParams"
                        :portValue="portValue"
                        :socketValue="socketValue"
                        :objType="objType"
                        @get-changed-params="changedParams = $event"
                        @handle-change="setPortAndSocketValues"
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
            @after-cancel="cancelEdit"
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
                        <span v-if="item.type !== 'password'"> : {{ item.value }} </span>
                    </p>
                    <p v-else class="d-block mt-2 ">
                        <span class="font-weight-bold">{{ item.id }}</span>
                        <span v-if="item.type !== 'password'"> : {{ item.value }} </span>
                    </p>
                </div>
            </template>
        </mxs-dlg>
    </collapsible-ctr>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
This component allows to read parameters and edit parameters. It means to be used for details page

PROPS:
- overridingModuleParams props allows to override parameters in module_parameters. This props is only used
  in case module_prameters doesn't include type info for nested object. e.g. log_throttling parameter
 */
import { mapState, mapGetters } from 'vuex'
import { OVERLAY_TRANSPARENT_LOADING } from '@share/overlayTypes'
import getParamInfo from '@src/mixins/getParamInfo'
import {
    objToTree,
    treeToObj,
    getSuffixFromValue,
    isServerOrListenerType,
} from '@src/utils/dataTableHelpers'

export default {
    name: 'details-parameters-table',
    mixins: [getParamInfo],
    props: {
        resourceId: { type: String, required: true },
        parameters: { type: Object, required: true },
        moduleParameters: { type: Array, required: true },
        updateResourceParameters: { type: Function, required: false },
        onEditSucceeded: { type: Function, required: false },
        // specical props to manipulate required or dependent input attribute
        isTree: { type: Boolean, default: false },
        expandAll: { type: Boolean, default: false },
        editable: { type: Boolean, default: true },
        objType: { type: String, default: '' },
    },

    data() {
        return {
            showEditBtn: false,
            // parameters
            keepPrimitiveValue: true,
            isValid: false,
            loadingEditableParams: false,
            isEditing: false,
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
            search_keyword: 'search_keyword',
        }),
        ...mapGetters({ isAdmin: 'user/isAdmin' }),
        isLoading() {
            return this.isMounting ? true : this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
        headers() {
            return [
                { text: 'Variable', value: 'id', width: '1px' },
                {
                    text: 'Value',
                    value: 'value',
                    width: 'auto',
                    editableCol: true,
                    autoTruncate: true,
                },
            ]
        },
        treeParams() {
            const {
                lodash: { cloneDeep },
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
            if (this.moduleParameters.length) this.processingTableRow(treeParamsClone)
            return treeParamsClone
        },

        hasChanged() {
            if (this.changedParams.length > 0 && this.isValid) return true
            else return false
        },
    },
    watch: {
        showConfirmDialog(val) {
            if (val && this.isEditing) this.$refs.form.validate()
        },
    },
    async mounted() {
        await this.$helpers.delay(400).then(() => (this.isMounting = false))
    },
    methods: {
        onEdit() {
            this.isEditing = true
        },
        mouseHandler(e) {
            if (this.isAdmin && this.editable) {
                if (e.type === 'mouseenter') this.showEditBtn = true
                else if (e.type === 'mouseleave') this.showEditBtn = false
            }
        },
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
         * in moduleParameters array.
         * @param {Object} resourceParam table object {id:'', value:''}
         */
        assignParamsTypeInfo(resourceParam) {
            const { id: paramId } = resourceParam

            const moduleParam = this.moduleParameters.find(param => param.name === paramId)

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
                                let suffixInfo = getSuffixFromValue(resourceParam, [
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
            if (isServerOrListenerType(this.objType)) this.setPortAndSocketValues(resourceParam)
        },

        /**
         * This function helps to assign value to component's data: portValue, socketValue
         * @param {Object} resourceParam object
         */
        setPortAndSocketValues(resourceParam) {
            const { id, value } = resourceParam
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
            const changedObj = treeToObj({
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
            this.isEditing = false
            this.changedParams = []
            // this helps to assign accurate parameter info and trigger setPortAndSocketValues
            this.processingTableRow(this.parametersTableRow)
        },

        async acceptEdit() {
            const parameters = treeToObj({
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
