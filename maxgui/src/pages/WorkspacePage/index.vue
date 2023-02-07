<template>
    <div class="mxs-workspace-page fill-height">
        <mxs-workspace />
        <confirm-leave-dlg
            v-model="isConfDlgOpened"
            :onSave="onLeave"
            :shouldDelAll.sync="shouldDelAll"
            @on-close="cancelLeave"
            @on-cancel="cancelLeave"
        />
        <conn-dlg-ctr v-model="isConnDlgOpened" :handleSave="handleOpenConn" />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'
import ConfirmLeaveDlg from '@wsComps/ConfirmLeaveDlg.vue'
import ConnDlgCtr from '@wkeComps/QueryEditor/ConnDlgCtr.vue'
import { mapState, mapMutations } from 'vuex'

export default {
    name: 'workspace-page',
    components: { ConfirmLeaveDlg, ConnDlgCtr },
    data() {
        return {
            isConfDlgOpened: false,
            shouldDelAll: true,
            to: '',
        }
    },
    computed: {
        ...mapState({
            is_conn_dlg_opened: state => state.mxsWorkspace.is_conn_dlg_opened,
            pre_select_conn_rsrc: state => state.queryConnsMem.pre_select_conn_rsrc,
        }),
        allConns() {
            return QueryConn.all()
        },
        // all connections having binding_type === QUERY_CONN_BINDING_TYPES.WORKSHEET
        wkeConnOpts() {
            return QueryConn.getters('getWkeConns').map(c => ({
                ...c,
                disabled: Boolean(c.worksheet_id),
            }))
        },
        activeWkeId() {
            return Worksheet.getters('getActiveWkeId')
        },
        isConnDlgOpened: {
            get() {
                return this.is_conn_dlg_opened
            },
            set(v) {
                this.SET_IS_CONN_DLG_OPENED(v)
            },
        },
        wkeConns() {
            return QueryConn.getters('getWkeConns')
        },
        availableConnOpts() {
            return this.wkeConnOpts.filter(cnn => !cnn.disabled)
        },
    },
    beforeRouteLeave(to, from, next) {
        if (this.to) {
            next()
        } else {
            this.to = to
            /**
             * Allow to leave page immediately if next path is to login page (user logouts)
             * or if there is no active connections
             */
            if (this.allConns.length === 0) this.leavePage()
            else
                switch (to.path) {
                    case '/login':
                        this.leavePage()
                        break
                    case '/404':
                        this.cancelLeave()
                        QueryConn.dispatch('validateConns')
                        break
                    default:
                        this.shouldDelAll = true
                        this.isConfDlgOpened = true
                }
        }
    },
    watch: {
        pre_select_conn_rsrc: {
            immediate: true,
            async handler(v) {
                if (v) await this.handlePreSelectConnRsrc()
            },
        },
        allConns: {
            deep: true,
            immediate: true,
            handler(v) {
                this.SET_CONNS_TO_BE_VALIDATED(v)
            },
        },
    },
    async beforeCreate() {
        await this.$store.dispatch('mxsWorkspace/initWorkspace')
    },
    async created() {
        await QueryConn.dispatch('validateConns')
    },
    methods: {
        ...mapMutations({
            SET_IS_CONN_DLG_OPENED: 'mxsWorkspace/SET_IS_CONN_DLG_OPENED',
            SET_CONNS_TO_BE_VALIDATED: 'mxsWorkspace/SET_CONNS_TO_BE_VALIDATED',
        }),
        async onLeave() {
            if (this.shouldDelAll) await QueryConn.dispatch('disconnectAll')
            this.leavePage()
        },
        leavePage() {
            this.$router.push(this.to)
        },
        cancelLeave() {
            this.to = null
        },
        async handleOpenConn(opts) {
            await QueryConn.dispatch('openQueryEditorConn', opts)
        },
        /**
         * Check if there is an available connection (connection that has not been bound to a worksheet),
         * bind it to the current worksheet. otherwise open dialog
         */
        async handlePreSelectConnRsrc() {
            const conn = this.availableConnOpts.find(
                conn => conn.name === this.pre_select_conn_rsrc.id
            )
            if (conn) await await QueryConn.dispatch('onChangeWkeConn', conn)
            else this.SET_IS_CONN_DLG_OPENED(true)
        },
    },
}
</script>
