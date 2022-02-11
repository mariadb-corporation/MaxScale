<template>
    <base-dialog
        v-model="isDlgOpened"
        :onSave="onSave"
        :title="title"
        :saveText="type"
        :minBodyWidth="minBodyWidth"
        :closeImmediate="closeImmediate"
        :hasSavingErr="hasSavingErr"
        :allowEnterToSubmit="allowEnterToSubmit"
        v-on="$listeners"
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

        <template v-slot:actions="{ cancel, save, isSaveDisabled }">
            <slot name="action-prepend"></slot>
            <v-spacer />
            <v-btn
                small
                height="36"
                color="primary"
                class="cancel font-weight-medium px-7 text-capitalize"
                rounded
                outlined
                depressed
                @click="cancel"
            >
                {{ $t('cancel') }}
            </v-btn>
            <v-btn
                small
                height="36"
                color="primary"
                class="save font-weight-medium px-7 text-capitalize"
                rounded
                depressed
                :disabled="isSaveDisabled"
                @click="save"
            >
                {{ $t(type) }}
            </v-btn>
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
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'confirm-dialog',
    props: {
        value: { type: Boolean, required: true },
        type: { type: String, required: true }, //delete, unlink, destroy, stop, start
        title: { type: String, required: true },
        onSave: { type: Function, required: true },
        item: { type: Object, default: null },
        smallInfo: { type: String, default: '' },
        minBodyWidth: { type: String, default: '466px' },
        closeImmediate: { type: Boolean, default: false },
        hasSavingErr: { type: Boolean, default: false },
        allowEnterToSubmit: { type: Boolean, default: true },
    },
    computed: {
        isDlgOpened: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
    },
}
</script>
