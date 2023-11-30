<template>
    <mxs-conf-dlg
        v-model="isConfDlgOpened"
        :title="title"
        :smallInfo="smallInfo"
        saveText="execute"
        minBodyWidth="768px"
        :hasSavingErr="hasSavingErr"
        :allowEnterToSubmit="false"
        :onSave="onSave"
        v-on="$listeners"
    >
        <template v-slot:body-prepend>
            <table v-if="hasSavingErr" class="tbl-code pa-4">
                <tr>
                    <td><b>sql</b></td>
                    <td>{{ currSql }}</td>
                </tr>
                <tr v-for="(v, key) in errMsgObj" :key="key">
                    <td>
                        <b>{{ key }}</b>
                    </td>
                    <td>{{ v }}</td>
                </tr>
            </table>

            <div
                v-else
                class="mb-4 pt-2 pl-2 mxs-color-helper all-border-table-border"
                :style="{ height: `${editorHeight}px` }"
            >
                <!-- Workaround: assign true to skipRegCompleters props when curr_editor_mode is TXT_EDITOR
               in order to not call regCompleters. In other words, when multiple editors are visible
               on the same page, they all re-call registerCompletionItemProvider which causes duplicated
               completion items
               https://github.com/microsoft/monaco-editor/issues/1957
                -->
                <sql-editor
                    v-if="isConfDlgOpened"
                    v-model="currSql"
                    :class="`fill-height`"
                    :completionItems="completionItems"
                    :options="{
                        fontSize: 10,
                        contextmenu: false,
                        wordWrap: 'on',
                    }"
                    :skipRegCompleters="skipRegCompleters"
                />
            </div>
        </template>
    </mxs-conf-dlg>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 * Events
 * update:sqlTobeExecuted?: (string)
 */
import SqlEditor from '@wsComps/SqlEditor'
export default {
    name: 'execute-sql-dialog',
    components: {
        'sql-editor': SqlEditor,
    },
    props: {
        value: { type: Boolean, required: true },
        title: { type: String, required: true },
        smallInfo: { type: String, default: '' },
        hasSavingErr: { type: Boolean, required: true },
        errMsgObj: { type: Object, required: true },
        sqlTobeExecuted: { type: String, required: true },
        editorHeight: { type: Number, default: 250 },
        completionItems: { type: Array, required: true },
        skipRegCompleters: { type: Boolean, required: true },
        onSave: { type: Function, required: true },
    },
    computed: {
        isConfDlgOpened: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        currSql: {
            get() {
                return this.sqlTobeExecuted
            },
            set(value) {
                this.$emit('update:sqlTobeExecuted', value)
            },
        },
    },
}
</script>
