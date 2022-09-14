<template>
    <div
        class="d-flex align-center mxs-color-helper border-bottom-table-border"
        :style="{ height: `${height}px` }"
    >
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop mxs-color-helper white text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    class="toolbar-square-btn"
                    text
                    color="accent-dark"
                    :disabled="disableRevert"
                    v-on="on"
                    @click="$emit('on-revert')"
                >
                    <v-icon size="16">
                        $vuetify.icons.mxs_reload
                    </v-icon>
                </v-btn>
            </template>
            <span class="d-inline-block text-center">
                {{ $mxs_t('revertChanges') }}
            </span>
        </v-tooltip>
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop mxs-color-helper white text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    class="toolbar-square-btn"
                    text
                    color="accent-dark"
                    :disabled="disableApply"
                    v-on="on"
                    @click="$emit('on-apply')"
                >
                    <v-icon size="16">
                        $vuetify.icons.mxs_running
                    </v-icon>
                </v-btn>
            </template>
            <span class="d-inline-block text-center">
                {{ $mxs_t('applyChanges') }}
            </span>
        </v-tooltip>
        <slot name="ddl-editor-toolbar-right-slot" />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-06
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
