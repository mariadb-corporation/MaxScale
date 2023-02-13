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
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'
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
        }),
        allConns() {
            return QueryConn.all()
        },
        // all connections having binding_type === QUERY_CONN_BINDING_TYPES.QUERY_EDITOR
        queryEditorConnOpts() {
            return QueryConn.getters('getQueryEditorConns').map(c => ({
                ...c,
                disabled: Boolean(c.query_editor_id),
            }))
        },
        isConnDlgOpened: {
            get() {
                return this.is_conn_dlg_opened
            },
            set(v) {
                this.SET_IS_CONN_DLG_OPENED(v)
            },
        },
        queryEditorConns() {
            return QueryConn.getters('getQueryEditorConns')
        },
        availableConnOpts() {
            return this.queryEditorConnOpts.filter(cnn => !cnn.disabled)
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
                    default:
                        this.shouldDelAll = true
                        this.isConfDlgOpened = true
                }
        }
    },
    async beforeCreate() {
        await this.$store.dispatch('mxsWorkspace/initWorkspace')
    },
    async created() {
        // Set baseURL: '/' to all worksheets as maxgui and maxscale API are on the same domain
        Worksheet.all().forEach(wke => {
            WorksheetTmp.update({ where: wke.id, data: { request_config: { baseURL: '/' } } })
        })
        await QueryConn.dispatch('validateConns')
    },
    methods: {
        ...mapMutations({
            SET_IS_CONN_DLG_OPENED: 'mxsWorkspace/SET_IS_CONN_DLG_OPENED',
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
    },
}
</script>
