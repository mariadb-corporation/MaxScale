<template>
    <div
        class="d-flex align-center mxs-color-helper border-bottom-table-border"
        :style="{ height: `${height}px` }"
    >
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn"
            text
            color="primary"
            :disabled="disableRevert"
            @click="$emit('on-revert')"
        >
            <template v-slot:btn-content>
                <v-icon size="16">$vuetify.icons.mxs_reload</v-icon>
            </template>
            {{ $mxs_t('revertChanges') }}
        </mxs-tooltip-btn>
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn"
            text
            color="primary"
            :disabled="disableApply"
            @click="$emit('on-apply')"
        >
            <template v-slot:btn-content>
                <v-icon size="16">$vuetify.icons.mxs_running</v-icon>
            </template>
            {{ $mxs_t('applyChanges') }}
        </mxs-tooltip-btn>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'ddl-editor-toolbar',
    props: {
        disableApply: { type: Boolean, required: true },
        disableRevert: { type: Boolean, required: true },
        height: { type: Number, required: true },
    },
}
</script>
