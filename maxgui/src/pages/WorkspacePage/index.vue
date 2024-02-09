<template>
    <div class="mxs-workspace-page fill-height">
        <mxs-workspace />
        <confirm-leave-dlg
            v-model="isConfDlgOpened"
            @on-confirm="onConfirm"
            @after-close="cancelLeave"
            @after-cancel="cancelLeave"
        />
        <conn-dlg-ctr v-model="isConnDlgOpened" />
        <migr-create-dlg :handleSave="createEtlTask" />
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
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'
import ConfirmLeaveDlg from '@wsComps/ConfirmLeaveDlg.vue'
import ConnDlgCtr from '@wsSrc/components/ConnDlgCtr.vue'
import MigrCreateDlg from '@wkeComps/DataMigration/MigrCreateDlg.vue'
import { mapState, mapMutations } from 'vuex'

export default {
    name: 'workspace-page',
    components: { ConfirmLeaveDlg, ConnDlgCtr, MigrCreateDlg },
    data() {
        return {
            isConfDlgOpened: false,
            to: '',
        }
    },
    computed: {
        ...mapState({
            conn_dlg: state => state.mxsWorkspace.conn_dlg,
        }),
        allConns() {
            return QueryConn.all()
        },
        isConnDlgOpened: {
            get() {
                return this.conn_dlg.is_opened
            },
            set(v) {
                this.SET_CONN_DLG({ ...this.conn_dlg, is_opened: v })
            },
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
            SET_CONN_DLG: 'mxsWorkspace/SET_CONN_DLG',
        }),
        async onConfirm(shouldDelAll) {
            if (shouldDelAll) await QueryConn.dispatch('disconnectAll')
            this.leavePage()
        },
        leavePage() {
            this.$router.push(this.to)
        },
        cancelLeave() {
            this.to = null
        },
        createEtlTask(name) {
            EtlTask.dispatch('createEtlTask', name)
        },
    },
}
</script>
