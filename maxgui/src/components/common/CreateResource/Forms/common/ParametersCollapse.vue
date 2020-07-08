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
                showAll
                editableCell
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

                <template v-slot:value="{ data: { item } }">
                    <parameter-input-container
                        :item="item"
                        :requiredParams="requiredParams"
                        :parentForm="parentForm"
                        :usePortOrSocket="usePortOrSocket"
                        :changedParametersArr="changedParametersArr"
                        :assignPortSocketDependencyValues="assignPortSocketDependencyValues"
                        :portValue="portValue"
                        :socketValue="socketValue"
                        :addressValue="addressValue"
                        :isListener="isListener"
                        createMode
                        @handle-change="changedParametersArr = $event"
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
 * Change Date: 2024-06-15
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
- requiredParams: accepts array of string , it simply enables required attribute in parameter-input dynamically
- usePortOrSocket: accepts boolean , if true, get portValue, addressValue, and socketValue, 
  passing them to parameter-input for handling special input field when editting server or listener. 
  If editing listener, addressValue will be null
- isListener: accepts boolean , if true, address won't be required
*/
export default {
    name: 'parameters-collapse',
    props: {
        parameters: { type: Array, required: true },
        // specical props to manipulate required or dependent input attribute
        usePortOrSocket: { type: Boolean, default: false },
        requiredParams: { type: Array },
        parentForm: { type: Object },
        isListener: { type: Boolean, default: false },
        isTree: { type: Boolean, default: false },
    },
    data: function() {
        return {
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
            const self = this
            let parameters = self.parameters
            let arr = []
            for (let i = 0; i < parameters.length; ++i) {
                let paramObj = self.$help.lodash.cloneDeep(parameters[i])
                let defaultValue
                switch (paramObj.type) {
                    case 'bool':
                        defaultValue = paramObj.default_value === 'true'
                        break
                    default:
                        /* this ensure 0 default_value could be assigned,
                        undefined default_value property will fallback to null to make the input visibled
                        */
                        defaultValue = this.$help.isUndefined(paramObj.default_value)
                            ? null
                            : paramObj.default_value
                }

                paramObj['value'] = defaultValue
                paramObj['id'] = paramObj.name
                delete paramObj.name
                arr.push(paramObj)
                this.assignPortSocketDependencyValues(paramObj.id, paramObj.value)
            }
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
        /* 
            Function to be called by parent component
        */
        getParameterObj() {
            return this.$help.arrOfObjToObj(this.changedParametersArr)
        },
    },
}
</script>
