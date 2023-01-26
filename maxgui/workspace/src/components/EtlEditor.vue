<template>
    <div class="d-flex flex-column transform-editor rounded pa-2">
        <code class="mariadb-code-style ml-8 mb-2 ">
            <span class="editor-comment"> -- {{ label }} </span>
        </code>
        <sql-editor
            v-model="sql"
            :class="`${editorClass} fill-height`"
            :options="{
                contextmenu: false,
                wordWrap: 'on',
            }"
            :colors="{ 'editor.background': '#fbfbfb' }"
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
import SqlEditor from '@workspaceSrc/components/SqlEditor'
export default {
    name: 'etl-editor',
    components: {
        'sql-editor': SqlEditor,
    },
    props: {
        value: { type: String, required: true },
        label: { type: String, required: true },
        skipRegEditorCompleters: { type: Boolean, default: false },
        editorClass: { type: String, default: '' },
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
    border: thin solid #e8eef1;
    background-color: #fbfbfb;
    .editor-comment {
        color: #60a0b0;
    }
}
</style>
