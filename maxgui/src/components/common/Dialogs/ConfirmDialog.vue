<template>
    <base-dialog
        v-model="isDialogOpen"
        :onCancel="onCancelHandler"
        :onSave="onSave"
        :onClose="onCloseHandler"
        :title="title"
        :saveText="type"
        :minBodyWidth="minBodyWidth"
    >
        <template v-slot:form-body>
            <p v-if="!$help.isNull(item)">
                <span class="confirmations-text">
                    {{ $t(`confirmations.${type}`, { targetId: item.id }) }}
                </span>
            </p>
            <slot name="body-prepend"></slot>
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
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'confirm-dialog',
    props: {
        type: { type: String, required: true }, //delete, unlink, destroy, stop, start
        title: { type: String, required: true },
        onSave: { type: Function, required: true },
        onClose: { type: Function },
        onCancel: { type: Function },
        item: { type: Object, default: null },
        smallInfo: { type: String, default: '' },
        minBodyWidth: { type: String, default: '466px' },
    },
    data() {
        return {
            isDialogOpen: false,
        }
    },
    methods: {
        closeDialog() {
            this.isDialogOpen = false
        },
        open() {
            this.isDialogOpen = true
        },
        onCancelHandler() {
            this.onCancel && this.onCancel()
            this.closeDialog()
        },
        onCloseHandler() {
            this.onClose && this.onClose()
            this.closeDialog()
        },
    },
}
</script>
