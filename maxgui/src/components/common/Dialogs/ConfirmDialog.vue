<template>
    <base-dialog
        v-model="computeShowDialog"
        :onCancel="onCancel"
        :onSave="handleSave"
        :onClose="onClose"
        :title="title"
        :saveText="type"
    >
        <template v-slot:body>
            <p v-if="!$help.isNull(item)">
                <span>
                    {{ $t(`confirmations.${type}`, { targetId: item.id }) }}
                </span>
            </p>
            <small>
                {{ smallInfo }}
            </small>
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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'confirm-dialog',
    props: {
        value: Boolean,
        type: String, // delete or destroy
        title: String,
        item: Object,
        onSave: { type: Function, required: true },
        smallInfo: String,
        onClose: { type: Function, required: true },
        onCancel: { type: Function, required: true },
    },
    data() {
        return {
            show: false,
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
    methods: {
        async handleSave() {
            await this.onSave()
            this.onCancel()
        },
    },
}
</script>
