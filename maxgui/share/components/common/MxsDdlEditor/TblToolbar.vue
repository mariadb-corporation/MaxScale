<template>
    <div
        ref="container"
        class="pb-2 d-flex align-center flex-1"
        :class="{ 'flex-row-reverse': reverse }"
    >
        <v-spacer />
        <mxs-tooltip-btn
            v-if="selectedItems.length"
            btnClass="delete-btn ml-2 pa-1 text-capitalize"
            x-small
            outlined
            depressed
            color="error"
            @click="$emit('on-delete-selected-items', selectedItems)"
        >
            <template v-slot:btn-content>
                {{ $mxs_t('drop') }}
                <template v-if="selectedItems.length > 1"> ({{ selectedItems.length }}) </template>
            </template>
            {{ $mxs_t('dropSelected') }}
        </mxs-tooltip-btn>
        <v-btn
            class="add-btn ml-2 pa-1 text-capitalize"
            x-small
            outlined
            depressed
            color="primary"
            @click="$emit('on-add')"
        >
            {{ $mxs_t('add') }}
        </v-btn>
        <mxs-tooltip-btn
            v-if="showRotateTable"
            btnClass="rotate-btn ml-2 pa-1"
            x-small
            outlined
            depressed
            color="primary"
            @click="isVertTableMode = !isVertTableMode"
        >
            <template v-slot:btn-content>
                <v-icon size="14" :class="{ 'rotate-left': !isVertTableMode }">
                    mdi-format-rotate-90
                </v-icon>
            </template>
            {{ $mxs_t(isVertTableMode ? 'switchToHorizTable' : 'switchToVertTable') }}
        </mxs-tooltip-btn>
        <slot name="append" />
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'tbl-toolbar',
    props: {
        selectedItems: { type: Array, required: true },
        isVertTable: { type: Boolean, default: true }, //sync
        showRotateTable: { type: Boolean, default: true },
        reverse: { type: Boolean, default: false },
    },
    computed: {
        isVertTableMode: {
            get() {
                return this.isVertTable
            },
            set(v) {
                this.$emit('update:isVertTable', v)
            },
        },
    },
    mounted() {
        this.$emit('get-computed-height', this.$refs.container.clientHeight)
    },
}
</script>
