<template>
    <confirm-dialog
        v-model="isConfDlgOpened"
        :title="title"
        :smallInfo="smallInfo"
        type="execute"
        minBodyWidth="768px"
        :hasSavingErr="hasSavingErr"
        :allowEnterToSubmit="false"
        :onSave="onSave"
        v-on="$listeners"
    >
        <template v-slot:body-prepend>
            <table v-if="hasSavingErr" class="err-tbl-code pa-4">
                <tr>
                    <td><b>sql</b></td>
                    <td>{{ executedSql }}</td>
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
                class="mb-4 pt-2 pl-2 color border-all-table-border"
                :style="{ height: `${editorHeight}px` }"
            >
                <!-- Workaround: assign true to skipRegCompleters props when getCurrEditorMode is TXT_EDITOR
               in order to not call regCompleters. In other words, when multiple editors are visible
               on the same page, they all re-call registerCompletionItemProvider which causes duplicated
               completion items
               https://github.com/microsoft/monaco-editor/issues/1957
                -->
                <query-editor
                    v-if="isConfDlgOpened"
                    v-model="currSql"
                    :class="`fill-height`"
                    :cmplList="getDbCmplList"
                    :options="{
                        fontSize: 10,
                        contextmenu: false,
                        wordWrap: 'on',
                    }"
                    :skipRegCompleters="isTxtEditor"
                />
            </div>
        </template>
    </confirm-dialog>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 * Events
 * update:sqlTobeExecuted?: (string)
 */
import { mapGetters, mapState } from 'vuex'
import QueryEditor from '@/components/QueryEditor'
export default {
    name: 'execute-sql-dialog',
    components: {
        'query-editor': QueryEditor,
    },
    props: {
        value: { type: Boolean, required: true },
        title: { type: String, required: true },
        smallInfo: { type: String, default: '' },
        hasSavingErr: { type: Boolean, required: true },
        executedSql: { type: String, required: true },
        errMsgObj: { type: Object, required: true },
        sqlTobeExecuted: { type: String, required: true },
        editorHeight: { type: Number, default: 250 },
        onSave: { type: Function, required: true },
    },
    computed: {
        ...mapState({
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
        }),
        ...mapGetters({
            getDbCmplList: 'query/getDbCmplList',
            getCurrEditorMode: 'query/getCurrEditorMode',
        }),
        isTxtEditor() {
            return this.getCurrEditorMode === this.SQL_EDITOR_MODES.TXT_EDITOR
        },
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
