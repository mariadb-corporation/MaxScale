<template>
    <mxs-dlg
        v-model="showReconnDialog"
        :title="queryErrMsg"
        minBodyWidth="624px"
        :onSave="handleReconnect"
        cancelText="disconnect"
        saveText="reconnect"
        :showCloseIcon="false"
        @on-cancel="deleteConn"
    >
        <template v-slot:form-body>
            <table v-if="showReconnDialog" class="tbl-code pa-4">
                <tr v-for="(v, key) in lostCnnErrMsgObj" :key="key">
                    <td>
                        <b>{{ key }}</b>
                    </td>
                    <td>{{ v }}</td>
                </tr>
            </table>
        </template>
    </mxs-dlg>
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
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import QueryTabTmp from '@queryEditorSrc/store/orm/models/QueryTabTmp'
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'

export default {
    name: 'reconn-dlg-ctr',
    props: {
        onReconnectCb: { type: Function, default: () => null },
    },
    computed: {
        lostCnnErrMsgObj() {
            return QueryConn.getters('getLostCnnErrMsgObj')
        },
        queryErrMsg() {
            return this.$typy(this.lostCnnErrMsgObj, 'message').safeString
        },
        showReconnDialog: {
            get() {
                return Boolean(this.queryErrMsg)
            },
            set() {
                QueryTabTmp.update({
                    where: Worksheet.getters('getActiveQueryTabId'),
                    data: { lost_cnn_err_msg_obj: {} },
                })
            },
        },
    },
    methods: {
        async deleteConn() {
            await QueryConn.dispatch('disconnect', { id: QueryConn.getters('getActiveWkeConn').id })
        },
        async handleReconnect() {
            await QueryConn.dispatch('reconnect')
            await this.onReconnectCb()
        },
    },
}
</script>
