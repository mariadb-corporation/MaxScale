<template>
    <base-dialog
        v-model="showReconnDialog"
        :title="queryErrMsg"
        minBodyWidth="624px"
        :onSave="reconnect"
        cancelText="disconnect"
        saveText="reconnect"
        :showCloseIcon="false"
        @on-cancel="deleteConn"
    >
        <template v-slot:form-body>
            <table v-if="showReconnDialog" class="err-tbl-code pa-4">
                <tr v-for="(v, key) in getQueryErrMsgObj" :key="key">
                    <td>
                        <b>{{ key }}</b>
                    </td>
                    <td>{{ v }}</td>
                </tr>
            </table>
        </template>
    </base-dialog>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState, mapGetters, mapMutations } from 'vuex'

export default {
    name: 'reconn-dialog',
    computed: {
        ...mapState({
            active_wke_id: state => state.query.active_wke_id,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
        }),
        ...mapGetters({
            getQueryErrMsgObj: 'query/getQueryErrMsgObj',
        }),
        queryErrMsg() {
            return this.$typy(this.getQueryErrMsgObj, 'message').safeString
        },
        showReconnDialog: {
            get() {
                return Boolean(this.queryErrMsg)
            },
            set() {
                this.UPDATE_LOST_CNN_ERR_MSG_OBJ_MAP({ id: this.active_wke_id })
            },
        },
    },
    methods: {
        ...mapMutations({
            UPDATE_LOST_CNN_ERR_MSG_OBJ_MAP: 'query/UPDATE_LOST_CNN_ERR_MSG_OBJ_MAP',
        }),
        ...mapActions({
            reconnect: 'query/reconnect',
            disconnect: 'query/disconnect',
        }),
        async deleteConn() {
            await this.disconnect({ id: this.curr_cnct_resource.id })
        },
    },
}
</script>
