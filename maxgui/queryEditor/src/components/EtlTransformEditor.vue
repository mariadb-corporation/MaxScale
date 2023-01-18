<template>
    <div class="d-flex flex-column transform-editor">
        <label class="field__label mxs-color-helper text-small-text label-required">
            {{ label }}
        </label>
        <sql-editor
            v-model="sql"
            :class="`sql-editor ${editorClass} rounded pa-2`"
            :style="{ height: editorHeight }"
            :options="{
                contextmenu: false,
                wordWrap: 'on',
            }"
            :skipRegCompleters="skipRegEditorCompleters"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import SqlEditor from '@queryEditorSrc/components/SqlEditor'
export default {
    name: 'etl-transform-editor',
    components: {
        'sql-editor': SqlEditor,
    },
    props: {
        value: { type: String, required: true },
        label: { type: String, required: true },
        skipRegEditorCompleters: { type: Boolean, default: false },
        editorClass: { type: String, default: '' },
        editorHeight: { type: String, default: '60px' },
    },
    computed: {
        sql: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
    },
}
</script>
<style lang="scss" scoped>
.field__label {
    font-size: 0.875rem !important;
}
.transform-editor {
    margin-bottom: 16px;
    .sql-editor {
        border: thin solid #e8eef1;
    }
}
</style>
