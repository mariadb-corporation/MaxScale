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
            <table v-show="hasSavingErr" class="alter-err-tbl pa-4">
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
                v-show="!hasSavingErr"
                class="mb-4 pt-2 pl-2 color border-all-table-border"
                :style="{ height: `${editorHeight}px` }"
            >
                <query-editor
                    v-model="currSql"
                    :class="`fill-height`"
                    :cmplList="getDbCmplList"
                    :options="{
                        fontSize: 10,
                        contextmenu: false,
                        wordWrap: 'on',
                    }"
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
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 * Events
 * update:sqlTobeExecuted?: (string)
 */
import { mapGetters } from 'vuex'
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
        ...mapGetters({
            getDbCmplList: 'query/getDbCmplList',
        }),
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

<style lang="scss" scoped>
.alter-err-tbl {
    width: 100%;
    background-color: $reflection;
    td {
        color: $code-color;
        vertical-align: top;
        padding-bottom: 4px;
        &:first-of-type {
            padding-right: 16px;
        }
    }
}
</style>
