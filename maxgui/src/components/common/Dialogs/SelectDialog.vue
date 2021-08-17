<template>
    <base-dialog
        v-model="computeShowDialog"
        :onCancel="onCancel"
        :onSave="onSave"
        :onClose="onClose"
        :title="title"
        :saveText="mode"
        :isSaveDisabled="isSaveDisabled"
    >
        <template v-slot:body>
            <p class="select-label">
                {{ $tc('specify', multiple ? 2 : 1) }}

                {{ $tc(entityName, multiple ? 2 : 1) }}
            </p>

            <select-dropdown
                :entityName="entityName"
                :items="itemsList"
                :defaultItems="defaultItems"
                :multiple="multiple"
                :clearable="clearable"
                :showPlaceHolder="false"
                @is-equal="isSaveDisabled = $event"
                @get-selected-items="handleGetSelectedItems"
            />
            <slot name="body-append"></slot>
        </template>
    </base-dialog>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
This component emits two events
@selected-items: value = Array
@on-open: event triggered after dialog is opened
*/
export default {
    name: 'select-dialog',
    props: {
        value: { type: Boolean, required: true },
        mode: { type: String, required: true }, // change or add
        title: { type: String, required: true },
        entityName: { type: String, required: true },
        clearable: { type: Boolean, default: false },
        handleSave: { type: Function, required: true },
        onClose: { type: Function, required: true },
        onCancel: { type: Function, required: true },
        multiple: { type: Boolean, default: false },
        itemsList: { type: Array, required: true },
        defaultItems: { type: [Array, Object], default: () => [] },
    },
    data() {
        return {
            show: false,
            selectedItems: [],
            isSaveDisabled: true,
        }
    },
    computed: {
        computeShowDialog: {
            // get value from props
            get() {
                return this.value
            },
            // set the value to show property in data
            set(value) {
                this.show = value
            },
        },
    },
    watch: {
        value: function(val) {
            if (val) {
                this.$emit('on-open')
            } else {
                this.selectedItems.length && (this.selectedItems = [])
                !this.isSaveDisabled && (this.isSaveDisabled = true)
            }
        },
    },

    methods: {
        handleGetSelectedItems(items) {
            this.selectedItems = items
            this.$emit('selected-items', items)
        },
        async onSave() {
            await this.handleSave()
            this.selectedItems = []
            this.onCancel()
        },
    },
}
</script>
