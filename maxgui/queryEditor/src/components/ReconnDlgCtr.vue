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
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapGetters, mapMutations } from 'vuex'
export default {
    name: 'reconn-dlg-ctr',
    props: {
        onReconnectCb: { type: Function, default: () => null },
    },
    computed: {
        ...mapGetters({
            getLostCnnErrMsgObj: 'queryConn/getLostCnnErrMsgObj',
            getCurrWkeConn: 'queryConn/getCurrWkeConn',
            getActiveSessionId: 'querySession/getActiveSessionId',
        }),
        queryErrMsg() {
            return this.$typy(this.getLostCnnErrMsgObj, 'message').safeString
        },
        showReconnDialog: {
            get() {
                return Boolean(this.queryErrMsg)
            },
            set() {
                this.PATCH_LOST_CNN_ERR_MSG_OBJ_MAP({ id: this.getActiveSessionId })
            },
        },
    },
    methods: {
        ...mapMutations({
            PATCH_LOST_CNN_ERR_MSG_OBJ_MAP: 'queryConn/PATCH_LOST_CNN_ERR_MSG_OBJ_MAP',
        }),
        ...mapActions({
            reconnect: 'queryConn/reconnect',
            disconnect: 'queryConn/disconnect',
        }),
        async deleteConn() {
            await this.disconnect({ id: this.getCurrWkeConn.id })
        },
        async handleReconnect() {
            await this.reconnect()
            await this.onReconnectCb()
        },
    },
}
</script>
