<template>
    <!-- rendered if usePortOrSocket -->
    <parameter-input
        v-if="handleShowSpecialInputs(item.id)"
        :parentForm="parentForm"
        :item="item"
        :portValue="portValue"
        :socketValue="socketValue"
        :addressValue="addressValue"
        :isListener="isListener"
        :createMode="createMode"
        @on-input-change="handleItemChange"
    />
    <parameter-input
        v-else-if="requiredParams.includes(item.id)"
        :item="item"
        required
        :createMode="createMode"
        @on-input-change="handleItemChange"
    />
    <parameter-input
        v-else
        :item="item"
        :createMode="createMode"
        @on-input-change="handleItemChange"
    />
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
This component render item object to input, it's a container component for parameter-input

PROPS:
- requiredParams: accepts array of string , it simply enables required attribute in parameter-input dynamically
- usePortOrSocket: accepts boolean , if true, get portValue, addressValue, and socketValue, 
  passing them to parameter-input for handling special input field when editting server or listener. 
  If editing listener, addressValue will be null
- isListener: accepts boolean , if true, address won't be required
*/
export default {
    name: 'parameter-input-container',
    props: {
        item: { type: Object, required: true },
        parentForm: { type: Object },
        isListener: { type: Boolean, default: false },
        createMode: { type: Boolean, default: false },
        usePortOrSocket: { type: Boolean, default: false },
        changedParametersArr: { type: Array, required: true },
        requiredParams: { type: Array, default: () => [] },
        assignPortSocketDependencyValues: { type: Function, required: true },
        addressValue: { type: String },
        portValue: { type: Number },
        socketValue: { type: String },
    },

    methods: {
        /**
         * @param {String} id id of parameter
         * @return {Boolean} true if usePortOrSocket is true and id matches requirements
         */
        handleShowSpecialInputs(id) {
            return this.usePortOrSocket && (id === 'port' || id === 'socket' || id === 'address')
        },

        /**
         * @param {Object} newItem Object item received from parameter-input {id:'', value:"", type:""}
         * @param {Boolean} changed Detect whether the input has been modified
         * @return push or re-assign or splice newItem to changedParametersArr which be rendered in showConfirmDialog
         * Also assigining value to component's data: portValue, socketValue, addressValue for
         * validation in parameter-input
         */
        handleItemChange(newItem, changed) {
            let clone = this.$help.lodash.cloneDeep(this.changedParametersArr)

            let targetIndex = clone.findIndex(o => {
                return newItem.nodeId !== undefined
                    ? o.nodeId == newItem.nodeId
                    : o.id === newItem.id
            })

            if (changed) {
                // if item is not in the changedParametersArr list
                if (targetIndex === -1) {
                    clone.push(newItem)
                    this.$emit('handle-change', clone)
                } else {
                    // if item is already in the array,eg: value of enum_mask param has changed
                    clone[targetIndex] = newItem

                    this.$emit('handle-change', clone)
                }
            } else if (targetIndex > -1) {
                clone.splice(targetIndex, 1)
                this.$emit('handle-change', clone)
            }
            this.assignPortSocketDependencyValues(newItem.id, newItem.value)
        },
    },
}
</script>
