<template>
    <collapse
        wrapperClass="mt-4 d-inline-flex flex-column"
        titleWrapperClass="mx-n9"
        :toggleOnClick="() => (showParameters = !showParameters)"
        :isContentVisible="showParameters"
        :title="`${$tc('parameters', 2)}`"
    >
        <template v-slot:content>
            <data-table
                :headers="variableValueTableHeaders"
                :data="parametersTableRow"
                :showAll="showAll"
                :editableCell="editableCell"
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

                <template v-slot:value="{ data: { item } }">
                    <parameter-input-container
                        :item="item"
                        :parentForm="parentForm"
                        :usePortOrSocket="usePortOrSocket"
                        :changedParametersArr="changedParametersArr"
                        :portValue="portValue"
                        :socketValue="socketValue"
                        :isListener="isListener"
                        @get-changed-params="changedParametersArr = $event"
                        @handle-change="assignPortSocketDependencyValues"
                    />
                </template>
            </data-table>
            <parameter-tooltip
                v-if="parameterTooltip.item"
                :parameterTooltip="parameterTooltip"
                :activator="`#param-${parameterTooltip.item.id}_${componentId}`"
            />
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
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
This component allows to edit parameters taken from parameters array that must have similar format to
module parameters. All default_values will be returned as string regardless of type
The component is meant to be used for creating resource

PROPS:
- usePortOrSocket: accepts boolean , if true, get portValue, and socketValue,
  passing them to parameter-input for handling special input field when editting server or listener.
- isListener: accepts boolean , if true, address parameter will not be required
*/
export default {
    name: 'parameters-collapse',
    props: {
        parameters: { type: Array, required: true },
        // specical props to manipulate required or dependent input attribute
        usePortOrSocket: { type: Boolean, default: false }, // needed for server, listener
        parentForm: { type: Object }, // needed for server, listener
        isListener: { type: Boolean, default: false },
        isTree: { type: Boolean, default: false },
    },
    data: function() {
        return {
            // for data-table
            showAll: true,
            editableCell: true,
            keepPrimitiveValue: true,
            // nested form
            isValid: false,
            // Parameters table section
            showParameters: true,
            variableValueTableHeaders: [
                { text: 'Variable', value: 'id', width: '1px' },
                { text: 'Value', value: 'value', width: '1px', editableCol: true },
            ],
            // parameters input
            changedParametersArr: [],
            //
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
            const parameters = this.parameters
            let arr = []
            parameters.forEach(param => {
                let paramObj = this.$help.lodash.cloneDeep(param)
                /* this ensure 0 default_value could be assigned,
                   undefined default_value property will fallback to null to make the input visibled
                 */
                const defaultValue = this.$help.isUndefined(paramObj.default_value)
                    ? null
                    : paramObj.default_value

                paramObj['value'] = defaultValue
                paramObj['id'] = paramObj.name
                delete paramObj.name
                arr.push(paramObj)
                this.assignPortSocketDependencyValues(paramObj)
            })
            return arr
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
         * This function helps to assign value to component's data: portValue, socketValue
         * @param {Object} parameter object
         */
        assignPortSocketDependencyValues(parameter) {
            if (this.usePortOrSocket) {
                const { id, value } = parameter
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

        /*
            Function to be called by parent component
        */
        getParameterObj() {
            let resultObj = {}
            this.changedParametersArr.forEach(obj => {
                resultObj[obj.id] = obj.value
            })
            return resultObj
        },
    },
}
</script>
