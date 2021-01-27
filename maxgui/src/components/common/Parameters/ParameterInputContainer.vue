<template>
    <parameter-input
        v-if="handleShowSpecialInputs"
        :parentForm="parentForm"
        :item="item"
        :portValue="portValue"
        :socketValue="socketValue"
        :isListener="isListener"
        @on-input-change="handleItemChange"
    />
    <parameter-input
        v-else
        :item="item"
        :isListener="isListener"
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
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
This component render item object to input, it's a container component for parameter-input
PROPS explanation:
- usePortOrSocket: if true, passing the value of portValue, socketValue props,
  to parameter-input for handling special input field when editting server or listener.
- portValue, socketValue and parentForm are passed if a server or listener is being
  created or updated, this helps to facilitate special rules for port, socket and address parameter
- isListener: if true, address input won't be required
- changedParametersArr: accepts array, it contains changed parameter objects which will be updated by parent component
  when get-changed-params event is emitted

Emits:
- $emit('get-changed-params', changedParams: Array)
- $emit('handle-change', newItem: Object)
*/
export default {
    name: 'parameter-input-container',
    props: {
        item: { type: Object, required: true },
        parentForm: { type: Object },
        isListener: { type: Boolean, default: false },
        usePortOrSocket: { type: Boolean, default: false },
        changedParametersArr: { type: Array, required: true },
        portValue: { type: Number },
        socketValue: { type: String },
    },
    computed: {
        /**
         * @return {Boolean} true if usePortOrSocket is true and id matches requirements
         */
        handleShowSpecialInputs: function() {
            let params = ['port', 'socket', 'address']
            return this.usePortOrSocket && params.includes(this.item.id)
        },
    },
    methods: {
        /**This functions emits get-changed-params with new value for changedParametersArr.
         * If changed is true, push, re-assign or splice to newItem then passing it in get-changed-params event
         * Also emits handle-change with newItem
         * @param {Object} newItem Object item received from parameter-input
         * @param {Boolean} changed Detect whether the input has been modified
         */
        handleItemChange(newItem, changed) {
            let changedParams = this.$help.lodash.cloneDeep(this.changedParametersArr)

            let targetIndex = changedParams.findIndex(o => {
                return newItem.nodeId !== undefined
                    ? o.nodeId == newItem.nodeId
                    : o.id === newItem.id
            })

            if (changed) {
                // if newItem is not included in changedParametersArr
                if (targetIndex === -1) {
                    changedParams.push(newItem)
                    this.$emit('get-changed-params', changedParams)
                } else {
                    // if newItem is already included in changedParametersArr,eg: value of enum_mask param has changed
                    changedParams[targetIndex] = newItem

                    this.$emit('get-changed-params', changedParams)
                }
            } else if (targetIndex > -1) {
                // remove item from changedParams at targetIndex
                changedParams.splice(targetIndex, 1)
                this.$emit('get-changed-params', changedParams)
            }
            this.$emit('handle-change', newItem)
        },
    },
}
</script>
