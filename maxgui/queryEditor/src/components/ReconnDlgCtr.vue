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
                <tr v-for="(v, key) in getLostCnnErrMsgObj" :key="key">
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

import { mapActions, mapGetters } from 'vuex'
import QueryTabMem from '@queryEditorSrc/store/orm/models/QueryTabMem'

export default {
    name: 'reconn-dlg-ctr',
    props: {
        onReconnectCb: { type: Function, default: () => null },
    },
    computed: {
        ...mapGetters({
            getLostCnnErrMsgObj: 'queryConns/getLostCnnErrMsgObj',
            getActiveWkeConn: 'queryConns/getActiveWkeConn',
            getActiveQueryTabId: 'queryTab/getActiveQueryTabId',
        }),
        queryErrMsg() {
            return this.$typy(this.getLostCnnErrMsgObj, 'message').safeString
        },
        showReconnDialog: {
            get() {
                return Boolean(this.queryErrMsg)
            },
            set() {
                QueryTabMem.update({
                    where: this.getActiveQueryTabId,
                    data: { lost_cnn_err_msg_obj: {} },
                })
            },
        },
    },
    methods: {
        ...mapActions({
            reconnect: 'queryConns/reconnect',
            disconnect: 'queryConns/disconnect',
        }),
        async deleteConn() {
            await this.disconnect({ id: this.getActiveWkeConn.id })
        },
        async handleReconnect() {
            await this.reconnect()
            await this.onReconnectCb()
        },
    },
}
</script>
